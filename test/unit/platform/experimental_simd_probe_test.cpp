// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>

#if defined(__has_include)
#if __has_include(<experimental/simd>)
#include <experimental/simd>
#if defined(__cpp_lib_experimental_parallel_simd) && (__cpp_lib_experimental_parallel_simd >= 201803L)
#define NESSO_HAS_EXPERIMENTAL_SIMD 1
#else
#define NESSO_SIMD_PROBE_SKIP_REASON "feature macro __cpp_lib_experimental_parallel_simd absent"
#endif
#else
#define NESSO_SIMD_PROBE_SKIP_REASON "header <experimental/simd> not found"
#endif
#else
#define NESSO_SIMD_PROBE_SKIP_REASON "compiler has no __has_include"
#endif

namespace
{

#if defined(NESSO_HAS_EXPERIMENTAL_SIMD)

// 1..7 and 1..5 fit in the float mantissa, so reduction order cannot change the sums.
constexpr std::size_t PROBE_WIDTH = 384;
constexpr float EXPECTED_DOT = 4585.0F;
constexpr float EXPECTED_L2_SQUARED = 2691.0F;

using SimdFloat = std::experimental::native_simd<float>;
constexpr std::size_t LANES = SimdFloat::size();
static_assert(LANES > 0);
static_assert(PROBE_WIDTH % LANES == 0, "native_simd<float>::size() does not divide 384");

[[nodiscard]] bool SameBits(float left, float right)
{
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] float LeftValue(std::size_t index)
{
    constexpr std::array<float, 7> pattern{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F};
    return pattern[index % pattern.size()];
}

[[nodiscard]] float RightValue(std::size_t index)
{
    constexpr std::array<float, 5> pattern{1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
    return pattern[index % pattern.size()];
}

void FillProbe(std::array<float, PROBE_WIDTH> &left, std::array<float, PROBE_WIDTH> &right)
{
    for (std::size_t index = 0; index < PROBE_WIDTH; ++index)
    {
        left[index] = LeftValue(index);
        right[index] = RightValue(index);
    }
}

[[nodiscard]] float ScalarDot(const std::array<float, PROBE_WIDTH> &left, const std::array<float, PROBE_WIDTH> &right)
{
    float sum = 0.0F;
    for (std::size_t index = 0; index < PROBE_WIDTH; ++index)
    {
        sum += left[index] * right[index];
    }
    return sum;
}

[[nodiscard]] float ScalarL2Squared(const std::array<float, PROBE_WIDTH> &left,
                                    const std::array<float, PROBE_WIDTH> &right)
{
    float sum = 0.0F;
    for (std::size_t index = 0; index < PROBE_WIDTH; ++index)
    {
        const float delta = left[index] - right[index];
        sum += delta * delta;
    }
    return sum;
}

[[nodiscard]] float SimdDot(const std::array<float, PROBE_WIDTH> &left, const std::array<float, PROBE_WIDTH> &right)
{
    float sum = 0.0F;
    for (std::size_t index = 0; index < PROBE_WIDTH; index += LANES)
    {
        const SimdFloat left_chunk(left.data() + index, std::experimental::element_aligned);
        const SimdFloat right_chunk(right.data() + index, std::experimental::element_aligned);
        sum += std::experimental::reduce(left_chunk * right_chunk);
    }
    return sum;
}

[[nodiscard]] float SimdL2Squared(const std::array<float, PROBE_WIDTH> &left,
                                  const std::array<float, PROBE_WIDTH> &right)
{
    float sum = 0.0F;
    for (std::size_t index = 0; index < PROBE_WIDTH; index += LANES)
    {
        const SimdFloat left_chunk(left.data() + index, std::experimental::element_aligned);
        const SimdFloat right_chunk(right.data() + index, std::experimental::element_aligned);
        const SimdFloat delta = left_chunk - right_chunk;
        sum += std::experimental::reduce(delta * delta);
    }
    return sum;
}

void Report(bool dot_ok, bool l2_ok)
{
    std::cout << std::format("experimental/simd probe: built native_simd<float> lanes={} dot={} l2={}\n", LANES,
                             dot_ok ? "ok" : "MISMATCH", l2_ok ? "ok" : "MISMATCH")
              << std::flush;
}

[[nodiscard]] bool RunProbe()
{
    std::array<float, PROBE_WIDTH> left{};
    std::array<float, PROBE_WIDTH> right{};
    FillProbe(left, right);

    const float scalar_dot = ScalarDot(left, right);
    const float simd_dot = SimdDot(left, right);
    const float scalar_l2 = ScalarL2Squared(left, right);
    const float simd_l2 = SimdL2Squared(left, right);
    const bool dot_ok = SameBits(simd_dot, scalar_dot) && SameBits(scalar_dot, EXPECTED_DOT);
    const bool l2_ok = SameBits(simd_l2, scalar_l2) && SameBits(scalar_l2, EXPECTED_L2_SQUARED) &&
                       SameBits(std::sqrt(simd_l2), std::sqrt(scalar_l2));
    Report(dot_ok, l2_ok);
    return dot_ok && l2_ok;
}

#else

[[nodiscard]] bool RunProbe()
{
    std::cout << std::format("experimental/simd probe: SKIP unsupported: {}\n", NESSO_SIMD_PROBE_SKIP_REASON)
              << std::flush;
    return true;
}

#endif

} // namespace

#ifndef NESSO_SIMD_PROBE_STANDALONE
#include <catch2/catch_all.hpp>

TEST_CASE("experimental simd probe", "[experimental_simd][unit]")
{
    REQUIRE(RunProbe());
#if !defined(NESSO_HAS_EXPERIMENTAL_SIMD)
    SKIP(NESSO_SIMD_PROBE_SKIP_REASON);
#endif
}
#endif

#ifdef NESSO_SIMD_PROBE_STANDALONE
int main() { return RunProbe() ? 0 : 1; }
#endif
