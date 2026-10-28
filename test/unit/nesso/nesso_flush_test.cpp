// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/batch_plan.hpp>
#include <embed/embedder.hpp>
#include <nesso.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
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

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void writeLines(const std::filesystem::path &path, size_t count, std::string_view tag)
{
    std::ofstream output(path);
    REQUIRE(output);
    for (size_t index = 0; index < count; ++index)
    {
        output << tag << '-' << index << " database connection\n";
    }
    REQUIRE(output);
}

class RecordingEmbedder final : public nesso::embed::Embedder
{
  public:
    explicit RecordingEmbedder(size_t sessions) : sessions_(sessions) {}

    [[nodiscard]] size_t sessions() const noexcept override { return sessions_; }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    embedBatch(std::span<const std::string> texts, ProgressCallback) const override
    {
        batches_.push_back(texts.size());
        return std::vector<std::vector<float>>(texts.size(), std::vector<float>{1.F, 0.F, 0.F, 0.F});
    }

    [[nodiscard]] const std::vector<size_t> &batches() const { return batches_; }

  private:
    size_t sessions_;
    mutable std::vector<size_t> batches_;
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
    embedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
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

nesso::Nesso fallibleApp(BatchBehavior batch)
{
    return nesso::Nesso{nesso::Config{}, std::make_unique<FallibleEmbedder>(batch)};
}

void requireKind(const std::expected<nesso::TextResults, nesso::Error> &result, nesso::ErrorKind kind,
                 nesso::ErrorCause cause)
{
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == kind);
    CHECK(result.error().cause == cause);
}

void requireIndexKind(const std::expected<nesso::IndexSummary, nesso::Error> &result, nesso::ErrorKind kind,
                      nesso::ErrorCause cause)
{
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == kind);
    CHECK(result.error().cause == cause);
}

} // namespace

TEST_CASE("zero sessions flush every line in one batch", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_zero_sessions"};
    const auto log = dir.path() / "tail.log";
    writeLines(log, 10, "delta");
    auto embedder = std::make_unique<RecordingEmbedder>(0);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    REQUIRE(app.index({.files = {log}, .output = dir.path() / "zero.nesso"}).has_value());
    REQUIRE(recorder->batches().size() == 1);
    CHECK(recorder->batches().front() == 10);
}

TEST_CASE("an exact batch leaves the final flush empty", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_exact_batch"};
    const auto log = dir.path() / "exact.log";
    writeLines(log, nesso::embed::BATCH_WINDOW, "epsilon");
    auto embedder = std::make_unique<RecordingEmbedder>(1);
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    REQUIRE(app.index({.files = {log}, .output = dir.path() / "exact.nesso"}).has_value());
    CHECK(recorder->batches() == std::vector<size_t>{nesso::embed::BATCH_WINDOW});
    REQUIRE(app.grep({.query = "database", .files = {log}}).has_value());
    CHECK(recorder->batches().size() == 2);
}

TEST_CASE("index flushes a fixed window for every session count", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_window"};
    const auto log = dir.path() / "window.log";
    constexpr size_t total = nesso::embed::BATCH_WINDOW + 1;
    writeLines(log, total, "window");
    auto singleEmbedder = std::make_unique<RecordingEmbedder>(1);
    auto multiEmbedder = std::make_unique<RecordingEmbedder>(3);
    const RecordingEmbedder *single = singleEmbedder.get();
    const RecordingEmbedder *multi = multiEmbedder.get();
    const nesso::Nesso singleApp{nesso::Config{}, std::move(singleEmbedder)};
    const nesso::Nesso multiApp{nesso::Config{}, std::move(multiEmbedder)};
    REQUIRE(singleApp.index({.files = {log}, .output = dir.path() / "single.nesso"}).has_value());
    REQUIRE(multiApp.index({.files = {log}, .output = dir.path() / "multi.nesso"}).has_value());
    const std::vector<size_t> expected{nesso::embed::BATCH_WINDOW, 1};
    CHECK(single->batches() == expected);
    CHECK(multi->batches() == expected);
}

TEST_CASE("a flush reports an embedder error", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_error"};
    const auto full = dir.path() / "full.log";
    const auto tail = dir.path() / "tail.log";
    writeLines(full, 40, "gamma");
    writeLines(tail, 10, "delta");
    const nesso::Nesso app = fallibleApp(BatchBehavior::Error);
    requireKind(app.grep({.query = "database", .files = {full}}), nesso::ErrorKind::Embed,
                nesso::ErrorCause::Inference);
    requireIndexKind(app.index({.files = {full}, .output = dir.path() / "err.nesso"}), nesso::ErrorKind::Embed,
                     nesso::ErrorCause::Inference);
    requireKind(app.grep({.query = "database", .files = {tail}}), nesso::ErrorKind::Embed,
                nesso::ErrorCause::Inference);
    requireIndexKind(app.index({.files = {tail}, .output = dir.path() / "tail.nesso"}), nesso::ErrorKind::Embed,
                     nesso::ErrorCause::Inference);
}

TEST_CASE("a short batch is a dimension mismatch", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_short_batch"};
    const auto log = dir.path() / "tail.log";
    writeLines(log, 10, "delta");
    const nesso::Nesso app = fallibleApp(BatchBehavior::Short);
    requireKind(app.grep({.query = "database", .files = {log}}), nesso::ErrorKind::Embed,
                nesso::ErrorCause::MismatchedDimensions);
    requireIndexKind(app.index({.files = {log}, .output = dir.path() / "short.nesso"}), nesso::ErrorKind::Embed,
                     nesso::ErrorCause::MismatchedDimensions);
}

TEST_CASE("a narrow vector fails the score", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_narrow"};
    const auto log = dir.path() / "tail.log";
    writeLines(log, 10, "delta");
    const nesso::Nesso app = fallibleApp(BatchBehavior::Narrow);
    requireKind(app.grep({.query = "database", .files = {log}}), nesso::ErrorKind::Search,
                nesso::ErrorCause::MismatchedDimensions);
}

TEST_CASE("a query embed error is reported", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_query_error"};
    const auto log = dir.path() / "tail.log";
    writeLines(log, 10, "delta");
    const nesso::Nesso app = fallibleApp(BatchBehavior::QueryError);
    requireKind(app.grep({.query = "database", .files = {log}}), nesso::ErrorKind::QueryEmbed,
                nesso::ErrorCause::Inference);
}

TEST_CASE("a query embed exception leaves grep", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_query_throw"};
    const auto log = dir.path() / "tail.log";
    writeLines(log, 10, "delta");
    const nesso::Nesso app = fallibleApp(BatchBehavior::QueryThrow);
    REQUIRE_THROWS_AS(app.grep({.query = "database", .files = {log}}), std::runtime_error);
}

TEST_CASE("a batch embed exception leaves the flush", "[nesso][text]")
{
    const TempDir dir{"nesso_flush_batch_throw"};
    const auto full = dir.path() / "full.log";
    const auto tail = dir.path() / "tail.log";
    writeLines(full, 40, "gamma");
    writeLines(tail, 10, "delta");
    const nesso::Nesso app = fallibleApp(BatchBehavior::Throw);
    REQUIRE_THROWS_AS(app.grep({.query = "database", .files = {full}}), std::runtime_error);
    REQUIRE_THROWS_AS(app.index({.files = {full}, .output = dir.path() / "throw.nesso"}), std::runtime_error);
    REQUIRE_THROWS_AS(app.grep({.query = "database", .files = {tail}}), std::runtime_error);
    REQUIRE_THROWS_AS(app.index({.files = {tail}, .output = dir.path() / "tail-throw.nesso"}), std::runtime_error);
}
