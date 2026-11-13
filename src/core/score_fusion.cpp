// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/score_fusion.hpp>

#include <cmath>

namespace nesso::core
{

// NOLINTNEXTLINE(readability-identifier-naming)
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

    double sumSquares = 0.0;
    for (std::size_t begin = 0; begin < values.size(); begin += ZSCORE_BLOCK)
    {
        const std::size_t end = begin + ZSCORE_BLOCK < values.size() ? begin + ZSCORE_BLOCK : values.size();
        double block = 0.0;
        for (std::size_t index = begin; index < end; ++index)
        {
            const double delta = values[index] - stats.mean;
            block += delta * delta;
        }
        sumSquares += block;
    }
    stats.deviation = std::sqrt(sumSquares / static_cast<double>(values.size()));
    return stats;
}

double zScore(double value, const ZScoreStats &stats) noexcept
{
    if (stats.deviation == 0.0)
    {
        return 0.0;
    }
    return (value - stats.mean) / stats.deviation;
}

float fuseHybridScore(double dense, double bm25, const ZScoreStats &denseStats, const ZScoreStats &bm25Stats) noexcept
{
    const double fused =
        (HYBRID_DENSE_WEIGHT * zScore(dense, denseStats)) + (HYBRID_LEXICAL_WEIGHT * zScore(bm25, bm25Stats));
    return static_cast<float>(fused);
}

std::vector<float> fuseHybridScores(std::span<const double> dense, std::span<const double> bm25)
{
    if (dense.size() != bm25.size())
    {
        return {};
    }
    const ZScoreStats denseStats = ComputeZScoreStats(dense);
    const ZScoreStats bm25Stats = ComputeZScoreStats(bm25);
    std::vector<float> fused;
    fused.reserve(dense.size());
    for (std::size_t index = 0; index < dense.size(); ++index)
    {
        fused.push_back(fuseHybridScore(dense[index], bm25[index], denseStats, bm25Stats));
    }
    return fused;
}

} // namespace nesso::core
