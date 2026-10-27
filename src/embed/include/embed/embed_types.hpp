// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_EMBED_TYPES_HPP
#define EMBED_EMBED_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nesso::embed
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
// One ONNX Run of the whole corpus, padded to 256, asks for tens of gigabytes.
// A batch of 32 padded to its own longest sequence stays under a few hundred MB.
// int8 vectors depend on the other texts in that window, so callers must not
// change the window size.
inline constexpr size_t EMBED_BATCH_SIZE = 32;
inline constexpr std::string_view MINILM_MODEL_ID = "sentence-transformers/all-MiniLM-L6-v2";

} // namespace nesso::embed

#endif // EMBED_EMBED_TYPES_HPP
