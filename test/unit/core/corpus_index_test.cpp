// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/corpus_index.hpp>

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

using nesso::core::CorpusLine;
using nesso::core::CorpusPolicy;
using nesso::core::CorpusWriter;
using nesso::core::EngineError;
using nesso::core::NO_DISTINCT;

class TempDir
{
  public:
    TempDir() : path_(uniquePath()) { REQUIRE(std::filesystem::create_directory(path_)); }

    ~TempDir() { std::filesystem::remove_all(path_); }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    TempDir(TempDir &&) = delete;
    TempDir &operator=(TempDir &&) = delete;
    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    static std::filesystem::path uniquePath()
    {
        static unsigned counter = 0;
        return std::filesystem::temp_directory_path() /
               ("nesso_corpus_v3_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
    }

    std::filesystem::path path_;
};

CorpusPolicy policy(std::uint32_t dimensions = 2)
{
    return {{"batch", "window:1024,max:32,tokens:1024,order:length-then-input"},
            {"dedup", "exact-v1"},
            {"dimensions", std::to_string(dimensions)},
            {"mask", "none-v1"},
            {"model.id", "model"},
            {"model.quantization", "int8"},
            {"model.sha256", std::string(64, 'a')},
            {"tokens.max", "256"},
            {"truncate", "4096-codepoints"},
            {"vocab.sha256", std::string(64, 'b')}};
}

void writeBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
}

