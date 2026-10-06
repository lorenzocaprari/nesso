// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <cmath>
#include <core/distance.hpp>
#include <random>
#include <span>
#include <vector>

using core::math::CosineSimilarity;

namespace
{

[[nodiscard]] float naiveCosine(std::span<const float> left, std::span<const float> right)
{
    REQUIRE(left.size() == right.size());
    REQUIRE_FALSE(left.empty());

    double dot = 0.0;
    double normLeft = 0.0;
    double normRight = 0.0;
    for (size_t index = 0; index < left.size(); ++index)
    {
        const double a = static_cast<double>(left[index]);
        const double b = static_cast<double>(right[index]);
        dot += a * b;
        normLeft += a * a;
        normRight += b * b;
    }
    if (normLeft == 0.0 || normRight == 0.0)
    {
        return 0.0f;
    }
    return static_cast<float>(dot / (std::sqrt(normLeft) * std::sqrt(normRight)));
}

} // namespace

TEST_CASE("CosineSimilarity matches double-precision oracle on random inputs", "[CosineSimilarity][oracle][Unit]")
{
    auto seed = GENERATE(take(32, random(1, 1000000)));
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
    std::uniform_int_distribution<size_t> dimDist(1, 24);
    std::uniform_real_distribution<float> valueDist(-2.0f, 2.0f);
    std::bernoulli_distribution zeroRow(0.1);

    const size_t dimensions = dimDist(rng);
    std::vector<float> left(dimensions);
    std::vector<float> right(dimensions);
    for (float &component : left)
    {
        component = zeroRow(rng) ? 0.0f : valueDist(rng);
    }
    for (float &component : right)
    {
        component = zeroRow(rng) ? 0.0f : valueDist(rng);
    }

    const auto expected = naiveCosine(left, right);
    const auto actual = CosineSimilarity::calculate<float>(left, right);
    REQUIRE(actual.has_value());
    REQUIRE(actual.value() == Catch::Approx(expected).margin(1e-5f));
}
