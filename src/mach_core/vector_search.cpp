// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/vector_search.hpp"

#include "include/mach_core/distance.hpp"

#include <algorithm>
#include <utility>

namespace mach_core
{

template <SupportedScalar T>
std::expected<std::vector<SearchResult<T>>, EngineError> searchTopKCosine(const StorageEngine<T> &engine,
                                                                          std::span<const T> query, size_t k)
{
    if (!engine.isOpen()) [[unlikely]]
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (query.size() != engine.getDimensions()) [[unlikely]]
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (k == 0 || engine.getVectorCount() == 0)
    {
        return std::vector<SearchResult<T>>{};
    }

    std::vector<SearchResult<T>> results;
    results.reserve(static_cast<size_t>(engine.getVectorCount()));
    for (uint64_t index = 0; index < engine.getVectorCount(); ++index)
    {
        auto vector = engine.getVector(index);
        if (!vector)
        {
            return std::unexpected(vector.error());
        }

        auto score = math::CosineSimilarity::calculate(query, *vector);
        if (!score)
        {
            return std::unexpected(score.error());
        }
        results.push_back({.index = index, .score = *score});
    }

    const auto resultOrder = [](const SearchResult<T> &left, const SearchResult<T> &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };
    const size_t resultCount = std::min(k, results.size());
    std::partial_sort(results.begin(), results.begin() + static_cast<std::ptrdiff_t>(resultCount), results.end(),
                      resultOrder);
    results.resize(resultCount);
    return results;
}

template std::expected<std::vector<SearchResult<float>>, EngineError>
searchTopKCosine<float>(const StorageEngine<float> &, std::span<const float>, size_t);
template std::expected<std::vector<SearchResult<double>>, EngineError>
searchTopKCosine<double>(const StorageEngine<double> &, std::span<const double>, size_t);

} // namespace mach_core
