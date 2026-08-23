// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef CORE_CORE_TYPES_HPP
#define CORE_CORE_TYPES_HPP

#include <array>
#include <concepts>
#include <cstdint>
#include <span>

namespace core
{

// Constrain types to valid vector floating points
template <typename T>
concept SupportedScalar = std::same_as<T, float> || std::same_as<T, double>;

enum class EngineError : uint8_t
{
    FileOpenFailure,
    FileResizeFailure,
    MmapMappingFailure,
    MismatchedDimensions,
    IndexOutOfBounds,
    DatabaseNotInitialized,
    CorruptDatabase
};

// Packed structure containing file metadata on disk
#pragma pack(push, 1)
struct DatabaseHeader
{
    std::array<uint8_t, 4> magic = {'N', 'E', 'S', 'S'}; // Magic bytes identifying the format
    uint32_t version = 1;                                // Schema version control
    uint64_t dimensions = 0;                             // Vector dimensionality
    uint64_t vector_count = 0;                           // Total vectors written to disk
};
#pragma pack(pop)

} // namespace core

#endif // CORE_CORE_TYPES_HPP
