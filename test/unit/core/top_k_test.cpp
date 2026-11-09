// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <bit>
#include <catch2/catch_all.hpp>
#include <core/core_types.hpp>
#include <core/distance.hpp>
#include <core/top_k.hpp>
#include <cstdint>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

using namespace nesso::core;

namespace
{

std::vector<float> pack(std::initializer_list<std::span<const float>> rows)
{
    std::vector<float> matrix;
    for (const std::span<const float> row : rows)
    {
        matrix.insert(matrix.end(), row.begin(), row.end());
    }
    return matrix;
}

} // namespace

TEST_CASE("topK ranks rows by dot product", "[topK][core][Unit]")
{
    const std::array<float, 3> alpha{1.0F, 0.0F, 0.0F};
    const std::array<float, 3> beta{0.0F, 1.0F, 0.0F};
    const std::array<float, 3> gamma{0.7F, 0.7F, 0.0F};
    const auto matrix = pack({alpha, beta, gamma});

    const auto results = topK(std::array<float, 3>{0.6F, 0.8F, 0.0F}, matrix, 3, 2);
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 2);
    REQUIRE((*results)[0].index == 2);
    REQUIRE((*results)[1].index == 1);
    REQUIRE((*results)[0].score > (*results)[1].score);
}

TEST_CASE("topK breaks score ties by ascending index", "[topK][core][Unit]")
{
    const std::array<float, 3> row{1.0F, 0.0F, 0.0F};
    const auto matrix = pack({row, row});

    const auto results = topK(row, matrix, 3, 2);
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 2);
    REQUIRE((*results)[0].index == 0);
    REQUIRE((*results)[1].index == 1);
    REQUIRE((*results)[0].score == Catch::Approx(1.0F));
}

TEST_CASE("topK rejects a query whose width differs from the matrix", "[topK][core][Unit]")
{
    const std::array<float, 3> row{1.0F, 0.0F, 0.0F};
    const auto results = topK(std::array<float, 2>{1.0F, 0.0F}, std::span<const float>{row}, 3, 1);
    REQUIRE_FALSE(results.has_value());
    REQUIRE(results.error() == EngineError::MismatchedDimensions);
}

TEST_CASE("topK clamps k to the row count", "[topK][core][Unit]")
{
    const std::array<float, 2> row{1.0F, 0.0F};
    const auto results = topK(row, std::span<const float>{row}, 2, 10);
    REQUIRE(results.has_value());
    REQUIRE(results->size() == 1);
}

TEST_CASE("topK returns no hits for an empty matrix or zero k", "[topK][core][Unit]")
{
    const std::array<float, 3> query{1.0F, 0.0F, 0.0F};
    const auto empty = topK(query, {}, 3, 1);
    REQUIRE(empty.has_value());
    REQUIRE(empty->empty());

    const auto zeroK = topK(query, query, 3, 0);
    REQUIRE(zeroK.has_value());
    REQUIRE(zeroK->empty());
}

TEST_CASE("topK rejects a zero-width or ragged matrix", "[topK][core][Unit]")
{
    const std::array<float, 3> row{1.0F, 0.0F, 0.0F};
    const auto zeroWidth = topK(std::span<const float>{}, row, 0, 1);
    REQUIRE_FALSE(zeroWidth.has_value());
    REQUIRE(zeroWidth.error() == EngineError::DatabaseNotInitialized);

    const auto ragged = topK(std::array<float, 2>{1.0F, 0.0F}, row, 2, 1);
    REQUIRE_FALSE(ragged.has_value());
    REQUIRE(ragged.error() == EngineError::CorruptDatabase);
}

std::uint32_t scoreBits(float score) { return std::bit_cast<std::uint32_t>(score); }

void requireSameHits(const std::vector<Hit> &actual, const std::vector<Hit> &expected)
{
    REQUIRE(actual.size() == expected.size());
    for (size_t index = 0; index < actual.size(); ++index)
    {
        CHECK(actual[index].index == expected[index].index);
        CHECK(scoreBits(actual[index].score) == scoreBits(expected[index].score));
    }
}

