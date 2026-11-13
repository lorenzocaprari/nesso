// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/top_k.hpp>
#include <embed/embedder.hpp>
#include <nesso/nesso.hpp>

#include "test_embedder_identity.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{

class TempDir
{
  public:
    explicit TempDir(const std::string &name)
        : path_(std::filesystem::temp_directory_path() / (name + "_" + std::to_string(getpid())))
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    TempDir(TempDir &&) = delete;
    TempDir &operator=(TempDir &&) = delete;

    ~TempDir() { std::filesystem::remove_all(path_); }

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

class FixedEmbedder final : public nesso::embed::Embedder
{
  public:
    [[nodiscard]] size_t sessions() const noexcept override { return 1; }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        return testEmbedderIdentity();
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    embedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
    {
        std::vector<std::vector<float>> rows;
        rows.reserve(texts.size());
        for (const std::string &text : texts)
        {
            std::uint32_t hash = 2166136261u;
            for (const char byte : text)
            {
                hash ^= static_cast<unsigned char>(byte);
                hash *= 16777619u;
            }
            rows.push_back({static_cast<float>(hash % 97U), 0.F, 0.F, 0.F});
        }
        if (progress)
        {
            progress(texts.size(), texts.size());
        }
        return rows;
    }
};

void writeLog(const std::filesystem::path &path, size_t uniqueLines)
{
    std::ofstream output(path);
    REQUIRE(output);
    for (size_t index = 0; index < uniqueLines; ++index)
    {
        output << "database connection " << index << '\n';
    }
    output << '\n';
    output << " \t\n";
    output << "database connection 0\n";
    output << "database connection 3\n";
    REQUIRE(output);
}

std::vector<char> readBytes(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct Seen
{
    std::string source;
    std::uint64_t line = 0;
    std::uint32_t bits = 0;
    std::string text;
    std::uint32_t occurrences = 0;
    bool arrayElement = false;

    bool operator==(const Seen &) const = default;
};

std::vector<Seen> freeze(const nesso::TextResults &results)
{
    std::vector<Seen> seen;
    seen.reserve(results.matches.size());
    for (const nesso::TextMatch &match : results.matches)
    {
        seen.push_back(Seen{.source = match.source,
                            .line = match.line,
                            .bits = std::bit_cast<std::uint32_t>(match.score),
                            .text = match.text,
                            .occurrences = match.occurrences,
                            .arrayElement = match.arrayElement});
    }
    return seen;
}

nesso::Nesso app(size_t threads)
{
    return nesso::Nesso{nesso::Config{.modelDir = {}, .threads = threads}, std::make_unique<FixedEmbedder>()};
}

} // namespace

TEST_CASE("search rankings match across thread counts and corpus bytes do not change", "[nesso][text][Unit]")
{
    const TempDir dir{"nesso_scan_threads"};
    const size_t uniqueLines = nesso::core::PARALLEL_MIN_ROWS + 5;
    const auto log = dir.path() / "app.log";
    writeLog(log, uniqueLines);

    const std::array<size_t, 6> budgets{1, 2, 3, 4, 7, 0};
    std::vector<std::vector<char>> corpora;
    corpora.reserve(budgets.size());
    for (const size_t threads : budgets)
    {
        const auto corpus = dir.path() / ("corpus-" + std::to_string(threads) + ".nesso");
        const auto indexed = app(threads).index({.files = {log}, .output = corpus});
        REQUIRE(indexed.has_value());
        corpora.push_back(readBytes(corpus));
    }
    for (const std::vector<char> &corpus : corpora)
    {
        CHECK(corpus == corpora.front());
    }

    const auto corpus = dir.path() / "corpus-1.nesso";
    const std::array<size_t, 4> limits{1, 8, uniqueLines, uniqueLines + 10};
    for (const size_t limit : limits)
    {
        const auto serial = app(1).search(
            {.query = "database connection", .index = corpus, .topK = limit, .mode = nesso::SearchMode::Dense});
        REQUIRE(serial.has_value());
        REQUIRE_FALSE(serial->matches.empty());
        const std::vector<Seen> expected = freeze(*serial);
        for (const size_t threads : budgets)
        {
            const auto ranked = app(threads).search(
                {.query = "database connection", .index = corpus, .topK = limit, .mode = nesso::SearchMode::Dense});
            REQUIRE(ranked.has_value());
            CHECK(freeze(*ranked) == expected);
        }
        const auto grepped = app(4).grep(
            {.query = "database connection", .files = {log}, .topK = limit, .mode = nesso::SearchMode::Dense});
        REQUIRE(grepped.has_value());
        CHECK(freeze(*grepped) == expected);
    }

    const auto zeroA = app(1).search({.query = "database connection", .index = corpus, .topK = 0});
    const auto zeroB = app(4).search({.query = "database connection", .index = corpus, .topK = 0});
    REQUIRE_FALSE(zeroA.has_value());
    REQUIRE_FALSE(zeroB.has_value());
    CHECK(zeroA.error().kind == zeroB.error().kind);

    const auto smallLog = dir.path() / "small.log";
    writeLog(smallLog, 12);
    const auto smallCorpus = dir.path() / "small.nesso";
    REQUIRE(app(1).index({.files = {smallLog}, .output = smallCorpus}).has_value());
    const auto smallSerial = app(1).search(
        {.query = "database connection", .index = smallCorpus, .topK = 6, .mode = nesso::SearchMode::Dense});
    const auto smallParallel = app(7).search(
        {.query = "database connection", .index = smallCorpus, .topK = 6, .mode = nesso::SearchMode::Dense});
    REQUIRE(smallSerial.has_value());
    REQUIRE(smallParallel.has_value());
    CHECK(freeze(*smallParallel) == freeze(*smallSerial));
    const auto smallGrep =
        app(3).grep({.query = "database connection", .files = {smallLog}, .topK = 6, .mode = nesso::SearchMode::Dense});
    REQUIRE(smallGrep.has_value());
    CHECK(freeze(*smallGrep) == freeze(*smallSerial));
}
