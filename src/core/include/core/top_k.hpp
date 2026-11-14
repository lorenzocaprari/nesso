// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_TOP_K_HPP
#define CORE_TOP_K_HPP

#include <core/core_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace nesso::core
{

struct Hit
{
    uint64_t index = 0;
    float score = 0.0F;
};

/// Strict weak order for ranking: higher score first, ties by lower index.
/// Non-NaN scores rank before NaN; two NaNs break by index. ±0.0 compare equal.
[[nodiscard]] inline bool rankBefore(float leftScore, uint64_t leftIndex, float rightScore,
                                     uint64_t rightIndex) noexcept
{
    const bool leftNan = std::isnan(leftScore);
    const bool rightNan = std::isnan(rightScore);
    if (leftNan != rightNan)
    {
        return !leftNan;
    }
    if (leftNan)
    {
        return leftIndex < rightIndex;
    }
    return leftScore != rightScore ? leftScore > rightScore : leftIndex < rightIndex;
}

/// Row count below which a scan stays on the calling thread.
inline constexpr size_t PARALLEL_MIN_ROWS = 1024;

/// Upper bound on workers in the process-wide scan pool.
inline constexpr size_t PARALLEL_MAX_THREADS = 64;

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

/// Runs `fn(context, begin, end)` on contiguous pieces of `[0, rows)`.
///
/// `threads` counts the caller. One piece when `threads <= 1`, `rows` is below
/// `PARALLEL_MIN_ROWS`, or a worker cannot be started. The pool is process-wide
/// and grows to the widest budget seen, capped at `PARALLEL_MAX_THREADS`, so a
/// later scan does not spawn threads again. `fn` must not call `parallelRows`.
void parallelRows(size_t threads, size_t rows, void (*fn)(void *context, size_t begin, size_t end), void *context);

} // namespace detail

/// Inputs for a dense top-k scan over a row-major embedding matrix.
struct TopKRequest
{
    std::span<const float> query;
    std::span<const float> matrix;
    size_t dims = 0;
    size_t k = 0;
    size_t threads = 1;
};

/// Highest dot-product rows of a row-major `matrix` (`rows * dims` floats).
/// Text for those rows is the caller's job: a hit carries only `index` and `score`.
///
/// `threads` is the CPU budget, including the caller. Each worker scores a
/// contiguous row chunk and keeps a private top k. The chunks merge by score
/// descending, then ascending index. A budget of one, or a short matrix, stays
/// on the caller and produces the same hits.
[[nodiscard]] std::expected<std::vector<Hit>, EngineError> topK(const TopKRequest &request);

/// Inputs for an exact top-k scan over `count` indices.
struct RankTopRequest
{
    size_t count = 0;
    size_t k = 0;
    size_t threads = 1;
    float (*score)(void *context, size_t index) = nullptr;
    bool (*keep)(void *context, size_t index) = nullptr;
    void *context = nullptr;
};

/// Exact top k over `count` indices.
///
/// `keep` is consulted first. `score` runs once for each kept index. Chunks keep
/// a private top k and merge by score descending, then ascending index. `threads`
/// follows the same cutoff as `topK`.
[[nodiscard]] std::vector<Hit> rankTop(const RankTopRequest &request);

} // namespace nesso::core

#endif // CORE_TOP_K_HPP
