// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_SCORE_FUSION_HPP
#define CORE_SCORE_FUSION_HPP

#include <cstddef>
#include <span>
#include <vector>

namespace nesso::core
{

/// Fixed block size for ordinal z-score sums ([ADR 0011](docs/adr/0011-hybrid-search-and-identifier-routing.md)).
inline constexpr std::size_t ZSCORE_BLOCK = 4096;

/// Dense fusion weight for hybrid scoring.
inline constexpr double HYBRID_DENSE_WEIGHT = 0.8;

/// BM25 fusion weight for hybrid scoring.
inline constexpr double HYBRID_LEXICAL_WEIGHT = 0.2;

/// Population mean and standard deviation of `values` in double, summed in
/// fixed `ZSCORE_BLOCK` blocks in index order. A zero deviation yields z = 0.
struct ZScoreStats
{
    double mean = 0.0;
    double deviation = 0.0;
};

[[nodiscard]] ZScoreStats zScoreStats(std::span<const double> values);

/// `(value - mean) / deviation`, or `0` when `deviation` is 0.
[[nodiscard]] double zScore(double value, const ZScoreStats &stats) noexcept;

/// `HYBRID_DENSE_WEIGHT * z(dense) + HYBRID_LEXICAL_WEIGHT * z(bm25)`, then
/// `static_cast<float>`. Callers sort on that float.
[[nodiscard]] float fuseHybridScore(double dense, double bm25, const ZScoreStats &denseStats,
                                    const ZScoreStats &bm25Stats) noexcept;

/// One fused float per aligned dense/bm25 pair. Lengths must match.
[[nodiscard]] std::vector<float> fuseHybridScores(std::span<const double> dense, std::span<const double> bm25);

} // namespace nesso::core

#endif // CORE_SCORE_FUSION_HPP
