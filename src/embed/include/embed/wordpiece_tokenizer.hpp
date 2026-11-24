// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_WORDPIECE_TOKENIZER_HPP
#define EMBED_WORDPIECE_TOKENIZER_HPP

#include <embed/embed_types.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nesso::embed
{

struct TokenizedInput
{
    std::vector<int64_t> input_ids;
    std::vector<int64_t> attention_mask;
    std::vector<int64_t> token_type_ids;
};

class WordPieceTokenizer
{
  public:
    [[nodiscard]] static auto FromVocabFile(const std::filesystem::path &vocab_path,
                                            size_t max_sequence_length = DEFAULT_MAX_SEQUENCE_LENGTH)
        -> std::expected<WordPieceTokenizer, EmbedError>;

    [[nodiscard]] auto Encode(std::string_view text) const -> std::expected<TokenizedInput, EmbedError>;

  private:
    struct SpecialTokenIds
    {
        int64_t cls_id = 0;
        int64_t sep_id = 0;
        int64_t unk_id = 0;
    };

    WordPieceTokenizer(std::unordered_map<std::string, int64_t> vocab, const SpecialTokenIds &ids,
                       size_t max_sequence_length);

    [[nodiscard]] auto WordPieceTokenize(std::string_view token) const -> std::vector<std::string>;

    std::unordered_map<std::string, int64_t> vocab_;
    int64_t cls_id_ = 0;
    int64_t sep_id_ = 0;
    int64_t unk_id_ = 0;
    size_t max_sequence_length_ = DEFAULT_MAX_SEQUENCE_LENGTH;
};

} // namespace nesso::embed

#endif // EMBED_WORDPIECE_TOKENIZER_HPP
