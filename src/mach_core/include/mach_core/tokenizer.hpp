// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_TOKENIZER_HPP
#define MACH_CORE_TOKENIZER_HPP

#include "core_types.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mach_core
{

class Tokenizer
{
  public:
    static constexpr int64_t kDefaultMaxSeqLen = 128;

    [[nodiscard]] static std::expected<Tokenizer, EngineError> load(const std::filesystem::path &vocabPath,
                                                                    int64_t maxSeqLen = kDefaultMaxSeqLen);

    [[nodiscard]] std::expected<std::vector<int64_t>, EngineError> encodeIds(std::string_view text) const;
    [[nodiscard]] std::vector<int64_t> attentionMask(const std::vector<int64_t> &ids) const;
    [[nodiscard]] int64_t maxSeqLen() const noexcept { return m_maxSeqLen; }
    [[nodiscard]] int64_t padId() const noexcept { return m_padId; }
    [[nodiscard]] int64_t unkId() const noexcept { return m_unkId; }
    [[nodiscard]] int64_t clsId() const noexcept { return m_clsId; }
    [[nodiscard]] int64_t sepId() const noexcept { return m_sepId; }

  private:
    [[nodiscard]] std::vector<std::string> basicTokenize(std::string_view text) const;
    [[nodiscard]] std::expected<std::vector<int64_t>, EngineError> wordPiece(std::string_view word) const;

    std::unordered_map<std::string, int64_t> m_tokenToId;
    int64_t m_maxSeqLen{kDefaultMaxSeqLen};
    int64_t m_padId{0};
    int64_t m_unkId{0};
    int64_t m_clsId{0};
    int64_t m_sepId{0};
};

} // namespace mach_core

#endif // MACH_CORE_TOKENIZER_HPP
