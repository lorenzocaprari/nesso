// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/batch_plan.hpp>
#include <embed/embedder.hpp>
#include <nesso/nesso.hpp>

#include "test_embedder_identity.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
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

// Letters only, so the masked dedup key keeps every line distinct.
std::string LetterSuffix(std::size_t index)
{
    std::string suffix;
    do
    {
        suffix.push_back(static_cast<char>('a' + static_cast<int>(index % 26)));
        index /= 26;
    } while (index > 0);
    return suffix;
}

std::string LineText(std::size_t index) { return "disk event " + LetterSuffix(index); }

void WriteScoredLines(const std::filesystem::path &path, std::size_t count)
{
    std::ofstream output(path);
    REQUIRE(output);
    for (std::size_t index = 0; index < count; ++index)
    {
        output << LineText(index) << '\n';
    }
    REQUIRE(output);
}

// Each text embeds to {score, 0, 0, 0}; the query embeds to {1, 0, 0, 0}, so
// the dense score is the table value. Unknown texts score 0.
class TableEmbedder final : public nesso::embed::Embedder
{
  public:
    TableEmbedder(std::unordered_map<std::string, float> scores, std::size_t fail_on_batch)
        : scores_(std::move(scores)), fail_on_batch_(fail_on_batch)
    {
    }

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
        ++batches_;
        if (batches_ == fail_on_batch_)
        {
            return std::unexpected(nesso::embed::EmbedError::InferenceFailure);
        }
        std::vector<std::vector<float>> rows;
        rows.reserve(texts.size());
        for (const std::string &text : texts)
        {
            const auto found = scores_.find(text);
            rows.push_back({found == scores_.end() ? 0.F : found->second, 0.F, 0.F, 0.F});
        }
        if (progress)
        {
            progress(texts.size(), texts.size());
        }
        return rows;
    }

  private:
    std::unordered_map<std::string, float> scores_;
    std::size_t fail_on_batch_;
    mutable std::size_t batches_ = 0;
};

struct Row
{
    std::string source;
    std::uint64_t line = 0;
    std::uint32_t bits = 0;
    std::string text;
    std::uint32_t occurrences = 0;
    std::string mode;
    std::optional<std::uint32_t> dense;
    std::optional<std::uint32_t> lexical;

    bool operator==(const Row &) const = default;
};

std::optional<std::uint32_t> Bits(std::optional<float> value)
{
    if (!value)
    {
        return std::nullopt;
    }
    return std::bit_cast<std::uint32_t>(*value);
}

std::vector<Row> Freeze(const nesso::TextResults &results)
{
    std::vector<Row> rows;
    rows.reserve(results.matches.size());
    for (const nesso::TextMatch &match : results.matches)
    {
        rows.push_back(Row{.source = match.source,
                           .line = match.line,
                           .bits = std::bit_cast<std::uint32_t>(match.score),
                           .text = match.text,
                           .occurrences = match.occurrences,
                           .mode = match.mode,
                           .dense = Bits(match.dense),
                           .lexical = Bits(match.lexical)});
    }
    return rows;
}

bool SameResults(const nesso::TextResults &left, const nesso::TextResults &right)
{
    return Freeze(left) == Freeze(right) && left.skipped_lines == right.skipped_lines &&
           left.empty_lines == right.empty_lines && left.multiple_sources == right.multiple_sources &&
           left.lexical_fallback == right.lexical_fallback && left.grouped == right.grouped;
}

struct Call
{
    std::vector<Row> rows;
    std::size_t lines_done = 0;
    std::size_t lines_total = 0;
};

class RecordingObserver final : public nesso::GrepObserver
{
  public:
    void OnProvisional(const nesso::TextResults &results, size_t lines_done, size_t lines_total) override
    {
        calls.push_back({.rows = Freeze(results), .lines_done = lines_done, .lines_total = lines_total});
    }

    void OnScanDone() noexcept override { ++scan_done; }

    std::vector<Call> calls;
    std::size_t scan_done = 0;
};

constexpr std::size_t WINDOW = nesso::embed::BATCH_WINDOW;
constexpr std::size_t LINES = (2 * WINDOW) + 52;

nesso::Nesso App(std::unordered_map<std::string, float> scores, std::size_t fail_on_batch = 0)
{
    return nesso::Nesso{nesso::Config{}, std::make_unique<TableEmbedder>(std::move(scores), fail_on_batch)};
}

// Every flush finds a better line, so the top-k changes at each one.
std::unordered_map<std::string, float> RisingScores()
{
    std::unordered_map<std::string, float> scores;
    for (std::size_t index = 0; index < LINES; ++index)
    {
        scores[LineText(index)] = static_cast<float>(index) / static_cast<float>(LINES);
    }
    return scores;
}

