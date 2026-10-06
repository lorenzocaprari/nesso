// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <array>
#include <catch2/catch_all.hpp>
#include <core/core_types.hpp>
#include <core/top_k.hpp>
#include <span>
#include <vector>

using namespace core;

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
