// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/score_fusion.hpp>

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace nesso::core
{

ZScoreStats ComputeZScoreStats(std::span<const double> values)
{
    ZScoreStats stats;
    if (values.empty())
    {
        return stats;
    }

    double sum = 0.0;
    for (std::size_t begin = 0; begin < values.size(); begin += ZSCORE_BLOCK)
    {
        const std::size_t end = begin + ZSCORE_BLOCK < values.size() ? begin + ZSCORE_BLOCK : values.size();
        double block = 0.0;
        for (std::size_t index = begin; index < end; ++index)
        {
            block += values[index];
        }
        sum += block;
    }
    stats.mean = sum / static_cast<double>(values.size());

    double sum_squares = 0.0;
    for (std::size_t begin = 0; begin < values.size(); begin += ZSCORE_BLOCK)
    {
        const std::size_t end = begin + ZSCORE_BLOCK < values.size() ? begin + ZSCORE_BLOCK : values.size();
        double block = 0.0;
        for (std::size_t index = begin; index < end; ++index)
        {
            const double delta = values[index] - stats.mean;
            block += delta * delta;
        }
        sum_squares += block;
    }
    stats.deviation = std::sqrt(sum_squares / static_cast<double>(values.size()));
    return stats;
}

double ZScore(double value, const ZScoreStats &stats) noexcept
{
    if (stats.deviation == 0.0)
    {
        return 0.0;
    }
    return (value - stats.mean) / stats.deviation;
}

float FuseHybridScore(double dense, double bm25, const ZScoreStats &dense_stats, const ZScoreStats &bm25_stats) noexcept
{
    const double fused =
        (HYBRID_DENSE_WEIGHT * ZScore(dense, dense_stats)) + (HYBRID_LEXICAL_WEIGHT * ZScore(bm25, bm25_stats));
    return static_cast<float>(fused);
}

std::vector<float> FuseHybridScores(std::span<const double> dense, std::span<const double> bm25)
{
    if (dense.size() != bm25.size())
    {
        return {};
    }
    const ZScoreStats dense_stats = ComputeZScoreStats(dense);
    const ZScoreStats bm25_stats = ComputeZScoreStats(bm25);
    std::vector<float> fused;
    fused.reserve(dense.size());
    for (std::size_t index = 0; index < dense.size(); ++index)
    {
        fused.push_back(FuseHybridScore(dense[index], bm25[index], dense_stats, bm25_stats));
    }
    return fused;
}

} // namespace nesso::core
