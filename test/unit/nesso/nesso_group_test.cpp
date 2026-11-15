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

    [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

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

void WriteFile(const std::filesystem::path &path, std::string_view text)
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
        return TestEmbedderIdentity();
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback) const override
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
        return TestEmbedderIdentity();
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError>
    embed(std::string_view text) const override
    {
        return Hashed(text);
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback) const override
    {
        std::vector<std::vector<float>> rows;
        rows.reserve(texts.size());
        for (const std::string &text : texts)
        {
            rows.push_back(Hashed(text));
        }
        return rows;
    }

  private:
    [[nodiscard]] static std::vector<float> Hashed(std::string_view text)
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

nesso::Nesso ScriptedApp() { return nesso::Nesso{nesso::Config{}, std::make_unique<ScriptedEmbedder>()}; }

const nesso::TextMatch *MatchAtLine(const nesso::TextResults &results, std::uint64_t line)
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

void RequireSameRow(const nesso::TextMatch &left, const nesso::TextMatch &right)
{
    REQUIRE(left.line == right.line);
    REQUIRE(left.score == right.score);
    REQUIRE(left.text == right.text);
    REQUIRE(left.array_element == right.array_element);
    REQUIRE(left.occurrences == right.occurrences);
    REQUIRE(left.mode == right.mode);
    REQUIRE(left.dense == right.dense);
    REQUIRE(left.lexical == right.lexical);
}

void RequireSameResults(const nesso::TextResults &left, const nesso::TextResults &right)
{
    REQUIRE(left.matches.size() == right.matches.size());
    CHECK(left.lexical_fallback == right.lexical_fallback);
    CHECK(left.grouped == right.grouped);
    for (std::size_t index = 0; index < left.matches.size(); ++index)
    {
        RequireSameRow(left.matches[index], right.matches[index]);
        CHECK(left.matches[index].source == right.matches[index].source);
    }
}

} // namespace

