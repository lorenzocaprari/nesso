// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/embedder.hpp>
#include <nesso/nesso.hpp>

#include "test_embedder_identity.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

void writeFile(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream output(path);
    REQUIRE(output);
    output << text;
    REQUIRE(output);
}

class ScriptedEmbedder final : public nesso::embed::Embedder
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
    embedBatch(std::span<const std::string> texts, ProgressCallback) const override
    {
        std::vector<std::vector<float>> rows;
        rows.reserve(texts.size());
        for (const std::string &text : texts)
        {
            if (text.find("BEST") != std::string::npos)
            {
                rows.push_back({1.F, 0.F, 0.F, 0.F});
            }
            else if (text.find("MID") != std::string::npos)
            {
                rows.push_back({0.5F, 0.F, 0.F, 0.F});
            }
            else
            {
                rows.push_back({0.F, 1.F, 0.F, 0.F});
            }
        }
        return rows;
    }
};

class HashEmbedder final : public nesso::embed::Embedder
{
  public:
    [[nodiscard]] size_t sessions() const noexcept override { return 1; }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        return testEmbedderIdentity();
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError>
    embed(std::string_view text) const override
    {
        return hashed(text);
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    embedBatch(std::span<const std::string> texts, ProgressCallback) const override
    {
        std::vector<std::vector<float>> rows;
        rows.reserve(texts.size());
        for (const std::string &text : texts)
        {
            rows.push_back(hashed(text));
        }
        return rows;
    }

  private:
    [[nodiscard]] static std::vector<float> hashed(std::string_view text)
    {
        std::uint32_t state = 2166136261u;
        for (const char character : text)
        {
            const auto byte = static_cast<unsigned char>(character);
            state ^= byte;
            state *= 16777619u;
        }
        std::vector<float> values(4);
        for (float &value : values)
        {
            state = (state * 1664525u) + 1013904223u;
            const int bucket = static_cast<int>(state % 2001u) - 1000;
            value = static_cast<float>(bucket);
        }
        return values;
    }
};

nesso::Nesso scriptedApp() { return nesso::Nesso{nesso::Config{}, std::make_unique<ScriptedEmbedder>()}; }

const nesso::TextMatch *matchAtLine(const nesso::TextResults &results, std::uint64_t line)
{
    for (const nesso::TextMatch &match : results.matches)
    {
        if (match.line == line)
        {
            return &match;
        }
    }
    return nullptr;
}

void requireSameRow(const nesso::TextMatch &left, const nesso::TextMatch &right)
{
    REQUIRE(left.line == right.line);
    REQUIRE(left.score == right.score);
    REQUIRE(left.text == right.text);
    REQUIRE(left.arrayElement == right.arrayElement);
    REQUIRE(left.occurrences == right.occurrences);
    REQUIRE(left.mode == right.mode);
    REQUIRE(left.dense == right.dense);
    REQUIRE(left.lexical == right.lexical);
}

void requireSameResults(const nesso::TextResults &left, const nesso::TextResults &right)
{
    REQUIRE(left.matches.size() == right.matches.size());
    CHECK(left.lexicalFallback == right.lexicalFallback);
    CHECK(left.grouped == right.grouped);
    for (std::size_t index = 0; index < left.matches.size(); ++index)
    {
        requireSameRow(left.matches[index], right.matches[index]);
        CHECK(left.matches[index].source == right.matches[index].source);
    }
}

} // namespace

