// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "corpus_detail.hpp"

#include <catch2/catch_all.hpp>
#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/lexical_terms.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace
{

using nesso::core::Bm25Index;
using nesso::core::CorpusLine;
using nesso::core::CorpusPolicy;
using nesso::core::CorpusWriter;
using nesso::core::EngineError;
using nesso::core::LexicalLinePostings;
using nesso::core::NO_DISTINCT;
using nesso::core::detail::CorpusLimits;
using nesso::core::detail::CorpusWriterAccess;

class TempDir
{
  public:
    TempDir() : path_(UniquePath()) { REQUIRE(std::filesystem::create_directory(path_)); }

    ~TempDir() { std::filesystem::remove_all(path_); }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    TempDir(TempDir &&) = delete;
    TempDir &operator=(TempDir &&) = delete;
    [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

  private:
    static std::filesystem::path UniquePath()
    {
        static unsigned counter = 0;
        return std::filesystem::temp_directory_path() /
               ("nesso_corpus_v3_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
    }

    std::filesystem::path path_;
};

CorpusPolicy Policy(std::uint32_t dimensions = 2)
{
    return {{"batch", "window:1024,max:32,tokens:1024,order:length-then-input"},
            {"bm25", std::string{nesso::core::BM25_POLICY}},
            {"dedup", "masked-v1"},
            {"dimensions", std::to_string(dimensions)},
            {"lexical.terms", std::string{nesso::core::LEXICAL_TERMS_VERSION}},
            {"mask", "template-v1"},
            {"model.id", "model"},
            {"model.quantization", "int8"},
            {"model.sha256", std::string(64, 'a')},
            {"tokens.max", "256"},
            {"truncate", "4096-codepoints"},
            {"vocab.sha256", std::string(64, 'b')}};
}

std::vector<std::string> TermsOf(std::string_view text) { return nesso::core::LexicalTerms(text); }

void WriteBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
}

std::string ReadBytes(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::uint64_t GetU64(std::string_view bytes, std::size_t offset)
{
    REQUIRE(offset + sizeof(std::uint64_t) <= bytes.size());
    std::uint64_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void PutU32(std::string &bytes, std::size_t offset, std::uint32_t value)
{
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void PutU64(std::string &bytes, std::size_t offset, std::uint64_t value)
{
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void Reseal(std::string &bytes)
{
    REQUIRE(bytes.size() >= nesso::core::CORPUS_CHECKSUM_OFFSET + nesso::core::CORPUS_CHECKSUM_BYTES);
    const auto checksum = nesso::core::ComputeCorpusChecksum(std::as_bytes(std::span{bytes}));
    std::memcpy(bytes.data() + nesso::core::CORPUS_CHECKSUM_OFFSET, checksum.data(), checksum.size());
}

void WriteExample(const std::filesystem::path &corpus, const std::filesystem::path &source)
{
    WriteBytes(source, "alpha\n\nbravo\nalpha\n");
    auto writer = CorpusWriter::Create(corpus, Policy());
    REQUIRE(writer);
    auto info = nesso::core::SourceInfoFor(source, "message");
    REQUIRE(info);
    REQUIRE(writer->AddSource(*info) == 0);
    REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 1);
    REQUIRE(writer->AddSource({.path = "bundle.tar!app.log", .stored_only = true}) == 2);
    const std::array<float, 2> alpha{1.F, 0.F};
    const std::array<float, 2> bravo{0.F, 1.F};
    REQUIRE(writer->AddDistinct("alpha", alpha) == 0);
    REQUIRE(writer->AddDistinct("bravo", bravo) == 1);
    const std::array<CorpusLine, 7> lines{
        CorpusLine{.line_number = 1, .byte_offset = 0, .byte_length = 5, .source_index = 0, .distinct_id = 0},
        CorpusLine{.line_number = 2, .byte_offset = 6, .byte_length = 0, .source_index = 0, .distinct_id = NO_DISTINCT},
        CorpusLine{.line_number = 3, .byte_offset = 7, .byte_length = 5, .source_index = 0, .distinct_id = 1},
        CorpusLine{.line_number = 4, .byte_offset = 13, .byte_length = 5, .source_index = 0, .distinct_id = 0},
        CorpusLine{.line_number = 1, .source_index = 1, .distinct_id = 1, .stored_text = "bravo from stdin"},
        CorpusLine{.line_number = 2, .source_index = 1, .distinct_id = NO_DISTINCT, .stored_text = ""},
        CorpusLine{.line_number = 1,
                   .source_index = 2,
                   .distinct_id = 0,
                   .array_element = true,
                   .stored_text = "archive original"}};
    const std::array<std::vector<std::string>, 7> terms{
        TermsOf("alpha"),           {}, TermsOf("bravo"), TermsOf("alpha"), TermsOf("bravo from stdin"), {},
        TermsOf("archive original")};
    REQUIRE(writer->AddLines(lines, terms));
    REQUIRE(writer->Finish());
}

bool ReadVarintAt(std::string_view bytes, std::size_t &cursor, std::uint64_t &value)
{
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7)
    {
        if (cursor >= bytes.size())
        {
            return false;
        }
        const auto piece = static_cast<unsigned char>(bytes[cursor]);
        value |= static_cast<std::uint64_t>(piece & 0x7FU) << shift;
        ++cursor;
        if ((piece & 0x80U) == 0)
        {
            return true;
        }
    }
    return false;
}

struct LexicalSites
{
    std::size_t first_term_byte = 0;
    std::size_t first_frequency = 0;
    std::size_t first_posting_length = 0;
    std::size_t first_delta = 0;
    std::uint64_t term_count = 0;
};

LexicalSites ParseLexicalSites(std::string_view bytes)
{
    const auto origin = GetU64(bytes, 88);
    LexicalSites sites;
    sites.term_count = GetU64(bytes, origin);
    const auto posting_lines = GetU64(bytes, origin + 8);
    std::size_t cursor = origin + 16;
    for (std::uint64_t term = 0; term < sites.term_count; ++term)
    {
        std::uint64_t length = 0;
        REQUIRE(ReadVarintAt(bytes, cursor, length));
        REQUIRE(length > 0);
        REQUIRE(cursor + length <= bytes.size());
        if (term == 0)
        {
            sites.first_term_byte = cursor;
        }
        cursor += static_cast<std::size_t>(length);
    }
    for (std::uint64_t line = 0; line < posting_lines; ++line)
    {
        std::uint64_t unique = 0;
        REQUIRE(ReadVarintAt(bytes, cursor, unique));
        for (std::uint64_t index = 0; index < unique; ++index)
        {
            if (sites.first_delta == 0)
            {
                sites.first_delta = cursor;
            }
            std::uint64_t delta = 0;
            REQUIRE(ReadVarintAt(bytes, cursor, delta));
            if (sites.first_frequency == 0)
            {
                sites.first_frequency = cursor;
            }
            std::uint64_t frequency = 0;
            REQUIRE(ReadVarintAt(bytes, cursor, frequency));
        }
        if (sites.first_posting_length == 0 && unique > 0)
        {
            sites.first_posting_length = cursor;
        }
        std::uint64_t length = 0;
        REQUIRE(ReadVarintAt(bytes, cursor, length));
    }
    return sites;
}

bool HasSpill(const std::filesystem::path &directory)
{
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        const std::string name = entry.path().filename().string();
        if (name.find(".lines.") != std::string::npos || name.find(".texts.") != std::string::npos ||
            name.find(".vectors.") != std::string::npos || name.find(".terms.") != std::string::npos ||
            name.find(".lexrun.") != std::string::npos || name.find(".lexical.") != std::string::npos ||
            name.find(".tmp.") != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("corpus metadata preserves positional aggregate initialization", "[corpus][core]")
{
    const nesso::core::LogChunk chunk{"text", 7, "source.log", 20, true};
    CHECK(chunk.array_element);
    CHECK(chunk.byte_length == 0);

    const nesso::core::SourceInfo source{"source.log", 42, 13, true};
    CHECK(source.size == 42);
    CHECK(source.modified_time == 13);
    CHECK(source.tracked);
    CHECK(source.json_field == "message");
    CHECK_FALSE(source.stored_only);
}

TEST_CASE("v3 stores ordered lines, distinct vectors, policy and stored texts", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "corpus.nesso";
    const auto source = dir.Path() / "app.log";
    WriteExample(path, source);
    REQUIRE_FALSE(HasSpill(dir.Path()));

    const auto mapped = nesso::core::MapCorpusFile(path);
    REQUIRE(mapped);
    CHECK(mapped->version == nesso::core::CORPUS_VERSION);
    CHECK(mapped->corpus.Policy() == Policy());
    CHECK(mapped->corpus.Dimensions() == 2);
    REQUIRE(mapped->corpus.Sources().size() == 3);
    CHECK(mapped->corpus.Sources()[0].path == source.string());
    CHECK(mapped->corpus.Sources()[0].size == 19);
    CHECK(mapped->corpus.Sources()[0].tracked);
    CHECK(mapped->corpus.Sources()[1].stored_only);
    CHECK(mapped->corpus.Sources()[2].path == "bundle.tar!app.log");
    REQUIRE(mapped->corpus.Lines().size() == 7);
    CHECK(mapped->corpus.Lines()[1].distinct_id == NO_DISTINCT);
    CHECK(mapped->corpus.Lines()[2].byte_offset == 7);
    CHECK(mapped->corpus.Lines()[2].byte_length == 5);
    REQUIRE(mapped->corpus.Lines()[4].stored_text.has_value());
    CHECK(mapped->corpus.Lines()[4].stored_text.value() == "bravo from stdin");
    REQUIRE(mapped->corpus.Lines()[5].stored_text.has_value());
    CHECK(mapped->corpus.Lines()[5].stored_text.value().empty());
    CHECK(mapped->corpus.Lines()[6].array_element);
    REQUIRE(mapped->corpus.Texts().size() == 2);
    CHECK(mapped->corpus.Texts()[0] == "alpha");
    CHECK(mapped->corpus.Texts()[1] == "bravo");
    REQUIRE(mapped->corpus.Matrix().size() == 4);
    CHECK(mapped->corpus.Matrix()[0] == 1.F);
    CHECK(mapped->corpus.Matrix()[3] == 1.F);
    CHECK(reinterpret_cast<std::uintptr_t>(mapped->corpus.Matrix().data()) % 64 == 0);
    CHECK(GetU64(ReadBytes(path), 48) % 64 == 0);
    REQUIRE(mapped->corpus.LexicalPostings().size() == 7);
    CHECK(mapped->corpus.LexicalPostings()[1].length == 0);
    CHECK(mapped->corpus.LexicalPostings()[1].entries.empty());
    CHECK(mapped->corpus.LexicalPostings()[5].length == 0);
    {
        const auto expected_alpha = TermsOf("alpha");
        std::uint32_t length = 0;
        for (const auto &[termId, frequency] : mapped->corpus.LexicalPostings()[0].entries)
        {
            REQUIRE(termId < mapped->corpus.LexicalDictionary().size());
            length += frequency;
        }
        CHECK(mapped->corpus.LexicalPostings()[0].length == expected_alpha.size());
        CHECK(length == expected_alpha.size());
        Bm25Index rebuilt;
        for (const LexicalLinePostings &row : mapped->corpus.LexicalPostings())
        {
            std::vector<std::string_view> tokens;
            tokens.reserve(row.length);
            for (const auto &[termId, frequency] : row.entries)
            {
                for (std::uint32_t copy = 0; copy < frequency; ++copy)
                {
                    tokens.push_back(mapped->corpus.LexicalDictionary()[termId]);
                }
            }
            rebuilt.AddLine(tokens);
        }
        CHECK(rebuilt.LineCount() == 7);
    }

    const auto expanded = nesso::core::ReadCorpusFile(path);
    REQUIRE(expanded);
    REQUIRE(expanded->size() == 5);
    CHECK((*expanded)[0].chunk.text == "alpha");
    CHECK((*expanded)[1].chunk.text == "bravo");
    CHECK((*expanded)[3].chunk.text == "bravo from stdin");
    CHECK((*expanded)[4].chunk.text == "archive original");
    CHECK((*expanded)[4].chunk.array_element);
    CHECK((*expanded)[1].embedding == std::vector<float>{0.F, 1.F});
}

TEST_CASE("duplicate lines share one stored vector", "[corpus][core]")
{
    const TempDir dir;
    const auto source = dir.Path() / "duplicates.log";
    constexpr std::size_t count = 1000;
    std::string contents;
    for (std::size_t index = 0; index < count; ++index)
    {
        contents += "dup\n";
    }
    WriteBytes(source, contents);
    auto writer = CorpusWriter::Create(dir.Path() / "duplicates.nesso", Policy(384));
    REQUIRE(writer);
    auto info = nesso::core::SourceInfoFor(source, "message");
    REQUIRE(info);
    REQUIRE(writer->AddSource(*info) == 0);
    REQUIRE(writer->AddDistinct("dup", std::vector<float>(384, 1.F)) == 0);
    std::vector<CorpusLine> lines;
    std::vector<std::vector<std::string>> terms;
    const std::vector<std::string> dup_terms = TermsOf("dup");
    for (std::size_t index = 0; index < count; ++index)
    {
        lines.push_back({.line_number = index + 1, .byte_offset = index * 4, .byte_length = 3, .distinct_id = 0});
        terms.push_back(dup_terms);
    }
    REQUIRE(writer->AddLines(lines, terms));
    REQUIRE(writer->Finish());
    auto mapped = nesso::core::MapCorpusFile(dir.Path() / "duplicates.nesso");
    REQUIRE(mapped);
    CHECK(mapped->corpus.Lines().size() == count);
    CHECK(mapped->corpus.Texts().size() == 1);
    CHECK(mapped->corpus.Matrix().size() == 384);
    REQUIRE(mapped->corpus.LexicalPostings().size() == count);
    CHECK(mapped->corpus.LexicalDictionary().size() == 1);
    CHECK(mapped->corpus.LexicalDictionary()[0] == "dup");
    CHECK(std::filesystem::file_size(dir.Path() / "duplicates.nesso") < count * 384 * sizeof(float) / 4);
}

TEST_CASE("empty v3 corpora and source metadata are handled explicitly", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "empty.nesso";
    auto writer = CorpusWriter::Create(path, Policy(0));
    REQUIRE(writer);
    REQUIRE(writer->Finish());
    const auto mapped = nesso::core::MapCorpusFile(path);
    REQUIRE(mapped);
    CHECK(mapped->corpus.Lines().empty());
    CHECK(mapped->corpus.Texts().empty());
    CHECK(mapped->corpus.Matrix().empty());
    CHECK(mapped->corpus.LexicalDictionary().empty());
    CHECK(mapped->corpus.LexicalPostings().empty());
    CHECK(mapped->corpus.Dimensions() == 0);
    const auto stdin_info = nesso::core::SourceInfoFor("-", "message");
    REQUIRE(stdin_info);
    CHECK(stdin_info->stored_only);
    CHECK_FALSE(stdin_info->tracked);
    CHECK_FALSE(nesso::core::SourceInfoFor(dir.Path() / "missing", "message"));

    const auto gzip = dir.Path() / "APP.LOG.GZ";
    {
        std::ofstream output(gzip, std::ios::binary);
        const std::array<unsigned char, 4> bytes{0x1f, 0x8b, 0x08, 0x00};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(output);
    }
    const auto gzip_info = nesso::core::SourceInfoFor(gzip, "host");
    REQUIRE(gzip_info);
    CHECK(gzip_info->stored_only);
    CHECK_FALSE(gzip_info->tracked);
    CHECK(gzip_info->json_field == "host");
    CHECK(gzip_info->size == std::filesystem::file_size(gzip));
    const auto plain = dir.Path() / "plain.log";
    WriteBytes(plain, "alpha\n");
    const auto plain_info = nesso::core::SourceInfoFor(plain, "message");
    REQUIRE(plain_info);
    CHECK(plain_info->tracked);
    CHECK_FALSE(plain_info->stored_only);
}

TEST_CASE("v3 writer rejects invalid policies and unusable destinations", "[corpus][core]")
{
    const TempDir dir;
    CorpusPolicy invalid = Policy();
    SECTION("missing field") { invalid.erase("batch"); }
    SECTION("bad model hash") { invalid["model.sha256"] = "not-a-hash"; }
    SECTION("bad vocabulary hash") { invalid["vocab.sha256"] = std::string(64, 'G'); }
    SECTION("unsupported quantization") { invalid["model.quantization"] = "unknown"; }
    SECTION("noncanonical dimension") { invalid["dimensions"] = "02"; }
    SECTION("dimension too large") { invalid["dimensions"] = "65537"; }
    SECTION("invalid token limit") { invalid["tokens.max"] = "0"; }
    SECTION("empty value") { invalid["batch"] = ""; }
    SECTION("newline in value") { invalid["batch"] = "one\ntwo"; }
    SECTION("NUL in value") { invalid["batch"] = std::string{"a\0b", 3}; }
    SECTION("invalid key") { invalid["Bad Key"] = "value"; }
    SECTION("model id too long") { invalid["model.id"] = std::string(1025, 'x'); }
    SECTION("policy too long") { invalid["extra"] = std::string(65536, 'x'); }
    const auto rejected = CorpusWriter::Create(dir.Path() / "invalid.nesso", invalid);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error() == EngineError::CorruptDatabase);
    CHECK_FALSE(HasSpill(dir.Path()));
}

TEST_CASE("v3 writer validates records and preserves an existing destination", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "corpus.nesso";
    WriteBytes(path, "old corpus");
    {
        auto writer = CorpusWriter::Create(path, Policy());
        REQUIRE(writer);
        CHECK_FALSE(writer->AddSource({.path = ""}));
        CHECK_FALSE(writer->AddSource({.path = std::string{"a\0b", 3}}));
        REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 0);
        CHECK_FALSE(writer->AddDistinct("empty", {}));
        CHECK_FALSE(writer->AddDistinct("wrong width", std::array<float, 1>{1.F}));
        CHECK_FALSE(
            writer->AddDistinct("nonfinite", std::array<float, 2>{1.F, std::numeric_limits<float>::infinity()}));
        CHECK_FALSE(
            writer->AddDistinct(std::string(8ULL * 1024ULL * 1024ULL + 1, 'x'), std::array<float, 2>{1.F, 0.F}));
        REQUIRE(writer->AddDistinct("alpha", std::array<float, 2>{1.F, 0.F}) == 0);
        CHECK_FALSE(writer->AddDistinct("alpha", std::array<float, 2>{1.F, 0.F}));
        const std::array<CorpusLine, 1> no_stored{CorpusLine{.line_number = 1, .distinct_id = 0}};
        const std::array<std::vector<std::string>, 1> alpha_terms{TermsOf("alpha")};
        CHECK_FALSE(writer->AddLines(no_stored, alpha_terms));
        const std::array<CorpusLine, 1> bad_source{
            CorpusLine{.line_number = 1, .source_index = 1, .distinct_id = 0, .stored_text = "alpha"}};
        CHECK_FALSE(writer->AddLines(bad_source, alpha_terms));
        const std::array<CorpusLine, 1> bad_id{CorpusLine{.line_number = 1, .distinct_id = 1, .stored_text = "alpha"}};
        CHECK_FALSE(writer->AddLines(bad_id, alpha_terms));
        const std::array<CorpusLine, 1> good{CorpusLine{.line_number = 1, .distinct_id = 0, .stored_text = "alpha"}};
        REQUIRE(writer->AddLines(good, alpha_terms));
        CHECK_FALSE(writer->AddLines(good, alpha_terms));
    }
    CHECK(ReadBytes(path) == "old corpus");
    CHECK_FALSE(HasSpill(dir.Path()));
    CHECK_FALSE(CorpusWriter::Create(dir.Path(), Policy()));
    CHECK_FALSE(CorpusWriter::Create(dir.Path() / "missing" / "corpus", Policy()));
}

TEST_CASE("v3 writer detects unreferenced vectors and transfers spill ownership", "[corpus][core]")
{
    const TempDir dir;
    {
        auto first = CorpusWriter::Create(dir.Path() / "first", Policy());
        auto second = CorpusWriter::Create(dir.Path() / "second", Policy());
        REQUIRE(first);
        REQUIRE(second);
        *first = std::move(*second);
        REQUIRE(first->AddDistinct("unused", std::array<float, 2>{1.F, 0.F}));
        const auto rejected = first->Finish();
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::CorruptDatabase);
    }
    CHECK_FALSE(HasSpill(dir.Path()));
}

TEST_CASE("v3 streams lines before their distinct vectors", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "streamed.nesso";
    auto writer = CorpusWriter::Create(path, Policy());
    REQUIRE(writer);
    REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 0);
    REQUIRE(writer->ReserveDistinct("alpha") == 0);
    REQUIRE(writer->ReserveDistinct("bravo") == 1);
    const std::array<CorpusLine, 3> lines{CorpusLine{.line_number = 1, .distinct_id = 0, .stored_text = "alpha"},
                                          CorpusLine{.line_number = 2, .distinct_id = NO_DISTINCT, .stored_text = ""},
                                          CorpusLine{.line_number = 3, .distinct_id = 1, .stored_text = "bravo"}};
    const std::array<std::vector<std::string>, 3> terms{TermsOf("alpha"), {}, TermsOf("bravo")};
    REQUIRE(writer->AddLines(lines, terms));
    CHECK_FALSE(writer->Finish());
    CHECK_FALSE(writer->AddVector(1, std::array<float, 2>{0.F, 1.F}));
    REQUIRE(writer->AddVector(0, std::array<float, 2>{1.F, 0.F}));
    REQUIRE(writer->AddVector(1, std::array<float, 2>{0.F, 1.F}));
    REQUIRE(writer->Finish());
    const auto mapped = nesso::core::MapCorpusFile(path);
    REQUIRE(mapped);
    CHECK(mapped->corpus.Lines().size() == 3);
    CHECK(mapped->corpus.Texts().size() == 2);
    CHECK(mapped->corpus.Matrix().size() == 4);
}

TEST_CASE("v3 rejects unsupported versions and malformed files", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "corpus.nesso";
    WriteExample(path, dir.Path() / "app.log");
    std::string bytes = ReadBytes(path);
    const auto source_offset = GetU64(bytes, 32);
    const auto line_offset = GetU64(bytes, 40);
    const auto matrix_offset = GetU64(bytes, 48);
    const auto distinct_offset = GetU64(bytes, 64);
    const auto text_offset = GetU64(bytes, 72);
    const auto stored_offset = GetU64(bytes, 80);
    EngineError expected = EngineError::CorruptDatabase;
    bool sealed = true;

    SECTION("v2")
    {
        PutU32(bytes, 4, 2);
        expected = EngineError::UnsupportedVersion;
    }
    SECTION("future version")
    {
        PutU32(bytes, 4, 4);
        expected = EngineError::UnsupportedVersion;
    }
    SECTION("bad magic") { bytes[0] = 'X'; }
    SECTION("short header")
    {
        bytes.resize(12);
        sealed = false;
    }
    SECTION("truncated matrix") { bytes.pop_back(); }
    SECTION("trailing bytes") { bytes.push_back('x'); }
    SECTION("misaligned matrix") { PutU64(bytes, 48, matrix_offset + 1); }
    SECTION("policy over its cap") { PutU32(bytes, 8, 64U * 1024U + 1U); }
    SECTION("dimensions over their cap") { PutU32(bytes, 12, 65537); }
    SECTION("zero dimensions") { PutU32(bytes, 12, 0); }
    SECTION("bad source section offset") { PutU64(bytes, 32, source_offset + 8); }
    SECTION("bad line section size") { PutU64(bytes, 40, line_offset + 1); }
    SECTION("bad distinct section size") { PutU64(bytes, 64, distinct_offset + 1); }
    SECTION("bad text section offset") { PutU64(bytes, 72, text_offset + 1); }
    SECTION("bad stored section offset") { PutU64(bytes, 80, matrix_offset + 1); }
    SECTION("out-of-range lexical offset")
    {
        PutU64(bytes, 88, matrix_offset);
        PutU64(bytes, 96, 8);
    }
    SECTION("truncated lexical table") { PutU64(bytes, 96, 1); }
    SECTION("lexical length past matrix") { PutU64(bytes, 96, matrix_offset); }
    SECTION("bad policy offset") { PutU64(bytes, 104, 0); }
    SECTION("checksum mismatch")
    {
        bytes[matrix_offset] = static_cast<char>(bytes[matrix_offset] ^ 1);
        sealed = false;
    }
    SECTION("checksum field")
    {
        bytes[nesso::core::CORPUS_CHECKSUM_OFFSET] = static_cast<char>(bytes[nesso::core::CORPUS_CHECKSUM_OFFSET] ^ 1);
        sealed = false;
    }
    SECTION("header byte changed")
    {
        bytes[8] = static_cast<char>(bytes[8] ^ 1);
        sealed = false;
    }
    SECTION("policy ordering") { bytes[128] = 'z'; }
    SECTION("malformed hash")
    {
        const auto hash = bytes.find("model.sha256=");
        REQUIRE(hash != std::string::npos);
        bytes[hash + std::string_view{"model.sha256="}.size()] = 'g';
    }
    SECTION("bad source flags") { PutU32(bytes, source_offset + 12, 4); }
    SECTION("bad source offset") { PutU64(bytes, source_offset, source_offset); }
    SECTION("bad source length") { PutU32(bytes, source_offset + 8, 0); }
    SECTION("reserved source field") { PutU32(bytes, source_offset + 44, 1); }
    SECTION("bad line number") { PutU64(bytes, line_offset, 0); }
    SECTION("bad line order") { PutU64(bytes, line_offset + 56, 1); }
    SECTION("bad source reference") { PutU32(bytes, line_offset + 24, 99); }
    SECTION("bad distinct reference") { PutU32(bytes, line_offset + 28, 99); }
    SECTION("bad skipped flag") { PutU32(bytes, line_offset + 44, 4); }
    SECTION("bad source range") { PutU64(bytes, line_offset + 16, 1000); }
    SECTION("unexpected stored offset") { PutU64(bytes, line_offset + 32, stored_offset); }
    SECTION("stored-only source without text") { PutU32(bytes, line_offset + 4 * 56 + 44, 0); }
    SECTION("bad stored text offset") { PutU64(bytes, line_offset + 4 * 56 + 32, 0); }
    SECTION("reserved line field") { PutU64(bytes, line_offset + 48, 1); }
    SECTION("bad distinct text offset") { PutU64(bytes, distinct_offset, text_offset + 1); }
    SECTION("bad distinct text length") { PutU32(bytes, distinct_offset + 8, 8U * 1024U * 1024U + 1U); }
    SECTION("reserved distinct field") { PutU32(bytes, distinct_offset + 12, 1); }
    SECTION("duplicate distinct texts") { bytes.replace(text_offset + 5, 5, "alpha"); }
    SECTION("unreferenced distinct vector")
    {
        PutU32(bytes, line_offset + 2 * 56 + 28, 0);
        PutU32(bytes, line_offset + 4 * 56 + 28, 0);
    }
    if (sealed)
    {
        Reseal(bytes);
    }
    WriteBytes(path, bytes);
    const auto result = nesso::core::MapCorpusFile(path);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == expected);
    if (expected == EngineError::UnsupportedVersion)
    {
        REQUIRE(result.error().version.has_value());
        CHECK(*result.error().version != nesso::core::CORPUS_VERSION);
    }
}

TEST_CASE("v3 reports a count over this build's caps as a limit, not corruption", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "corpus.nesso";
    WriteExample(path, dir.Path() / "app.log");
    std::string bytes = ReadBytes(path);
    const auto lexical_offset = GetU64(bytes, 88);

    SECTION("sources") { PutU64(bytes, 16, 1'000'001); }
    SECTION("lines") { PutU64(bytes, 24, 10'000'001); }
    SECTION("distinct texts") { PutU64(bytes, 56, 1'000'001); }
    SECTION("lexical bytes") { PutU64(bytes, 96, 512ULL * 1024ULL * 1024ULL + 1ULL); }
    SECTION("lexical terms") { PutU64(bytes, lexical_offset, 4'000'001); }

    Reseal(bytes);
    WriteBytes(path, bytes);
    const auto mapped = nesso::core::MapCorpusFile(path);
    REQUIRE_FALSE(mapped);
    CHECK(mapped.error().code == EngineError::LimitsExceeded);
    REQUIRE(mapped.error().version.has_value());
    CHECK(*mapped.error().version == nesso::core::CORPUS_VERSION);
}

TEST_CASE("v3 writer refuses records after it finished", "[corpus][core]")
{
    const TempDir dir;
    auto writer = CorpusWriter::Create(dir.Path() / "corpus.nesso", Policy(0));
    REQUIRE(writer);
    REQUIRE(writer->Finish());
    const auto source = writer->AddSource({.path = "-", .stored_only = true});
    REQUIRE_FALSE(source);
    CHECK(source.error() == EngineError::CorruptDatabase);
    const auto distinct = writer->ReserveDistinct("alpha");
    REQUIRE_FALSE(distinct);
    CHECK(distinct.error() == EngineError::CorruptDatabase);
    const auto lines = writer->AddLines({}, {});
    REQUIRE_FALSE(lines);
    CHECK(lines.error() == EngineError::CorruptDatabase);
}

TEST_CASE("v3 writer caps default to the format limits", "[corpus][core]")
{
    const nesso::core::detail::CorpusLimits limits;
    CHECK(limits.sources == nesso::core::detail::MAX_SOURCES);
    CHECK(limits.lines == nesso::core::detail::MAX_LINES);
    CHECK(limits.distinct == nesso::core::detail::MAX_DISTINCT);
    CHECK(limits.lexical_terms == nesso::core::detail::MAX_LEXICAL_TERMS);
    CHECK(limits.lexical_bytes == nesso::core::detail::MAX_LEXICAL_BYTES);

    const TempDir dir;
    auto writer = CorpusWriterAccess::Create(dir.Path() / "access.nesso", Policy(), limits);
    REQUIRE(writer);
    REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 0);
    REQUIRE(writer->Finish());
    auto plain = CorpusWriter::Create(dir.Path() / "plain.nesso", Policy());
    REQUIRE(plain);
    REQUIRE(plain->AddSource({.path = "-", .stored_only = true}) == 0);
    REQUIRE(plain->Finish());
    CHECK(ReadBytes(dir.Path() / "access.nesso") == ReadBytes(dir.Path() / "plain.nesso"));
}

