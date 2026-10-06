// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef CORE_CORE_TYPES_HPP
#define CORE_CORE_TYPES_HPP

#include <concepts>
#include <cstdint>

namespace core
{

// Constrain types to valid vector floating points
template <typename T>
concept SupportedScalar = std::same_as<T, float> || std::same_as<T, double>;

enum class EngineError : uint8_t
{
    FileOpenFailure,
    MismatchedDimensions,
    DatabaseNotInitialized,
    CorruptDatabase
};

} // namespace core

#endif // CORE_CORE_TYPES_HPP
