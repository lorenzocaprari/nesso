// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_EMBED_TYPES_HPP
#define EMBED_EMBED_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

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
inline constexpr std::string_view MINILM_MODEL_ID = "sentence-transformers/all-MiniLM-L6-v2";
/// Prompt-prefix rule for MiniLM: no prefix on a query or a passage.
inline constexpr std::string_view MINILM_EMBED_PREFIX = "none";

/// Last-hidden MiniLM tensor: [batch, sequence, MINILM_EMBEDDING_DIMENSIONS].
[[nodiscard]] inline auto MatchesMiniLmHiddenStatesShape(std::span<const int64_t> shape, size_t batch_size,
                                                         size_t sequence_length) noexcept -> bool
{
    return shape.size() == 3 && std::cmp_equal(shape[0], batch_size) && std::cmp_equal(shape[1], sequence_length) &&
           std::cmp_equal(shape[2], MINILM_EMBEDDING_DIMENSIONS);
}

} // namespace nesso::embed

#endif // EMBED_EMBED_TYPES_HPP