std::vector<Hit> oracleTop(std::span<const float> query, std::span<const float> matrix, size_t dims, size_t k)
{
    std::vector<Hit> hits;
    const size_t rows = matrix.size() / dims;
    hits.reserve(rows);
    for (size_t row = 0; row < rows; ++row)
    {
        const auto score = math::DistanceMetrics::dotProduct(query, matrix.subspan(row * dims, dims));
        REQUIRE(score.has_value());
        hits.push_back(Hit{.index = static_cast<uint64_t>(row), .score = *score});
    }
    const auto better = [](const Hit &left, const Hit &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };
    detail::selectTopKInPlace(hits, k, better);
    return hits;
}

struct CollectedRanges
{
    std::mutex mutex;
    std::vector<std::pair<size_t, size_t>> ranges;
};

void recordRange(void *raw, size_t begin, size_t end)
{
    auto *collected = static_cast<CollectedRanges *>(raw);
    std::lock_guard lock(collected->mutex);
    collected->ranges.emplace_back(begin, end);
}

void requireContiguousChunks(std::vector<std::pair<size_t, size_t>> ranges, size_t rows, size_t expectedChunks)
{
    REQUIRE(ranges.size() == expectedChunks);
    std::ranges::sort(ranges);
    REQUIRE(ranges.front().first == 0);
    REQUIRE(ranges.back().second == rows);
    size_t shortest = rows;
    size_t longest = 0;
    for (size_t index = 0; index < ranges.size(); ++index)
    {
        REQUIRE(ranges[index].second >= ranges[index].first);
        if (index > 0)
        {
            CHECK(ranges[index].first == ranges[index - 1].second);
        }
        const size_t length = ranges[index].second - ranges[index].first;
        shortest = std::min(shortest, length);
        longest = std::max(longest, length);
    }
    CHECK(longest - shortest <= 1);
    if (expectedChunks > 1)
    {
        CHECK(shortest > 0);
    }
}

TEST_CASE("parallelRows covers a long range with uneven contiguous chunks", "[topK][core][Unit]")
{
    const size_t rows = PARALLEL_MIN_ROWS + 1;
    for (const size_t threads : {size_t{2}, size_t{3}, size_t{4}, size_t{7}})
    {
        CollectedRanges collected;
        detail::parallelRows(threads, rows, recordRange, &collected);
        requireContiguousChunks(collected.ranges, rows, threads);
    }

    CollectedRanges capped;
    detail::parallelRows(PARALLEL_MAX_THREADS + 9, rows, recordRange, &capped);
    requireContiguousChunks(capped.ranges, rows, std::min(PARALLEL_MAX_THREADS, rows));

    CollectedRanges small;
    detail::parallelRows(7, 17, recordRange, &small);
    requireContiguousChunks(small.ranges, 17, 1);

    CollectedRanges serial;
    detail::parallelRows(1, rows, recordRange, &serial);
    requireContiguousChunks(serial.ranges, rows, 1);

    CollectedRanges unset;
    detail::parallelRows(0, rows, recordRange, &unset);
    requireContiguousChunks(unset.ranges, rows, 1);

    CollectedRanges empty;
    detail::parallelRows(4, 0, recordRange, &empty);
    requireContiguousChunks(empty.ranges, 0, 1);
}

