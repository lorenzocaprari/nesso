// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "corpus_detail.hpp"

#include <core/corpus_index.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nesso::core::detail
{

static bool termByteLess(std::string_view left, std::string_view right) noexcept
{
    const std::size_t count = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto leftByte = static_cast<unsigned char>(left[index]);
        const auto rightByte = static_cast<unsigned char>(right[index]);
        if (leftByte != rightByte)
        {
            return leftByte < rightByte;
        }
    }
    return left.size() < right.size();
}

static bool appendVarint(std::vector<std::byte> &out, std::uint64_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<std::byte>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<std::byte>(value));
    return true;
}

struct VarintDecode
{
    std::uint64_t &cursor;
    std::uint64_t &value;
};

static bool readVarint(std::span<const std::byte> bytes, const VarintDecode &out)
{
    out.value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7)
    {
        if (out.cursor >= bytes.size())
        {
            return false;
        }
        const auto piece = static_cast<std::uint64_t>(bytes[out.cursor++]);
        out.value |= (piece & 0x7FU) << shift;
        if ((piece & 0x80U) == 0)
        {
            return shift < 63 || piece <= 1;
        }
    }
    return false;
}

static bool appendU64(std::vector<std::byte> &out, std::uint64_t value)
{
    std::array<std::byte, sizeof(value)> raw{};
    std::memcpy(raw.data(), &value, sizeof(value));
    out.insert(out.end(), raw.begin(), raw.end());
    return true;
}

static bool readU64At(std::span<const std::byte> bytes, std::uint64_t &cursor, std::uint64_t &value)
{
    if (!rangeInside(cursor, sizeof(value), bytes.size()))
    {
        return false;
    }
    std::memcpy(&value, bytes.data() + cursor, sizeof(value));
    cursor += sizeof(value);
    return true;
}
static std::vector<std::string> buildLexicalDictionary(std::span<const std::vector<std::string>> lineTerms)
{
    std::vector<std::string> dictionary;
    std::unordered_set<std::string> seen;
    for (const std::vector<std::string> &terms : lineTerms)
    {
        for (const std::string &term : terms)
        {
            if (seen.insert(term).second)
            {
                dictionary.push_back(term);
            }
        }
    }
    std::ranges::sort(dictionary, termByteLess);
    return dictionary;
}

static std::unordered_map<std::string, std::uint32_t> lexicalTermIds(std::span<const std::string> dictionary)
{
    std::unordered_map<std::string, std::uint32_t> ids;
    ids.reserve(dictionary.size());
    for (std::uint32_t index = 0; index < dictionary.size(); ++index)
    {
        ids.emplace(dictionary[index], index);
    }
    return ids;
}

static void appendLexicalDictionaryBytes(std::vector<std::byte> &out, std::span<const std::string> dictionary)
{
    for (const std::string &term : dictionary)
    {
        appendVarint(out, term.size());
        for (const char byte : term)
        {
            out.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
        }
    }
}

static void appendLexicalLinePostings(std::vector<std::byte> &out, std::span<const std::string> terms,
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
    appendVarint(out, entries.size());
    std::uint32_t previous = 0;
    for (const auto &[termId, frequency] : entries)
    {
        appendVarint(out, static_cast<std::uint64_t>(termId) - previous);
        appendVarint(out, frequency);
        previous = termId;
    }
    appendVarint(out, terms.size());
}

