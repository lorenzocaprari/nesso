// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/mach_embed/wordpiece_tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace mach_embed
{

static constexpr const char *CLS_TOKEN = "[CLS]";
static constexpr const char *SEP_TOKEN = "[SEP]";

static std::string unkTokenSymbol() { return std::string("[") + "UNK" + "]"; }

static std::string toLower(std::string_view text)
{
    std::string lowered;
    lowered.reserve(text.size());
    for (const char rawCh : text)
    {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(rawCh))));
    }
    return lowered;
}

static bool isPunctuation(char ch) { return std::ispunct(static_cast<unsigned char>(ch)) != 0; }

WordPieceTokenizer::WordPieceTokenizer(std::unordered_map<std::string, int64_t> vocab, int64_t clsId, int64_t sepId,
                                       int64_t unkId, size_t maxSequenceLength)
    : vocab_(std::move(vocab)), clsId_(clsId), sepId_(sepId), unkId_(unkId), maxSequenceLength_(maxSequenceLength)
{
}

std::expected<WordPieceTokenizer, EmbedError> WordPieceTokenizer::fromVocabFile(const std::filesystem::path &vocabPath,
                                                                                size_t maxSequenceLength)
{
    std::ifstream input(vocabPath);
    if (!input)
    {
        return std::unexpected(EmbedError::VocabLoadFailure);
    }

    std::unordered_map<std::string, int64_t> vocab;
    std::string token;
    int64_t nextId = 0;
    while (std::getline(input, token))
    {
        vocab.emplace(token, nextId++);
    }

    if (vocab.empty())
    {
        return std::unexpected(EmbedError::VocabLoadFailure);
    }

    const auto lookup = [&vocab](const std::string &symbol) -> int64_t
    {
        const auto it = vocab.find(symbol);
        return it == vocab.end() ? 0 : it->second;
    };

    const int64_t clsId = lookup(CLS_TOKEN);
    const int64_t sepId = lookup(SEP_TOKEN);
    const int64_t unkId = lookup(unkTokenSymbol());
    return WordPieceTokenizer(std::move(vocab), clsId, sepId, unkId, maxSequenceLength);
}

std::vector<std::string> WordPieceTokenizer::basicTokenize(std::string_view text) const
{
    const std::string lowered = toLower(text);
    std::vector<std::string> tokens;
    std::string current;

    const auto flush = [&tokens, &current]()
    {
        if (!current.empty())
        {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (const char ch : lowered)
    {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0)
        {
            flush();
            continue;
        }
        if (isPunctuation(ch))
        {
            flush();
            tokens.emplace_back(1, ch);
            continue;
        }
        current.push_back(ch);
    }
    flush();
    return tokens;
}

std::vector<std::string> WordPieceTokenizer::wordPieceTokenize(std::string_view token) const
{
    if (token.empty())
    {
        return {};
    }

    const std::string unkToken = unkTokenSymbol();
    std::vector<std::string> pieces;
    size_t start = 0;
    while (start < token.size())
    {
        size_t end = token.size();
        std::string match;
        while (end > start)
        {
            std::string candidate(token.substr(start, end - start));
            if (start > 0)
            {
                candidate.insert(0, "##");
            }
            if (vocab_.contains(candidate))
            {
                match = std::move(candidate);
                break;
            }
            --end;
        }

        if (match.empty())
        {
            pieces.push_back(unkToken);
            break;
        }

        pieces.push_back(match);
        start = end;
    }
    return pieces;
}

std::expected<TokenizedInput, EmbedError> WordPieceTokenizer::encode(std::string_view text) const
{
    if (text.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }

    TokenizedInput encoded;
    encoded.inputIds.push_back(clsId_);
    encoded.attentionMask.push_back(1);
    encoded.tokenTypeIds.push_back(0);

    for (const std::string &basicToken : basicTokenize(text))
    {
        for (const std::string &piece : wordPieceTokenize(basicToken))
        {
            if (encoded.inputIds.size() >= maxSequenceLength_)
            {
                break;
            }

            const auto it = vocab_.find(piece);
            encoded.inputIds.push_back(it == vocab_.end() ? unkId_ : it->second);
            encoded.attentionMask.push_back(1);
            encoded.tokenTypeIds.push_back(0);
        }
        if (encoded.inputIds.size() >= maxSequenceLength_)
        {
            break;
        }
    }

    if (encoded.inputIds.size() >= maxSequenceLength_)
    {
        encoded.inputIds.back() = sepId_;
    }
    else
    {
        encoded.inputIds.push_back(sepId_);
        encoded.attentionMask.push_back(1);
        encoded.tokenTypeIds.push_back(0);
    }

    return encoded;
}

} // namespace mach_embed
