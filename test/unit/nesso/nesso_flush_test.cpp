// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/corpus_index.hpp>
#include <embed/batch_plan.hpp>
#include <embed/embedder.hpp>
#include <nesso/nesso.hpp>

#include "test_embedder_identity.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <stdexcept>
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

    [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

  private:
    std::filesystem::path path_;
};

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

void WriteLines(const std::filesystem::path &path, size_t count, std::string_view tag)
{
    std::ofstream output(path);
    REQUIRE(output);
    for (size_t index = 0; index < count; ++index)
    {
        output << tag << '-' << LetterSuffix(index) << " database connection\n";
    }
    REQUIRE(output);
}

class RecordingEmbedder final : public nesso::embed::Embedder
{
  public:
    explicit RecordingEmbedder(size_t sessions) : sessions_(sessions) {}

    [[nodiscard]] size_t sessions() const noexcept override { return sessions_; }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        return TestEmbedderIdentity();
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback) const override
    {
        batches_.push_back(texts.size());
        seen_.insert(seen_.end(), texts.begin(), texts.end());
        return std::vector<std::vector<float>>(texts.size(), std::vector<float>{1.F, 0.F, 0.F, 0.F});
    }

    [[nodiscard]] const std::vector<size_t> &Batches() const { return batches_; }

    [[nodiscard]] const std::vector<std::string> &Seen() const { return seen_; }

  private:
    size_t sessions_;
    mutable std::vector<size_t> batches_;
    mutable std::vector<std::string> seen_;
};

enum class BatchBehavior : std::uint8_t
{
    Error,
    Throw,
    Short,
    Narrow,
    QueryError,
    QueryThrow
};

class FallibleEmbedder final : public nesso::embed::Embedder
{
  public:
    explicit FallibleEmbedder(BatchBehavior batch) : batch_(batch) {}

    [[nodiscard]] size_t sessions() const noexcept override { return 1; }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        return TestEmbedderIdentity();
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        if (batch_ == BatchBehavior::QueryThrow)
        {
            throw std::runtime_error("query embed failed");
        }
        if (batch_ == BatchBehavior::QueryError)
        {
            return std::unexpected(nesso::embed::EmbedError::InferenceFailure);
        }
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
    {
        if (progress)
        {
            progress(1, texts.size());
        }
        if (batch_ == BatchBehavior::Throw)
        {
            throw std::runtime_error("batch embed failed");
        }
        if (batch_ == BatchBehavior::Error)
        {
            return std::unexpected(nesso::embed::EmbedError::InferenceFailure);
        }
        if (batch_ == BatchBehavior::Short)
        {
            return {};
        }
        return std::vector<std::vector<float>>(texts.size(), std::vector<float>{1.F});
    }

  private:
    BatchBehavior batch_;
};

nesso::Nesso FallibleApp(BatchBehavior batch)
{
    return nesso::Nesso{nesso::Config{}, std::make_unique<FallibleEmbedder>(batch)};
}

void RequireKind(const std::expected<nesso::TextResults, nesso::Error> &result, nesso::ErrorKind kind,
                 nesso::ErrorCause cause)
{
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == kind);
    CHECK(result.error().cause == cause);
}

void RequireIndexKind(const std::expected<nesso::IndexSummary, nesso::Error> &result, nesso::ErrorKind kind,
                      nesso::ErrorCause cause)
{
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == kind);
    CHECK(result.error().cause == cause);
}

class CinGuard
{
  public:
    explicit CinGuard(std::istream &input) : previous_(std::cin.rdbuf(input.rdbuf())) { std::cin.clear(); }

    CinGuard(const CinGuard &) = delete;
    CinGuard &operator=(const CinGuard &) = delete;

    ~CinGuard()
    {
        std::cin.rdbuf(previous_);
        std::cin.clear();
    }

  private:
    std::streambuf *previous_;
};

} // namespace

TEST_CASE("zero sessions flush every line in one batch", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_zero_sessions"};
    const auto log = dir.Path() / "tail.log";
    WriteLines(log, 10, "delta");
    auto embedder = std::make_unique<RecordingEmbedder>(0);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    REQUIRE(app.Index({.files = {log}, .output = dir.Path() / "zero.nesso"}).has_value());
    REQUIRE(recorder->Batches().size() == 1);
    CHECK(recorder->Batches().front() == 10);
}

