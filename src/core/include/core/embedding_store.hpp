// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef CORE_EMBEDDING_STORE_HPP
#define CORE_EMBEDDING_STORE_HPP

#include "core_types.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace core
{

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

struct LogChunk
{
    std::string text;
    uint64_t lineNumber = 0;
    std::string source;
};

template <SupportedScalar T> struct EmbeddingSearchResult
{
    uint64_t index = 0;
    T score{};
    LogChunk chunk{};
};

template <SupportedScalar T> class EmbeddingStore
{
  public:
    [[nodiscard]] std::expected<void, EngineError> insert(std::span<const T> embedding, LogChunk chunk);

    [[nodiscard]] std::expected<std::vector<EmbeddingSearchResult<T>>, EngineError> searchTopK(std::span<const T> query,
                                                                                               size_t k) const;

    [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

  private:
    struct Entry
    {
        std::vector<T> embedding{};
        LogChunk chunk{};
    };

    std::vector<Entry> entries_;
    uint64_t dimensions_ = 0;
};

extern template class EmbeddingStore<float>;
extern template class EmbeddingStore<double>;

} // namespace core

#endif // CORE_EMBEDDING_STORE_HPP