TEST_CASE("dense --group keeps one row per key and -k counts groups", "[nesso][text]")
{
    const TempDir dir{"nesso_group_dense"};
    const auto log = dir.path() / "app.log";
    writeFile(log, "same 1\nsame 2\nsame 3\nother 1\n");
    const nesso::Nesso app = scriptedApp();

    const auto lines = app.grep({.query = "same", .files = {log}, .topK = 2, .mode = nesso::SearchMode::Dense});
    REQUIRE(lines.has_value());
    CHECK_FALSE(lines->grouped);
    REQUIRE(lines->matches.size() == 2);
    CHECK(lines->matches[0].text == "same 1");
    CHECK(lines->matches[1].text == "same 2");

    const auto grouped =
        app.grep({.query = "same", .files = {log}, .topK = 2, .mode = nesso::SearchMode::Dense, .group = true});
    REQUIRE(grouped.has_value());
    CHECK(grouped->grouped);
    REQUIRE(grouped->matches.size() == 2);
    CHECK(grouped->matches[0].text == "same 1");
    CHECK(grouped->matches[0].line == 1);
    CHECK(grouped->matches[0].occurrences == 3);
    CHECK(grouped->matches[0].score == 0.0F);
    CHECK(grouped->matches[0].mode == "dense");
    CHECK(grouped->matches[0].dense == 0.0F);
    CHECK_FALSE(grouped->matches[0].lexical.has_value());
    CHECK(grouped->matches[1].text == "other 1");
    CHECK(grouped->matches[1].line == 4);
    CHECK(grouped->matches[1].occurrences == 1);

    writeFile(log, "user 10 note\nuser 20 note\nBEST unique line\n");
    const auto best =
        app.grep({.query = "same", .files = {log}, .topK = 1, .mode = nesso::SearchMode::Dense, .group = true});
    REQUIRE(best.has_value());
    REQUIRE(best->matches.size() == 1);
    CHECK(best->matches[0].text == "BEST unique line");
    CHECK(best->matches[0].score == 1.0F);

    const auto two =
        app.grep({.query = "same", .files = {log}, .topK = 2, .mode = nesso::SearchMode::Dense, .group = true});
    REQUIRE(two.has_value());
    REQUIRE(two->matches.size() == 2);
    CHECK(two->matches[0].text == "BEST unique line");
    CHECK(two->matches[1].text == "user 10 note");
    CHECK(two->matches[1].occurrences == 2);
    CHECK(two->matches[1].line == 1);
    CHECK(two->matches[1].score == 0.0F);
}

TEST_CASE("lexical --group shows the first raw line and the best member score", "[nesso][text]")
{
    const TempDir dir{"nesso_group_lexical"};
    const auto log = dir.path() / "app.log";
    writeFile(log, "user 10 note\nuser 20 note\n");
    const nesso::Nesso app = scriptedApp();

    const nesso::GrepRequest ungrouped{.query = "20", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Lexical};
    const auto lines = app.grep(ungrouped);
    REQUIRE(lines.has_value());
    REQUIRE(lines->matches.size() == 1);
    CHECK(lines->matches[0].line == 2);

    const auto grouped =
        app.grep({.query = "20", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Lexical, .group = true});
    REQUIRE(grouped.has_value());
    REQUIRE(grouped->matches.size() == 1);
    CHECK(grouped->matches[0].text == "user 10 note");
    CHECK(grouped->matches[0].line == 1);
    CHECK(grouped->matches[0].occurrences == 2);
    CHECK(grouped->matches[0].score == lines->matches[0].score);
    CHECK(grouped->matches[0].lexical == lines->matches[0].lexical);
    CHECK_FALSE(grouped->matches[0].dense.has_value());
    CHECK(grouped->matches[0].mode == "lexical");

    writeFile(log, "user 10 note\nnode 20 up\nuser 20 note\n");
    const auto ties =
        app.grep({.query = "20", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Lexical, .group = true});
    REQUIRE(ties.has_value());
    REQUIRE(ties->matches.size() == 2);
    CHECK(ties->matches[0].mode == "lexical");
    CHECK(ties->matches[1].mode == "lexical");
    const bool userFirst = ties->matches[0].text == "user 10 note";
    CHECK((userFirst || ties->matches[0].text == "node 20 up"));
    CHECK(ties->matches[0].score >= ties->matches[1].score);

    std::string longHit = "BEST";
    for (int repeat = 0; repeat < 16; ++repeat)
    {
        longHit += " zz";
    }
    longHit.push_back('\n');
    writeFile(log, std::string{"best\n"} + longHit);
    const auto flat = app.grep({.query = "BEST", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Lexical});
    const auto tier =
        app.grep({.query = "BEST", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Lexical, .group = true});
    REQUIRE(flat.has_value());
    REQUIRE(tier.has_value());
    REQUIRE(flat->matches.size() == 2);
    REQUIRE(tier->matches.size() == 2);
    const nesso::TextMatch *shortLine = matchAtLine(*flat, 1);
    const nesso::TextMatch *longLine = matchAtLine(*flat, 2);
    REQUIRE(shortLine != nullptr);
    REQUIRE(longLine != nullptr);
    REQUIRE(shortLine->score > longLine->score);
    CHECK(tier->matches[0].text == longLine->text);
    CHECK(tier->matches[0].score == longLine->score);
    CHECK(tier->matches[0].line == 2);
    CHECK(tier->matches[1].text == "best");
    CHECK(tier->matches[1].score == shortLine->score);
}

