// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef EMBED_EMBED_TYPES_HPP
#define EMBED_EMBED_TYPES_HPP

#include <cstddef>
#include <cstdint>

namespace embed
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

} // namespace embed

#endif // EMBED_EMBED_TYPES_HPP