TEST_CASE("v3 writer reports each count past its cap as a limit", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "corpus.nesso";
    WriteBytes(path, "old corpus");
    const std::array<CorpusLine, 1> alpha{CorpusLine{.line_number = 1, .distinct_id = 0, .stored_text = "alpha beta"}};
    const std::array<std::vector<std::string>, 1> alpha_terms{TermsOf("alpha beta")};
    const auto writer_with = [&path](std::uint64_t CorpusLimits::*cap, std::uint64_t value)
    {
        CorpusLimits limits;
        limits.*cap = value;
        auto writer = CorpusWriterAccess::Create(path, Policy(), limits);
        REQUIRE(writer);
        return std::move(*writer);
    };

    SECTION("sources")
    {
        auto writer = writer_with(&CorpusLimits::sources, 2);
        REQUIRE(writer.AddSource({.path = "-", .stored_only = true}) == 0);
        REQUIRE(writer.AddSource({.path = "-", .stored_only = true}) == 1);
        const auto rejected = writer.AddSource({.path = "-", .stored_only = true});
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::LimitsExceeded);
    }
    SECTION("distinct texts")
    {
        auto writer = writer_with(&CorpusLimits::distinct, 2);
        REQUIRE(writer.ReserveDistinct("alpha") == 0);
        REQUIRE(writer.ReserveDistinct("bravo") == 1);
        const auto rejected = writer.ReserveDistinct("charlie");
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::LimitsExceeded);
    }
    SECTION("lines")
    {
        auto writer = writer_with(&CorpusLimits::lines, 3);
        REQUIRE(writer.AddSource({.path = "-", .stored_only = true}) == 0);
        const std::array<CorpusLine, 2> two{
            CorpusLine{.line_number = 1, .distinct_id = NO_DISTINCT, .stored_text = ""},
            CorpusLine{.line_number = 2, .distinct_id = NO_DISTINCT, .stored_text = ""}};
        const std::array<std::vector<std::string>, 2> no_terms{};
        REQUIRE(writer.AddLines(two, no_terms));
        const std::array<CorpusLine, 2> next{
            CorpusLine{.line_number = 3, .distinct_id = NO_DISTINCT, .stored_text = ""},
            CorpusLine{.line_number = 4, .distinct_id = NO_DISTINCT, .stored_text = ""}};
        const auto rejected = writer.AddLines(next, no_terms);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::LimitsExceeded);
        const auto mismatched = writer.AddLines(std::span{next}.first(1), {});
        REQUIRE_FALSE(mismatched);
        CHECK(mismatched.error() == EngineError::CorruptDatabase);
    }
    SECTION("lexical terms")
    {
        auto writer = writer_with(&CorpusLimits::lexical_terms, 1);
        REQUIRE(writer.AddSource({.path = "-", .stored_only = true}) == 0);
        REQUIRE(writer.AddDistinct("alpha beta", std::array<float, 2>{1.F, 0.F}) == 0);
        REQUIRE(writer.AddLines(alpha, alpha_terms));
        const auto rejected = writer.Finish();
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::LimitsExceeded);
    }
    SECTION("lexical bytes in a posting row")
    {
        auto writer = writer_with(&CorpusLimits::lexical_bytes, 32);
        REQUIRE(writer.AddSource({.path = "-", .stored_only = true}) == 0);
        REQUIRE(writer.AddDistinct("alpha beta", std::array<float, 2>{1.F, 0.F}) == 0);
        REQUIRE(writer.AddLines(alpha, alpha_terms));
        const auto rejected = writer.Finish();
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::LimitsExceeded);
    }
    SECTION("lexical bytes in the dictionary header")
    {
        auto writer = writer_with(&CorpusLimits::lexical_bytes, 8);
        const auto rejected = writer.Finish();
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::LimitsExceeded);
    }
    CHECK(ReadBytes(path) == "old corpus");
    CHECK_FALSE(HasSpill(dir.Path()));
}

