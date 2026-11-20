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

    // Full coverage keeps the plain 0.8 / 0.2 blend.
    const float low_pair = nesso::core::FuseHybridScore(dense[0], bm25[0], 1.0, dense_stats, bm25_stats);
    const float high_pair = nesso::core::FuseHybridScore(dense[1], bm25[1], 1.0, dense_stats, bm25_stats);
    CHECK(low_pair == Catch::Approx(-1.0F));
    CHECK(high_pair == Catch::Approx(1.0F));
    CHECK(std::isfinite(low_pair));
    CHECK(std::isfinite(high_pair));
}

TEST_CASE("only natural-language queries use coverage", "[fusion][core]")
{
    CHECK(nesso::core::HybridUsesCoverage(nesso::core::QueryClass::NaturalLanguage));
    CHECK_FALSE(nesso::core::HybridUsesCoverage(nesso::core::QueryClass::Mixed));
    CHECK_FALSE(nesso::core::HybridUsesCoverage(nesso::core::QueryClass::Identifier));
    CHECK(nesso::core::HybridUsesCoverage(nesso::core::ClassifyQuery("hardware raised an unexpected interrupt")));
    CHECK_FALSE(nesso::core::HybridUsesCoverage(nesso::core::ClassifyQuery("error 0x0b85eee0")));
}

TEST_CASE("FuseHybridScore scales the lexical term by coverage squared", "[fusion][core]")
{
    const std::vector<double> dense{1.0, 5.0};
    const std::vector<double> bm25{0.0, 10.0};
    const nesso::core::ZScoreStats dense_stats = nesso::core::ComputeZScoreStats(dense);
    const nesso::core::ZScoreStats bm25_stats = nesso::core::ComputeZScoreStats(bm25);
    // z(dense) = 1 and z(bm25) = 1 for the second pair.
    CHECK(nesso::core::FuseHybridScore(dense[1], bm25[1], 0.5, dense_stats, bm25_stats) ==
          Catch::Approx(0.8F + (0.2F * 0.25F)));
    CHECK(nesso::core::FuseHybridScore(dense[1], bm25[1], 0.0, dense_stats, bm25_stats) == Catch::Approx(0.8F));
}

TEST_CASE("a one-word lexical match does not outrank a strong dense match", "[fusion][core]")
{
    // 1000 lines with BM25 0, one line matching one rare word of a nine-word
    // query (huge z(bm25), weak dense), and one line with the best dense score
    // and no lexical hit. Without coverage the rare word wins.
    // Dense similarities spread over 0.10–0.30, as on a real log.
    std::vector<double> dense(1002, 0.0);
    for (std::size_t index = 0; index < 1000; ++index)
    {
        dense[index] = 0.1 + (0.2 * static_cast<double>(index % 100) / 100.0);
    }
    std::vector<double> bm25(1002, 0.0);
    std::vector<double> coverage(1002, 0.0);
    dense[1000] = 0.25;
    bm25[1000] = 13.5;
    coverage[1000] = 1.0 / 9.0;
    dense[1001] = 0.55;

    const std::vector<double> full_coverage(1002, 1.0);
    const std::vector<float> plain = nesso::core::FuseHybridScores(dense, bm25, full_coverage);
    REQUIRE(plain.size() == 1002U);
    CHECK(plain[1000] > plain[1001]);

    const std::vector<float> fused = nesso::core::FuseHybridScores(dense, bm25, coverage);
    REQUIRE(fused.size() == 1002U);
    CHECK(fused[1001] > fused[1000]);
}

TEST_CASE("FuseHybridScores returns one float per aligned triple", "[fusion][core]")
{
    SECTION("fused vector matches per-pair fusion")
    {
        const std::vector<double> dense{1.0, 5.0};
        const std::vector<double> bm25{0.0, 10.0};
        const std::vector<double> coverage{0.0, 1.0};
        const std::vector<float> fused = nesso::core::FuseHybridScores(dense, bm25, coverage);
        REQUIRE(fused.size() == 2U);
        // The first line has no lexical hit: z(bm25) = -1 times coverage 0.
        CHECK(fused[0] == Catch::Approx(-0.8F));
        CHECK(fused[1] == Catch::Approx(1.0F));
    }

    SECTION("length mismatch returns empty")
    {
        const std::vector<double> dense{1.0, 2.0};
        const std::vector<double> bm25{1.0};
        const std::vector<double> coverage{1.0, 1.0};
        CHECK(nesso::core::FuseHybridScores(dense, bm25, coverage).empty());
        const std::vector<double> bm25_pair{1.0, 2.0};
        const std::vector<double> coverage_short{1.0};
        CHECK(nesso::core::FuseHybridScores(dense, bm25_pair, coverage_short).empty());
    }
}
