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

    template <SupportedScalar T>
    [[nodiscard]] static std::expected<T, EngineError> calculateScalar(std::span<const T> a,
                                                                       std::span<const T> b) noexcept;
};

} // namespace mach_core::math

#endif // MACH_CORE_DISTANCE_HPP