std::string readBytes(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::uint64_t getU64(std::string_view bytes, std::size_t offset)
{
    REQUIRE(offset + sizeof(std::uint64_t) <= bytes.size());
    std::uint64_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void putU32(std::string &bytes, std::size_t offset, std::uint32_t value)
{
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void putU64(std::string &bytes, std::size_t offset, std::uint64_t value)
{
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void reseal(std::string &bytes)
{
    REQUIRE(bytes.size() >= nesso::core::CORPUS_CHECKSUM_OFFSET + nesso::core::CORPUS_CHECKSUM_BYTES);
    const auto checksum = nesso::core::corpusChecksum(std::as_bytes(std::span{bytes}));
    std::memcpy(bytes.data() + nesso::core::CORPUS_CHECKSUM_OFFSET, checksum.data(), checksum.size());
}

void writeExample(const std::filesystem::path &corpus, const std::filesystem::path &source)
{
    writeBytes(source, "alpha\n\nbravo\nalpha\n");
    auto writer = CorpusWriter::create(corpus, policy());
    REQUIRE(writer);
    auto info = nesso::core::sourceInfoFor(source, "message");
    REQUIRE(info);
    REQUIRE(writer->addSource(*info) == 0);
    REQUIRE(writer->addSource({.path = "-", .storedOnly = true}) == 1);
    REQUIRE(writer->addSource({.path = "bundle.tar!app.log", .storedOnly = true}) == 2);
    const std::array<float, 2> alpha{1.F, 0.F};
    const std::array<float, 2> bravo{0.F, 1.F};
    REQUIRE(writer->addDistinct("alpha", alpha) == 0);
    REQUIRE(writer->addDistinct("bravo", bravo) == 1);
    const std::array<CorpusLine, 7> lines{
        CorpusLine{.lineNumber = 1, .byteOffset = 0, .byteLength = 5, .sourceIndex = 0, .distinctId = 0},
        CorpusLine{.lineNumber = 2, .byteOffset = 6, .byteLength = 0, .sourceIndex = 0, .distinctId = NO_DISTINCT},
        CorpusLine{.lineNumber = 3, .byteOffset = 7, .byteLength = 5, .sourceIndex = 0, .distinctId = 1},
        CorpusLine{.lineNumber = 4, .byteOffset = 13, .byteLength = 5, .sourceIndex = 0, .distinctId = 0},
        CorpusLine{.lineNumber = 1, .sourceIndex = 1, .distinctId = 1, .storedText = "bravo from stdin"},
        CorpusLine{.lineNumber = 2, .sourceIndex = 1, .distinctId = NO_DISTINCT, .storedText = ""},
        CorpusLine{.lineNumber = 1,
                   .sourceIndex = 2,
                   .distinctId = 0,
                   .arrayElement = true,
                   .storedText = "archive original"}};
    REQUIRE(writer->addLines(lines));
    REQUIRE(writer->finish());
}

bool hasSpill(const std::filesystem::path &directory)
{
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        const std::string name = entry.path().filename().string();
        if (name.find(".lines.") != std::string::npos || name.find(".texts.") != std::string::npos ||
            name.find(".vectors.") != std::string::npos || name.find(".tmp.") != std::string::npos)
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
    CHECK(chunk.arrayElement);
    CHECK(chunk.byteLength == 0);

    const nesso::core::SourceInfo source{"source.log", 42, 13, true};
    CHECK(source.size == 42);
    CHECK(source.modifiedTime == 13);
    CHECK(source.tracked);
    CHECK(source.jsonField == "message");
    CHECK_FALSE(source.storedOnly);
}

TEST_CASE("v3 stores ordered lines, distinct vectors, policy and stored texts", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.path() / "corpus.nesso";
    const auto source = dir.path() / "app.log";
    writeExample(path, source);
    REQUIRE_FALSE(hasSpill(dir.path()));

    const auto mapped = nesso::core::mapCorpusFile(path);
    REQUIRE(mapped);
    CHECK(mapped->policy() == policy());
    CHECK(mapped->dimensions() == 2);
    REQUIRE(mapped->sources().size() == 3);
    CHECK(mapped->sources()[0].path == source.string());
    CHECK(mapped->sources()[0].size == 19);
    CHECK(mapped->sources()[0].tracked);
    CHECK(mapped->sources()[1].storedOnly);
    CHECK(mapped->sources()[2].path == "bundle.tar!app.log");
    REQUIRE(mapped->lines().size() == 7);
    CHECK(mapped->lines()[1].distinctId == NO_DISTINCT);
    CHECK(mapped->lines()[2].byteOffset == 7);
    CHECK(mapped->lines()[2].byteLength == 5);
    REQUIRE(mapped->lines()[4].storedText);
    CHECK(*mapped->lines()[4].storedText == "bravo from stdin");
    REQUIRE(mapped->lines()[5].storedText);
    CHECK(mapped->lines()[5].storedText->empty());
    CHECK(mapped->lines()[6].arrayElement);
    REQUIRE(mapped->texts().size() == 2);
    CHECK(mapped->texts()[0] == "alpha");
    CHECK(mapped->texts()[1] == "bravo");
    REQUIRE(mapped->matrix().size() == 4);
    CHECK(mapped->matrix()[0] == 1.F);
    CHECK(mapped->matrix()[3] == 1.F);
    CHECK(reinterpret_cast<std::uintptr_t>(mapped->matrix().data()) % 64 == 0);
    CHECK(getU64(readBytes(path), 48) % 64 == 0);

    const auto expanded = nesso::core::readCorpusFile(path);
    REQUIRE(expanded);
    REQUIRE(expanded->size() == 5);
    CHECK((*expanded)[0].chunk.text == "alpha");
    CHECK((*expanded)[1].chunk.text == "bravo");
    CHECK((*expanded)[3].chunk.text == "bravo from stdin");
    CHECK((*expanded)[4].chunk.text == "archive original");
    CHECK((*expanded)[4].chunk.arrayElement);
    CHECK((*expanded)[1].embedding == std::vector<float>{0.F, 1.F});
}

TEST_CASE("duplicate lines share one stored vector", "[corpus][core]")
{
    const TempDir dir;
    const auto source = dir.path() / "duplicates.log";
    constexpr std::size_t count = 1000;
    std::string contents;
    for (std::size_t index = 0; index < count; ++index)
    {
        contents += "dup\n";
    }
    writeBytes(source, contents);
    auto writer = CorpusWriter::create(dir.path() / "duplicates.nesso", policy(384));
    REQUIRE(writer);
    auto info = nesso::core::sourceInfoFor(source, "message");
    REQUIRE(info);
    REQUIRE(writer->addSource(*info) == 0);
    REQUIRE(writer->addDistinct("dup", std::vector<float>(384, 1.F)) == 0);
    std::vector<CorpusLine> lines;
    for (std::size_t index = 0; index < count; ++index)
    {
        lines.push_back({.lineNumber = index + 1, .byteOffset = index * 4, .byteLength = 3, .distinctId = 0});
    }
    REQUIRE(writer->addLines(lines));
    REQUIRE(writer->finish());
    auto mapped = nesso::core::mapCorpusFile(dir.path() / "duplicates.nesso");
    REQUIRE(mapped);
    CHECK(mapped->lines().size() == count);
    CHECK(mapped->texts().size() == 1);
    CHECK(mapped->matrix().size() == 384);
    CHECK(std::filesystem::file_size(dir.path() / "duplicates.nesso") < count * 384 * sizeof(float) / 4);
}

TEST_CASE("empty v3 corpora and source metadata are handled explicitly", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.path() / "empty.nesso";
    auto writer = CorpusWriter::create(path, policy(0));
    REQUIRE(writer);
    REQUIRE(writer->finish());
    const auto mapped = nesso::core::mapCorpusFile(path);
    REQUIRE(mapped);
    CHECK(mapped->lines().empty());
    CHECK(mapped->texts().empty());
    CHECK(mapped->matrix().empty());
    CHECK(mapped->dimensions() == 0);
    const auto stdinInfo = nesso::core::sourceInfoFor("-", "message");
    REQUIRE(stdinInfo);
    CHECK(stdinInfo->storedOnly);
    CHECK_FALSE(stdinInfo->tracked);
    CHECK_FALSE(nesso::core::sourceInfoFor(dir.path() / "missing", "message"));
}

TEST_CASE("v3 writer rejects invalid policies and unusable destinations", "[corpus][core]")
{
    const TempDir dir;
    CorpusPolicy invalid = policy();
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
    const auto rejected = CorpusWriter::create(dir.path() / "invalid.nesso", invalid);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error() == EngineError::CorruptDatabase);
    CHECK_FALSE(hasSpill(dir.path()));
}

TEST_CASE("v3 writer validates records and preserves an existing destination", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.path() / "corpus.nesso";
    writeBytes(path, "old corpus");
    {
        auto writer = CorpusWriter::create(path, policy());
        REQUIRE(writer);
        CHECK_FALSE(writer->addSource({.path = ""}));
        CHECK_FALSE(writer->addSource({.path = std::string{"a\0b", 3}}));
        REQUIRE(writer->addSource({.path = "-", .storedOnly = true}) == 0);
        CHECK_FALSE(writer->addDistinct("empty", {}));
        CHECK_FALSE(writer->addDistinct("wrong width", std::array<float, 1>{1.F}));
        CHECK_FALSE(
            writer->addDistinct("nonfinite", std::array<float, 2>{1.F, std::numeric_limits<float>::infinity()}));
        CHECK_FALSE(
            writer->addDistinct(std::string(8ULL * 1024ULL * 1024ULL + 1, 'x'), std::array<float, 2>{1.F, 0.F}));
        REQUIRE(writer->addDistinct("alpha", std::array<float, 2>{1.F, 0.F}) == 0);
        CHECK_FALSE(writer->addDistinct("alpha", std::array<float, 2>{1.F, 0.F}));
        const std::array<CorpusLine, 1> noStored{CorpusLine{.lineNumber = 1, .distinctId = 0}};
        CHECK_FALSE(writer->addLines(noStored));
        const std::array<CorpusLine, 1> badSource{
            CorpusLine{.lineNumber = 1, .sourceIndex = 1, .distinctId = 0, .storedText = "alpha"}};
        CHECK_FALSE(writer->addLines(badSource));
        const std::array<CorpusLine, 1> badId{CorpusLine{.lineNumber = 1, .distinctId = 1, .storedText = "alpha"}};
        CHECK_FALSE(writer->addLines(badId));
        const std::array<CorpusLine, 1> good{CorpusLine{.lineNumber = 1, .distinctId = 0, .storedText = "alpha"}};
        REQUIRE(writer->addLines(good));
        CHECK_FALSE(writer->addLines(good));
    }
    CHECK(readBytes(path) == "old corpus");
    CHECK_FALSE(hasSpill(dir.path()));
    CHECK_FALSE(CorpusWriter::create(dir.path(), policy()));
    CHECK_FALSE(CorpusWriter::create(dir.path() / "missing" / "corpus", policy()));
}

TEST_CASE("v3 writer detects unreferenced vectors and transfers spill ownership", "[corpus][core]")
{
    const TempDir dir;
    {
        auto first = CorpusWriter::create(dir.path() / "first", policy());
        auto second = CorpusWriter::create(dir.path() / "second", policy());
        REQUIRE(first);
        REQUIRE(second);
        *first = std::move(*second);
        REQUIRE(first->addDistinct("unused", std::array<float, 2>{1.F, 0.F}));
        const auto rejected = first->finish();
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == EngineError::CorruptDatabase);
    }
    CHECK_FALSE(hasSpill(dir.path()));
}

TEST_CASE("v3 streams lines before their distinct vectors", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.path() / "streamed.nesso";
    auto writer = CorpusWriter::create(path, policy());
    REQUIRE(writer);
    REQUIRE(writer->addSource({.path = "-", .storedOnly = true}) == 0);
    REQUIRE(writer->reserveDistinct("alpha") == 0);
    REQUIRE(writer->reserveDistinct("bravo") == 1);
    const std::array<CorpusLine, 3> lines{CorpusLine{.lineNumber = 1, .distinctId = 0, .storedText = "alpha"},
                                          CorpusLine{.lineNumber = 2, .distinctId = NO_DISTINCT, .storedText = ""},
                                          CorpusLine{.lineNumber = 3, .distinctId = 1, .storedText = "bravo"}};
    REQUIRE(writer->addLines(lines));
    CHECK_FALSE(writer->finish());
    CHECK_FALSE(writer->addVector(1, std::array<float, 2>{0.F, 1.F}));
    REQUIRE(writer->addVector(0, std::array<float, 2>{1.F, 0.F}));
    REQUIRE(writer->addVector(1, std::array<float, 2>{0.F, 1.F}));
    REQUIRE(writer->finish());
    const auto mapped = nesso::core::mapCorpusFile(path);
    REQUIRE(mapped);
    CHECK(mapped->lines().size() == 3);
    CHECK(mapped->texts().size() == 2);
    CHECK(mapped->matrix().size() == 4);
}

TEST_CASE("v3 rejects unsupported versions and malformed files", "[corpus][core]")
{
    const TempDir dir;
    const auto path = dir.path() / "corpus.nesso";
    writeExample(path, dir.path() / "app.log");
    std::string bytes = readBytes(path);
    const auto sourceOffset = getU64(bytes, 32);
    const auto lineOffset = getU64(bytes, 40);
    const auto matrixOffset = getU64(bytes, 48);
    const auto distinctOffset = getU64(bytes, 64);
    const auto textOffset = getU64(bytes, 72);
    const auto storedOffset = getU64(bytes, 80);
    EngineError expected = EngineError::CorruptDatabase;
    bool sealed = true;

    SECTION("v2")
    {
        putU32(bytes, 4, 2);
        expected = EngineError::UnsupportedVersion;
    }
    SECTION("future version")
    {
        putU32(bytes, 4, 4);
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
    SECTION("misaligned matrix") { putU64(bytes, 48, matrixOffset + 1); }
    SECTION("too many sources") { putU64(bytes, 16, 1'000'001); }
    SECTION("too many lines") { putU64(bytes, 24, 10'000'001); }
    SECTION("too many texts") { putU64(bytes, 56, 1'000'001); }
    SECTION("zero dimensions") { putU32(bytes, 12, 0); }
    SECTION("bad source section offset") { putU64(bytes, 32, sourceOffset + 8); }
    SECTION("bad line section size") { putU64(bytes, 40, lineOffset + 1); }
    SECTION("bad distinct section size") { putU64(bytes, 64, distinctOffset + 1); }
    SECTION("bad text section offset") { putU64(bytes, 72, textOffset + 1); }
    SECTION("bad stored section offset") { putU64(bytes, 80, matrixOffset + 1); }
    SECTION("reserved lexical offset") { putU64(bytes, 88, 1); }
    SECTION("reserved lexical length") { putU64(bytes, 96, 1); }
    SECTION("bad policy offset") { putU64(bytes, 104, 0); }
    SECTION("checksum mismatch")
    {
        bytes[matrixOffset] = static_cast<char>(bytes[matrixOffset] ^ 1);
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
    SECTION("bad source flags") { putU32(bytes, sourceOffset + 12, 4); }
    SECTION("bad source offset") { putU64(bytes, sourceOffset, sourceOffset); }
    SECTION("bad source length") { putU32(bytes, sourceOffset + 8, 0); }
    SECTION("reserved source field") { putU32(bytes, sourceOffset + 44, 1); }
    SECTION("bad line number") { putU64(bytes, lineOffset, 0); }
    SECTION("bad line order") { putU64(bytes, lineOffset + 56, 1); }
    SECTION("bad source reference") { putU32(bytes, lineOffset + 24, 99); }
    SECTION("bad distinct reference") { putU32(bytes, lineOffset + 28, 99); }
    SECTION("bad skipped flag") { putU32(bytes, lineOffset + 44, 4); }
    SECTION("bad source range") { putU64(bytes, lineOffset + 16, 1000); }
    SECTION("unexpected stored offset") { putU64(bytes, lineOffset + 32, storedOffset); }
    SECTION("stored-only source without text") { putU32(bytes, lineOffset + 4 * 56 + 44, 0); }
    SECTION("bad stored text offset") { putU64(bytes, lineOffset + 4 * 56 + 32, 0); }
    SECTION("reserved line field") { putU64(bytes, lineOffset + 48, 1); }
    SECTION("bad distinct text offset") { putU64(bytes, distinctOffset, textOffset + 1); }
    SECTION("bad distinct text length") { putU32(bytes, distinctOffset + 8, 8U * 1024U * 1024U + 1U); }
    SECTION("reserved distinct field") { putU32(bytes, distinctOffset + 12, 1); }
    SECTION("duplicate distinct texts") { bytes.replace(textOffset + 5, 5, "alpha"); }
    SECTION("unreferenced distinct vector")
    {
        putU32(bytes, lineOffset + 2 * 56 + 28, 0);
        putU32(bytes, lineOffset + 4 * 56 + 28, 0);
    }
    if (sealed)
    {
        reseal(bytes);
    }
    writeBytes(path, bytes);
    std::uint32_t observed = 0;
    const auto result = nesso::core::mapCorpusFile(path, &observed);
    REQUIRE_FALSE(result);
    CHECK(result.error() == expected);
    if (expected == EngineError::UnsupportedVersion)
    {
        CHECK(observed != nesso::core::CORPUS_VERSION);
    }
}
