// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef MACH_EMBED_WORDPIECE_TOKENIZER_HPP
#define MACH_EMBED_WORDPIECE_TOKENIZER_HPP

#include "embed_types.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mach_embed
{

struct TokenizedInput
{
    std::vector<int64_t> inputIds;
    std::vector<int64_t> attentionMask;
    std::vector<int64_t> tokenTypeIds;
};

class WordPieceTokenizer
{
  public:
    [[nodiscard]] static std::expected<WordPieceTokenizer, EmbedError>
    fromVocabFile(const std::filesystem::path &vocabPath, size_t maxSequenceLength = DEFAULT_MAX_SEQUENCE_LENGTH);

    [[nodiscard]] std::expected<TokenizedInput, EmbedError> encode(std::string_view text) const;

    [[nodiscard]] size_t maxSequenceLength() const noexcept { return maxSequenceLength_; }

  private:
    WordPieceTokenizer(std::unordered_map<std::string, int64_t> vocab, int64_t clsId, int64_t sepId, int64_t unkId,
                       size_t maxSequenceLength);

    [[nodiscard]] std::vector<std::string> basicTokenize(std::string_view text) const;
    [[nodiscard]] std::vector<std::string> wordPieceTokenize(std::string_view token) const;

    std::unordered_map<std::string, int64_t> vocab_;
    int64_t clsId_ = 0;
    int64_t sepId_ = 0;
    int64_t unkId_ = 0;
    size_t maxSequenceLength_ = DEFAULT_MAX_SEQUENCE_LENGTH;
};

} // namespace mach_embed

#endif // MACH_EMBED_WORDPIECE_TOKENIZER_HPP