TEST_CASE("hybrid and auto --group use the effective mode score", "[nesso][text]")
{
    const TempDir dir{"nesso_group_hybrid"};
    const auto log = dir.path() / "app.log";
    writeFile(log, "user 10 note\nuser 20 note\n");
    const nesso::Nesso app = scriptedApp();

    const auto lines = app.grep({.query = "20", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Hybrid});
    REQUIRE(lines.has_value());
    const nesso::TextMatch *first = matchAtLine(*lines, 1);
    const nesso::TextMatch *second = matchAtLine(*lines, 2);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(second->score != first->score);

    const auto grouped =
        app.grep({.query = "20", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Hybrid, .group = true});
    REQUIRE(grouped.has_value());
    REQUIRE(grouped->matches.size() == 1);
    CHECK(grouped->matches[0].text == "user 10 note");
    CHECK(grouped->matches[0].line == 1);
    CHECK(grouped->matches[0].occurrences == 2);
    CHECK(grouped->matches[0].score == second->score);
    CHECK(grouped->matches[0].lexical == second->lexical);
    CHECK(grouped->matches[0].dense == first->dense);
    CHECK(grouped->matches[0].mode == "hybrid");

    const auto natural =
        app.grep({.query = "note", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Auto, .group = true});
    REQUIRE(natural.has_value());
    REQUIRE_FALSE(natural->matches.empty());
    CHECK(natural->matches[0].mode == "hybrid");

    writeFile(log, "request failed for blk_111111111111 today\n"
                   "request failed for blk_38865049064139660 today\n");
    const auto identifierLines =
        app.grep({.query = "blk_38865049064139660", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Auto});
    const auto identifier = app.grep(
        {.query = "blk_38865049064139660", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Auto, .group = true});
    REQUIRE(identifierLines.has_value());
    REQUIRE(identifier.has_value());
    const nesso::TextMatch *hit = matchAtLine(*identifierLines, 2);
    REQUIRE(hit != nullptr);
    REQUIRE(identifier->matches.size() == 1);
    CHECK(identifier->matches[0].mode == "lexical");
    CHECK(identifier->matches[0].text == "request failed for blk_111111111111 today");
    CHECK(identifier->matches[0].occurrences == 2);
    CHECK(identifier->matches[0].score == hit->score);
    CHECK(identifier->matches[0].lexical == hit->lexical);
    CHECK_FALSE(identifier->lexicalFallback);

    writeFile(log, "plain words here\nmore plain words\n");
    const auto fallback = app.grep(
        {.query = "zzzznotpresent99", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Lexical, .group = true});
    REQUIRE(fallback.has_value());
    CHECK(fallback->lexicalFallback);
    REQUIRE(fallback->matches.size() == 2);
    CHECK(fallback->matches[0].mode == "hybrid");
    CHECK(fallback->matches[1].mode == "hybrid");
}

