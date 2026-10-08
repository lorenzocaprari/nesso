// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/embed/wordpiece_tokenizer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <span>

namespace embed
{

struct CodeRange
{
    char32_t first;
    char32_t last;
};

struct LowerRange
{
    char32_t first;
    char32_t last;
    std::int32_t delta;
};

struct Decomp
{
    char32_t code;
    std::uint8_t count;
    char32_t part0;
    char32_t part1;
    char32_t part2;
    char32_t part3;
};

#include "unicode_bert.inc"

static constexpr const char *CLS_TOKEN = "[CLS]";
static constexpr const char *SEP_TOKEN = "[SEP]";
static constexpr char32_t REPLACEMENT = 0xFFFD;
static constexpr std::size_t MAX_WORD_CHARS = 100;
static constexpr char32_t HANGUL_S_BASE = 0xAC00;
static constexpr char32_t HANGUL_L_BASE = 0x1100;
static constexpr char32_t HANGUL_V_BASE = 0x1161;
static constexpr char32_t HANGUL_T_BASE = 0x11A7;
static constexpr char32_t HANGUL_V_COUNT = 21;
static constexpr char32_t HANGUL_T_COUNT = 28;
static constexpr char32_t HANGUL_N_COUNT = HANGUL_V_COUNT * HANGUL_T_COUNT;
static constexpr char32_t HANGUL_L_COUNT = 19;
static constexpr char32_t HANGUL_S_COUNT = HANGUL_L_COUNT * HANGUL_N_COUNT;

static std::string unkTokenSymbol() { return std::string("[") + "UNK" + "]"; }

static bool containsCode(std::span<const CodeRange> ranges, char32_t code)
{
    std::size_t low = 0;
    std::size_t high = ranges.size();
    while (low < high)
    {
        const std::size_t mid = low + ((high - low) / 2);
        if (code < ranges[mid].first)
        {
            high = mid;
        }
        else if (code > ranges[mid].last)
        {
            low = mid + 1;
        }
        else
        {
            return true;
        }
    }
    return false;
}

static bool isWhitespace(char32_t code)
{
    return code == '\t' || code == '\n' || code == '\r' || containsCode(SPACE_RANGES, code);
}

static bool isControl(char32_t code) { return containsCode(CONTROL_RANGES, code); }

static bool isMark(char32_t code) { return containsCode(MARK_RANGES, code); }

static bool isAsciiPunctuation(char32_t code)
{
    return (code >= 33 && code <= 47) || (code >= 58 && code <= 64) || (code >= 91 && code <= 96) ||
           (code >= 123 && code <= 126);
}

static bool isPunctuation(char32_t code) { return isAsciiPunctuation(code) || containsCode(PUNCT_RANGES, code); }

// BERT BasicTokenizer's CJK set, which is narrower than every Han character.
static bool isChinese(char32_t code)
{
    return (code >= 0x4E00 && code <= 0x9FFF) || (code >= 0x3400 && code <= 0x4DBF) ||
           (code >= 0x20000 && code <= 0x2A6DF) || (code >= 0x2A700 && code <= 0x2B73F) ||
           (code >= 0x2B740 && code <= 0x2B81F) || (code >= 0x2B820 && code <= 0x2CEAF) ||
           (code >= 0xF900 && code <= 0xFAFF) || (code >= 0x2F800 && code <= 0x2FA1F);
}

static char32_t lowerCode(char32_t code)
{
    const std::span<const LowerRange> ranges{LOWER_RANGES};
    std::size_t low = 0;
    std::size_t high = ranges.size();
    while (low < high)
    {
        const std::size_t mid = low + ((high - low) / 2);
        const LowerRange &range = ranges[mid];
        if (code < range.first)
        {
            high = mid;
        }
        else if (code > range.last)
        {
            low = mid + 1;
        }
        else
        {
            return static_cast<char32_t>(static_cast<std::int32_t>(code) + range.delta);
        }
    }
    return code;
}

static void appendLower(char32_t code, std::vector<char32_t> &out)
{
    // Python str.lower() expands U+0130 to i + combining dot; accent stripping then drops the dot.
    if (code == 0x130)
    {
        out.push_back(0x69);
        out.push_back(0x307);
        return;
    }
    out.push_back(lowerCode(code));
}

static const Decomp *findDecomp(char32_t code)
{
    const std::span<const Decomp> table{DECOMP_TABLE};
    std::size_t low = 0;
    std::size_t high = table.size();
    while (low < high)
    {
        const std::size_t mid = low + ((high - low) / 2);
        if (code < table[mid].code)
        {
            high = mid;
        }
        else if (code > table[mid].code)
        {
            low = mid + 1;
        }
        else
        {
            return &table[mid];
        }
    }
    return nullptr;
}

static void decompose(char32_t code, std::vector<char32_t> &out)
{
    if (code >= HANGUL_S_BASE && code < (HANGUL_S_BASE + HANGUL_S_COUNT))
    {
        const char32_t index = code - HANGUL_S_BASE;
        out.push_back(HANGUL_L_BASE + (index / HANGUL_N_COUNT));
        out.push_back(HANGUL_V_BASE + ((index % HANGUL_N_COUNT) / HANGUL_T_COUNT));
        const char32_t trailIndex = index % HANGUL_T_COUNT;
        if (trailIndex != 0)
        {
            out.push_back(HANGUL_T_BASE + trailIndex);
        }
        return;
    }

    const Decomp *found = findDecomp(code);
    if (found == nullptr)
    {
        out.push_back(code);
        return;
    }
    out.push_back(found->part0);
    if (found->count > 1)
    {
        out.push_back(found->part1);
    }
    if (found->count > 2)
    {
        out.push_back(found->part2);
    }
    if (found->count > 3)
    {
        out.push_back(found->part3);
    }
}

static std::size_t utf8Length(std::uint32_t lead)
{
    if (lead < 0x80U)
    {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U)
    {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U)
    {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U)
    {
        return 4;
    }
    return 0;
}

static bool isContinuation(unsigned char byte) { return (static_cast<std::uint32_t>(byte) & 0xC0U) == 0x80U; }

static void appendUtf8(std::string &out, char32_t code)
{
    const auto value = static_cast<std::uint32_t>(code);
    if (value <= 0x7FU)
    {
        out.push_back(static_cast<char>(value));
        return;
    }
    if (value <= 0x7FFU)
    {
        out.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        return;
    }
    if (value <= 0xFFFFU)
    {
        out.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        return;
    }
    out.push_back(static_cast<char>(0xF0U | (value >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
}

static std::string utf8Of(std::span<const char32_t> codes)
{
    std::string out;
    for (const char32_t code : codes)
    {
        appendUtf8(out, code);
    }
    return out;
}

static std::string utf8OfOne(char32_t code)
{
    std::string out;
    appendUtf8(out, code);
    return out;
}

static std::vector<char32_t> decodeUtf8(std::string_view text)
{
    std::vector<char32_t> codes;
    codes.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size())
    {
        const auto lead = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index]));
        const std::size_t length = utf8Length(lead);
        if (length == 0 || index + length > text.size())
        {
            codes.push_back(REPLACEMENT);
            ++index;
            continue;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset)
        {
            if (!isContinuation(static_cast<unsigned char>(text[index + offset])))
            {
                valid = false;
                break;
            }
        }
        if (!valid)
        {
            codes.push_back(REPLACEMENT);
            ++index;
            continue;
        }

        char32_t code = lead;
        if (length == 2)
        {
            const auto byte1 = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 1]));
            code = static_cast<char32_t>(((lead & 0x1FU) << 6U) | (byte1 & 0x3FU));
        }
        else if (length == 3)
        {
            const auto byte1 = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 1]));
            const auto byte2 = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 2]));
            code = static_cast<char32_t>(((lead & 0x0FU) << 12U) | ((byte1 & 0x3FU) << 6U) | (byte2 & 0x3FU));
        }
        else if (length == 4)
        {
            const auto byte1 = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 1]));
            const auto byte2 = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 2]));
            const auto byte3 = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 3]));
            code = static_cast<char32_t>(((lead & 0x07U) << 18U) | ((byte1 & 0x3FU) << 12U) | ((byte2 & 0x3FU) << 6U) |
                                         (byte3 & 0x3FU));
        }

        char32_t minimum = 0;
        if (length == 2)
        {
            minimum = 0x80;
        }
        else if (length == 3)
        {
            minimum = 0x800;
        }
        else if (length == 4)
        {
            minimum = 0x10000;
        }
        const bool surrogate = code >= 0xD800 && code <= 0xDFFF;
        if (code < minimum || code > 0x10FFFF || surrogate)
        {
            codes.push_back(REPLACEMENT);
            ++index;
            continue;
        }
        codes.push_back(code);
        index += length;
    }
    return codes;
}

