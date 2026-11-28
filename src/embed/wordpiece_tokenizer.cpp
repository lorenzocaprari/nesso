// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "embed/embed_types.hpp"
#include <cstddef>
#include <embed/wordpiece_tokenizer.hpp>

// Used by unicode_bert.inc (included inside namespace nesso::embed; must not
// #include <array> from the .inc or MSVC STL breaks).
// NOLINTNEXTLINE(misc-include-cleaner)
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nesso::embed
{

namespace
{
struct CodeRange
{
    char32_t first;
    char32_t last;
};
} // namespace

namespace
{
struct LowerRange
{
    char32_t first;
    char32_t last;
    std::int32_t delta;
};
} // namespace

namespace
{
struct Decomp
{
    char32_t code;
    std::uint8_t count;
    char32_t part0;
    char32_t part1;
    char32_t part2;
    char32_t part3;
};
} // namespace

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

static auto UnkTokenSymbol() -> std::string { return std::string("[") + "UNK" + "]"; }

static auto ContainsCode(std::span<const CodeRange> ranges, char32_t code) -> bool
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

static auto IsWhitespace(char32_t code) -> bool
{
    return code == '\t' || code == '\n' || code == '\r' || ContainsCode(SPACE_RANGES, code);
}

static auto IsControl(char32_t code) -> bool { return ContainsCode(CONTROL_RANGES, code); }

static auto IsMark(char32_t code) -> bool { return ContainsCode(MARK_RANGES, code); }

static auto IsAsciiPunctuation(char32_t code) -> bool
{
    return (code >= 33 && code <= 47) || (code >= 58 && code <= 64) || (code >= 91 && code <= 96) ||
           (code >= 123 && code <= 126);
}

static auto IsPunctuation(char32_t code) -> bool
{
    return IsAsciiPunctuation(code) || ContainsCode(PUNCT_RANGES, code);
}

// BERT BasicTokenizer's CJK set, which is narrower than every Han character.
static auto IsChinese(char32_t code) -> bool
{
    return (code >= 0x4E00 && code <= 0x9FFF) || (code >= 0x3400 && code <= 0x4DBF) ||
           (code >= 0x20000 && code <= 0x2A6DF) || (code >= 0x2A700 && code <= 0x2B73F) ||
           (code >= 0x2B740 && code <= 0x2B81F) || (code >= 0x2B820 && code <= 0x2CEAF) ||
           (code >= 0xF900 && code <= 0xFAFF) || (code >= 0x2F800 && code <= 0x2FA1F);
}

static auto LowerCode(char32_t code) -> char32_t
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

static void AppendLower(char32_t code, std::vector<char32_t> &out)
{
    // Python str.lower() expands U+0130 to i + combining dot; accent stripping
    // then drops the dot.
    if (code == 0x130)
    {
        out.push_back(0x69);
        out.push_back(0x307);
        return;
    }
    out.push_back(LowerCode(code));
}

static auto FindDecomp(char32_t code) -> const Decomp *
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

static void Decompose(char32_t code, std::vector<char32_t> &out)
{
    if (code >= HANGUL_S_BASE && code < (HANGUL_S_BASE + HANGUL_S_COUNT))
    {
        const char32_t index = code - HANGUL_S_BASE;
        out.push_back(HANGUL_L_BASE + (index / HANGUL_N_COUNT));
        out.push_back(HANGUL_V_BASE + ((index % HANGUL_N_COUNT) / HANGUL_T_COUNT));
        const char32_t trail_index = index % HANGUL_T_COUNT;
        if (trail_index != 0)
        {
            out.push_back(HANGUL_T_BASE + trail_index);
        }
        return;
    }

    const Decomp *found = FindDecomp(code);
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

static auto Utf8Length(std::uint32_t lead) -> std::size_t
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

static auto IsContinuation(unsigned char byte) -> bool { return (static_cast<std::uint32_t>(byte) & 0xC0U) == 0x80U; }

static void AppendUtf8(std::string &out, char32_t code)
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

static auto Utf8Of(std::span<const char32_t> codes) -> std::string
{
    std::string out;
    for (const char32_t code : codes)
    {
        AppendUtf8(out, code);
    }
    return out;
}

static auto Utf8OfOne(char32_t code) -> std::string
{
    std::string out;
    AppendUtf8(out, code);
    return out;
}

static auto DecodeUtf8(std::string_view text) -> std::vector<char32_t>
{
    std::vector<char32_t> codes;
    codes.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size())
    {
        const auto lead = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index]));
        const std::size_t length = Utf8Length(lead);
        if (length == 0 || index + length > text.size())
        {
            codes.push_back(REPLACEMENT);
            ++index;
            continue;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset)
        {
            if (!IsContinuation(static_cast<unsigned char>(text[index + offset])))
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

static void CleanBasicCodes(std::string_view text, std::vector<char32_t> &cleaned)
{
    for (const char32_t code : DecodeUtf8(text))
    {
        if (code == 0 || code == REPLACEMENT || IsControl(code))
        {
            continue;
        }
        cleaned.push_back(IsWhitespace(code) ? static_cast<char32_t>(' ') : code);
    }
}

static void SpaceChineseCodes(const std::vector<char32_t> &cleaned, std::vector<char32_t> &spaced)
{
    for (const char32_t code : cleaned)
    {
        if (IsChinese(code))
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
}

static void NormalizeBasicSpan(const std::vector<char32_t> &spaced, std::size_t begin, std::size_t end,
                               std::vector<char32_t> &plain)
{
    std::vector<char32_t> lowered;
    for (std::size_t cursor = begin; cursor < end; ++cursor)
    {
        AppendLower(spaced[cursor], lowered);
    }
    std::vector<char32_t> decomposed;
    for (const char32_t code : lowered)
    {
        Decompose(code, decomposed);
    }
    for (const char32_t code : decomposed)
    {
        if (!IsMark(code))
        {
            plain.push_back(code);
        }
    }
}

static void AppendPunctSplit(const std::vector<char32_t> &plain, std::vector<std::string> &tokens)
{
    std::vector<char32_t> current;
    const auto flush = [&tokens, &current] -> void
    {
        if (!current.empty())
        {
            tokens.push_back(Utf8Of(current));
            current.clear();
        }
    };
    for (const char32_t code : plain)
    {
        if (IsPunctuation(code))
        {
            flush();
            tokens.push_back(Utf8OfOne(code));
        }
        else
        {
            current.push_back(code);
        }
    }
    flush();
}

static auto BasicTokenize(std::string_view text) -> std::vector<std::string>
{
    std::vector<char32_t> cleaned;
    CleanBasicCodes(text, cleaned);

    std::vector<char32_t> spaced;
    SpaceChineseCodes(cleaned, spaced);

    std::vector<std::string> tokens;
    std::size_t index = 0;
    while (index < spaced.size())
    {
        while (index < spaced.size() && IsWhitespace(spaced[index]))
        {
            ++index;
        }
        if (index >= spaced.size())
        {
            break;
        }
        std::size_t end = index;
        while (end < spaced.size() && !IsWhitespace(spaced[end]))
        {
            ++end;
        }

        std::vector<char32_t> plain;
        NormalizeBasicSpan(spaced, index, end, plain);
        AppendPunctSplit(plain, tokens);
        index = end;
    }
    return tokens;
}

WordPieceTokenizer::WordPieceTokenizer(std::unordered_map<std::string, int64_t> vocab, const SpecialTokenIds &ids,
                                       size_t max_sequence_length)
    : vocab_(std::move(vocab)), cls_id_(ids.cls_id), sep_id_(ids.sep_id), unk_id_(ids.unk_id),
      max_sequence_length_(max_sequence_length)
{
}

auto WordPieceTokenizer::FromVocabFile(const std::filesystem::path &vocab_path, size_t max_sequence_length)
    -> std::expected<WordPieceTokenizer, EmbedError>
{
    std::ifstream input(vocab_path);
    if (!input)
    {
        return std::unexpected(EmbedError::VocabLoadFailure);
    }

    std::unordered_map<std::string, int64_t> vocab;
    std::string token;
    int64_t next_id = 0;
    while (std::getline(input, token))
    {
        if (!token.empty() && token.back() == '\r')
        {
            token.pop_back();
        }
        vocab.emplace(token, next_id++);
    }

    if (vocab.empty())
    {
        return std::unexpected(EmbedError::VocabLoadFailure);
    }

    const auto lookup = [&vocab](const std::string &symbol) -> std::optional<int64_t>
    {
        const auto found = vocab.find(symbol);
        if (found == vocab.end())
        {
            return std::nullopt;
        }
        return found->second;
    };

    const auto cls_id = lookup(CLS_TOKEN);
    const auto sep_id = lookup(SEP_TOKEN);
    const auto unk_id = lookup(UnkTokenSymbol());
    if (!cls_id || !sep_id || !unk_id)
    {
        return std::unexpected(EmbedError::VocabLoadFailure);
    }
    return WordPieceTokenizer(std::move(vocab), {.cls_id = *cls_id, .sep_id = *sep_id, .unk_id = *unk_id},
                              max_sequence_length);
}

auto WordPieceTokenizer::WordPieceTokenize(std::string_view token) const -> std::vector<std::string>
{
    const std::vector<char32_t> chars = DecodeUtf8(token);
    const std::string unk_token = UnkTokenSymbol();
    if (chars.empty())
    {
        return {};
    }
    if (chars.size() > MAX_WORD_CHARS)
    {
        return {unk_token};
    }

    std::vector<std::string> pieces;
    std::size_t start = 0;
    while (start < chars.size())
    {
        std::size_t end = chars.size();
        std::string match;
        while (end > start)
        {
            std::string candidate = Utf8Of(std::span<const char32_t>(chars).subspan(start, end - start));
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
            return {unk_token};
        }

        pieces.push_back(std::move(match));
        start = end;
    }
    return pieces;
}

auto WordPieceTokenizer::Encode(std::string_view text) const -> std::expected<TokenizedInput, EmbedError>
{
    if (text.empty())
    {
        return std::unexpected(EmbedError::InvalidInput);
    }

    TokenizedInput encoded;
    encoded.input_ids.push_back(cls_id_);
    encoded.attention_mask.push_back(1);
    encoded.token_type_ids.push_back(0);

    for (const std::string &basic_token : BasicTokenize(text))
    {
        for (const std::string &piece : WordPieceTokenize(basic_token))
        {
            if (encoded.input_ids.size() >= max_sequence_length_)
            {
                break;
            }

            const auto found = vocab_.find(piece);
            encoded.input_ids.push_back(found == vocab_.end() ? unk_id_ : found->second);
            encoded.attention_mask.push_back(1);
            encoded.token_type_ids.push_back(0);
        }
        if (encoded.input_ids.size() >= max_sequence_length_)
        {
            break;
        }
    }

    if (encoded.input_ids.size() >= max_sequence_length_)
    {
        encoded.input_ids.back() = sep_id_;
    }
    else
    {
        encoded.input_ids.push_back(sep_id_);
        encoded.attention_mask.push_back(1);
        encoded.token_type_ids.push_back(0);
    }

    return encoded;
}

} // namespace nesso::embed