TEST_CASE("search --group matches grep and keeps the first line when a source is missing", "[nesso][text]")
{
    const TempDir dir{"nesso_group_search"};
    const auto log = dir.path() / "app.log";
    writeFile(log, "user 10 note\nuser 20 note\nBEST unique line\n");
    const nesso::Nesso app = scriptedApp();
    const auto indexed = app.index({.files = {log}, .output = dir.path() / "corpus.nesso"});
    REQUIRE(indexed.has_value());

    const auto grepped =
        app.grep({.query = "20", .files = {log}, .topK = 5, .mode = nesso::SearchMode::Hybrid, .group = true});
    const auto searched = app.search({.query = "20",
                                      .index = dir.path() / "corpus.nesso",
                                      .topK = 5,
                                      .mode = nesso::SearchMode::Hybrid,
                                      .group = true});
    REQUIRE(grepped.has_value());
    REQUIRE(searched.has_value());
    REQUIRE(grepped->matches.size() == 2);
    REQUIRE(searched->matches.size() == 2);
    const nesso::TextMatch *grepUser = nullptr;
    const nesso::TextMatch *searchUser = nullptr;
    for (const nesso::TextMatch &match : grepped->matches)
    {
        if (match.text == "user 10 note")
        {
            grepUser = &match;
        }
    }
    for (const nesso::TextMatch &match : searched->matches)
    {
        if (match.text == "user 10 note")
        {
            searchUser = &match;
        }
    }
    REQUIRE(grepUser != nullptr);
    REQUIRE(searchUser != nullptr);
    CHECK(searched->grouped);
    CHECK(searchUser->text == grepUser->text);
    CHECK(searchUser->line == 1);
    CHECK(searchUser->score == grepUser->score);
    CHECK(searchUser->lexical == grepUser->lexical);
    CHECK(searchUser->dense == grepUser->dense);
    CHECK(searchUser->occurrences == 2);
    CHECK(searchUser->mode == "hybrid");

    const auto lexical = app.search({.query = "20",
                                     .index = dir.path() / "corpus.nesso",
                                     .topK = 1,
                                     .mode = nesso::SearchMode::Lexical,
                                     .group = true});
    REQUIRE(lexical.has_value());
    REQUIRE(lexical->matches.size() == 1);
    CHECK(lexical->matches[0].text == "user 10 note");
    CHECK(lexical->matches[0].occurrences == 2);
    CHECK(lexical->matches[0].mode == "lexical");

    const auto dense = app.search({.query = "note",
                                   .index = dir.path() / "corpus.nesso",
                                   .topK = 5,
                                   .mode = nesso::SearchMode::Dense,
                                   .group = true});
    REQUIRE(dense.has_value());
    REQUIRE(dense->matches.size() == 2);
    CHECK(dense->matches[0].text == "BEST unique line");
    CHECK(dense->matches[0].score == 1.0F);
    CHECK(dense->matches[1].text == "user 10 note");
    CHECK(dense->matches[1].occurrences == 2);

    writeFile(log, "kept 10\n\nkept 20\n");
    const auto blankIndex = app.index({.files = {log}, .output = dir.path() / "blank.nesso"});
    REQUIRE(blankIndex.has_value());
    const auto blank = app.search({.query = "kept",
                                   .index = dir.path() / "blank.nesso",
                                   .topK = 5,
                                   .mode = nesso::SearchMode::Dense,
                                   .group = true});
    REQUIRE(blank.has_value());
    REQUIRE(blank->matches.size() == 1);
    CHECK(blank->matches[0].text == "kept 10");
    CHECK(blank->matches[0].occurrences == 2);

    std::filesystem::remove(log);
    const auto missing = app.search({.query = "20",
                                     .index = dir.path() / "corpus.nesso",
                                     .topK = 5,
                                     .mode = nesso::SearchMode::Lexical,
                                     .group = true});
    REQUIRE(missing.has_value());
    REQUIRE(missing->matches.size() == 1);
    CHECK(missing->matches[0].text == "user 10 note");
    CHECK(missing->matches[0].occurrences == 2);
    CHECK_FALSE(missing->staleSources.empty());

    const auto first = dir.path() / "a.log";
    const auto second = dir.path() / "b.log";
    writeFile(first, "same 1\n");
    writeFile(second, "same 2\n");
    const auto multiIndex = app.index({.files = {first, second}, .output = dir.path() / "multi.nesso"});
    REQUIRE(multiIndex.has_value());
    const auto multi = app.search({.query = "same",
                                   .index = dir.path() / "multi.nesso",
                                   .topK = 5,
                                   .mode = nesso::SearchMode::Dense,
                                   .group = true});
    REQUIRE(multi.has_value());
    CHECK(multi->multipleSources);
    REQUIRE(multi->matches.size() == 1);
    CHECK(multi->matches[0].source == first.string());
    CHECK(multi->matches[0].text == "same 1");
    CHECK(multi->matches[0].occurrences == 2);

    std::stringstream input{"user 10 note\nuser 20 note\n"};
    const CinGuard guard{input};
    const auto stdinIndex = app.index({.files = {"-"}, .output = dir.path() / "stdin.nesso"});
    REQUIRE(stdinIndex.has_value());
    const auto stdinSearch = app.search({.query = "20",
                                         .index = dir.path() / "stdin.nesso",
                                         .topK = 5,
                                         .mode = nesso::SearchMode::Lexical,
                                         .group = true});
    REQUIRE(stdinSearch.has_value());
    REQUIRE(stdinSearch->matches.size() == 1);
    CHECK(stdinSearch->matches[0].source == "-");
    CHECK(stdinSearch->matches[0].text == "user 10 note");
    CHECK(stdinSearch->matches[0].occurrences == 2);
    CHECK(stdinSearch->matches[0].mode == "lexical");
}

