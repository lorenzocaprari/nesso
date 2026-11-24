// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/dedup.hpp>
#include <embed/batch_plan.hpp>
#include <embed/embedder.hpp>
#include <nesso/nesso.hpp>

#include "test_embedder_identity.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
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
    auto operator=(const TempDir &) -> TempDir & = delete;
    TempDir(TempDir &&) = delete;
    auto operator=(TempDir &&) -> TempDir & = delete;
    ~TempDir() { std::filesystem::remove_all(path_); }

    [[nodiscard]] auto Path() const -> const std::filesystem::path & { return path_; }

  private:
    std::filesystem::path path_;
};

// Letters only, so the masked dedup key keeps every line distinct.
auto LetterSuffix(std::size_t index) -> std::string
{
    std::string suffix;
    do
    {
        suffix.push_back(static_cast<char>('a' + static_cast<int>(index % 26)));
        index /= 26;
    } while (index > 0);
    return suffix;
}

auto LineText(std::size_t index) -> std::string { return "disk event " + LetterSuffix(index); }

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

    [[nodiscard]] auto sessions() const noexcept -> size_t override { return 1; }

    [[nodiscard]] auto identity() const
        -> std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> override
    {
        return TestEmbedderIdentity();
    }

    [[nodiscard]] auto embed(std::string_view) const
        -> std::expected<std::vector<float>, nesso::embed::EmbedError> override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] auto EmbedBatch(std::span<const std::string> texts, ProgressCallback progress) const
        -> std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError> override
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

    auto operator==(const Row &) const -> bool = default;
};

auto Bits(std::optional<float> value) -> std::optional<std::uint32_t>
{
    if (!value)
    {
        return std::nullopt;
    }
    return std::bit_cast<std::uint32_t>(*value);
}

auto Freeze(const nesso::TextResults &results) -> std::vector<Row>
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

auto SameResults(const nesso::TextResults &left, const nesso::TextResults &right) -> bool
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

auto App(std::unordered_map<std::string, float> scores, std::size_t fail_on_batch = 0) -> nesso::Nesso
{
    return nesso::Nesso{nesso::Config{}, std::make_unique<TableEmbedder>(std::move(scores), fail_on_batch)};
}

// Every flush finds a better line, so the top-k changes at each one.
auto RisingScores() -> std::unordered_map<std::string, float>
{
    std::unordered_map<std::string, float> scores;
    for (std::size_t index = 0; index < LINES; ++index)
    {
        scores[LineText(index)] = static_cast<float>(index) / static_cast<float>(LINES);
    }
    return scores;
}

// The best lines are all in the first window; later lines score 0.
auto EarlyBestScores() -> std::unordered_map<std::string, float>
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

namespace
{

class CinGuard
{
  public:
    explicit CinGuard(std::istream &input) : previous_(std::cin.rdbuf(input.rdbuf())) { std::cin.clear(); }

    CinGuard(const CinGuard &) = delete;
    auto operator=(const CinGuard &) -> CinGuard & = delete;
    CinGuard(CinGuard &&) = delete;
    auto operator=(CinGuard &&) -> CinGuard & = delete;

    ~CinGuard()
    {
        std::cin.rdbuf(previous_);
        std::cin.clear();
    }

