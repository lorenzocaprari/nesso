// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0
#ifndef CORE_CORE_TYPES_HPP
#define CORE_CORE_TYPES_HPP

#include <concepts>
#include <cstdint>

namespace nesso::core
{

// Constrain types to valid vector floating points
template <typename T>
concept SupportedScalar = std::same_as<T, float> || std::same_as<T, double>;

enum class EngineError : uint8_t
{
    FileOpenFailure,
    MismatchedDimensions,
    DatabaseNotInitialized,
    CorruptDatabase,
    UnsupportedVersion,
    /// A count is past this build's caps (`MAX_SOURCES`, `MAX_LINES`,
    /// `MAX_DISTINCT`, `MAX_LEXICAL_TERMS`, `MAX_LEXICAL_BYTES`).
    LimitsExceeded
};

} // namespace nesso::core

#endif // CORE_CORE_TYPES_HPP
