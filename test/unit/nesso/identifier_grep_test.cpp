// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/embedder.hpp>
#include <nesso/nesso.hpp>

#include "test_embedder_identity.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
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

    [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream output(path);
    REQUIRE(output);
    output << text;
    REQUIRE(output);
}

class CountingEmbedder final : public nesso::embed::Embedder
{
  public:
    [[nodiscard]] size_t sessions() const noexcept override { return 1; }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        return TestEmbedderIdentity();
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        ++embeds_;
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback) const override
    {
        ++batches_;
        return std::vector<std::vector<float>>(texts.size(), std::vector<float>{0.25F, 0.F, 0.F, 0.F});
    }

    [[nodiscard]] size_t Embeds() const { return embeds_; }

    [[nodiscard]] size_t Batches() const { return batches_; }

  private:
    mutable size_t embeds_ = 0;
    mutable size_t batches_ = 0;
};

class CountingObserver final : public nesso::GrepObserver
{
  public:
    void OnProvisional(const nesso::TextResults &, size_t, size_t) override { ++provisional; }

    void OnScanDone() noexcept override { ++scan_done; }

    size_t provisional = 0;
    size_t scan_done = 0;
};

void RequireEqual(const nesso::TextResults &left, const nesso::TextResults &right)
{
    REQUIRE(left.lexical_fallback == right.lexical_fallback);
    REQUIRE(left.grouped == right.grouped);
    REQUIRE(left.matches.size() == right.matches.size());
    for (size_t index = 0; index < left.matches.size(); ++index)
    {
        const nesso::TextMatch &first = left.matches[index];
        const nesso::TextMatch &second = right.matches[index];
        CHECK(first.source == second.source);
        CHECK(first.line == second.line);
        CHECK(first.score == second.score);
        CHECK(first.text == second.text);
        CHECK(first.array_element == second.array_element);
        CHECK(first.occurrences == second.occurrences);
        CHECK(first.mode == second.mode);
        CHECK(first.dense == second.dense);
        CHECK(first.lexical == second.lexical);
    }
}

void RequireLexicalHit(const nesso::TextResults &results, uint64_t line, std::string_view text)
{
    REQUIRE_FALSE(results.matches.empty());
    CHECK(results.matches[0].line == line);
    CHECK(results.matches[0].text == text);
    CHECK(results.matches[0].mode == "lexical");
    CHECK_FALSE(results.matches[0].dense.has_value());
    REQUIRE(results.matches[0].lexical.has_value());
    CHECK(results.matches[0].score == *results.matches[0].lexical);
    CHECK_FALSE(results.lexical_fallback);
}

constexpr std::string_view LOG_TEXT = "blk note without the full id\n"
                                      "request failed for blk_-1608999687919862906 on node\n"
                                      "host 10.0.0.1 connected\n"
                                      "token 0a000086 here\n";

} // namespace

