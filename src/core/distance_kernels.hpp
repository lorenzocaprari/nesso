// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef CORE_DISTANCE_KERNELS_HPP
#define CORE_DISTANCE_KERNELS_HPP

#include <cstdint>
#include <span>

namespace core::math::detail
{

enum class DistanceKernel : uint8_t
{
    Scalar,
    Avx2Fma
};

[[nodiscard]] DistanceKernel selectedKernel() noexcept;

[[nodiscard]] float dotProductKernel(DistanceKernel kernel, std::span<const float> a,
                                     std::span<const float> b) noexcept;

[[nodiscard]] float l2SquaredDistanceKernel(DistanceKernel kernel, std::span<const float> a,
                                            std::span<const float> b) noexcept;

[[nodiscard]] float dotProductAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept;

[[nodiscard]] float l2SquaredDistanceAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept;

} // namespace core::math::detail

#endif // CORE_DISTANCE_KERNELS_HPP