  private:
    std::streambuf *previous_;
};

auto Fnv(std::string_view text, std::uint64_t hash = 1469598103934665603ULL) -> std::uint64_t
{
    for (const char byte : text)
    {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// The score of a text depends on the other texts of its batch, the way an int8
// vector depends on its padded run. A window with other members or another
// member order gives other scores.
auto WindowScore(std::string_view text, std::span<const std::string> batch) -> float
{
    std::uint64_t tag = 0;
    for (std::size_t index = 0; index < batch.size(); ++index)
    {
        tag = Fnv(batch[index], tag + index);
    }
    const float base = text.starts_with("kernel panic") ? 2.F : static_cast<float>(Fnv(text) % 1000) / 1000.F;
    return base + (static_cast<float>(tag % 97) / 100000.F);
}

class WindowEmbedder final : public nesso::embed::Embedder
{
  public:
    [[nodiscard]] auto sessions() const noexcept -> size_t override { return 1; }

    [[nodiscard]] auto identity() const
        -> std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> override
    {
        return TestEmbedderIdentity();
    }

    [[nodiscard]] auto embed(std::string_view) const
        -> std::expected<std::vector<float>, nesso::embed::EmbedError> override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] auto EmbedBatch(std::span<const std::string> texts, ProgressCallback progress) const
        -> std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError> override
    {
        batches_->emplace_back(texts.begin(), texts.end());
        std::vector<std::vector<float>> rows;
        rows.reserve(texts.size());
        for (const std::string &text : texts)
        {
            rows.push_back({WindowScore(text, texts), 0.F, 0.F, 0.F});
        }
        if (progress)
        {
            progress(texts.size(), texts.size());
        }
        return rows;
    }

    explicit WindowEmbedder(std::vector<std::vector<std::string>> &batches) : batches_(&batches) {}

  private:
    std::vector<std::vector<std::string>> *batches_;
};

// Three windows. Window 0 ends with a line repeated past -k, window 1 holds
// lines that share one masked key with different raw text, and the needle is
// the last line of window 2.
auto WindowedLines() -> std::vector<std::string>
{
    std::vector<std::string> lines;
    for (std::size_t index = 0; index < WINDOW; ++index)
    {
        lines.push_back(LineText(index));
    }
    for (int repeat = 0; repeat < 6; ++repeat)
    {
        lines.push_back(LineText(1));
    }
    for (int job = 1; job <= 7; ++job)
    {
        lines.push_back("job " + std::to_string(job) + " done");
    }
    for (std::size_t index = WINDOW; index < (2 * WINDOW) + 40; ++index)
    {
        lines.push_back(LineText(index));
    }
    lines.emplace_back("kernel panic on node");
    return lines;
}

auto Joined(const std::vector<std::string> &lines) -> std::string
{
    std::string text;
    for (const std::string &line : lines)
    {
        text += line;
        text += '\n';
    }
    return text;
}

void WriteText(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream output(path);
    REQUIRE(output);
    output << text;
    REQUIRE(output);
}

// Input order, the way main embeds: ids in first-occurrence order, one batch
// per window of WINDOW ids.
struct Reference
{
    std::vector<std::uint32_t> ids;
    std::vector<std::vector<std::string>> windows;
    std::vector<float> scores;
};

auto InputOrderReference(const std::vector<std::string> &lines) -> Reference
{
    Reference reference;
    nesso::core::DistinctIndex distinct;
    std::vector<std::string> texts;
    for (const std::string &line : lines)
    {
        const std::uint32_t entry_id = distinct.Add(line);
        reference.ids.push_back(entry_id);
        if (entry_id == texts.size())
        {
            texts.push_back(line);
        }
    }
    for (std::size_t begin = 0; begin < texts.size(); begin += WINDOW)
    {
        const std::size_t end = std::min(begin + WINDOW, texts.size());
        std::vector<std::string> window(texts.begin() + static_cast<std::ptrdiff_t>(begin),
                                        texts.begin() + static_cast<std::ptrdiff_t>(end));
        for (const std::string &text : window)
        {
            reference.scores.push_back(WindowScore(text, window));
        }
        reference.windows.push_back(std::move(window));
    }
    return reference;
}

// Dense top-k over input lines: score descending, then line; --group keeps the
// first line of each id.
auto DenseReference(const std::vector<std::string> &lines, const Reference &reference, const std::string &source,
                    std::size_t top_k, bool group) -> std::vector<Row>
{
    std::vector<std::uint32_t> occurrences(reference.scores.size(), 0);
    for (const std::uint32_t entry_id : reference.ids)
    {
        ++occurrences[entry_id];
    }
    std::vector<std::size_t> order;
    std::vector<char> seen(reference.scores.size(), 0);
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        const std::uint32_t entry_id = reference.ids[line];
        if (group && seen[entry_id] != 0)
        {
            continue;
        }
        seen[entry_id] = 1;
        order.push_back(line);
    }
    std::ranges::stable_sort(
        order, [&reference](std::size_t left, std::size_t right) -> bool
        { return reference.scores[reference.ids[left]] > reference.scores[reference.ids[right]]; });
    order.resize(std::min(order.size(), top_k));
    std::vector<Row> rows;
    for (const std::size_t line : order)
    {
        const float score = reference.scores[reference.ids[line]];
        rows.push_back(Row{.source = source,
                           .line = line + 1,
                           .bits = std::bit_cast<std::uint32_t>(score),
                           .text = lines[line],
                           .occurrences = occurrences[reference.ids[line]],
                           .mode = "dense",
                           .dense = std::bit_cast<std::uint32_t>(score),
                           .lexical = std::nullopt});
    }
    return rows;
}

struct WindowRun
{
    std::expected<nesso::TextResults, nesso::Error> result;
    std::vector<std::vector<std::string>> batches;
};

auto RunWindowed(const nesso::GrepRequest &request, const std::string &stdin_text) -> WindowRun
{
    WindowRun run{.result = std::unexpected(nesso::Error{}), .batches = {}};
    const nesso::Nesso app{nesso::Config{}, std::make_unique<WindowEmbedder>(run.batches)};
    if (request.files.front() == "-")
    {
        std::stringstream input{stdin_text};
        const CinGuard guard{input};
        run.result = app.Grep(request);
    }
    else
    {
        run.result = app.Grep(request);
    }
    return run;
}

} // namespace