TEST_CASE("identifier auto grep skips the embedder and matches lexical", "[nesso][grep][identifier]")
{
    const TempDir dir{"identifier_grep"};
    const auto log = dir.Path() / "ids.log";
    WriteFile(log, LOG_TEXT);
    auto embedder = std::make_unique<CountingEmbedder>();
    CountingEmbedder *hook = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};

    const auto blk =
        app.Grep({.query = "blk_-1608999687919862906", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    REQUIRE(blk.has_value());
    CHECK(hook->Embeds() == 0);
    CHECK(hook->Batches() == 0);
    RequireLexicalHit(*blk, 2, "request failed for blk_-1608999687919862906 on node");

    const auto quoted =
        app.Grep({.query = "\"0a000086\"", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    REQUIRE(quoted.has_value());
    CHECK(hook->Embeds() == 0);
    CHECK(hook->Batches() == 0);
    RequireLexicalHit(*quoted, 4, "token 0a000086 here");

    const auto address = app.Grep({.query = "10.0.0.1", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    REQUIRE(address.has_value());
    CHECK(hook->Embeds() == 0);
    CHECK(hook->Batches() == 0);
    RequireLexicalHit(*address, 3, "host 10.0.0.1 connected");

    const auto lexical =
        app.Grep({.query = "blk_-1608999687919862906", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Lexical});
    REQUIRE(lexical.has_value());
    CHECK(hook->Embeds() > 0);
    CHECK(hook->Batches() > 0);
    RequireEqual(*blk, *lexical);

    const size_t after_lexical = hook->Embeds();
    const auto grouped = app.Grep({.query = "blk_-1608999687919862906",
                                   .files = {log},
                                   .top_k = 5,
                                   .mode = nesso::SearchMode::Auto,
                                   .group = true});
    REQUIRE(grouped.has_value());
    CHECK(hook->Embeds() == after_lexical);
    CHECK(grouped->grouped);
    CHECK(grouped->matches[0].mode == "lexical");
    CHECK_FALSE(grouped->matches[0].dense.has_value());
    const auto grouped_lexical = app.Grep({.query = "blk_-1608999687919862906",
                                           .files = {log},
                                           .top_k = 5,
                                           .mode = nesso::SearchMode::Lexical,
                                           .group = true});
    REQUIRE(grouped_lexical.has_value());
    CHECK(hook->Embeds() > after_lexical);
    RequireEqual(*grouped, *grouped_lexical);

    const size_t before_other = hook->Embeds();
    const auto natural = app.Grep({.query = "connected", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    REQUIRE(natural.has_value());
    CHECK(hook->Embeds() > before_other);
    CHECK(natural->matches[0].mode == "hybrid");
    CHECK(natural->matches[0].dense.has_value());

    const size_t before_dense = hook->Embeds();
    const auto dense =
        app.Grep({.query = "blk_-1608999687919862906", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Dense});
    REQUIRE(dense.has_value());
    CHECK(hook->Embeds() > before_dense);
    CHECK(dense->matches[0].mode == "dense");

    const size_t before_hybrid = hook->Embeds();
    const auto hybrid =
        app.Grep({.query = "blk_-1608999687919862906", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Hybrid});
    REQUIRE(hybrid.has_value());
    CHECK(hook->Embeds() > before_hybrid);
    CHECK(hybrid->matches[0].mode == "hybrid");

    const size_t before_mixed = hook->Embeds();
    const auto mixed = app.Grep(
        {.query = "failed blk_-1608999687919862906", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    REQUIRE(mixed.has_value());
    CHECK(hook->Embeds() > before_mixed);
    CHECK(mixed->matches[0].mode == "hybrid");
}

TEST_CASE("identifier auto grep does not create an embedder when a line matches", "[nesso][grep][identifier]")
{
    const TempDir dir{"identifier_grep_nomodel"};
    const auto log = dir.Path() / "ids.log";
    const auto empty = dir.Path() / "empty.log";
    const auto nomodel = dir.Path() / "nomodel";
    WriteFile(log, LOG_TEXT);
    WriteFile(empty, "");
    std::filesystem::create_directory(nomodel);
    nesso::Config config;
    config.model_dir = nomodel;
    const nesso::Nesso app{config};

    const auto hit =
        app.Grep({.query = "blk_-1608999687919862906", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    REQUIRE(hit.has_value());
    RequireLexicalHit(*hit, 2, "request failed for blk_-1608999687919862906 on node");

    const auto natural = app.Grep({.query = "connected", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    REQUIRE_FALSE(natural.has_value());
    CHECK(natural.error().kind == nesso::ErrorKind::EmbedderLoad);

    const auto missed =
        app.Grep({.query = "blk_-1608999687919862906", .files = {empty}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    REQUIRE_FALSE(missed.has_value());
    CHECK(missed.error().kind == nesso::ErrorKind::EmptyInput);

    const auto prose = app.Grep({.query = "connected", .files = {empty}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    REQUIRE_FALSE(prose.has_value());
    CHECK(prose.error().kind == nesso::ErrorKind::EmbedderLoad);
}

TEST_CASE("identifier auto grep with no BM25 hit still embeds", "[nesso][grep][identifier]")
{
    const TempDir dir{"identifier_grep_fallback"};
    const auto log = dir.Path() / "prose.log";
    WriteFile(log, "completely unrelated content here\nanother prose line without tokens\n");
    auto embedder = std::make_unique<CountingEmbedder>();
    CountingEmbedder *hook = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};

    const auto ranked =
        app.Grep({.query = "blk_-1608999687919862906", .files = {log}, .top_k = 2, .mode = nesso::SearchMode::Auto});
    REQUIRE(ranked.has_value());
    CHECK(hook->Embeds() > 0);
    CHECK(hook->Batches() > 0);
    CHECK(ranked->lexical_fallback);
    REQUIRE(ranked->matches.size() == 2);
    CHECK(ranked->matches[0].mode == "hybrid");
    CHECK(ranked->matches[0].dense.has_value());
    CHECK(ranked->matches[0].lexical.has_value());

    const auto lexical =
        app.Grep({.query = "blk_-1608999687919862906", .files = {log}, .top_k = 2, .mode = nesso::SearchMode::Lexical});
    REQUIRE(lexical.has_value());
    RequireEqual(*ranked, *lexical);

    const auto nomodel = dir.Path() / "nomodel";
    std::filesystem::create_directory(nomodel);
    nesso::Config config;
    config.model_dir = nomodel;
    const nesso::Nesso bare{config};
    const auto missed =
        bare.Grep({.query = "blk_-1608999687919862906", .files = {log}, .top_k = 2, .mode = nesso::SearchMode::Auto});
    REQUIRE_FALSE(missed.has_value());
    CHECK(missed.error().kind == nesso::ErrorKind::EmbedderLoad);
}

TEST_CASE("identifier auto grep does not report provisional dense rows", "[nesso][grep][identifier]")
{
    const TempDir dir{"identifier_grep_observer"};
    const auto log = dir.Path() / "ids.log";
    WriteFile(log, LOG_TEXT);
    auto embedder = std::make_unique<CountingEmbedder>();
    CountingEmbedder *hook = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    CountingObserver observer;
    const auto ranked = app.Grep({.query = "blk_-1608999687919862906",
                                  .files = {log},
                                  .top_k = 5,
                                  .progress = true,
                                  .mode = nesso::SearchMode::Auto,
                                  .observer = &observer});
    REQUIRE(ranked.has_value());
    RequireLexicalHit(*ranked, 2, "request failed for blk_-1608999687919862906 on node");
    CHECK(hook->Embeds() == 0);
    CHECK(hook->Batches() == 0);
    CHECK(observer.provisional == 0);
    CHECK(observer.scan_done == 1);
}
