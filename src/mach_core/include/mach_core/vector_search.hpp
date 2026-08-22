// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_VECTOR_SEARCH_HPP
#define MACH_CORE_VECTOR_SEARCH_HPP

#include "core_types.hpp"
#include "storage_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace mach_core
{

template <SupportedScalar T> struct SearchResult
{
    uint64_t index;
    T score;
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

template <SupportedScalar T>
[[nodiscard]] std::expected<std::vector<SearchResult<T>>, EngineError>
searchTopKCosine(const StorageEngine<T> &engine, std::span<const T> query, size_t k);

} // namespace mach_core

#endif // MACH_CORE_VECTOR_SEARCH_HPP
