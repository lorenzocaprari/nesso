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

[[nodiscard]] DistanceKernel SelectDistanceKernel(std::string_view forced, bool avx2_fma) noexcept;

[[nodiscard]] DistanceKernel SelectedKernel() noexcept;

[[nodiscard]] float DotProductKernel(DistanceKernel kernel, std::span<const float> a,
                                     std::span<const float> b) noexcept;

[[nodiscard]] float L2SquaredDistanceKernel(DistanceKernel kernel, std::span<const float> a,
                                            std::span<const float> b) noexcept;

[[nodiscard]] float DotProductAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept;

[[nodiscard]] float L2SquaredDistanceAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept;

} // namespace nesso::core::math::detail

#endif // CORE_DISTANCE_KERNELS_HPP