TEST_CASE("an exact batch leaves the final flush empty", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_exact_batch"};
    const auto log = dir.Path() / "exact.log";
    WriteLines(log, nesso::embed::BATCH_WINDOW, "epsilon");
    auto embedder = std::make_unique<RecordingEmbedder>(1);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    REQUIRE(app.Index({.files = {log}, .output = dir.Path() / "exact.nesso"}).has_value());
    CHECK(recorder->Batches() == std::vector<size_t>{nesso::embed::BATCH_WINDOW});
    REQUIRE(app.Grep({.query = "database", .files = {log}}).has_value());
    CHECK(recorder->Batches().size() == 2);
}

TEST_CASE("index flushes a fixed window for every session count", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_window"};
    const auto log = dir.Path() / "window.log";
    constexpr size_t total = nesso::embed::BATCH_WINDOW + 1;
    WriteLines(log, total, "window");
    auto single_embedder = std::make_unique<RecordingEmbedder>(1);
    auto multi_embedder = std::make_unique<RecordingEmbedder>(3);
    const RecordingEmbedder *single = single_embedder.get();
    const RecordingEmbedder *multi = multi_embedder.get();
    const nesso::Nesso single_app{nesso::Config{}, std::move(single_embedder)};
    const nesso::Nesso multi_app{nesso::Config{}, std::move(multi_embedder)};
    REQUIRE(single_app.Index({.files = {log}, .output = dir.Path() / "single.nesso"}).has_value());
    REQUIRE(multi_app.Index({.files = {log}, .output = dir.Path() / "multi.nesso"}).has_value());
    const std::vector<size_t> expected{nesso::embed::BATCH_WINDOW, 1};
    CHECK(single->Batches() == expected);
    CHECK(multi->Batches() == expected);
}

TEST_CASE("a full window reports the embed error and stops", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_window_error"};
    const auto log = dir.Path() / "full.log";
    WriteLines(log, nesso::embed::BATCH_WINDOW + 3, "gamma");
    const nesso::Nesso app = FallibleApp(BatchBehavior::Error);
    RequireKind(app.Grep({.query = "database", .files = {log}}), nesso::ErrorKind::Embed, nesso::ErrorCause::Inference);
    RequireIndexKind(app.Index({.files = {log}, .output = dir.Path() / "err.nesso"}), nesso::ErrorKind::Embed,
                     nesso::ErrorCause::Inference);
}

TEST_CASE("a flush reports an embedder error", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_error"};
    const auto full = dir.Path() / "full.log";
    const auto tail = dir.Path() / "tail.log";
    WriteLines(full, 40, "gamma");
    WriteLines(tail, 10, "delta");
    const nesso::Nesso app = FallibleApp(BatchBehavior::Error);
    RequireKind(app.Grep({.query = "database", .files = {full}}), nesso::ErrorKind::Embed,
                nesso::ErrorCause::Inference);
    RequireIndexKind(app.Index({.files = {full}, .output = dir.Path() / "err.nesso"}), nesso::ErrorKind::Embed,
                     nesso::ErrorCause::Inference);
    RequireKind(app.Grep({.query = "database", .files = {tail}}), nesso::ErrorKind::Embed,
                nesso::ErrorCause::Inference);
    RequireIndexKind(app.Index({.files = {tail}, .output = dir.Path() / "tail.nesso"}), nesso::ErrorKind::Embed,
                     nesso::ErrorCause::Inference);
}

TEST_CASE("a short batch is a dimension mismatch", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_short_batch"};
    const auto log = dir.Path() / "tail.log";
    WriteLines(log, 10, "delta");
    const nesso::Nesso app = FallibleApp(BatchBehavior::Short);
    RequireKind(app.Grep({.query = "database", .files = {log}}), nesso::ErrorKind::Embed,
                nesso::ErrorCause::MismatchedDimensions);
    RequireIndexKind(app.Index({.files = {log}, .output = dir.Path() / "short.nesso"}), nesso::ErrorKind::Embed,
                     nesso::ErrorCause::MismatchedDimensions);
}