TEST_CASE("dense --group keeps one row per key and -k counts groups", "[nesso][text]")
{
    const TempDir dir{"nesso_group_dense"};
    const auto log = dir.Path() / "app.log";
    WriteFile(log, "same 1\nsame 2\nsame 3\nother 1\n");
    const nesso::Nesso app = ScriptedApp();

    const auto lines = app.Grep({.query = "same", .files = {log}, .top_k = 2, .mode = nesso::SearchMode::Dense});
    REQUIRE(lines.has_value());
    CHECK_FALSE(lines->grouped);
    REQUIRE(lines->matches.size() == 2);
    CHECK(lines->matches[0].text == "same 1");
    CHECK(lines->matches[1].text == "same 2");

    const auto grouped =
        app.Grep({.query = "same", .files = {log}, .top_k = 2, .mode = nesso::SearchMode::Dense, .group = true});
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

    WriteFile(log, "user 10 note\nuser 20 note\nBEST unique line\n");
    const auto best =
        app.Grep({.query = "same", .files = {log}, .top_k = 1, .mode = nesso::SearchMode::Dense, .group = true});
    REQUIRE(best.has_value());
    REQUIRE(best->matches.size() == 1);
    CHECK(best->matches[0].text == "BEST unique line");
    CHECK(best->matches[0].score == 1.0F);

    const auto two =
        app.Grep({.query = "same", .files = {log}, .top_k = 2, .mode = nesso::SearchMode::Dense, .group = true});
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
    const auto log = dir.Path() / "app.log";
    WriteFile(log, "user 10 note\nuser 20 note\n");
    const nesso::Nesso app = ScriptedApp();

    const nesso::GrepRequest ungrouped{.query = "20", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Lexical};
    const auto lines = app.Grep(ungrouped);
    REQUIRE(lines.has_value());
    REQUIRE(lines->matches.size() == 1);
    CHECK(lines->matches[0].line == 2);

    const auto grouped =
        app.Grep({.query = "20", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Lexical, .group = true});
    REQUIRE(grouped.has_value());
    REQUIRE(grouped->matches.size() == 1);
    CHECK(grouped->matches[0].text == "user 10 note");
    CHECK(grouped->matches[0].line == 1);
    CHECK(grouped->matches[0].occurrences == 2);
    CHECK(grouped->matches[0].score == lines->matches[0].score);
    CHECK(grouped->matches[0].lexical == lines->matches[0].lexical);
    CHECK_FALSE(grouped->matches[0].dense.has_value());
    CHECK(grouped->matches[0].mode == "lexical");

    WriteFile(log, "user 10 note\nnode 20 up\nuser 20 note\n");
    const auto ties =
        app.Grep({.query = "20", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Lexical, .group = true});
    REQUIRE(ties.has_value());
    REQUIRE(ties->matches.size() == 2);
    CHECK(ties->matches[0].mode == "lexical");
    CHECK(ties->matches[1].mode == "lexical");
    const bool user_first = ties->matches[0].text == "user 10 note";
    CHECK((user_first || ties->matches[0].text == "node 20 up"));
    CHECK(ties->matches[0].score >= ties->matches[1].score);

    std::string long_hit = "BEST";
    for (int repeat = 0; repeat < 16; ++repeat)
    {
        long_hit += " zz";
    }
    long_hit.push_back('\n');
    WriteFile(log, std::string{"best\n"} + long_hit);
    const auto flat = app.Grep({.query = "BEST", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Lexical});
    const auto tier =
        app.Grep({.query = "BEST", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Lexical, .group = true});
    REQUIRE(flat.has_value());
    REQUIRE(tier.has_value());
    REQUIRE(flat->matches.size() == 2);
    REQUIRE(tier->matches.size() == 2);
    const nesso::TextMatch *short_line = MatchAtLine(*flat, 1);
    const nesso::TextMatch *long_line = MatchAtLine(*flat, 2);
    REQUIRE(short_line != nullptr);
    REQUIRE(long_line != nullptr);
    REQUIRE(short_line->score > long_line->score);
    CHECK(tier->matches[0].text == long_line->text);
    CHECK(tier->matches[0].score == long_line->score);
    CHECK(tier->matches[0].line == 2);
    CHECK(tier->matches[1].text == "best");
    CHECK(tier->matches[1].score == short_line->score);
}

TEST_CASE("hybrid and auto --group use the effective mode score", "[nesso][text]")
{
    const TempDir dir{"nesso_group_hybrid"};
    const auto log = dir.Path() / "app.log";
    WriteFile(log, "user 10 note\nuser 20 note\n");
    const nesso::Nesso app = ScriptedApp();

    const auto lines = app.Grep({.query = "20", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Hybrid});
    REQUIRE(lines.has_value());
    const nesso::TextMatch *first = MatchAtLine(*lines, 1);
    const nesso::TextMatch *second = MatchAtLine(*lines, 2);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(second->score != first->score);

    const auto grouped =
        app.Grep({.query = "20", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Hybrid, .group = true});
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
        app.Grep({.query = "note", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto, .group = true});
    REQUIRE(natural.has_value());
    REQUIRE_FALSE(natural->matches.empty());
    CHECK(natural->matches[0].mode == "hybrid");

    WriteFile(log, "request failed for blk_111111111111 today\n"
                   "request failed for blk_38865049064139660 today\n");
    const auto identifier_lines =
        app.Grep({.query = "blk_38865049064139660", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto});
    const auto identifier = app.Grep(
        {.query = "blk_38865049064139660", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Auto, .group = true});
    REQUIRE(identifier_lines.has_value());
    REQUIRE(identifier.has_value());
    const nesso::TextMatch *hit = MatchAtLine(*identifier_lines, 2);
    REQUIRE(hit != nullptr);
    REQUIRE(identifier->matches.size() == 1);
    CHECK(identifier->matches[0].mode == "lexical");
    CHECK(identifier->matches[0].text == "request failed for blk_111111111111 today");
    CHECK(identifier->matches[0].occurrences == 2);
    CHECK(identifier->matches[0].score == hit->score);
    CHECK(identifier->matches[0].lexical == hit->lexical);
    CHECK_FALSE(identifier->lexical_fallback);

    WriteFile(log, "plain words here\nmore plain words\n");
    const auto fallback = app.Grep(
        {.query = "zzzznotpresent99", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Lexical, .group = true});
    REQUIRE(fallback.has_value());
    CHECK(fallback->lexical_fallback);
    REQUIRE(fallback->matches.size() == 2);
    CHECK(fallback->matches[0].mode == "hybrid");
    CHECK(fallback->matches[1].mode == "hybrid");
}

