// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "corpus_detail.hpp"

#include <catch2/catch_all.hpp>
#include <core/corpus_index.hpp>
#include <core/lexical_terms.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace
{

using nesso::core::EngineError;
using nesso::core::TermByteLess;
using nesso::core::detail::EncodeLexicalSection;
using nesso::core::detail::EncodeLexicalSectionSpilled;
using nesso::core::detail::MAX_TEXT_BYTES;
using nesso::core::detail::WriteRaw;
using nesso::core::detail::WriteU32;

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
               ("nesso_lexical_spill_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
    }

    std::filesystem::path path_;
};

void WriteTermsSpill(const std::filesystem::path &path, std::span<const std::vector<std::string>> line_terms)
{
    std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
    REQUIRE(output);
    for (const std::vector<std::string> &row : line_terms)
    {
        REQUIRE(WriteU32(output, static_cast<std::uint32_t>(row.size())));
        for (const std::string &term : row)
        {
            REQUIRE(WriteU32(output, static_cast<std::uint32_t>(term.size())));
            REQUIRE(WriteRaw(output, term.data(), term.size()));
        }
    }
    output.close();
    REQUIRE(output);
}

std::vector<std::byte> ReadAllBytes(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0);
    std::vector<std::byte> bytes(size);
    if (size != 0)
    {
        REQUIRE(input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size)));
    }
    return bytes;
}

bool HasNamedSpill(const std::filesystem::path &directory, std::string_view needle)
{
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.path().filename().string().find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

void ExpectSpilledMatchesOracle(std::span<const std::vector<std::string>> line_terms, std::uint64_t budget)
{
    const TempDir dir;
    const auto terms_path = dir.Path() / "terms.spill";
    WriteTermsSpill(terms_path, line_terms);
    const auto oracle = EncodeLexicalSection(line_terms);
    const auto spilled = EncodeLexicalSectionSpilled(terms_path, line_terms.size(), dir.Path() / "out", budget);
    REQUIRE(spilled);
    CHECK(spilled->bytes == oracle.size());
    CHECK(ReadAllBytes(spilled->path) == oracle);
    std::filesystem::remove(spilled->path);
    CHECK_FALSE(HasNamedSpill(dir.Path(), ".lexrun."));
    CHECK_FALSE(HasNamedSpill(dir.Path(), ".lexical."));
}

} // namespace

TEST_CASE("spilled lexical encode matches oracle for empty input", "[corpus][lexical][spill]")
{
    ExpectSpilledMatchesOracle({}, 1024);
}

TEST_CASE("spilled lexical encode matches oracle for one chunk", "[corpus][lexical][spill]")
{
    const std::vector<std::vector<std::string>> lines{{"alpha", "bravo"}, {"alpha"}, {}};
    ExpectSpilledMatchesOracle(lines, 1ULL << 20);
}

TEST_CASE("spilled lexical encode matches oracle across many chunks", "[corpus][lexical][spill]")
{
    std::vector<std::vector<std::string>> lines;
    for (int index = 0; index < 40; ++index)
    {
        lines.push_back({"term" + std::to_string(index), "shared", "z-last"});
    }
    // Tiny budget forces a flush after each line's unique footprint.
    ExpectSpilledMatchesOracle(lines, 96);
}

TEST_CASE("spilled lexical encode keeps terms that span chunks", "[corpus][lexical][spill]")
{
    const std::vector<std::vector<std::string>> lines{{"shared", "aaa"}, {"shared", "mmm"}, {"shared", "zzz"}};
    ExpectSpilledMatchesOracle(lines, 80);
}

TEST_CASE("spilled lexical encode sorts dictionary with TermByteLess", "[corpus][lexical][spill]")
{
    const std::vector<std::vector<std::string>> lines{{"\xFF", "a", ""}, {"a", "b"}};
    ExpectSpilledMatchesOracle(lines, 64);
    CHECK(TermByteLess(std::string{""}, std::string{"a"}));
}