TEST_CASE("v3 policy keeps embed.prefix optional and reads a missing key as none", "[corpus][core]")
{
    const TempDir dir;
    CHECK(nesso::core::EMBED_PREFIX_KEY == "embed.prefix");
    CHECK(nesso::core::EmbedPrefixOf(Policy()) == "none");
    CorpusPolicy recorded = Policy();
    recorded.emplace("embed.prefix", "none");
    CHECK(nesso::core::EmbedPrefixOf(recorded) == "none");
    CorpusPolicy other = Policy();
    other.emplace("embed.prefix", "query-passage-v1");
    CHECK(nesso::core::EmbedPrefixOf(other) == "query-passage-v1");

    const auto write = [&dir](const std::string &name, const CorpusPolicy &policy)
    {
        const auto path = dir.Path() / name;
        auto writer = CorpusWriter::Create(path, policy);
        REQUIRE(writer);
        REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 0);
        REQUIRE(writer->AddDistinct("alpha", std::array<float, 2>{1.F, 0.F}) == 0);
        const CorpusLine line{.line_number = 1, .distinct_id = 0, .stored_text = "alpha"};
        const std::vector<std::string> terms = TermsOf("alpha");
        REQUIRE(writer->AddLines(std::span{&line, 1}, std::span{&terms, 1}));
        REQUIRE(writer->Finish());
        return path;
    };

    const auto legacy = nesso::core::MapCorpusFile(write("legacy.nesso", Policy()));
    REQUIRE(legacy);
    CHECK_FALSE(legacy->corpus.Policy().contains("embed.prefix"));
    CHECK(nesso::core::EmbedPrefixOf(legacy->corpus.Policy()) == "none");

    const auto with_key = write("recorded.nesso", recorded);
    CHECK(ReadBytes(with_key).find("\nembed.prefix=none\nlexical.terms=") != std::string::npos);
    const auto mapped = nesso::core::MapCorpusFile(with_key);
    REQUIRE(mapped);
    CHECK(mapped->corpus.Policy() == recorded);
    CHECK(nesso::core::EmbedPrefixOf(mapped->corpus.Policy()) == "none");

    const auto other_mapped = nesso::core::MapCorpusFile(write("other.nesso", other));
    REQUIRE(other_mapped);
    CHECK(nesso::core::EmbedPrefixOf(other_mapped->corpus.Policy()) == "query-passage-v1");

    CorpusPolicy empty = Policy();
    empty.emplace("embed.prefix", "");
    const auto rejected = CorpusWriter::Create(dir.Path() / "empty-prefix.nesso", empty);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error() == EngineError::CorruptDatabase);
}