TEST_CASE("a narrow vector fails the score", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_narrow"};
    const auto log = dir.Path() / "tail.log";
    WriteLines(log, 10, "delta");
    const nesso::Nesso app = FallibleApp(BatchBehavior::Narrow);
    RequireKind(app.Grep({.query = "database", .files = {log}}), nesso::ErrorKind::Search,
                nesso::ErrorCause::MismatchedDimensions);
}

TEST_CASE("a query embed error is reported", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_query_error"};
    const auto log = dir.Path() / "tail.log";
    WriteLines(log, 10, "delta");
    const nesso::Nesso app = FallibleApp(BatchBehavior::QueryError);
    RequireKind(app.Grep({.query = "database", .files = {log}}), nesso::ErrorKind::QueryEmbed,
                nesso::ErrorCause::Inference);
}

TEST_CASE("a query embed exception leaves grep", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_query_throw"};
    const auto log = dir.Path() / "tail.log";
    WriteLines(log, 10, "delta");
    const nesso::Nesso app = FallibleApp(BatchBehavior::QueryThrow);
    REQUIRE_THROWS_AS(app.Grep({.query = "database", .files = {log}}), std::runtime_error);
}

TEST_CASE("a batch embed exception leaves the flush", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_batch_throw"};
    const auto full = dir.Path() / "full.log";
    const auto tail = dir.Path() / "tail.log";
    WriteLines(full, 40, "gamma");
    WriteLines(tail, 10, "delta");
    const nesso::Nesso app = FallibleApp(BatchBehavior::Throw);
    REQUIRE_THROWS_AS(app.Grep({.query = "database", .files = {full}}), std::runtime_error);
    REQUIRE_THROWS_AS(app.Index({.files = {full}, .output = dir.Path() / "throw.nesso"}), std::runtime_error);
    REQUIRE_THROWS_AS(app.Grep({.query = "database", .files = {tail}}), std::runtime_error);
    REQUIRE_THROWS_AS(app.Index({.files = {tail}, .output = dir.Path() / "tail-throw.nesso"}), std::runtime_error);
}

TEST_CASE("identical lines produce one embed call", "[nesso][text]")
{
    const TempDir dir{"nesso_dedup_identical"};
    const auto log = dir.Path() / "same.log";
    {
        std::ofstream output(log);
        REQUIRE(output);
        for (int index = 0; index < 8; ++index)
        {
            output << "repeat me\n";
        }
        REQUIRE(output);
    }

    auto embedder = std::make_unique<RecordingEmbedder>(1);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto indexed = app.Index({.files = {log}, .output = dir.Path() / "same.nesso"});
    REQUIRE(indexed.has_value());
    CHECK(indexed->chunks == 8);
    CHECK(recorder->Batches() == std::vector<size_t>{1});
    CHECK(recorder->Seen() == std::vector<std::string>{"repeat me"});

    const auto grepped = app.Grep({.query = "repeat", .files = {log}, .top_k = 8});
    REQUIRE(grepped.has_value());
    REQUIRE(grepped->matches.size() == 8);
    for (size_t index = 0; index < grepped->matches.size(); ++index)
    {
        CHECK(grepped->matches[index].line == index + 1);
        CHECK(grepped->matches[index].text == "repeat me");
        CHECK(grepped->matches[index].occurrences == 8);
        CHECK(grepped->matches[index].score == grepped->matches.front().score);
    }
    CHECK(recorder->Batches() == std::vector<size_t>{1, 1});

    const auto searched = app.Search({.query = "repeat", .index = dir.Path() / "same.nesso", .top_k = 8});
    REQUIRE(searched.has_value());
    REQUIRE(searched->matches.size() == 8);
    for (const nesso::TextMatch &match : searched->matches)
    {
        CHECK(match.occurrences == 8);
    }
}

