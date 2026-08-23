// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/core/embedding_store.hpp"

#include "include/core/distance.hpp"
#include "include/core/vector_search.hpp"

#include <utility>

namespace core
{

template <SupportedScalar T>
std::expected<void, EngineError> EmbeddingStore<T>::insert(std::span<const T> embedding, LogChunk chunk)
{
    if (embedding.empty()) [[unlikely]]
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }

    if (entries_.empty())
    {
        dimensions_ = embedding.size();
    }
    else if (embedding.size() != dimensions_) [[unlikely]]
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }

    entries_.push_back({std::vector<T>(embedding.begin(), embedding.end()), std::move(chunk)});
    return {};
}

template <SupportedScalar T>
std::expected<std::vector<EmbeddingSearchResult<T>>, EngineError>
EmbeddingStore<T>::searchTopK(std::span<const T> query, size_t k) const
{
    if (entries_.empty() || k == 0)
    {
        return std::vector<EmbeddingSearchResult<T>>{};
    }
    if (query.size() != dimensions_) [[unlikely]]
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }

    std::vector<EmbeddingSearchResult<T>> results;
    results.reserve(entries_.size());
    for (uint64_t index = 0; index < entries_.size(); ++index)
    {
        const auto &entry = entries_[index];
        const std::span<const T> stored{entry.embedding.data(), entry.embedding.size()};
        const auto score = math::DistanceMetrics::dotProduct(query, stored);
        if (!score)
        {
            return std::unexpected(score.error());
        }

        results.push_back({.index = index, .score = *score, .chunk = entry.chunk});
    }

    const auto resultOrder = [](const EmbeddingSearchResult<T> &left, const EmbeddingSearchResult<T> &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };
    detail::selectTopKInPlace(results, k, resultOrder);
    return results;
}

template class EmbeddingStore<float>;
template class EmbeddingStore<double>;

} // namespace core