TEST_CASE("a needle in the last window is the first provisional row", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_needle"};
    const auto log = dir.Path() / "app.log";
    std::vector<std::string> lines;
    for (std::size_t index = 0; index + 1 < LINES; ++index)
    {
        lines.push_back(LineText(index));
    }
    lines.emplace_back("kernel panic on node");
    WriteText(log, Joined(lines));
    std::unordered_map<std::string, float> scores = EarlyBestScores();
    scores["kernel panic on node"] = 0.95F;

    RecordingObserver observer;
    const auto result = App(scores).Grep(
        {.query = "kernel panic", .files = {log}, .top_k = 3, .mode = nesso::SearchMode::Dense, .observer = &observer});
    REQUIRE(result.has_value());
    REQUIRE_FALSE(observer.calls.empty());
    const Call &first = observer.calls.front();
    REQUIRE_FALSE(first.rows.empty());
    CHECK(first.rows.front().text == "kernel panic on node");
    CHECK(first.rows.front().line == LINES);
    CHECK(first.lines_done == LINES - (2 * WINDOW));
    CHECK(observer.calls.back().rows == Freeze(*result));
    REQUIRE_FALSE(result->matches.empty());
    CHECK(result->matches.front().text == "kernel panic on node");
}

TEST_CASE("final results match input-order embedding in every mode", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_reference"};
    const auto log = dir.Path() / "app.log";
    const std::vector<std::string> lines = WindowedLines();
    const std::string text = Joined(lines);
    WriteText(log, text);
    const Reference reference = InputOrderReference(lines);
    REQUIRE(reference.windows.size() == 3);

    // "disk event" is in every window, so the order is the identity. The other
    // two queries move window 2 or window 1 first.
    const auto query = GENERATE(as<std::string>{}, "disk event", "kernel panic", "job done");
    const auto mode = GENERATE(nesso::SearchMode::Dense, nesso::SearchMode::Hybrid, nesso::SearchMode::Lexical,
                               nesso::SearchMode::Auto);
    const bool group = GENERATE(false, true);
    // Each case runs both input kinds, observed and plain; which kind is
    // observed alternates.
    const bool from_stdin = group != (mode == nesso::SearchMode::Hybrid || mode == nesso::SearchMode::Auto);
    const std::size_t top_k = 4;
    RecordingObserver observer;
    nesso::GrepRequest request{.query = query,
                               .files = {from_stdin ? std::filesystem::path{"-"} : log},
                               .top_k = top_k,
                               .mode = mode,
                               .group = group,
                               .observer = &observer};
    const WindowRun run = RunWindowed(request, text);
    REQUIRE(run.result.has_value());

    // Each window is embedded once, with its input-order members.
    REQUIRE(run.batches.size() == reference.windows.size());
    std::vector<std::vector<std::string>> sorted_batches = run.batches;
    std::ranges::sort(sorted_batches);
    std::vector<std::vector<std::string>> sorted_windows = reference.windows;
    std::ranges::sort(sorted_windows);
    CHECK(sorted_batches == sorted_windows);
    if (query == "disk event")
    {
        CHECK(run.batches == reference.windows);
    }
    else
    {
        CHECK(run.batches != reference.windows);
    }

    const std::string source = from_stdin ? "-" : log.string();
    const std::vector<Row> rows = Freeze(*run.result);
    if (mode == nesso::SearchMode::Dense)
    {
        CHECK(rows == DenseReference(lines, reference, source, top_k, group));
    }
    for (const Row &row : rows)
    {
        CHECK(row.source == source);
        REQUIRE(row.line >= 1);
        REQUIRE(row.line <= lines.size());
        CHECK(row.text == lines[row.line - 1]);
        if (row.dense)
        {
            CHECK(*row.dense == std::bit_cast<std::uint32_t>(reference.scores[reference.ids[row.line - 1]]));
        }
    }

    // The same request on the other input kind and without the observer.
    nesso::GrepRequest other = request;
    other.files = {from_stdin ? log : std::filesystem::path{"-"}};
    other.observer = nullptr;
    const WindowRun again = RunWindowed(other, text);
    REQUIRE(again.result.has_value());
    std::vector<Row> other_rows = Freeze(*again.result);
    for (Row &row : other_rows)
    {
        row.source = source;
    }
    CHECK(other_rows == rows);
    CHECK(again.batches == run.batches);
    REQUIRE_FALSE(observer.calls.empty());
    CHECK(observer.calls.size() <= reference.windows.size());
    CHECK(observer.scan_done == 1);
}

