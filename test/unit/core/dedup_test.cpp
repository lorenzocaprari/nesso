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

auto ViewsOf(const std::vector<std::string> &lines) -> std::vector<std::string_view>
{
    std::vector<std::string_view> views;
    views.reserve(lines.size());
    for (const std::string &line : lines)
    {
        views.emplace_back(line);
    }
    return views;
}

auto PlainToken(std::size_t index) -> std::string
{
    // A non-hex letter plus base-26 keeps the token off the mask rules.
    std::string token = "w";
    do
    {
        token.push_back(static_cast<char>('a' + static_cast<int>(index % 26)));
        index /= 26;
    } while (index > 0);
    return token;
}

} // namespace

TEST_CASE("identical lines collapse to one distinct text", "[dedup]")
{
    const std::vector<std::string> lines(6, "repeat me");
    nesso::core::DistinctIndex index;
    std::vector<std::uint32_t> ids;
    for (const std::string &line : lines)
    {
        ids.push_back(index.Add(line));
    }

    CHECK(index.Pending().size() == 1);
    CHECK(index.Pending().front() == "repeat me");
    CHECK(index.Occurrences(0) == 6);
    CHECK(ids == std::vector<std::uint32_t>(6, 0));

    const std::vector<std::uint32_t> counts = nesso::core::OccurrenceCounts(ViewsOf(lines));
    CHECK(counts == std::vector<std::uint32_t>(6, 6));
    CHECK(nesso::core::OccurrenceCounts(ViewsOf(lines)) == counts);
}

TEST_CASE("distinct ids follow first occurrence", "[dedup]")
{
    const std::vector<std::string> lines{"beta", "alpha", "beta"};
    nesso::core::DistinctIndex index;
    CHECK(index.Add(lines[0]) == 0);
    CHECK(index.Add(lines[1]) == 1);
    CHECK(index.Add(lines[2]) == 0);
    CHECK(index.Pending().size() == 2);
    CHECK(index.Pending()[0] == "beta");
    CHECK(index.Pending()[1] == "alpha");
    CHECK(index.Occurrences(0) == 2);
    CHECK(index.Occurrences(1) == 1);
    index.ClearPending();
    CHECK(index.Pending().empty());
    CHECK(index.Add("beta") == 0);
    CHECK(index.Pending().empty());
    CHECK(index.Occurrences(0) == 3);
}

TEST_CASE("expanded distinct scores match the search oracle", "[dedup]")
{
    const std::vector<std::string> lines{"beta", "alpha", "beta", "gamma", "alpha", "alpha"};
    nesso::core::DistinctIndex index;
    std::vector<std::uint32_t> ids;
    ids.reserve(lines.size());
    for (const std::string &line : lines)
    {
        ids.push_back(index.Add(line));
    }
    REQUIRE(index.Pending().size() == 3);
    REQUIRE(index.Pending()[0] == "beta");
    REQUIRE(index.Pending()[1] == "alpha");
    REQUIRE(index.Pending()[2] == "gamma");

    const std::vector<float> score_by_id{0.2F, 0.9F, 0.4F};
    const std::vector<std::uint32_t> counts = nesso::core::OccurrenceCounts(ViewsOf(lines));
    std::vector<float> matrix(lines.size());
    for (size_t line = 0; line < lines.size(); ++line)
    {
        matrix[line] = score_by_id[ids[line]];
        CHECK(counts[line] == index.Occurrences(ids[line]));
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
    const auto ranked = nesso::core::TopK({.query = query, .matrix = matrix, .dims = 1, .k = top});
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
    std::ranges::sort(oracle, [](const Row &left, const Row &right) -> bool
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
    index.Reserve(nesso::embed::BATCH_WINDOW + 2);
    std::vector<std::string> embedded;
    const auto flush = [&] -> void
    {
        embedded.insert(embedded.end(), index.Pending().begin(), index.Pending().end());
        index.ClearPending();
    };

    CHECK(index.Add("shared") == 0);
    for (size_t line = 1; line < nesso::embed::BATCH_WINDOW; ++line)
    {
        CHECK(index.Add(PlainToken(line)) == static_cast<std::uint32_t>(line));
        if (index.Pending().size() == nesso::embed::BATCH_WINDOW)
        {
            flush();
        }
    }
    CHECK(index.Pending().empty());
    CHECK(index.Add("shared") == 0);
    CHECK(index.Pending().empty());
    CHECK(index.Occurrences(0) == 2);
    CHECK(index.Add("fresh") == static_cast<std::uint32_t>(nesso::embed::BATCH_WINDOW));
    CHECK(index.Pending().size() == 1);
    flush();

    CHECK(std::count(embedded.begin(), embedded.end(), "shared") == 1);
    CHECK(embedded.back() == "fresh");
    CHECK(embedded.size() == nesso::embed::BATCH_WINDOW + 1);
}

TEST_CASE("masked keys collapse to the first raw line", "[dedup]")
{
    nesso::core::DistinctIndex index;
    CHECK(index.Add("block blk_10 done") == 0);
    CHECK(index.Add("block blk_20 done") == 0);
    CHECK(index.Add("block blk_-3 done") == 0);
    CHECK(index.Pending().size() == 1);
    CHECK(index.Pending().front() == "block blk_10 done");
    CHECK(index.Text(0) == "block blk_10 done");
    CHECK(index.Occurrences(0) == 3);
    CHECK(index.Add("node dn228 ready") == 1);
    CHECK(index.Add("node cn541 ready") == 1);
    CHECK(index.Add("api v2 ready") == 2);
    CHECK(index.Pending()[1] == "node dn228 ready");
    CHECK(index.Pending()[2] == "api v2 ready");
    CHECK(index.Occurrences(1) == 2);

    const std::vector<std::string> lines{"block blk_20 done", "block blk_10 done", "sunny beach"};
    const std::vector<std::uint32_t> counts = nesso::core::OccurrenceCounts(ViewsOf(lines));
    CHECK(counts == std::vector<std::uint32_t>{2, 2, 1});
    CHECK(nesso::core::OccurrenceCounts(ViewsOf(lines)) == counts);
}

TEST_CASE("occurrence counts of an empty input are empty", "[dedup]")
{
    const std::vector<std::string_view> none;
    CHECK(nesso::core::OccurrenceCounts(none).empty());
}
