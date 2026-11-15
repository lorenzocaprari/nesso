// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <cmath>
#include <core/distance.hpp>
#include <random>
#include <span>
#include <vector>

using nesso::core::math::CosineSimilarity;

namespace
{

[[nodiscard]] float NaiveCosine(std::span<const float> left, std::span<const float> right)
{
    REQUIRE(left.size() == right.size());
    REQUIRE_FALSE(left.empty());

    double dot = 0.0;
    double norm_left = 0.0;
    double norm_right = 0.0;
    for (size_t index = 0; index < left.size(); ++index)
    {
        const double a = static_cast<double>(left[index]);
        const double b = static_cast<double>(right[index]);
        dot += a * b;
        norm_left += a * a;
        norm_right += b * b;
    }
    if (norm_left == 0.0 || norm_right == 0.0)
    {
        return 0.0f;
    }
    return static_cast<float>(dot / (std::sqrt(norm_left) * std::sqrt(norm_right)));
}

} // namespace

TEST_CASE("CosineSimilarity matches double-precision oracle on random inputs", "[CosineSimilarity][oracle][Unit]")
{
    auto seed = GENERATE(take(32, random(1, 1000000)));
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
    std::uniform_int_distribution<size_t> dim_dist(1, 24);
    std::uniform_real_distribution<float> value_dist(-2.0f, 2.0f);
    std::bernoulli_distribution zero_row(0.1);

    const size_t dimensions = dim_dist(rng);
    std::vector<float> left(dimensions);
    std::vector<float> right(dimensions);
    for (float &component : left)
    {
        component = zero_row(rng) ? 0.0f : value_dist(rng);
    }
    for (float &component : right)
    {
        component = zero_row(rng) ? 0.0f : value_dist(rng);
    }

    const auto expected = NaiveCosine(left, right);
    const auto actual = CosineSimilarity::Calculate<float>(left, right);
    REQUIRE(actual.has_value());
    REQUIRE(actual.value() == Catch::Approx(expected).margin(1e-5f));
}
