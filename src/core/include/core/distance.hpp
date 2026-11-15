// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_DISTANCE_HPP
#define CORE_DISTANCE_HPP

#include <core/core_types.hpp>
#include <expected>
#include <span>

namespace nesso::core::math
{

class CosineSimilarity
{
  public:
    template <SupportedScalar T>
    [[nodiscard]] static std::expected<T, EngineError> Calculate(std::span<const T> a, std::span<const T> b) noexcept;
};

class DistanceMetrics
{
  public:
    template <SupportedScalar T>
    [[nodiscard]] static std::expected<T, EngineError> DotProduct(std::span<const T> a, std::span<const T> b) noexcept;

    template <SupportedScalar T>
    [[nodiscard]] static std::expected<T, EngineError> L2SquaredDistance(std::span<const T> a,
                                                                         std::span<const T> b) noexcept;
};

namespace detail
{
template <SupportedScalar T> [[nodiscard]] T DotProductScalar(std::span<const T> a, std::span<const T> b) noexcept;
template <SupportedScalar T>
[[nodiscard]] T L2SquaredDistanceScalar(std::span<const T> a, std::span<const T> b) noexcept;
} // namespace detail

} // namespace nesso::core::math

#endif // CORE_DISTANCE_HPP
