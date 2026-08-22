// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef MACH_CORE_DISTANCE_HPP
#define MACH_CORE_DISTANCE_HPP

#include "core_types.hpp"
#include <expected>
#include <span>

namespace mach_core::math
{

class CosineSimilarity
{
  public:
    template <SupportedScalar T>
    [[nodiscard]] static std::expected<T, EngineError> calculate(std::span<const T> a, std::span<const T> b) noexcept;
};

class DistanceMetrics
{
  public:
    template <SupportedScalar T>
    [[nodiscard]] static std::expected<T, EngineError> dotProduct(std::span<const T> a, std::span<const T> b) noexcept;

    template <SupportedScalar T>
    [[nodiscard]] static std::expected<T, EngineError> l2SquaredDistance(std::span<const T> a,
                                                                         std::span<const T> b) noexcept;
};

namespace detail
{
template <SupportedScalar T> [[nodiscard]] T dotProductScalar(std::span<const T> a, std::span<const T> b) noexcept;
template <SupportedScalar T>
[[nodiscard]] T l2SquaredDistanceScalar(std::span<const T> a, std::span<const T> b) noexcept;
#ifdef __AVX2__
[[nodiscard]] float dotProductAvx2(std::span<const float> a, std::span<const float> b) noexcept;
[[nodiscard]] float l2SquaredDistanceAvx2(std::span<const float> a, std::span<const float> b) noexcept;
#endif
} // namespace detail

} // namespace mach_core::math

#endif // MACH_CORE_DISTANCE_HPP