TEST_CASE("a duplicate group spanning two windows is embedded once", "[nesso][text]")
{
    const TempDir dir{"nesso_dedup_window"};
    const auto log = dir.Path() / "window.log";
    const size_t total = nesso::embed::BATCH_WINDOW + 2;
    {
        std::ofstream output(log);
        REQUIRE(output);
        output << "shared\n";
        for (size_t index = 1; index < nesso::embed::BATCH_WINDOW; ++index)
        {
            output << LetterSuffix(index) << '\n';
        }
        output << "shared\n";
        output << "fresh\n";
        REQUIRE(output);
    }

    auto embedder = std::make_unique<RecordingEmbedder>(2);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto indexed = app.Index({.files = {log}, .output = dir.Path() / "window.nesso"});
    REQUIRE(indexed.has_value());
    CHECK(indexed->chunks == total);
    CHECK(recorder->Batches() == std::vector<size_t>{nesso::embed::BATCH_WINDOW, 1});
    CHECK(std::count(recorder->Seen().begin(), recorder->Seen().end(), "shared") == 1);
    CHECK(std::find(recorder->Seen().begin(), recorder->Seen().end(), "fresh") != recorder->Seen().end());

    const auto tail = dir.Path() / "tail.log";
    {
        std::ofstream output(tail);
        REQUIRE(output);
        for (size_t index = 0; index < nesso::embed::BATCH_WINDOW; ++index)
        {
            output << "row" << LetterSuffix(index) << '\n';
        }
        output << "row" << LetterSuffix(0) << '\n';
        REQUIRE(output);
    }
    auto tail_embedder = std::make_unique<RecordingEmbedder>(1);
    const RecordingEmbedder *tail_recorder = tail_embedder.get();
    const nesso::Nesso tail_app{nesso::Config{}, std::move(tail_embedder)};
    const auto tail_indexed = tail_app.Index({.files = {tail}, .output = dir.Path() / "tail.nesso"});
    REQUIRE(tail_indexed.has_value());
    CHECK(tail_indexed->chunks == nesso::embed::BATCH_WINDOW + 1);
    CHECK(tail_recorder->Batches() == std::vector<size_t>{nesso::embed::BATCH_WINDOW});
    CHECK(std::count(tail_recorder->Seen().begin(), tail_recorder->Seen().end(), "rowa") == 1);
    const auto stored = nesso::core::ReadCorpusFile(dir.Path() / "tail.nesso");
    REQUIRE(stored.has_value());
    REQUIRE(stored->size() == nesso::embed::BATCH_WINDOW + 1);
    CHECK(stored->front().chunk.text == "rowa");
    CHECK(stored->back().chunk.text == "rowa");
    CHECK(stored->front().embedding == stored->back().embedding);

    const auto grepped = app.Grep({.query = "shared", .files = {log}, .top_k = total});
    REQUIRE(grepped.has_value());
    REQUIRE(grepped->matches.size() == total);
    size_t shared = 0;
    for (const nesso::TextMatch &match : grepped->matches)
    {
        if (match.text == "shared")
        {
            ++shared;
            CHECK(match.occurrences == 2);
        }
        else if (match.text == "fresh")
        {
            CHECK(match.occurrences == 1);
        }
        else
        {
            CHECK(match.occurrences == 1);
        }
    }
    CHECK(shared == 2);
    CHECK(grepped->matches.front().line < grepped->matches.back().line);
}

TEST_CASE("duplicates after a full window are written without another embed", "[nesso][text]")
{
    const TempDir dir{"nesso_dedup_tail_window"};
    const auto log = dir.Path() / "tail-window.log";
    const size_t total = nesso::embed::BATCH_WINDOW * 2;
    {
        std::ofstream output(log);
        REQUIRE(output);
        for (size_t index = 0; index < nesso::embed::BATCH_WINDOW; ++index)
        {
            output << "row" << LetterSuffix(index) << '\n';
        }
        for (size_t index = 0; index < nesso::embed::BATCH_WINDOW; ++index)
        {
            output << "rowa\n";
        }
        REQUIRE(output);
    }

    auto embedder = std::make_unique<RecordingEmbedder>(1);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto indexed = app.Index({.files = {log}, .output = dir.Path() / "tail-window.nesso"});
    REQUIRE(indexed.has_value());
    CHECK(indexed->chunks == total);
    CHECK(recorder->Batches() == std::vector<size_t>{nesso::embed::BATCH_WINDOW});
    CHECK(std::count(recorder->Seen().begin(), recorder->Seen().end(), "rowa") == 1);
    const auto stored = nesso::core::ReadCorpusFile(dir.Path() / "tail-window.nesso");
    REQUIRE(stored.has_value());
    REQUIRE(stored->size() == total);
    CHECK(stored->front().chunk.text == "rowa");
    CHECK(stored->back().chunk.text == "rowa");
    CHECK(stored->front().embedding == stored->back().embedding);
}

