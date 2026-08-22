// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef MACH_EMBED_EMBED_TYPES_HPP
#define MACH_EMBED_EMBED_TYPES_HPP

#include <cstddef>
#include <cstdint>

namespace mach_embed
{

enum class EmbedError : uint8_t
{
    VocabLoadFailure,
    TokenizationFailure,
    ModelLoadFailure,
    InferenceFailure,
    InvalidInput
};

inline constexpr size_t DEFAULT_MAX_SEQUENCE_LENGTH = 256;
inline constexpr size_t MINILM_EMBEDDING_DIMENSIONS = 384;

} // namespace mach_embed

#endif // MACH_EMBED_EMBED_TYPES_HPP
