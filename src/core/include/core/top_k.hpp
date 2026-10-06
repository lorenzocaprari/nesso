// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef CORE_TOP_K_HPP
#define CORE_TOP_K_HPP

#include "core_types.hpp"
#include "distance.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace core
{

struct Hit
{
    uint64_t index = 0;
    float score = 0.0F;
};

namespace detail
{

template <typename ResultT, typename Compare>
void selectTopKInPlace(std::vector<ResultT> &results, size_t k, Compare compare)
{
    if (k == 0 || results.empty())
    {
        results.clear();
        return;
    }

    const size_t resultCount = std::min(k, results.size());
    std::partial_sort(results.begin(), results.begin() + static_cast<std::ptrdiff_t>(resultCount), results.end(),
                      compare);
    results.resize(resultCount);
}

} // namespace detail

/// Highest dot-product rows of a row-major `matrix` (`rows * dims` floats).
/// Text for those rows is the caller's job: a hit carries only `index` and `score`.
[[nodiscard]] inline std::expected<std::vector<Hit>, EngineError>
topK(std::span<const float> query, std::span<const float> matrix, size_t dims, size_t k)
{
    if (k == 0 || matrix.empty())
    {
        return std::vector<Hit>{};
    }
    if (dims == 0)
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (query.size() != dims)
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (matrix.size() % dims != 0)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    const size_t rows = matrix.size() / dims;
    std::vector<Hit> hits;
    hits.reserve(rows);
    for (size_t row = 0; row < rows; ++row)
    {
        const std::span<const float> stored = matrix.subspan(row * dims, dims);
        const auto score = math::DistanceMetrics::dotProduct(query, stored);
        if (!score)
        {
            return std::unexpected(score.error());
        }
        hits.push_back({.index = static_cast<uint64_t>(row), .score = *score});
    }

    const auto resultOrder = [](const Hit &left, const Hit &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };
    detail::selectTopKInPlace(hits, k, resultOrder);
    return hits;
}

} // namespace core

#endif // CORE_TOP_K_HPP
