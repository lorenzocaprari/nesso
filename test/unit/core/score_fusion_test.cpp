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
        const nesso::core::ZScoreStats stats = nesso::core::zScoreStats({});
        CHECK(stats.mean == 0.0);
        CHECK(stats.deviation == 0.0);
    }

    SECTION("singleton")
    {
        const std::vector<double> values{42.0};
        const nesso::core::ZScoreStats stats = nesso::core::zScoreStats(values);
        CHECK(stats.mean == 42.0);
        CHECK(stats.deviation == 0.0);
        CHECK(nesso::core::zScore(42.0, stats) == 0.0);
    }

    SECTION("all equal values")
    {
        const std::vector<double> values{3.0, 3.0, 3.0, 3.0};
        const nesso::core::ZScoreStats stats = nesso::core::zScoreStats(values);
        CHECK(stats.mean == 3.0);
        CHECK(stats.deviation == 0.0);
        CHECK(nesso::core::zScore(3.0, stats) == 0.0);
    }
}

TEST_CASE("zScoreStats uses population deviation over the full vector", "[fusion][core]")
{
    const std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};
    const nesso::core::ZScoreStats stats = nesso::core::zScoreStats(values);

    CHECK(stats.mean == Catch::Approx(3.0));
    const double expectedDeviation = std::sqrt(2.0);
    CHECK(stats.deviation == Catch::Approx(expectedDeviation));

    CHECK(nesso::core::zScore(5.0, stats) == Catch::Approx(std::sqrt(2.0)));
    CHECK(nesso::core::zScore(1.0, stats) == Catch::Approx(-std::sqrt(2.0)));
}

TEST_CASE("fuseHybridScore applies fixed weights and truncates to float", "[fusion][core]")
{
    CHECK(nesso::core::HYBRID_DENSE_WEIGHT == 0.8);
    CHECK(nesso::core::HYBRID_LEXICAL_WEIGHT == 0.2);

    const std::vector<double> dense{1.0, 5.0};
    const std::vector<double> bm25{0.0, 10.0};
    const nesso::core::ZScoreStats denseStats = nesso::core::zScoreStats(dense);
    const nesso::core::ZScoreStats bm25Stats = nesso::core::zScoreStats(bm25);

    const float lowPair = nesso::core::fuseHybridScore(dense[0], bm25[0], denseStats, bm25Stats);
    const float highPair = nesso::core::fuseHybridScore(dense[1], bm25[1], denseStats, bm25Stats);
    CHECK(lowPair == Catch::Approx(-1.0F));
    CHECK(highPair == Catch::Approx(1.0F));
    CHECK(std::isfinite(lowPair));
    CHECK(std::isfinite(highPair));
}

TEST_CASE("fuseHybridScores returns one float per aligned pair", "[fusion][core]")
{
    SECTION("fused vector matches per-pair fusion")
    {
        const std::vector<double> dense{1.0, 5.0};
        const std::vector<double> bm25{0.0, 10.0};
        const std::vector<float> fused = nesso::core::fuseHybridScores(dense, bm25);
        REQUIRE(fused.size() == 2U);
        CHECK(fused[0] == Catch::Approx(-1.0F));
        CHECK(fused[1] == Catch::Approx(1.0F));
    }

    SECTION("length mismatch returns empty")
    {
        const std::vector<double> dense{1.0, 2.0};
        const std::vector<double> bm25{1.0};
        CHECK(nesso::core::fuseHybridScores(dense, bm25).empty());
    }
}
