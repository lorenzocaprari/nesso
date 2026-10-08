// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef EMBED_EMBED_TYPES_HPP
#define EMBED_EMBED_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

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
inline constexpr std::string_view MINILM_MODEL_ID = "sentence-transformers/all-MiniLM-L6-v2";

} // namespace embed

#endif // EMBED_EMBED_TYPES_HPP