// The best lines are all in the first window; later lines score 0.
std::unordered_map<std::string, float> EarlyBestScores()
{
    std::unordered_map<std::string, float> scores;
    for (std::size_t index = 0; index < 10; ++index)
    {
        scores[LineText(index * 7)] = 0.9F - (static_cast<float>(index) / 100.F);
    }
    return scores;
}

} // namespace

TEST_CASE("provisional results arrive at flush boundaries only", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_flush"};
    const auto log = dir.Path() / "app.log";
    WriteScoredLines(log, LINES);

    RecordingObserver observer;
    const auto result = App(RisingScores())
                            .Grep({.query = "disk event",
                                   .files = {log},
                                   .top_k = 3,
                                   .mode = nesso::SearchMode::Dense,
                                   .observer = &observer});
    REQUIRE(result.has_value());
    REQUIRE(observer.calls.size() == 3);
    CHECK(observer.calls[0].lines_done == WINDOW);
    CHECK(observer.calls[1].lines_done == 2 * WINDOW);
    CHECK(observer.calls[2].lines_done == LINES);
    for (const Call &call : observer.calls)
    {
        CHECK(call.lines_total == LINES);
        REQUIRE(call.rows.size() == 3);
        for (const Row &row : call.rows)
        {
            CHECK(row.mode == "dense");
            CHECK(row.dense == std::optional<std::uint32_t>{row.bits});
            CHECK_FALSE(row.lexical.has_value());
        }
    }
    // The best line so far is the last line of each flushed window.
    CHECK(observer.calls[0].rows.front().line == WINDOW);
    CHECK(observer.calls[1].rows.front().line == 2 * WINDOW);
    CHECK(observer.calls[2].rows == Freeze(*result));
    CHECK(observer.scan_done == 1);
}

TEST_CASE("an unchanged top-k is not reported again", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_same"};
    const auto log = dir.Path() / "app.log";
    WriteScoredLines(log, LINES);

    RecordingObserver observer;
    const auto result = App(EarlyBestScores())
                            .Grep({.query = "disk event",
                                   .files = {log},
                                   .top_k = 5,
                                   .mode = nesso::SearchMode::Dense,
                                   .observer = &observer});
    REQUIRE(result.has_value());
    REQUIRE(observer.calls.size() == 1);
    CHECK(observer.calls.front().lines_done == WINDOW);
    CHECK(observer.calls.front().rows == Freeze(*result));
    CHECK(observer.scan_done == 1);
}

TEST_CASE("final results do not depend on an observer in any mode", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_modes"};
    const auto log = dir.Path() / "app.log";
    WriteScoredLines(log, LINES);

    const auto mode = GENERATE(nesso::SearchMode::Dense, nesso::SearchMode::Hybrid, nesso::SearchMode::Lexical,
                               nesso::SearchMode::Auto);
    const bool group = GENERATE(false, true);
    const auto query = GENERATE(as<std::string>{}, "disk event", "event ab");
    nesso::GrepRequest request{.query = query, .files = {log}, .top_k = 4, .mode = mode, .group = group};

    const auto plain = App(RisingScores()).Grep(request);
    RecordingObserver observer;
    request.observer = &observer;
    const auto observed = App(RisingScores()).Grep(request);
    REQUIRE(plain.has_value());
    REQUIRE(observed.has_value());
    CHECK(SameResults(*plain, *observed));
    CHECK(observer.scan_done == 1);
    REQUIRE_FALSE(observer.calls.empty());
    for (const Call &call : observer.calls)
    {
        for (const Row &row : call.rows)
        {
            CHECK(row.mode == "dense");
            CHECK_FALSE(row.lexical.has_value());
        }
    }
}

TEST_CASE("scan done is reported once when embedding fails", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_error"};
    const auto log = dir.Path() / "app.log";
    WriteScoredLines(log, LINES);

    const std::size_t failing_batch = GENERATE(1U, 2U);
    RecordingObserver observer;
    const auto result =
        App(RisingScores(), failing_batch)
            .Grep({.query = "disk event", .files = {log}, .mode = nesso::SearchMode::Hybrid, .observer = &observer});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == nesso::ErrorKind::Embed);
    CHECK(observer.calls.size() == failing_batch - 1);
    CHECK(observer.scan_done == 1);
}

TEST_CASE("scan done is reported once when grep fails before embedding", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_empty"};
    const auto log = dir.Path() / "empty.log";
    {
        std::ofstream output(log);
        output << "\n";
    }
    RecordingObserver observer;
    const auto result = App({}).Grep({.query = "disk", .files = {log}, .observer = &observer});
    REQUIRE_FALSE(result.has_value());
    CHECK(observer.calls.empty());
    CHECK(observer.scan_done == 1);
}
