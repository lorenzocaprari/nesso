// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_DISTANCE_KERNELS_HPP
#define CORE_DISTANCE_KERNELS_HPP

#include <cstdint>
#include <span>
#include <string_view>

namespace nesso::core::math::detail
{

enum class DistanceKernel : uint8_t
{
    Scalar,
    Avx2Fma
};

[[nodiscard]] auto SelectDistanceKernel(std::string_view forced, bool avx2_fma) noexcept -> DistanceKernel;

[[nodiscard]] auto SelectedKernel() noexcept -> DistanceKernel;

[[nodiscard]] auto DotProductKernel(DistanceKernel kernel, std::span<const float> a, std::span<const float> b) noexcept
    -> float;

[[nodiscard]] auto L2SquaredDistanceKernel(DistanceKernel kernel, std::span<const float> a,
                                           std::span<const float> b) noexcept -> float;

[[nodiscard]] auto DotProductAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept -> float;

[[nodiscard]] auto L2SquaredDistanceAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept -> float;

} // namespace nesso::core::math::detail

#endif // CORE_DISTANCE_KERNELS_HPP