TEST_CASE("topK at several thread counts matches the single-thread hit list", "[topK][core][Unit]")
{
    constexpr size_t dims = 8;
    const size_t rows = PARALLEL_MIN_ROWS + 1;
    REQUIRE(rows % 2 != 0);
    REQUIRE(rows % 3 != 0);
    REQUIRE(rows % 4 != 0);
    REQUIRE(rows % 7 != 0);

    std::vector<float> query(dims, 0.0F);
    query[0] = 1.0F;
    std::vector<float> varied(rows * dims, 0.0F);
    std::vector<float> tied(rows * dims, 0.0F);
    for (size_t row = 0; row < rows; ++row)
    {
        varied[(row * dims)] = static_cast<float>(row % 13U);
        tied[(row * dims)] = 3.0F;
    }

    const std::array<size_t, 8> budgets{0, 1, 2, 3, 4, 7, PARALLEL_MAX_THREADS, PARALLEL_MAX_THREADS + 5};
    const std::array<size_t, 6> limits{1, 2, 146, 147, rows - 1, rows + 4};
    for (const size_t threads : budgets)
    {
        for (const size_t k : limits)
        {
            const auto variedHits = topK(query, varied, dims, k, threads);
            const auto tiedHits = topK(query, tied, dims, k, threads);
            REQUIRE(variedHits.has_value());
            REQUIRE(tiedHits.has_value());
            requireSameHits(*variedHits, oracleTop(query, varied, dims, k));
            requireSameHits(*tiedHits, oracleTop(query, tied, dims, k));
        }
        const auto again = topK(query, varied, dims, 5, threads);
        REQUIRE(again.has_value());
        requireSameHits(*again, oracleTop(query, varied, dims, 5));
    }

    std::vector<float> small(17 * dims, 0.0F);
    for (size_t row = 0; row < 17; ++row)
    {
        small[(row * dims)] = static_cast<float>(16U - (row % 4U));
    }
    for (const size_t threads : {size_t{1}, size_t{2}, size_t{3}, size_t{4}, size_t{7}})
    {
        for (const size_t k : {size_t{1}, size_t{3}, size_t{17}, size_t{40}})
        {
            const auto hits = topK(query, small, dims, k, threads);
            REQUIRE(hits.has_value());
            requireSameHits(*hits, oracleTop(query, small, dims, k));
        }
    }

    const auto empty = topK(query, {}, dims, 3, 4);
    REQUIRE(empty.has_value());
    REQUIRE(empty->empty());
    const auto zeroK = topK(query, varied, dims, 0, 7);
    REQUIRE(zeroK.has_value());
    REQUIRE(zeroK->empty());
    const auto mismatch = topK(std::array<float, 2>{1.0F, 0.0F}, varied, dims, 1, 4);
    REQUIRE_FALSE(mismatch.has_value());
    REQUIRE(mismatch.error() == EngineError::MismatchedDimensions);
}

struct RankContext
{
    const std::vector<float> *scores = nullptr;
};

float contextScore(void *raw, size_t index) { return (*static_cast<const RankContext *>(raw)->scores)[index]; }

bool contextKeep(void *raw, size_t index)
{
    static_cast<void>(raw);
    return index % 5U != 0U;
}

TEST_CASE("rankTop keeps skipped rows out and matches a full sort", "[topK][core][Unit]")
{
    const size_t count = PARALLEL_MIN_ROWS + 1;
    std::vector<float> scores(count);
    for (size_t index = 0; index < count; ++index)
    {
        scores[index] = static_cast<float>(index % 11U);
    }
    RankContext context{.scores = &scores};
    std::vector<Hit> expected;
    for (size_t index = 0; index < count; ++index)
    {
        if (!contextKeep(&context, index))
        {
            continue;
        }
        expected.push_back(Hit{.index = static_cast<uint64_t>(index), .score = scores[index]});
    }
    const auto better = [](const Hit &left, const Hit &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };

    for (const size_t threads : {size_t{1}, size_t{2}, size_t{3}, size_t{4}, size_t{7}})
    {
        for (const size_t k : {size_t{1}, size_t{8}, count / 3, count, count + 2})
        {
            std::vector<Hit> wanted = expected;
            detail::selectTopKInPlace(wanted, k, better);
            const auto actual = rankTop(count, k, threads, contextScore, contextKeep, &context);
            requireSameHits(actual, wanted);
        }
    }

    REQUIRE(rankTop(count, 0, 4, contextScore, contextKeep, &context).empty());
    REQUIRE(rankTop(0, 3, 4, contextScore, contextKeep, &context).empty());
}

TEST_CASE("selectTopKInPlace clears on empty or zero k", "[topK][core][Unit]")
{
    const auto byScore = [](const Hit &left, const Hit &right) { return left.score > right.score; };

    std::vector<Hit> results{{.index = 0, .score = 1.0F}};
    detail::selectTopKInPlace(results, 0, byScore);
    REQUIRE(results.empty());

    std::vector<Hit> empty;
    detail::selectTopKInPlace(empty, 1, byScore);
    REQUIRE(empty.empty());
}