TEST_CASE("v3 policy keeps embed.text optional and reads a missing key as line", "[corpus][core]")
{
    const TempDir dir;
    CHECK(nesso::core::EMBED_TEXT_KEY == "embed.text");
    CHECK(nesso::core::EmbedTextOf(Policy()) == "line");
    CorpusPolicy recorded = Policy();
    recorded.emplace("embed.text", "message-v1");
    CHECK(nesso::core::EmbedTextOf(recorded) == "message-v1");

    const auto path = dir.Path() / "recorded.nesso";
    auto writer = CorpusWriter::Create(path, recorded);
    REQUIRE(writer);
    REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 0);
    REQUIRE(writer->AddDistinct("alpha", std::array<float, 2>{1.F, 0.F}) == 0);
    const CorpusLine line{.line_number = 1, .distinct_id = 0, .stored_text = "alpha"};
    const std::vector<std::string> terms = TermsOf("alpha");
    REQUIRE(writer->AddLines(std::span{&line, 1}, std::span{&terms, 1}));
    REQUIRE(writer->Finish());
    CHECK(ReadBytes(path).find("\nembed.text=message-v1\n") != std::string::npos);
    const auto mapped = nesso::core::MapCorpusFile(path);
    REQUIRE(mapped);
    CHECK(nesso::core::EmbedTextOf(mapped->corpus.Policy()) == "message-v1");

    CorpusPolicy empty = Policy();
    empty.emplace("embed.text", "");
    const auto rejected = CorpusWriter::Create(dir.Path() / "empty-text.nesso", empty);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error() == EngineError::CorruptDatabase);
}

