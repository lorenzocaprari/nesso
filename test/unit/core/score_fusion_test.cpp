// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/score_fusion.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

TEST_CASE("zScoreStats edge cases", "[fusion][core]")
{
    SECTION("empty span")
    {
        const nesso::core::ZScoreStats stats = nesso::core::ComputeZScoreStats({});
        CHECK(stats.mean == 0.0);
        CHECK(stats.deviation == 0.0);
    }

    SECTION("singleton")
    {
        const std::vector<double> values{42.0};
        const nesso::core::ZScoreStats stats = nesso::core::ComputeZScoreStats(values);
        CHECK(stats.mean == 42.0);
        CHECK(stats.deviation == 0.0);
        CHECK(nesso::core::ZScore(42.0, stats) == 0.0);
    }

    SECTION("all equal values")
    {
        const std::vector<double> values{3.0, 3.0, 3.0, 3.0};
        const nesso::core::ZScoreStats stats = nesso::core::ComputeZScoreStats(values);
        CHECK(stats.mean == 3.0);
        CHECK(stats.deviation == 0.0);
        CHECK(nesso::core::ZScore(3.0, stats) == 0.0);
    }
}

TEST_CASE("zScoreStats uses population deviation over the full vector", "[fusion][core]")
{
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};
    const nesso::core::ZScoreStats stats = nesso::core::ComputeZScoreStats(values);

    CHECK(stats.mean == Catch::Approx(3.0));
    const double expected_deviation = std::sqrt(2.0);
    CHECK(stats.deviation == Catch::Approx(expected_deviation));

    CHECK(nesso::core::ZScore(5.0, stats) == Catch::Approx(std::sqrt(2.0)));
    CHECK(nesso::core::ZScore(1.0, stats) == Catch::Approx(-std::sqrt(2.0)));
}

TEST_CASE("FuseHybridScore applies fixed weights and truncates to float", "[fusion][core]")
{
    CHECK(nesso::core::HYBRID_DENSE_WEIGHT == 0.8);
    CHECK(nesso::core::HYBRID_LEXICAL_WEIGHT == 0.2);

    const std::vector<double> dense{1.0, 5.0};
    const std::vector<double> bm25{0.0, 10.0};
    const nesso::core::ZScoreStats dense_stats = nesso::core::ComputeZScoreStats(dense);
    const nesso::core::ZScoreStats bm25_stats = nesso::core::ComputeZScoreStats(bm25);

    const float low_pair = nesso::core::FuseHybridScore(dense[0], bm25[0], dense_stats, bm25_stats);
    const float high_pair = nesso::core::FuseHybridScore(dense[1], bm25[1], dense_stats, bm25_stats);
    CHECK(low_pair == Catch::Approx(-1.0F));
    CHECK(high_pair == Catch::Approx(1.0F));
    CHECK(std::isfinite(low_pair));
    CHECK(std::isfinite(high_pair));
}

TEST_CASE("FuseHybridScores returns one float per aligned pair", "[fusion][core]")
{
    SECTION("fused vector matches per-pair fusion")
    {
        const std::vector<double> dense{1.0, 5.0};
        const std::vector<double> bm25{0.0, 10.0};
        const std::vector<float> fused = nesso::core::FuseHybridScores(dense, bm25);
        REQUIRE(fused.size() == 2U);
        CHECK(fused[0] == Catch::Approx(-1.0F));
        CHECK(fused[1] == Catch::Approx(1.0F));
    }

    SECTION("length mismatch returns empty")
    {
        const std::vector<double> dense{1.0, 2.0};
        const std::vector<double> bm25{1.0};
        CHECK(nesso::core::FuseHybridScores(dense, bm25).empty());
    }
}