TEST_CASE("blank log lines and JSON values are not embedded or returned", "[nesso][text]")
{
    const TempDir dir{"nesso_blank_records"};
    const auto log = dir.Path() / "lines.log";
    const auto json = dir.Path() / "values.json";
    {
        std::ofstream output(log, std::ios::binary);
        REQUIRE(output);
        output << " \t\r\n  keep log \t\n \t";
        REQUIRE(output);
    }
    {
        std::ofstream output(json, std::ios::binary);
        REQUIRE(output);
        output << R"([{"message":""},{"message":" \t "},{"message":"keep json"}])";
        REQUIRE(output);
    }

    auto embedder = std::make_unique<RecordingEmbedder>(1);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto check_matches = [&](const std::vector<nesso::TextMatch> &matches)
    {
        REQUIRE(matches.size() == 2);
        const auto log_match =
            std::ranges::find_if(matches, [&](const auto &match) { return match.source == log.string(); });
        REQUIRE(log_match != matches.end());
        CHECK(log_match->line == 2);
        CHECK(log_match->text == "  keep log \t");
        CHECK_FALSE(log_match->array_element);
        const auto json_match =
            std::ranges::find_if(matches, [&](const auto &match) { return match.source == json.string(); });
        REQUIRE(json_match != matches.end());
        CHECK(json_match->line == 3);
        CHECK(json_match->text == "keep json");
        CHECK(json_match->array_element);
    };

    const auto grepped = app.Grep({.query = "keep", .files = {log, json}, .top_k = 8});
    REQUIRE(grepped.has_value());
    CHECK(grepped->skipped_lines == 4);
    CHECK(grepped->empty_lines == 4);
    check_matches(grepped->matches);
    CHECK(recorder->Seen().size() == 2);
    CHECK(std::count(recorder->Seen().begin(), recorder->Seen().end(), "  keep log \t") == 1);
    CHECK(std::count(recorder->Seen().begin(), recorder->Seen().end(), "keep json") == 1);

    const auto corpus = dir.Path() / "records.nesso";
    const auto indexed = app.Index({.files = {log, json}, .output = corpus});
    REQUIRE(indexed.has_value());
    CHECK(indexed->chunks == 2);
    CHECK(indexed->skipped_lines == 4);
    CHECK(indexed->empty_lines == 4);
    CHECK(recorder->Seen().size() == 4);
    CHECK(std::count(recorder->Seen().begin(), recorder->Seen().end(), "  keep log \t") == 2);
    CHECK(std::count(recorder->Seen().begin(), recorder->Seen().end(), "keep json") == 2);
    const auto searched = app.Search({.query = "keep", .index = corpus, .top_k = 8});
    REQUIRE(searched.has_value());
    check_matches(searched->matches);

    const auto blank = dir.Path() / "blank.log";
    const auto existing = dir.Path() / "existing.nesso";
    {
        std::ofstream output(blank, std::ios::binary);
        REQUIRE(output);
        output << " \n\t \r\n";
        REQUIRE(output);
    }
    {
        std::ofstream output(existing, std::ios::binary);
        REQUIRE(output);
        output << "preserve";
        REQUIRE(output);
    }
    const auto empty_grep = app.Grep({.query = "keep", .files = {blank}});
    REQUIRE_FALSE(empty_grep.has_value());
    CHECK(empty_grep.error().kind == nesso::ErrorKind::EmptyInput);
    CHECK(empty_grep.error().cause == nesso::ErrorCause::NothingIndexable);
    CHECK(empty_grep.error().empty_lines == 2);
    const auto empty_index = app.Index({.files = {blank}, .output = existing});
    REQUIRE_FALSE(empty_index.has_value());
    CHECK(empty_index.error().kind == nesso::ErrorKind::EmptyInput);
    CHECK(empty_index.error().cause == nesso::ErrorCause::NothingIndexable);
    CHECK(empty_index.error().empty_lines == 2);
    std::ifstream preserved(existing, std::ios::binary);
    REQUIRE(preserved);
    std::string content;
    std::getline(preserved, content);
    CHECK(content == "preserve");
    CHECK(recorder->Seen().size() == 4);
}

