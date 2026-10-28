// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/dedup.hpp>
#include <core/top_k.hpp>
#include <embed/batch_plan.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace
{

std::vector<std::string_view> viewsOf(const std::vector<std::string> &lines)
{
    std::vector<std::string_view> views;
    views.reserve(lines.size());
    for (const std::string &line : lines)
    {
        views.emplace_back(line);
    }
    return views;
}

} // namespace

TEST_CASE("identical lines collapse to one distinct text", "[dedup]")
{
    const std::vector<std::string> lines(6, "repeat me");
    nesso::core::DistinctIndex index;
    std::vector<std::uint32_t> ids;
    for (const std::string &line : lines)
    {
        ids.push_back(index.add(line));
    }

    CHECK(index.pending().size() == 1);
    CHECK(index.pending().front() == "repeat me");
    CHECK(index.occurrences(0) == 6);
    CHECK(ids == std::vector<std::uint32_t>(6, 0));

    const std::vector<std::uint32_t> counts = nesso::core::occurrenceCounts(viewsOf(lines));
    CHECK(counts == std::vector<std::uint32_t>(6, 6));
    CHECK(nesso::core::occurrenceCounts(viewsOf(lines)) == counts);
}

TEST_CASE("distinct ids follow first occurrence", "[dedup]")
{
    const std::vector<std::string> lines{"beta", "alpha", "beta"};
    nesso::core::DistinctIndex index;
    CHECK(index.add(lines[0]) == 0);
    CHECK(index.add(lines[1]) == 1);
    CHECK(index.add(lines[2]) == 0);
    CHECK(index.pending().size() == 2);
    CHECK(index.pending()[0] == "beta");
    CHECK(index.pending()[1] == "alpha");
    CHECK(index.occurrences(0) == 2);
    CHECK(index.occurrences(1) == 1);
    index.clearPending();
    CHECK(index.pending().empty());
    CHECK(index.add("beta") == 0);
    CHECK(index.pending().empty());
    CHECK(index.occurrences(0) == 3);
}

TEST_CASE("expanded distinct scores match the search oracle", "[dedup]")
{
    const std::vector<std::string> lines{"beta", "alpha", "beta", "gamma", "alpha", "alpha"};
    nesso::core::DistinctIndex index;
    std::vector<std::uint32_t> ids;
    ids.reserve(lines.size());
    for (const std::string &line : lines)
    {
        ids.push_back(index.add(line));
    }
    REQUIRE(index.pending().size() == 3);
    REQUIRE(index.pending()[0] == "beta");
    REQUIRE(index.pending()[1] == "alpha");
    REQUIRE(index.pending()[2] == "gamma");

    const std::vector<float> scoreById{0.2F, 0.9F, 0.4F};
    const std::vector<std::uint32_t> counts = nesso::core::occurrenceCounts(viewsOf(lines));
    std::vector<float> matrix(lines.size());
    for (size_t line = 0; line < lines.size(); ++line)
    {
        matrix[line] = scoreById[ids[line]];
        CHECK(counts[line] == index.occurrences(ids[line]));
        CHECK(counts[line] >= 1);
    }
    CHECK(matrix[0] == matrix[2]);
    CHECK(matrix[1] == matrix[4]);
    CHECK(matrix[1] == matrix[5]);
    CHECK(counts[1] == 3);
    CHECK(counts[0] == 2);
    CHECK(counts[3] == 1);

    const std::array<float, 1> query{1.F};
    constexpr size_t top = 4;
    const auto ranked = nesso::core::topK(query, matrix, 1, top);
    REQUIRE(ranked.has_value());

    struct Row
    {
        std::uint64_t index = 0;
        float score = 0.0F;
        std::uint32_t occurrences = 0;
    };
    std::vector<Row> oracle;
    oracle.reserve(lines.size());
    for (size_t line = 0; line < lines.size(); ++line)
    {
        oracle.push_back(Row{.index = line, .score = matrix[line], .occurrences = counts[line]});
    }
    std::ranges::sort(oracle, [](const Row &left, const Row &right)
                      { return left.score != right.score ? left.score > right.score : left.index < right.index; });
    oracle.resize(top);

    REQUIRE(ranked->size() == oracle.size());
    for (size_t row = 0; row < oracle.size(); ++row)
    {
        const std::uint64_t hit = (*ranked)[row].index;
        CHECK(hit == oracle[row].index);
        CHECK((*ranked)[row].score == oracle[row].score);
        CHECK(counts[static_cast<size_t>(hit)] == oracle[row].occurrences);
    }
    CHECK(oracle[0].index == 1);
    CHECK(oracle[1].index == 4);
    CHECK(oracle[2].index == 5);
    CHECK(oracle[3].index == 3);
}

TEST_CASE("a repeated text that spans two windows stays one id", "[dedup]")
{
    nesso::core::DistinctIndex index;
    index.reserve(nesso::embed::BATCH_WINDOW + 2);
    std::vector<std::string> embedded;
    const auto flush = [&]()
    {
        embedded.insert(embedded.end(), index.pending().begin(), index.pending().end());
        index.clearPending();
    };

    CHECK(index.add("shared") == 0);
    for (size_t line = 1; line < nesso::embed::BATCH_WINDOW; ++line)
    {
        CHECK(index.add("unique-" + std::to_string(line)) == static_cast<std::uint32_t>(line));
        if (index.pending().size() == nesso::embed::BATCH_WINDOW)
        {
            flush();
        }
    }
    CHECK(index.pending().empty());
    CHECK(index.add("shared") == 0);
    CHECK(index.pending().empty());
    CHECK(index.occurrences(0) == 2);
    CHECK(index.add("fresh") == static_cast<std::uint32_t>(nesso::embed::BATCH_WINDOW));
    CHECK(index.pending().size() == 1);
    flush();

    CHECK(std::count(embedded.begin(), embedded.end(), "shared") == 1);
    CHECK(embedded.back() == "fresh");
    CHECK(embedded.size() == nesso::embed::BATCH_WINDOW + 1);
}

TEST_CASE("occurrence counts of an empty input are empty", "[dedup]")
{
    const std::vector<std::string_view> none;
    CHECK(nesso::core::occurrenceCounts(none).empty());
}
