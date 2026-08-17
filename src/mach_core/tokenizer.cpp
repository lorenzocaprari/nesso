// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/tokenizer.hpp"

#include <cctype>
#include <fstream>

namespace mach_core
{
namespace
{

[[nodiscard]] char asciiLower(char c) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

[[nodiscard]] bool isAsciiAlnum(char c) noexcept
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

} // namespace

std::expected<Tokenizer, EngineError> Tokenizer::load(const std::filesystem::path &vocabPath, int64_t maxSeqLen)
{
    if (maxSeqLen < 2)
    {
        return std::unexpected(EngineError::TokenizerFailure);
    }

    std::ifstream in(vocabPath);
    if (!in)
    {
        return std::unexpected(EngineError::TokenizerFailure);
    }

    Tokenizer tok;
    tok.m_maxSeqLen = maxSeqLen;
    std::string line;
    int64_t id = 0;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        tok.m_tokenToId.emplace(line, id);
        ++id;
    }
    if (tok.m_tokenToId.empty())
    {
        return std::unexpected(EngineError::TokenizerFailure);
    }

    const auto lookup = [&](const char *token) -> std::expected<int64_t, EngineError>
    {
        const auto it = tok.m_tokenToId.find(token);
        if (it == tok.m_tokenToId.end())
        {
            return std::unexpected(EngineError::TokenizerFailure);
        }
        return it->second;
    };

    auto pad = lookup("[PAD]");
    auto unk = lookup("[UNK]");
    auto cls = lookup("[CLS]");
    auto sep = lookup("[SEP]");
    if (!pad || !unk || !cls || !sep)
    {
        return std::unexpected(EngineError::TokenizerFailure);
    }
    tok.m_padId = *pad;
    tok.m_unkId = *unk;
    tok.m_clsId = *cls;
    tok.m_sepId = *sep;
    return tok;
}

std::vector<std::string> Tokenizer::basicTokenize(std::string_view text) const
{
    std::vector<std::string> words;
    std::string current;
    const auto flush = [&]()
    {
        if (!current.empty())
        {
            words.push_back(current);
            current.clear();
        }
    };

    for (const char raw : text)
    {
        const char c = asciiLower(raw);
        if (std::isspace(static_cast<unsigned char>(c)) != 0)
        {
            flush();
            continue;
        }
        if (!isAsciiAlnum(c))
        {
            flush();
            words.emplace_back(1, c);
            continue;
        }
        current.push_back(c);
    }
    flush();
    return words;
}

std::expected<std::vector<int64_t>, EngineError> Tokenizer::wordPiece(std::string_view word) const
{
    if (word.empty())
    {
        return std::vector<int64_t>{};
    }

    const auto whole = m_tokenToId.find(std::string(word));
    if (whole != m_tokenToId.end())
    {
        return std::vector<int64_t>{whole->second};
    }

    std::vector<int64_t> pieces;
    size_t start = 0;
    while (start < word.size())
    {
        size_t end = word.size();
        int64_t foundId = -1;
        while (end > start)
        {
            std::string piece;
            if (start > 0)
            {
                piece = "##";
            }
            piece.append(word.substr(start, end - start));
            const auto it = m_tokenToId.find(piece);
            if (it != m_tokenToId.end())
            {
                foundId = it->second;
                break;
            }
            --end;
        }
        if (foundId < 0)
        {
            return std::vector<int64_t>{m_unkId};
        }
        pieces.push_back(foundId);
        start = end;
    }
    return pieces;
}

std::expected<std::vector<int64_t>, EngineError> Tokenizer::encodeIds(std::string_view text) const
{
    std::vector<int64_t> ids;
    ids.reserve(static_cast<size_t>(m_maxSeqLen));
    ids.push_back(m_clsId);

    const auto words = basicTokenize(text);
    for (const auto &word : words)
    {
        auto pieces = wordPiece(word);
        if (!pieces)
        {
            return std::unexpected(pieces.error());
        }
        for (const int64_t piece : *pieces)
        {
            if (static_cast<int64_t>(ids.size()) >= m_maxSeqLen - 1)
            {
                break;
            }
            ids.push_back(piece);
        }
        if (static_cast<int64_t>(ids.size()) >= m_maxSeqLen - 1)
        {
            break;
        }
    }

    ids.push_back(m_sepId);
    while (static_cast<int64_t>(ids.size()) < m_maxSeqLen)
    {
        ids.push_back(m_padId);
    }
    return ids;
}

std::vector<int64_t> Tokenizer::attentionMask(const std::vector<int64_t> &ids) const
{
    std::vector<int64_t> mask(ids.size(), 0);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        mask[i] = ids[i] == m_padId ? 0 : 1;
    }
    return mask;
}

} // namespace mach_core