TEST_CASE("stdin duplicates are embedded once", "[nesso][text]")
{
    const TempDir dir{"nesso_dedup_stdin"};
    std::stringstream input{"same\nsame\n"};
    const CinGuard guard{input};
    auto embedder = std::make_unique<RecordingEmbedder>(1);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto grepped = app.Grep({.query = "same", .files = {"-"}, .top_k = 2});
    REQUIRE(grepped.has_value());
    CHECK(recorder->Batches() == std::vector<size_t>{1});
    CHECK(recorder->Seen() == std::vector<std::string>{"same"});
    REQUIRE(grepped->matches.size() == 2);
    CHECK(grepped->matches[0].source == "-");
    CHECK(grepped->matches[0].line == 1);
    CHECK(grepped->matches[1].line == 2);
    CHECK(grepped->matches[0].occurrences == 2);
    CHECK(grepped->matches[1].occurrences == 2);
    CHECK(grepped->matches[0].score == grepped->matches[1].score);

    std::stringstream again{"same\nsame\n"};
    const CinGuard index_guard{again};
    const auto indexed = app.Index({.files = {"-"}, .output = dir.Path() / "stdin.nesso"});
    REQUIRE(indexed.has_value());
    CHECK(indexed->chunks == 2);
    CHECK(recorder->Batches().back() == 1);
}

TEST_CASE("lines that differ by a masked id are embedded once", "[nesso][text]")
{
    const TempDir dir{"nesso_dedup_masked"};
    const auto log = dir.Path() / "blocks.log";
    {
        std::ofstream output(log);
        REQUIRE(output);
        output << "block blk_10 done\n";
        output << "block blk_20 done\n";
        output << "block blk_-3 done\n";
        REQUIRE(output);
    }

    auto embedder = std::make_unique<RecordingEmbedder>(1);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto indexed = app.Index({.files = {log}, .output = dir.Path() / "blocks.nesso"});
    REQUIRE(indexed.has_value());
    CHECK(indexed->chunks == 3);
    CHECK(recorder->Seen() == std::vector<std::string>{"block blk_10 done"});

    const auto stored = nesso::core::ReadCorpusFile(dir.Path() / "blocks.nesso");
    REQUIRE(stored.has_value());
    REQUIRE(stored->size() == 3);
    // File sources have no storedText; ReadCorpusFile falls back to the distinct
    // first raw line.
    CHECK(stored->at(0).chunk.text == "block blk_10 done");
    CHECK(stored->at(1).chunk.text == "block blk_10 done");
    CHECK(stored->at(2).chunk.text == "block blk_10 done");
    CHECK(stored->at(0).embedding == stored->at(1).embedding);
    CHECK(stored->at(1).embedding == stored->at(2).embedding);

    const auto grepped = app.Grep({.query = "block", .files = {log}, .top_k = 3, .mode = nesso::SearchMode::Dense});
    REQUIRE(grepped.has_value());
    REQUIRE(grepped->matches.size() == 3);
    CHECK(grepped->matches[0].text == "block blk_10 done");
    CHECK(grepped->matches[1].text == "block blk_20 done");
    CHECK(grepped->matches[2].text == "block blk_-3 done");
    for (const nesso::TextMatch &match : grepped->matches)
    {
        CHECK(match.occurrences == 3);
        CHECK(match.score == grepped->matches.front().score);
    }

    const auto searched = app.Search(
        {.query = "block", .index = dir.Path() / "blocks.nesso", .top_k = 3, .mode = nesso::SearchMode::Dense});
    REQUIRE(searched.has_value());
    REQUIRE(searched->matches.size() == 3);
    CHECK(searched->matches[0].text == "block blk_10 done");
    CHECK(searched->matches[1].text == "block blk_20 done");
    CHECK(searched->matches[2].text == "block blk_-3 done");
    for (const nesso::TextMatch &match : searched->matches)
    {
        CHECK(match.occurrences == 3);
    }
}

void WriteBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
}