TEST_CASE("dense keeps the first top-k lines of a repeated key", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_keep"};
    const auto log = dir.Path() / "jobs.log";
    std::vector<std::string> lines;
    for (int job = 1; job <= 9; ++job)
    {
        lines.push_back("job " + std::to_string(job) + " done");
    }
    lines.emplace_back("other line");
    lines.emplace_back("job 10 done");
    WriteText(log, Joined(lines));
    const std::unordered_map<std::string, float> scores{{"job 1 done", 0.8F}, {"other line", 0.1F}};
    const bool observed = GENERATE(false, true);
    RecordingObserver observer;
    nesso::GrepRequest request{.query = "job done",
                               .files = {log},
                               .top_k = 3,
                               .mode = nesso::SearchMode::Dense,
                               .observer = observed ? &observer : nullptr};

    const auto top = App(scores).Grep(request);
    REQUIRE(top.has_value());
    REQUIRE(top->matches.size() == 3);
    for (std::size_t index = 0; index < 3; ++index)
    {
        CHECK(top->matches[index].line == index + 1);
        CHECK(top->matches[index].text == lines[index]);
        CHECK(top->matches[index].occurrences == 10);
        CHECK(top->matches[index].score == 0.8F);
    }

    request.group = true;
    const auto grouped = App(scores).Grep(request);
    REQUIRE(grouped.has_value());
    REQUIRE(grouped->matches.size() == 2);
    CHECK(grouped->matches[0].text == "job 1 done");
    CHECK(grouped->matches[0].occurrences == 10);
    CHECK(grouped->matches[1].text == "other line");

    // More rows asked than lines: every line comes back once, in rank order.
    request.group = false;
    request.top_k = 50;
    const auto all = App(scores).Grep(request);
    REQUIRE(all.has_value());
    REQUIRE(all->matches.size() == lines.size());
    for (std::size_t index = 0; index < 10; ++index)
    {
        const std::size_t line = index < 9 ? index + 1 : 11;
        CHECK(all->matches[index].line == line);
        CHECK(all->matches[index].text == lines[line - 1]);
    }
    CHECK(all->matches.back().text == "other line");
}

TEST_CASE("lines done rise to the line total", "[nesso][grep][observer]")
{
    const TempDir dir{"nesso_grep_observer_lines_done"};
    const auto log = dir.Path() / "app.log";
    const std::vector<std::string> lines = WindowedLines();
    WriteText(log, Joined(lines));
    RecordingObserver observer;
    std::vector<std::vector<std::string>> batches;
    const nesso::Nesso app{nesso::Config{}, std::make_unique<WindowEmbedder>(batches)};
    const auto query = GENERATE(as<std::string>{}, "disk event", "kernel panic", "job done");
    const auto result = app.Grep({.query = query, .files = {log}, .top_k = 20, .observer = &observer});
    REQUIRE(result.has_value());
    // Every window changes the top 20 here, so each one reports.
    REQUIRE(observer.calls.size() == 3);
    std::size_t previous = 0;
    for (const Call &call : observer.calls)
    {
        CHECK(call.lines_done > previous);
        CHECK(call.lines_total == lines.size());
        previous = call.lines_done;
    }
    CHECK(previous == lines.size());
}