TEST_CASE("search --group matches grep and keeps the first line when a source "
          "is missing",
          "[nesso][text]")
{
    const TempDir dir{"nesso_group_search"};
    const auto log = dir.Path() / "app.log";
    WriteFile(log, "user 10 note\nuser 20 note\nBEST unique line\n");
    const nesso::Nesso app = ScriptedApp();
    const auto indexed = app.Index({.files = {log}, .output = dir.Path() / "corpus.nesso"});
    REQUIRE(indexed.has_value());

    const auto grepped =
        app.Grep({.query = "20", .files = {log}, .top_k = 5, .mode = nesso::SearchMode::Hybrid, .group = true});
    const auto searched = app.Search({.query = "20",
                                      .index = dir.Path() / "corpus.nesso",
                                      .top_k = 5,
                                      .mode = nesso::SearchMode::Hybrid,
                                      .group = true});
    REQUIRE(grepped.has_value());
    REQUIRE(searched.has_value());
    REQUIRE(grepped->matches.size() == 2);
    REQUIRE(searched->matches.size() == 2);
    const nesso::TextMatch *grep_user = nullptr;
    const nesso::TextMatch *search_user = nullptr;
    for (const nesso::TextMatch &match : grepped->matches)
    {
        if (match.text == "user 10 note")
        {
            grep_user = &match;
        }
    }
    for (const nesso::TextMatch &match : searched->matches)
    {
        if (match.text == "user 10 note")
        {
            search_user = &match;
        }
    }
    REQUIRE(grep_user != nullptr);
    REQUIRE(search_user != nullptr);
    CHECK(searched->grouped);
    CHECK(search_user->text == grep_user->text);
    CHECK(search_user->line == 1);
    CHECK(search_user->score == grep_user->score);
    CHECK(search_user->lexical == grep_user->lexical);
    CHECK(search_user->dense == grep_user->dense);
    CHECK(search_user->occurrences == 2);
    CHECK(search_user->mode == "hybrid");

    const auto lexical = app.Search({.query = "20",
                                     .index = dir.Path() / "corpus.nesso",
                                     .top_k = 1,
                                     .mode = nesso::SearchMode::Lexical,
                                     .group = true});
    REQUIRE(lexical.has_value());
    REQUIRE(lexical->matches.size() == 1);
    CHECK(lexical->matches[0].text == "user 10 note");
    CHECK(lexical->matches[0].occurrences == 2);
    CHECK(lexical->matches[0].mode == "lexical");

    const auto dense = app.Search({.query = "note",
                                   .index = dir.Path() / "corpus.nesso",
                                   .top_k = 5,
                                   .mode = nesso::SearchMode::Dense,
                                   .group = true});
    REQUIRE(dense.has_value());
    REQUIRE(dense->matches.size() == 2);
    CHECK(dense->matches[0].text == "BEST unique line");
    CHECK(dense->matches[0].score == 1.0F);
    CHECK(dense->matches[1].text == "user 10 note");
    CHECK(dense->matches[1].occurrences == 2);

    WriteFile(log, "kept 10\n\nkept 20\n");
    const auto blank_index = app.Index({.files = {log}, .output = dir.Path() / "blank.nesso"});
    REQUIRE(blank_index.has_value());
    const auto blank = app.Search({.query = "kept",
                                   .index = dir.Path() / "blank.nesso",
                                   .top_k = 5,
                                   .mode = nesso::SearchMode::Dense,
                                   .group = true});
    REQUIRE(blank.has_value());
    REQUIRE(blank->matches.size() == 1);
    CHECK(blank->matches[0].text == "kept 10");
    CHECK(blank->matches[0].occurrences == 2);

    std::filesystem::remove(log);
    const auto missing = app.Search({.query = "20",
                                     .index = dir.Path() / "corpus.nesso",
                                     .top_k = 5,
                                     .mode = nesso::SearchMode::Lexical,
                                     .group = true});
    REQUIRE(missing.has_value());
    REQUIRE(missing->matches.size() == 1);
    CHECK(missing->matches[0].text == "user 10 note");
    CHECK(missing->matches[0].occurrences == 2);
    CHECK_FALSE(missing->stale_sources.empty());

    const auto first = dir.Path() / "a.log";
    const auto second = dir.Path() / "b.log";
    WriteFile(first, "same 1\n");
    WriteFile(second, "same 2\n");
    const auto multi_index = app.Index({.files = {first, second}, .output = dir.Path() / "multi.nesso"});
    REQUIRE(multi_index.has_value());
    const auto multi = app.Search({.query = "same",
                                   .index = dir.Path() / "multi.nesso",
                                   .top_k = 5,
                                   .mode = nesso::SearchMode::Dense,
                                   .group = true});
    REQUIRE(multi.has_value());
    CHECK(multi->multiple_sources);
    REQUIRE(multi->matches.size() == 1);
    CHECK(multi->matches[0].source == first.string());
    CHECK(multi->matches[0].text == "same 1");
    CHECK(multi->matches[0].occurrences == 2);

    std::stringstream input{"user 10 note\nuser 20 note\n"};
    const CinGuard guard{input};
    const auto stdin_index = app.Index({.files = {"-"}, .output = dir.Path() / "stdin.nesso"});
    REQUIRE(stdin_index.has_value());
    const auto stdin_search = app.Search({.query = "20",
                                          .index = dir.Path() / "stdin.nesso",
                                          .top_k = 5,
                                          .mode = nesso::SearchMode::Lexical,
                                          .group = true});
    REQUIRE(stdin_search.has_value());
    REQUIRE(stdin_search->matches.size() == 1);
    CHECK(stdin_search->matches[0].source == "-");
    CHECK(stdin_search->matches[0].text == "user 10 note");
    CHECK(stdin_search->matches[0].occurrences == 2);
    CHECK(stdin_search->matches[0].mode == "lexical");
}