static std::vector<std::string> basicTokenize(std::string_view text)
{
    std::vector<char32_t> cleaned;
    for (const char32_t code : decodeUtf8(text))
    {
        if (code == 0 || code == REPLACEMENT || isControl(code))
        {
            continue;
        }
        cleaned.push_back(isWhitespace(code) ? static_cast<char32_t>(' ') : code);
    }

    std::vector<char32_t> spaced;
    for (const char32_t code : cleaned)
    {
        if (isChinese(code))
        {
            spaced.push_back(' ');
            spaced.push_back(code);
            spaced.push_back(' ');
        }
        else
        {
            spaced.push_back(code);
        }
    }

    std::vector<std::string> tokens;
    std::size_t index = 0;
    while (index < spaced.size())
    {
        while (index < spaced.size() && isWhitespace(spaced[index]))
        {
            ++index;
        }
        if (index >= spaced.size())
        {
            break;
        }
        std::size_t end = index;
        while (end < spaced.size() && !isWhitespace(spaced[end]))
        {
            ++end;
        }

        std::vector<char32_t> lowered;
        for (std::size_t cursor = index; cursor < end; ++cursor)
        {
            appendLower(spaced[cursor], lowered);
        }
        std::vector<char32_t> decomposed;
        for (const char32_t code : lowered)
        {
            decompose(code, decomposed);
        }
        std::vector<char32_t> plain;
        for (const char32_t code : decomposed)
        {
            if (!isMark(code))
            {
                plain.push_back(code);
            }
        }

        std::vector<char32_t> current;
        const auto flush = [&tokens, &current]()
        {
            if (!current.empty())
            {
                tokens.push_back(utf8Of(current));
                current.clear();
            }
        };
        for (const char32_t code : plain)
        {
            if (isPunctuation(code))
            {
                flush();
                tokens.push_back(utf8OfOne(code));
            }
            else
            {
                current.push_back(code);
            }
        }
        flush();
        index = end;
    }
    return tokens;
}

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

std::vector<std::string> WordPieceTokenizer::wordPieceTokenize(std::string_view token) const
{
    const std::vector<char32_t> chars = decodeUtf8(token);
    const std::string unkToken = unkTokenSymbol();
    if (chars.empty())
    {
        return {};
    }
    if (chars.size() > MAX_WORD_CHARS)
    {
        return {unkToken};
    }

    std::vector<std::string> pieces;
    std::size_t start = 0;
    while (start < chars.size())
    {
        std::size_t end = chars.size();
        std::string match;
        while (end > start)
        {
            std::string candidate = utf8Of(std::span<const char32_t>(chars).subspan(start, end - start));
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
            return {unkToken};
        }

        pieces.push_back(std::move(match));
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

} // namespace embed