std::vector<std::byte> encodeLexicalSection(std::span<const std::vector<std::string>> lineTerms)
{
    const std::vector<std::string> dictionary = buildLexicalDictionary(lineTerms);
    const std::unordered_map<std::string, std::uint32_t> ids = lexicalTermIds(dictionary);

    std::vector<std::byte> out;
    out.reserve(16 + (dictionary.size() * 8));
    appendU64(out, dictionary.size());
    appendU64(out, lineTerms.size());
    appendLexicalDictionaryBytes(out, dictionary);
    for (const std::vector<std::string> &terms : lineTerms)
    {
        appendLexicalLinePostings(out, terms, ids);
    }
    return out;
}
static std::expected<void, EngineError> decodeLexicalDictionary(std::span<const std::byte> bytes, std::uint64_t &cursor,
                                                                std::uint64_t termCount,
                                                                std::vector<std::string> &ownedTerms,
                                                                std::vector<std::string_view> &dictionary)
{
    ownedTerms.reserve(static_cast<std::size_t>(termCount));
    dictionary.reserve(static_cast<std::size_t>(termCount));
    std::string previous;
    for (std::uint64_t index = 0; index < termCount; ++index)
    {
        std::uint64_t length = 0;
        if (!readVarint(bytes, {.cursor = cursor, .value = length}) || length > MAX_TEXT_BYTES ||
            !rangeInside(cursor, length, bytes.size()))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        std::string term(static_cast<std::size_t>(length), '\0');
        for (std::uint64_t offset = 0; offset < length; ++offset)
        {
            term[static_cast<std::size_t>(offset)] =
                static_cast<char>(static_cast<unsigned char>(bytes[cursor + offset]));
        }
        cursor += length;
        if (!previous.empty() && !termByteLess(previous, term))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        previous = term;
        ownedTerms.push_back(std::move(term));
        dictionary.emplace_back(ownedTerms.back());
    }
    return {};
}

struct LexicalPostingLimits
{
    std::uint64_t termCount = 0;
    std::uint64_t postingLines = 0;
};

static std::expected<void, EngineError> decodeLexicalPostings(std::span<const std::byte> bytes, std::uint64_t &cursor,
                                                              const LexicalPostingLimits &limits,
                                                              std::vector<LexicalLinePostings> &postings)
{
    for (std::uint64_t line = 0; line < limits.postingLines; ++line)
    {
        std::uint64_t unique = 0;
        if (!readVarint(bytes, {.cursor = cursor, .value = unique}) || unique > limits.termCount)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        LexicalLinePostings &row = postings[static_cast<std::size_t>(line)];
        row.entries.reserve(static_cast<std::size_t>(unique));
        std::uint64_t previousId = 0;
        std::uint64_t lengthSum = 0;
        for (std::uint64_t index = 0; index < unique; ++index)
        {
            std::uint64_t delta = 0;
            std::uint64_t frequency = 0;
            if (!readVarint(bytes, {.cursor = cursor, .value = delta}) ||
                !readVarint(bytes, {.cursor = cursor, .value = frequency}) || frequency == 0 ||
                delta > limits.termCount || previousId + delta >= limits.termCount || (index > 0 && delta == 0))
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
            previousId += delta;
            lengthSum += frequency;
            if (lengthSum > MAX_TEXT_BYTES)
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
            row.entries.emplace_back(static_cast<std::uint32_t>(previousId), static_cast<std::uint32_t>(frequency));
        }
        std::uint64_t length = 0;
        if (!readVarint(bytes, {.cursor = cursor, .value = length}) || length != lengthSum || length > MAX_TEXT_BYTES)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        row.length = static_cast<std::uint32_t>(length);
    }
    return {};
}

std::expected<void, EngineError> decodeLexicalSection(std::span<const std::byte> bytes, std::uint64_t lineCount,
                                                      std::vector<std::string> &ownedTerms,
                                                      std::vector<std::string_view> &dictionary,
                                                      std::vector<LexicalLinePostings> &postings)
{
    std::uint64_t cursor = 0;
    std::uint64_t termCount = 0;
    std::uint64_t postingLines = 0;
    if (!readU64At(bytes, cursor, termCount) || !readU64At(bytes, cursor, postingLines) ||
        termCount > MAX_LEXICAL_TERMS || postingLines != lineCount)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    ownedTerms.clear();
    dictionary.clear();
    postings.clear();
    if (const auto decoded = decodeLexicalDictionary(bytes, cursor, termCount, ownedTerms, dictionary); !decoded)
    {
        return std::unexpected(decoded.error());
    }
    postings.resize(static_cast<std::size_t>(postingLines));
    if (const auto decoded =
            decodeLexicalPostings(bytes, cursor, {.termCount = termCount, .postingLines = postingLines}, postings);
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