TEST_CASE("grouped grep and search stay byte-identical across thread counts", "[nesso][text]")
{
    const TempDir dir{"nesso_group_threads"};
    const auto log = dir.Path() / "rows.log";
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

    nesso::Config indexer_config;
    indexer_config.threads = 1;
    nesso::Nesso indexer{indexer_config, std::make_unique<HashEmbedder>()};
    const auto indexed = indexer.Index({.files = {log}, .output = dir.Path() / "corpus.nesso"});
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
                const auto grep_result =
                    app.Grep({.query = "event", .files = {log}, .top_k = 5000, .mode = mode, .group = group});
                const auto search_result = app.Search({.query = "event",
                                                       .index = dir.Path() / "corpus.nesso",
                                                       .top_k = 5000,
                                                       .mode = mode,
                                                       .group = group});
                REQUIRE(grep_result.has_value());
                REQUIRE(search_result.has_value());
                CHECK(grep_result->grouped == group);
                CHECK(search_result->grouped == group);
                grepped.push_back(*grep_result);
                searched.push_back(*search_result);
            }
            RequireSameResults(grepped[0], grepped[1]);
            RequireSameResults(grepped[0], grepped[2]);
            RequireSameResults(searched[0], searched[1]);
            RequireSameResults(searched[0], searched[2]);
        }
    }
}
