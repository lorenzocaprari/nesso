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
#include <thread>
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

    [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

  private:
    std::filesystem::path path_;
};

class FixedEmbedder final : public nesso::embed::Embedder
{
  public:
    [[nodiscard]] size_t sessions() const noexcept override { return 1; }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        return TestEmbedderIdentity();
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
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

void WriteLog(const std::filesystem::path &path, size_t unique_lines)
{
    std::ofstream output(path);
    REQUIRE(output);
    for (size_t index = 0; index < unique_lines; ++index)
    {
        output << "database connection " << index << '\n';
    }
    output << '\n';
    output << " \t\n";
    output << "database connection 0\n";
    output << "database connection 3\n";
    REQUIRE(output);
}

std::vector<char> ReadBytes(const std::filesystem::path &path)
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
    bool array_element = false;

    bool operator==(const Seen &) const = default;
};

std::vector<Seen> Freeze(const nesso::TextResults &results)
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
                            .array_element = match.array_element});
    }
    return seen;
}

// Embeds on a worker thread, like a session pool, and reports progress there.
class WorkerEmbedder final : public nesso::embed::Embedder
{
  public:
    [[nodiscard]] size_t sessions() const noexcept override { return 2; }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        return TestEmbedderIdentity();
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
    {
        std::vector<std::vector<float>> rows;
        std::thread worker{[&texts, &rows]
                           {
                               rows.reserve(texts.size());
                               for (const std::string &text : texts)
                               {
                                   rows.push_back({static_cast<float>(text.size() % 31U), 0.F, 0.F, 0.F});
                               }
                           }};
        worker.join();
        if (progress)
        {
            progress(texts.size(), texts.size());
        }
        return rows;
    }
};

class ThreadObserver final : public nesso::GrepObserver
{
  public:
    void OnProvisional(const nesso::TextResults &, size_t, size_t) override
    {
        threads.push_back(std::this_thread::get_id());
    }

    void OnScanDone() noexcept override { threads.push_back(std::this_thread::get_id()); }

    std::vector<std::thread::id> threads;
};

std::string Letters(std::size_t index)
{
    std::string text;
    do
    {
        text.push_back(static_cast<char>('a' + static_cast<int>(index % 26)));
        index /= 26;
    } while (index > 0);
    return text;
}

nesso::Nesso App(size_t threads)
{
    return nesso::Nesso{nesso::Config{.model_dir = {}, .threads = threads}, std::make_unique<FixedEmbedder>()};
}

} // namespace

TEST_CASE("search rankings match across thread counts and corpus bytes do not change", "[nesso][text][Unit]")
{
    const TempDir dir{"nesso_scan_threads"};
    const size_t unique_lines = nesso::core::PARALLEL_MIN_ROWS + 5;
    const auto log = dir.Path() / "app.log";
    WriteLog(log, unique_lines);

    const std::array<size_t, 6> budgets{1, 2, 3, 4, 7, 0};
    std::vector<std::vector<char>> corpora;
    corpora.reserve(budgets.size());
    for (const size_t threads : budgets)
    {
        const auto corpus = dir.Path() / ("corpus-" + std::to_string(threads) + ".nesso");
        const auto indexed = App(threads).Index({.files = {log}, .output = corpus});
        REQUIRE(indexed.has_value());
        corpora.push_back(ReadBytes(corpus));
    }
    for (const std::vector<char> &corpus : corpora)
    {
        CHECK(corpus == corpora.front());
    }

    const auto corpus = dir.Path() / "corpus-1.nesso";
    const std::array<size_t, 4> limits{1, 8, unique_lines, unique_lines + 10};
    for (const size_t limit : limits)
    {
        const auto serial = App(1).Search(
            {.query = "database connection", .index = corpus, .top_k = limit, .mode = nesso::SearchMode::Dense});
        REQUIRE(serial.has_value());
        REQUIRE_FALSE(serial->matches.empty());
        const std::vector<Seen> expected = Freeze(*serial);
        for (const size_t threads : budgets)
        {
            const auto ranked = App(threads).Search(
                {.query = "database connection", .index = corpus, .top_k = limit, .mode = nesso::SearchMode::Dense});
            REQUIRE(ranked.has_value());
            CHECK(Freeze(*ranked) == expected);
        }
        const auto grepped = App(4).Grep(
            {.query = "database connection", .files = {log}, .top_k = limit, .mode = nesso::SearchMode::Dense});
        REQUIRE(grepped.has_value());
        CHECK(Freeze(*grepped) == expected);
    }

    const auto zero_a = App(1).Search({.query = "database connection", .index = corpus, .top_k = 0});
    const auto zero_b = App(4).Search({.query = "database connection", .index = corpus, .top_k = 0});
    REQUIRE_FALSE(zero_a.has_value());
    REQUIRE_FALSE(zero_b.has_value());
    CHECK(zero_a.error().kind == zero_b.error().kind);

    const auto small_log = dir.Path() / "small.log";
    WriteLog(small_log, 12);
    const auto small_corpus = dir.Path() / "small.nesso";
    REQUIRE(App(1).Index({.files = {small_log}, .output = small_corpus}).has_value());
    const auto small_serial = App(1).Search(
        {.query = "database connection", .index = small_corpus, .top_k = 6, .mode = nesso::SearchMode::Dense});
    const auto small_parallel = App(7).Search(
        {.query = "database connection", .index = small_corpus, .top_k = 6, .mode = nesso::SearchMode::Dense});
    REQUIRE(small_serial.has_value());
    REQUIRE(small_parallel.has_value());
    CHECK(Freeze(*small_parallel) == Freeze(*small_serial));
    const auto small_grep = App(3).Grep(
        {.query = "database connection", .files = {small_log}, .top_k = 6, .mode = nesso::SearchMode::Dense});
    REQUIRE(small_grep.has_value());
    CHECK(Freeze(*small_grep) == Freeze(*small_serial));
}

TEST_CASE("grep observer calls run on the thread that called grep", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_thread"};
    const auto log = dir.Path() / "app.log";
    {
        std::ofstream output(log);
        REQUIRE(output);
        for (std::size_t index = 0; index < 2100; ++index)
        {
            output << "disk event " << Letters(index * 37) << '\n';
        }
    }
    ThreadObserver observer;
    const nesso::Nesso app{nesso::Config{}, std::make_unique<WorkerEmbedder>()};
    const auto result = app.Grep({.query = "disk", .files = {log}, .observer = &observer});
    REQUIRE(result.has_value());
    REQUIRE(observer.threads.size() >= 2);
    for (const std::thread::id &thread : observer.threads)
    {
        CHECK(thread == std::this_thread::get_id());
    }
}