TEST_CASE("spilled lexical encode rejects a zero memory budget", "[corpus][lexical][spill]")
{
    const TempDir dir;
    const auto terms_path = dir.Path() / "terms.spill";
    WriteTermsSpill(terms_path, std::vector<std::vector<std::string>>{{"a"}});
    const auto spilled = EncodeLexicalSectionSpilled(terms_path, 1, dir.Path() / "out", 0);
    REQUIRE_FALSE(spilled);
    CHECK(spilled.error() == EngineError::CorruptDatabase);
}

TEST_CASE("spilled lexical encode rejects a missing terms spill", "[corpus][lexical][spill]")
{
    const TempDir dir;
    const auto spilled = EncodeLexicalSectionSpilled(dir.Path() / "missing.terms", 1, dir.Path() / "out", 1024);
    REQUIRE_FALSE(spilled);
    CHECK(spilled.error() == EngineError::FileOpenFailure);
}

TEST_CASE("spilled lexical encode rejects truncated and overlong term rows", "[corpus][lexical][spill]")
{
    const TempDir dir;
    SECTION("empty spill with a claimed line")
    {
        const auto terms_path = dir.Path() / "empty.spill";
        REQUIRE(std::ofstream(terms_path, std::ios::binary | std::ios::trunc));
        const auto spilled = EncodeLexicalSectionSpilled(terms_path, 1, dir.Path() / "out", 1024);
        REQUIRE_FALSE(spilled);
        CHECK(spilled.error() == EngineError::FileOpenFailure);
    }
    SECTION("term length past MAX_TEXT_BYTES")
    {
        const auto terms_path = dir.Path() / "overlong.spill";
        std::ofstream output(terms_path, std::ios::binary | std::ios::trunc);
        REQUIRE(output);
        REQUIRE(WriteU32(output, 1));
        REQUIRE(WriteU32(output, static_cast<std::uint32_t>(MAX_TEXT_BYTES + 1)));
        output.close();
        REQUIRE(output);
        const auto spilled = EncodeLexicalSectionSpilled(terms_path, 1, dir.Path() / "out", 1024);
        REQUIRE_FALSE(spilled);
        CHECK(spilled.error() == EngineError::FileOpenFailure);
    }
    SECTION("term body shorter than the length prefix")
    {
        const auto terms_path = dir.Path() / "short-body.spill";
        std::ofstream output(terms_path, std::ios::binary | std::ios::trunc);
        REQUIRE(output);
        REQUIRE(WriteU32(output, 1));
        REQUIRE(WriteU32(output, 8));
        REQUIRE(WriteRaw(output, "ab", 2));
        output.close();
        REQUIRE(output);
        const auto spilled = EncodeLexicalSectionSpilled(terms_path, 1, dir.Path() / "out", 1024);
        REQUIRE_FALSE(spilled);
        CHECK(spilled.error() == EngineError::FileOpenFailure);
    }
    SECTION("trailing bytes after complete rows")
    {
        const auto terms_path = dir.Path() / "trailing.spill";
        WriteTermsSpill(terms_path, std::vector<std::vector<std::string>>{{"alpha"}});
        {
            std::ofstream output(terms_path, std::ios::binary | std::ios::app);
            REQUIRE(output);
            REQUIRE(WriteRaw(output, "x", 1));
        }
        const auto spilled = EncodeLexicalSectionSpilled(terms_path, 1, dir.Path() / "out", 1024);
        REQUIRE_FALSE(spilled);
        CHECK(spilled.error() == EngineError::CorruptDatabase);
    }
}

TEST_CASE("spilled lexical encode rejects a missing spill parent directory", "[corpus][lexical][spill]")
{
    const TempDir dir;
    const auto terms_path = dir.Path() / "terms.spill";
    WriteTermsSpill(terms_path, std::vector<std::vector<std::string>>{{"alpha"}, {"bravo"}});
    const auto spilled = EncodeLexicalSectionSpilled(terms_path, 2, dir.Path() / "missing" / "out", 96);
    REQUIRE_FALSE(spilled);
    CHECK(spilled.error() == EngineError::FileOpenFailure);
}

TEST_CASE("spilled lexical encode keeps duplicate terms in one row", "[corpus][lexical][spill]")
{
    ExpectSpilledMatchesOracle(std::vector<std::vector<std::string>>{{"same", "same", "other"}}, 1024);
}
