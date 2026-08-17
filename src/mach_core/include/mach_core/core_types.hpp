// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_CORE_TYPES_HPP
#define MACH_CORE_CORE_TYPES_HPP

#include <array>
#include <concepts>
#include <cstdint>
#include <span>

namespace mach_core
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
    CorruptDatabase,
    SidecarIoFailure,
    TokenizerFailure,
    ModelLoadFailure,
    InferenceFailure
};

inline constexpr uint64_t kEmbeddingDims = 384;

// Packed structure containing file metadata on disk
#pragma pack(push, 1)
struct DatabaseHeader
{
    std::array<uint8_t, 4> magic = {'M', 'A', 'C', 'H'}; // Magic bytes identifying the format
    uint32_t version = 1;                                // Schema version control
    uint64_t dimensions = 0;                             // Vector dimensionality
    uint64_t vector_count = 0;                           // Total vectors written to disk
};
#pragma pack(pop)

} // namespace mach_core

#endif // MACH_CORE_CORE_TYPES_HPP