TEST_CASE("v3 lexical postings round-trip for empty and one-line corpora", "[corpus][core]")
{
    const TempDir dir;
    SECTION("empty")
    {
        const auto path = dir.Path() / "empty.nesso";
        auto writer = CorpusWriter::Create(path, Policy(0));
        REQUIRE(writer);
        REQUIRE(writer->Finish());
        const auto mapped = nesso::core::MapCorpusFile(path);
        REQUIRE(mapped);
        CHECK(mapped->corpus.LexicalDictionary().empty());
        CHECK(mapped->corpus.LexicalPostings().empty());
        CHECK(GetU64(ReadBytes(path), 96) >= 16);
    }
    SECTION("one line")
    {
        const auto path = dir.Path() / "one.nesso";
        auto writer = CorpusWriter::Create(path, Policy());
        REQUIRE(writer);
        REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 0);
        REQUIRE(writer->AddDistinct("blk_-123 failed", std::array<float, 2>{1.F, 0.F}) == 0);
        const CorpusLine line{.line_number = 1, .distinct_id = 0, .stored_text = "blk_-123 failed"};
        const std::vector<std::string> terms = TermsOf("blk_-123 failed");
        REQUIRE(writer->AddLines(std::span{&line, 1}, std::span{&terms, 1}));
        REQUIRE(writer->Finish());
        const auto mapped = nesso::core::MapCorpusFile(path);
        REQUIRE(mapped);
        REQUIRE(mapped->corpus.LexicalPostings().size() == 1);
        CHECK(mapped->corpus.LexicalPostings()[0].length == terms.size());
        std::vector<std::string> rebuilt;
        for (const auto &[termId, frequency] : mapped->corpus.LexicalPostings()[0].entries)
        {
            for (std::uint32_t copy = 0; copy < frequency; ++copy)
            {
                rebuilt.push_back(std::string{mapped->corpus.LexicalDictionary()[termId]});
            }
        }
        std::ranges::sort(rebuilt);
        auto expected = terms;
        std::ranges::sort(expected);
        CHECK(rebuilt == expected);
    }
}