std::string Unhex(std::string_view hex)
{
    std::string out;
    out.reserve(hex.size() / 2);
    const auto nibble = [](char character) -> int
    {
        if (character >= '0' && character <= '9')
        {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f')
        {
            return character - 'a' + 10;
        }
        return -1;
    };
    for (size_t index = 0; index + 1 < hex.size(); index += 2)
    {
        const int high = nibble(hex[index]);
        const int low = nibble(hex[index + 1]);
        REQUIRE(high >= 0);
        REQUIRE(low >= 0);
        out.push_back(static_cast<char>((high << 4) | low));
    }
    return out;
}

TEST_CASE("directories are read in unsigned path order", "[nesso][text]")
{
    const TempDir dir{"nesso_dir_order"};
    const auto root = dir.Path() / "logs";
    WriteBytes(root / "b.log", "beta database\n");
    WriteBytes(root / "nested" / "c.log", "gamma database\n");
    WriteBytes(root / "a.log", "alpha database\n");
    auto embedder = std::make_unique<RecordingEmbedder>(1);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto indexed = app.Index({.files = {root}, .output = dir.Path() / "logs.nesso"});
    REQUIRE(indexed.has_value());
    CHECK(recorder->Seen() == std::vector<std::string>{"alpha database", "beta database", "gamma database"});

    const auto grepped = app.Grep({.query = "database", .files = {root}});
    REQUIRE(grepped.has_value());
    REQUIRE(grepped->matches.size() == 3);
}

TEST_CASE("a gzip input is stored with its line text", "[nesso][text]")
{
    const TempDir dir{"nesso_gzip_index"};
    const auto path = dir.Path() / "app.log.gz";
    WriteBytes(path, Unhex("1f8b0800f722f26a02ff4b492c494c4a2c4e5548cecfcb4b4d2ec"
                           "9cccf53284a4d2b2d4e4de10200e756bf4f1c000000"));
    auto embedder = std::make_unique<RecordingEmbedder>(1);
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto corpus = dir.Path() / "app.nesso";
    REQUIRE(app.Index({.files = {path}, .output = corpus}).has_value());
    const auto mapped = nesso::core::MapCorpusFile(corpus);
    REQUIRE(mapped);
    REQUIRE(mapped->corpus.Sources().size() == 1);
    CHECK(mapped->corpus.Sources()[0].stored_only);
    CHECK_FALSE(mapped->corpus.Sources()[0].tracked);
    bool found = false;
    for (const nesso::core::CorpusLine &line : mapped->corpus.Lines())
    {
        if (line.stored_text && *line.stored_text == "database connection refused")
        {
            found = true;
        }
    }
    CHECK(found);

    const auto searched = app.Search({.query = "database", .index = corpus, .mode = nesso::SearchMode::Dense});
    REQUIRE(searched.has_value());
    REQUIRE_FALSE(searched->matches.empty());
    CHECK(searched->matches.front().text == "database connection refused");
    CHECK(searched->stale_sources.empty());
}

TEST_CASE("a truncated gzip fails the parse", "[nesso][text]")
{
    const TempDir dir{"nesso_gzip_bad"};
    const auto path = dir.Path() / "bad.gz";
    WriteBytes(path, std::string{"\x1f\x8b\x08\x00", 4});
    const nesso::Nesso app = FallibleApp(BatchBehavior::Error);
    const auto indexed = app.Index({.files = {path}, .output = dir.Path() / "bad.nesso"});
    REQUIRE_FALSE(indexed);
    CHECK(indexed.error().kind == nesso::ErrorKind::Parse);
    CHECK(indexed.error().cause == nesso::ErrorCause::CorruptGzip);
    CHECK(nesso::Message(indexed.error()).contains("truncated or corrupt"));
}

TEST_CASE("a dangling symlink inside a directory fails the walk", "[nesso][text]")
{
    const TempDir dir{"nesso_dir_dangling"};
    const auto root = dir.Path() / "logs";
    std::filesystem::create_directories(root);
    std::filesystem::create_symlink(root / "missing.log", root / "link.log");
    const nesso::Nesso app{nesso::Config{}, std::make_unique<RecordingEmbedder>(1)};
    const auto indexed = app.Index({.files = {root}, .output = dir.Path() / "logs.nesso"});
    REQUIRE_FALSE(indexed);
    CHECK(indexed.error().kind == nesso::ErrorKind::Parse);
    CHECK(indexed.error().cause == nesso::ErrorCause::FileOpen);
}
