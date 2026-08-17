// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/rrf.hpp"

#include <algorithm>
#include <unordered_map>

namespace mach_core
{

std::vector<SearchResult<float>> fuseRrf(std::span<const SearchResult<float>> semantic,
                                         std::span<const SearchResult<float>> lexical, size_t k, int rrfK)
{
    if (k == 0)
    {
        return {};
    }

    std::unordered_map<uint64_t, float> scores;
    const auto accumulate = [&](std::span<const SearchResult<float>> hits)
    {
        for (size_t rank = 0; rank < hits.size(); ++rank)
        {
            scores[hits[rank].index] += 1.0f / static_cast<float>(rrfK + static_cast<int>(rank) + 1);
        }
    };
    accumulate(semantic);
    accumulate(lexical);

    std::vector<SearchResult<float>> fused;
    fused.reserve(scores.size());
    for (const auto &[id, score] : scores)
    {
        fused.push_back({.index = id, .score = score});
    }
    const auto order = [](const SearchResult<float> &a, const SearchResult<float> &b)
    { return a.score != b.score ? a.score > b.score : a.index < b.index; };
    const size_t resultCount = std::min(k, fused.size());
    std::partial_sort(fused.begin(), fused.begin() + static_cast<std::ptrdiff_t>(resultCount), fused.end(), order);
    fused.resize(resultCount);
    return fused;
}

} // namespace mach_core