TEST_CASE("v3 corpus bytes match across repeated finishes", "[corpus][core]")
{
    const TempDir dir;
    const auto source = dir.Path() / "app.log";
    WriteBytes(source, "alpha\nbravo\nalpha\n");
    std::vector<std::string> corpora;
    for (int run = 0; run < 3; ++run)
    {
        const auto path = dir.Path() / ("corpus-" + std::to_string(run) + ".nesso");
        auto writer = CorpusWriter::Create(path, Policy());
        REQUIRE(writer);
        auto info = nesso::core::SourceInfoFor(source, "message");
        REQUIRE(info);
        REQUIRE(writer->AddSource(*info) == 0);
        REQUIRE(writer->AddDistinct("alpha", std::array<float, 2>{1.F, 0.F}) == 0);
        REQUIRE(writer->AddDistinct("bravo", std::array<float, 2>{0.F, 1.F}) == 1);
        const std::array<CorpusLine, 3> lines{
            CorpusLine{.line_number = 1, .byte_offset = 0, .byte_length = 5, .distinct_id = 0},
            CorpusLine{.line_number = 2, .byte_offset = 6, .byte_length = 5, .distinct_id = 1},
            CorpusLine{.line_number = 3, .byte_offset = 12, .byte_length = 5, .distinct_id = 0}};
        const std::array<std::vector<std::string>, 3> terms{TermsOf("alpha"), TermsOf("bravo"), TermsOf("alpha")};
        REQUIRE(writer->AddLines(lines, terms));
        REQUIRE(writer->Finish());
        corpora.push_back(ReadBytes(path));
    }
    CHECK(corpora[0] == corpora[1]);
    CHECK(corpora[0] == corpora[2]);
}

