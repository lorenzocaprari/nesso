// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_SCORE_FUSION_HPP
#define CORE_SCORE_FUSION_HPP

#include <core/query_class.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace nesso::core
{

/// Fixed block size for ordinal z-score sums ([ADR
/// 0011](docs/adr/0011-hybrid-search-and-identifier-routing.md)).
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

[[nodiscard]] auto ComputeZScoreStats(std::span<const double> values) -> ZScoreStats;

/// `(value - mean) / deviation`, or `0` when `deviation` is 0.
[[nodiscard]] auto ZScore(double value, const ZScoreStats &stats) noexcept -> double;

/// `HYBRID_DENSE_WEIGHT * z(dense) + HYBRID_LEXICAL_WEIGHT * z(bm25) *
/// coverage²`, then `static_cast<float>`. Callers sort on that float.
///
/// `coverage` is the line's share of distinct query terms (`Bm25Result`). It
/// keeps a line that matches one rare word of a long description from
/// outranking dense matches: BM25 is zero on most lines, so that word alone
/// gives a very large z(bm25). A line that matches the whole query keeps the
/// full lexical weight.
/// Whether hybrid fusion scales the lexical term by coverage for this query.
/// Only natural-language queries do. A query with an identifier token keeps
/// coverage 1 on every line, so a line holding only the identifier still gets
/// the full lexical weight ([ADR 0011](docs/adr/0011-hybrid-search-and-identifier-routing.md)).
[[nodiscard]] auto HybridUsesCoverage(QueryClass query_class) noexcept -> bool;

[[nodiscard]] auto FuseHybridScore(double dense, double bm25, double coverage, const ZScoreStats &dense_stats,
                                   const ZScoreStats &bm25_stats) noexcept -> float;

/// One fused float per aligned dense/bm25/coverage triple. Lengths must match,
/// otherwise the result is empty.
[[nodiscard]] auto FuseHybridScores(std::span<const double> dense, std::span<const double> bm25,
                                    std::span<const double> coverage) -> std::vector<float>;

} // namespace nesso::core

#endif // CORE_SCORE_FUSION_HPP
