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
    [[nodiscard]] static auto Calculate(std::span<const T> a, std::span<const T> b) noexcept
        -> std::expected<T, EngineError>;
};

class DistanceMetrics
{
  public:
    template <SupportedScalar T>
    [[nodiscard]] static auto DotProduct(std::span<const T> a, std::span<const T> b) noexcept
        -> std::expected<T, EngineError>;

    template <SupportedScalar T>
    [[nodiscard]] static auto L2SquaredDistance(std::span<const T> a, std::span<const T> b) noexcept
        -> std::expected<T, EngineError>;
};

namespace detail
{
template <SupportedScalar T>
[[nodiscard]] auto DotProductScalar(std::span<const T> a, std::span<const T> b) noexcept -> T;
template <SupportedScalar T>
[[nodiscard]] auto L2SquaredDistanceScalar(std::span<const T> a, std::span<const T> b) noexcept -> T;
} // namespace detail

} // namespace nesso::core::math

#endif // CORE_DISTANCE_HPP