TEST_CASE("v3 lexical payload errors stay corrupt", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "corpus.nesso";
    WriteExample(path, dir.Path() / "app.log");
    std::string bytes = ReadBytes(path);
    const auto sites = ParseLexicalSites(bytes);
    REQUIRE(sites.term_count >= 2);
    REQUIRE(sites.first_term_byte != 0);
    REQUIRE(sites.first_frequency != 0);
    REQUIRE(sites.first_posting_length != 0);
    REQUIRE(sites.first_delta != 0);
    const auto lexical_offset = GetU64(bytes, 88);
    const auto lexical_bytes = GetU64(bytes, 96);
    const auto matrix_offset = GetU64(bytes, 48);

    SECTION("posting line count") { PutU64(bytes, lexical_offset + 8, GetU64(bytes, 24) + 1); }
    SECTION("unsorted dictionary") { bytes[sites.first_term_byte] = static_cast<char>(0xFF); }
    SECTION("zero frequency")
    {
        REQUIRE(static_cast<unsigned char>(bytes[sites.first_frequency]) != 0);
        REQUIRE((static_cast<unsigned char>(bytes[sites.first_frequency]) & 0x80U) == 0);
        bytes[sites.first_frequency] = 0;
    }
    SECTION("posting length")
    {
        REQUIRE((static_cast<unsigned char>(bytes[sites.first_posting_length]) & 0x80U) == 0);
        REQUIRE(static_cast<unsigned char>(bytes[sites.first_posting_length]) < 127);
        ++bytes[sites.first_posting_length];
    }
    SECTION("term id delta")
    {
        REQUIRE(sites.term_count < 0x7F);
        REQUIRE((static_cast<unsigned char>(bytes[sites.first_delta]) & 0x80U) == 0);
        bytes[sites.first_delta] = static_cast<char>(0x7F);
    }
    SECTION("trailing lexical byte")
    {
        REQUIRE(lexical_offset + lexical_bytes < matrix_offset);
        PutU64(bytes, 96, lexical_bytes + 1);
    }

    Reseal(bytes);
    WriteBytes(path, bytes);
    const auto mapped = nesso::core::MapCorpusFile(path);
    REQUIRE_FALSE(mapped);
    CHECK(mapped.error().code == EngineError::CorruptDatabase);
}