TEST_CASE("grouped grep and search stay byte-identical across thread counts", "[nesso][text]")
{
    const TempDir dir{"nesso_group_threads"};
    const auto log = dir.path() / "rows.log";
    {
        std::ofstream output(log);
        REQUIRE(output);
        for (int index = 0; index < 1100; ++index)
        {
            output << "rowq" << index << " event\n";
        }
        output << "same 1\nsame 2\nsame 3\n";
        REQUIRE(output);
    }

    nesso::Config indexerConfig;
    indexerConfig.threads = 1;
    nesso::Nesso indexer{indexerConfig, std::make_unique<HashEmbedder>()};
    const auto indexed = indexer.index({.files = {log}, .output = dir.path() / "corpus.nesso"});
    REQUIRE(indexed.has_value());

    const nesso::SearchMode modes[] = {nesso::SearchMode::Auto, nesso::SearchMode::Hybrid, nesso::SearchMode::Dense,
                                       nesso::SearchMode::Lexical};
    const size_t threads[] = {1, 4, 7};
    for (const nesso::SearchMode mode : modes)
    {
        for (const bool group : {false, true})
        {
            std::vector<nesso::TextResults> grepped;
            std::vector<nesso::TextResults> searched;
            for (const size_t budget : threads)
            {
                nesso::Config config;
                config.threads = budget;
                nesso::Nesso app{config, std::make_unique<HashEmbedder>()};
                const auto grepResult =
                    app.grep({.query = "event", .files = {log}, .topK = 5000, .mode = mode, .group = group});
                const auto searchResult = app.search({.query = "event",
                                                      .index = dir.path() / "corpus.nesso",
                                                      .topK = 5000,
                                                      .mode = mode,
                                                      .group = group});
                REQUIRE(grepResult.has_value());
                REQUIRE(searchResult.has_value());
                CHECK(grepResult->grouped == group);
                CHECK(searchResult->grouped == group);
                grepped.push_back(*grepResult);
                searched.push_back(*searchResult);
            }
            requireSameResults(grepped[0], grepped[1]);
            requireSameResults(grepped[0], grepped[2]);
            requireSameResults(searched[0], searched[1]);
            requireSameResults(searched[0], searched[2]);
        }
    }
}
