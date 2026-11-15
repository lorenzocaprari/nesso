// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "core/core_types.hpp"
#include "corpus_detail.hpp"

#include <core/corpus_index.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nesso::core::detail
{

static bool TermByteLess(std::string_view left, std::string_view right) noexcept
{
    const std::size_t count = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto left_byte = static_cast<unsigned char>(left[index]);
        const auto right_byte = static_cast<unsigned char>(right[index]);
        if (left_byte != right_byte)
        {
            return left_byte < right_byte;
        }
    }
    return left.size() < right.size();
}

static bool AppendVarint(std::vector<std::byte> &out, std::uint64_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<std::byte>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<std::byte>(value));
    return true;
}

static std::optional<std::uint64_t> ReadVarint(std::span<const std::byte> bytes, std::uint64_t &cursor)
{
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7)
    {
        if (cursor >= bytes.size())
        {
            return std::nullopt;
        }
        const auto piece = static_cast<std::uint64_t>(bytes[cursor++]);
        value |= (piece & 0x7FU) << shift;
        if ((piece & 0x80U) == 0)
        {
            if (shift < 63 || piece <= 1)
            {
                return value;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

static bool AppendU64(std::vector<std::byte> &out, std::uint64_t value)
{
    std::array<std::byte, sizeof(value)> raw{};
    std::memcpy(raw.data(), &value, sizeof(value));
    out.insert(out.end(), raw.begin(), raw.end());
    return true;
}

static bool ReadU64At(std::span<const std::byte> bytes, std::uint64_t &cursor, std::uint64_t &value)
{
    if (!RangeInside(cursor, sizeof(value), bytes.size()))
    {
        return false;
    }
    std::memcpy(&value, bytes.subspan(static_cast<std::size_t>(cursor)).data(), sizeof(value));
    cursor += sizeof(value);
    return true;
}
static std::vector<std::string> BuildLexicalDictionary(std::span<const std::vector<std::string>> line_terms)
{
    std::vector<std::string> dictionary;
    std::unordered_set<std::string> seen;
    for (const std::vector<std::string> &terms : line_terms)
    {
        for (const std::string &term : terms)
        {
            if (seen.insert(term).second)
            {
                dictionary.push_back(term);
            }
        }
    }
    std::ranges::sort(dictionary, TermByteLess);
    return dictionary;
}

static std::unordered_map<std::string, std::uint32_t> LexicalTermIds(std::span<const std::string> dictionary)
{
    std::unordered_map<std::string, std::uint32_t> ids;
    ids.reserve(dictionary.size());
    for (std::uint32_t index = 0; index < dictionary.size(); ++index)
    {
        ids.emplace(dictionary[index], index);
    }
    return ids;
}

static void AppendLexicalDictionaryBytes(std::vector<std::byte> &out, std::span<const std::string> dictionary)
{
    for (const std::string &term : dictionary)
    {
        AppendVarint(out, term.size());
        for (const char byte : term)
        {
            out.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
        }
    }
}

static void AppendLexicalLinePostings(std::vector<std::byte> &out, std::span<const std::string> terms,
                                      const std::unordered_map<std::string, std::uint32_t> &ids)
{
    std::unordered_map<std::uint32_t, std::uint32_t> counts;
    counts.reserve(terms.size());
    for (const std::string &term : terms)
    {
        ++counts[ids.at(term)];
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
    entries.reserve(counts.size());
    for (const auto &[termId, frequency] : counts)
    {
        entries.emplace_back(termId, frequency);
    }
    std::ranges::sort(entries, [](const auto &left, const auto &right) { return left.first < right.first; });
    AppendVarint(out, entries.size());
    std::uint32_t previous = 0;
    for (const auto &[termId, frequency] : entries)
    {
        AppendVarint(out, static_cast<std::uint64_t>(termId) - previous);
        AppendVarint(out, frequency);
        previous = termId;
    }
    AppendVarint(out, terms.size());
}

std::vector<std::byte> EncodeLexicalSection(std::span<const std::vector<std::string>> line_terms)
{
    const std::vector<std::string> dictionary = BuildLexicalDictionary(line_terms);
    const std::unordered_map<std::string, std::uint32_t> ids = LexicalTermIds(dictionary);

    std::vector<std::byte> out;
    out.reserve(16 + (dictionary.size() * 8));
    AppendU64(out, dictionary.size());
    AppendU64(out, line_terms.size());
    AppendLexicalDictionaryBytes(out, dictionary);
    for (const std::vector<std::string> &terms : line_terms)
    {
        AppendLexicalLinePostings(out, terms, ids);
    }
    return out;
}
static std::expected<void, EngineError> DecodeLexicalDictionary(std::span<const std::byte> bytes, std::uint64_t &cursor,
                                                                std::uint64_t term_count,
                                                                std::vector<std::string> &owned_terms,
                                                                std::vector<std::string_view> &dictionary)
{
    owned_terms.reserve(static_cast<std::size_t>(term_count));
    dictionary.reserve(static_cast<std::size_t>(term_count));
    std::string previous;
    for (std::uint64_t index = 0; index < term_count; ++index)
    {
        const std::optional<std::uint64_t> length = ReadVarint(bytes, cursor);
        if (!length || *length > MAX_TEXT_BYTES || !RangeInside(cursor, *length, bytes.size()))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        std::string term(static_cast<std::size_t>(*length), '\0');
        for (std::uint64_t offset = 0; offset < *length; ++offset)
        {
            term[static_cast<std::size_t>(offset)] =
                static_cast<char>(static_cast<unsigned char>(bytes[cursor + offset]));
        }
        cursor += *length;
        if (!previous.empty() && !TermByteLess(previous, term))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        previous = term;
        owned_terms.push_back(std::move(term));
        dictionary.emplace_back(owned_terms.back());
    }
    return {};
}

struct LexicalPostingLimits
{
    std::uint64_t term_count = 0;
    std::uint64_t posting_lines = 0;
};

static std::expected<void, EngineError> DecodeLexicalPostings(std::span<const std::byte> bytes, std::uint64_t &cursor,
                                                              const LexicalPostingLimits &limits,
                                                              std::vector<LexicalLinePostings> &postings)
{
    for (std::uint64_t line = 0; line < limits.posting_lines; ++line)
    {
        const std::optional<std::uint64_t> unique = ReadVarint(bytes, cursor);
        if (!unique || *unique > limits.term_count)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        LexicalLinePostings &row = postings[static_cast<std::size_t>(line)];
        row.entries.reserve(static_cast<std::size_t>(*unique));
        std::uint64_t previous_id = 0;
        std::uint64_t length_sum = 0;
        for (std::uint64_t index = 0; index < *unique; ++index)
        {
            const std::optional<std::uint64_t> delta = ReadVarint(bytes, cursor);
            const std::optional<std::uint64_t> frequency = ReadVarint(bytes, cursor);
            if (!delta || !frequency || *frequency == 0 || *delta > limits.term_count ||
                previous_id + *delta >= limits.term_count || (index > 0 && *delta == 0))
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
            previous_id += *delta;
            length_sum += *frequency;
            if (length_sum > MAX_TEXT_BYTES)
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
            row.entries.emplace_back(static_cast<std::uint32_t>(previous_id), static_cast<std::uint32_t>(*frequency));
        }
        const std::optional<std::uint64_t> length = ReadVarint(bytes, cursor);
        if (!length || *length != length_sum || *length > MAX_TEXT_BYTES)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        row.length = static_cast<std::uint32_t>(*length);
    }
    return {};
}

std::expected<void, EngineError> DecodeLexicalSection(std::span<const std::byte> bytes, std::uint64_t line_count,
                                                      std::vector<std::string> &owned_terms,
                                                      std::vector<std::string_view> &dictionary,
                                                      std::vector<LexicalLinePostings> &postings)
{
    std::uint64_t cursor = 0;
    std::uint64_t term_count = 0;
    std::uint64_t posting_lines = 0;
    if (!ReadU64At(bytes, cursor, term_count) || !ReadU64At(bytes, cursor, posting_lines) ||
        term_count > MAX_LEXICAL_TERMS || posting_lines != line_count)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    owned_terms.clear();
    dictionary.clear();
    postings.clear();
    if (const auto decoded = DecodeLexicalDictionary(bytes, cursor, term_count, owned_terms, dictionary); !decoded)
    {
        return std::unexpected(decoded.error());
    }
    postings.resize(static_cast<std::size_t>(posting_lines));
    if (const auto decoded =
            DecodeLexicalPostings(bytes, cursor, {.term_count = term_count, .posting_lines = posting_lines}, postings);
        !decoded)
    {
        return std::unexpected(decoded.error());
    }
    if (cursor != bytes.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

} // namespace nesso::core::detail