TEST_CASE("v3 AddLines rejects term and order errors before writing", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "corpus.nesso";
    WriteBytes(path, "old corpus");
    auto writer = CorpusWriter::Create(path, Policy());
    REQUIRE(writer);
    REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 0);
    REQUIRE(writer->AddDistinct("alpha", std::array<float, 2>{1.F, 0.F}) == 0);
    const CorpusLine line{.line_number = 2, .distinct_id = 0, .stored_text = "alpha"};
    const std::vector<std::string> alpha = TermsOf("alpha");

    SECTION("term count mismatch")
    {
        const std::array<std::vector<std::string>, 2> terms{alpha, alpha};
        const auto rejected = writer->AddLines(std::span{&line, 1}, terms);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::CorruptDatabase);
    }
    SECTION("empty term")
    {
        const std::vector<std::string> terms{""};
        const auto rejected = writer->AddLines(std::span{&line, 1}, std::span{&terms, 1});
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::CorruptDatabase);
    }
    SECTION("NUL term")
    {
        const std::vector<std::string> terms{std::string{"a\0b", 3}};
        const auto rejected = writer->AddLines(std::span{&line, 1}, std::span{&terms, 1});
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::CorruptDatabase);
    }
    SECTION("terms on a skipped line")
    {
        const CorpusLine skipped{.line_number = 2, .distinct_id = NO_DISTINCT, .stored_text = ""};
        const auto rejected = writer->AddLines(std::span{&skipped, 1}, std::span{&alpha, 1});
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::CorruptDatabase);
    }
    SECTION("line order")
    {
        const std::array<CorpusLine, 2> lines{line,
                                              CorpusLine{.line_number = 1, .distinct_id = 0, .stored_text = "alpha"}};
        const std::array<std::vector<std::string>, 2> terms{alpha, alpha};
        const auto rejected = writer->AddLines(lines, terms);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::CorruptDatabase);
    }
    CHECK(ReadBytes(path) == "old corpus");
}

TEST_CASE("v3 file open failures stay distinct from corrupt corpora", "[corpus][core]")
{
    const TempDir dir;
    SECTION("directory destination")
    {
        const auto rejected = CorpusWriter::Create(dir.Path(), Policy());
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::FileOpenFailure);
    }
    SECTION("missing corpus")
    {
        const auto mapped = nesso::core::MapCorpusFile(dir.Path() / "missing.nesso");
        REQUIRE_FALSE(mapped);
        CHECK(mapped.error().code == EngineError::FileOpenFailure);
    }
    SECTION("finish cannot reopen a spill")
    {
        const auto path = dir.Path() / "corpus.nesso";
        auto writer = CorpusWriter::Create(path, Policy());
        REQUIRE(writer);
        REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 0);
        REQUIRE(writer->AddDistinct("alpha", std::array<float, 2>{1.F, 0.F}) == 0);
        const CorpusLine line{.line_number = 1, .distinct_id = 0, .stored_text = "alpha"};
        const std::vector<std::string> terms = TermsOf("alpha");
        REQUIRE(writer->AddLines(std::span{&line, 1}, std::span{&terms, 1}));
        for (const auto &entry : std::filesystem::directory_iterator(dir.Path()))
        {
            if (entry.path().filename().string().find(".lines.") != std::string::npos)
            {
                std::filesystem::remove(entry.path());
            }
        }
        const auto rejected = writer->Finish();
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::FileOpenFailure);
    }
    SECTION("finish cannot encode after the terms spill is removed")
    {
        const auto path = dir.Path() / "corpus.nesso";
        auto writer = CorpusWriter::Create(path, Policy());
        REQUIRE(writer);
        REQUIRE(writer->AddSource({.path = "-", .stored_only = true}) == 0);
        REQUIRE(writer->AddDistinct("alpha", std::array<float, 2>{1.F, 0.F}) == 0);
        const CorpusLine line{.line_number = 1, .distinct_id = 0, .stored_text = "alpha"};
        const std::vector<std::string> terms = TermsOf("alpha");
        REQUIRE(writer->AddLines(std::span{&line, 1}, std::span{&terms, 1}));
        for (const auto &entry : std::filesystem::directory_iterator(dir.Path()))
        {
            if (entry.path().filename().string().find(".terms.") != std::string::npos)
            {
                std::filesystem::remove(entry.path());
            }
        }
        const auto rejected = writer->Finish();
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::FileOpenFailure);
    }
}

TEST_CASE("v3 map rejects a policy dimension that disagrees with the header", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.Path() / "corpus.nesso";
    WriteExample(path, dir.Path() / "app.log");
    std::string bytes = ReadBytes(path);
    const auto field = bytes.find("dimensions=2\n");
    REQUIRE(field != std::string::npos);
    bytes[field + std::string_view{"dimensions="}.size()] = '3';
    Reseal(bytes);
    WriteBytes(path, bytes);
    const auto mapped = nesso::core::MapCorpusFile(path);
    REQUIRE_FALSE(mapped);
    CHECK(mapped.error().code == EngineError::CorruptDatabase);
}
