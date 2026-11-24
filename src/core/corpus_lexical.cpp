// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "core/core_types.hpp"
#include "corpus_detail.hpp"

#include <core/corpus_index.hpp>
#include <core/lexical_terms.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nesso::core::detail
{

static auto AppendVarint(std::vector<std::byte> &out, std::uint64_t value) -> bool
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<std::byte>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    out.push_back(static_cast<std::byte>(value));
    return true;
}

static auto ReadVarint(std::span<const std::byte> bytes, std::uint64_t &cursor) -> std::optional<std::uint64_t>
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

static auto AppendU64(std::vector<std::byte> &out, std::uint64_t value) -> bool
{
    std::array<std::byte, sizeof(value)> raw{};
    std::memcpy(raw.data(), &value, sizeof(value));
    out.insert(out.end(), raw.begin(), raw.end());
    return true;
}

static auto ReadU64At(std::span<const std::byte> bytes, std::uint64_t &cursor, std::uint64_t &value) -> bool
{
    if (!RangeInside(cursor, sizeof(value), bytes.size()))
    {
        return false;
    }
    std::memcpy(&value, bytes.subspan(static_cast<std::size_t>(cursor)).data(), sizeof(value));
    cursor += sizeof(value);
    return true;
}
static auto BuildLexicalDictionary(std::span<const std::vector<std::string>> line_terms) -> std::vector<std::string>
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

static auto LexicalTermIds(std::span<const std::string> dictionary) -> std::unordered_map<std::string, std::uint32_t>
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
    for (const auto &[term_id, frequency] : counts)
    {
        entries.emplace_back(term_id, frequency);
    }
    std::ranges::sort(entries, [](const auto &left, const auto &right) -> auto { return left.first < right.first; });
    AppendVarint(out, entries.size());
    std::uint32_t previous = 0;
    for (const auto &[term_id, frequency] : entries)
    {
        AppendVarint(out, static_cast<std::uint64_t>(term_id) - previous);
        AppendVarint(out, frequency);
        previous = term_id;
    }
    AppendVarint(out, terms.size());
}

auto EncodeLexicalSection(std::span<const std::vector<std::string>> line_terms) -> std::vector<std::byte>
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

inline constexpr std::uint64_t TERM_NODE_OVERHEAD = 64;

static void RemovePath(const std::filesystem::path &path)
{
    if (path.empty())
    {
        return;
    }
    std::error_code error;
    std::filesystem::remove(path, error);
}

static void RemovePaths(const std::vector<std::filesystem::path> &paths)
{
    for (const std::filesystem::path &path : paths)
    {
        RemovePath(path);
    }
}

static auto CreateLexicalTemp(const std::filesystem::path &spill_prefix, std::string_view suffix)
    -> std::expected<std::filesystem::path, EngineError>
{
    static std::atomic<std::uint64_t> counter{0};
    for (unsigned attempt = 0; attempt < 16; ++attempt)
    {
        const auto number = counter.fetch_add(1, std::memory_order_relaxed);
        std::filesystem::path path = spill_prefix.string() + "." + std::string{suffix} + "." +
                                     std::to_string(getpid()) + "." + std::to_string(number);
        errno = 0;
        std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::noreplace);
        if (output)
        {
            output.close();
            if (!output)
            {
                RemovePath(path);
                return std::unexpected(EngineError::FileOpenFailure);
            }
            return path;
        }
        if (errno != EEXIST)
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
    }
    return std::unexpected(EngineError::FileOpenFailure);
}

static auto ReadSpilledTermRow(std::istream &terms, std::vector<std::string> &row) -> bool
{
    std::uint32_t count = 0;
    if (!ReadU32Stream(terms, count))
    {
        return false;
    }
    row.clear();
    row.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t length = 0;
        if (!ReadU32Stream(terms, length) || length > MAX_TEXT_BYTES)
        {
            return false;
        }
        std::string term(length, '\0');
        if (length != 0 && !terms.read(term.data(), static_cast<std::streamsize>(length)))
        {
            return false;
        }
        row.push_back(std::move(term));
    }
    return true;
}

static auto RowUniqueFootprint(std::span<const std::string> row, const std::unordered_set<std::string> &seen)
    -> std::uint64_t
{
    std::uint64_t bytes = 0;
    std::unordered_set<std::string_view> local;
    local.reserve(row.size());
    for (const std::string &term : row)
    {
        if (!local.insert(term).second || seen.contains(term))
        {
            continue;
        }
        bytes += term.size() + TERM_NODE_OVERHEAD;
    }
    return bytes;
}

static auto WriteSortedRun(const std::filesystem::path &path, std::unordered_set<std::string> &seen) -> bool
{
    std::vector<std::string> terms(seen.begin(), seen.end());
    seen.clear();
    std::ranges::sort(terms, TermByteLess);
    std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output || !WriteU64(output, terms.size()))
    {
        return false;
    }
    for (const std::string &term : terms)
    {
        if (!WriteU32(output, static_cast<std::uint32_t>(term.size())) || !WriteRaw(output, term.data(), term.size()))
        {
            return false;
        }
    }
    output.close();
    return static_cast<bool>(output);
}

static auto FlushUniqueRun(const std::filesystem::path &spill_prefix, std::unordered_set<std::string> &seen,
                           std::vector<std::filesystem::path> &runs) -> bool
{
    auto run = CreateLexicalTemp(spill_prefix, "lexrun");
    if (!run)
    {
        return false;
    }
    runs.push_back(*run);
    return WriteSortedRun(*run, seen);
}

static auto BuildUniqueTermRuns(std::istream &terms, std::uint64_t line_count,
                                const std::filesystem::path &spill_prefix, std::uint64_t chunk_budget)
    -> std::expected<std::vector<std::filesystem::path>, EngineError>
{
    std::vector<std::filesystem::path> runs;
    std::unordered_set<std::string> seen;
    std::uint64_t seen_bytes = 0;
    std::vector<std::string> row;
    for (std::uint64_t index = 0; index < line_count; ++index)
    {
        if (!ReadSpilledTermRow(terms, row))
        {
            RemovePaths(runs);
            return std::unexpected(EngineError::FileOpenFailure);
        }
        const std::uint64_t added = RowUniqueFootprint(row, seen);
        if (!seen.empty() && seen_bytes + added > chunk_budget)
        {
            if (!FlushUniqueRun(spill_prefix, seen, runs))
            {
                RemovePaths(runs);
                return std::unexpected(EngineError::FileOpenFailure);
            }
            seen_bytes = 0;
        }
        for (std::string &term : row)
        {
            const std::uint64_t cost = term.size() + TERM_NODE_OVERHEAD;
            if (seen.insert(std::move(term)).second)
            {
                seen_bytes += cost;
            }
        }
    }
    if (!AtEnd(terms))
    {
        RemovePaths(runs);
        return std::unexpected(EngineError::CorruptDatabase);
    }
    // Always emit at least one run when there were no prior flushes so merge
    // sees a defined (possibly empty) dictionary input. Flush only then:
    // the call has the side effect the outer condition guards.
    if ((!seen.empty() || runs.empty()) && !FlushUniqueRun(spill_prefix, seen, runs))
    {
        RemovePaths(runs);
        return std::unexpected(EngineError::FileOpenFailure);
    }
    return runs;
}

static auto ReadRunTerm(std::istream &input, std::uint64_t &remaining, std::string &term) -> bool
{
    if (remaining == 0)
    {
        return false;
    }
    std::uint32_t length = 0;
    if (!ReadU32Stream(input, length) || length > MAX_TEXT_BYTES)
    {
        return false;
    }
    term.assign(length, '\0');
    if (length != 0 && !input.read(term.data(), static_cast<std::streamsize>(length)))
    {
        return false;
    }
    --remaining;
    return true;
}

namespace
{
struct LexicalRunCursor
{
    std::ifstream input;
    std::uint64_t remaining = 0;
    std::string term;
    bool active = false;
};
} // namespace

static auto SmallestActiveRun(std::span<const LexicalRunCursor> cursors) -> std::optional<std::size_t>
{
    std::optional<std::size_t> best;
    for (std::size_t index = 0; index < cursors.size(); ++index)
    {
        if (!cursors[index].active)
        {
            continue;
        }
        if (!best || TermByteLess(cursors[index].term, cursors[*best].term))
        {
            best = index;
        }
    }
    return best;
}

static auto MergeSortedRuns(const std::vector<std::filesystem::path> &runs, std::uint64_t max_terms)
    -> std::expected<std::vector<std::string>, EngineError>
{
    if (runs.empty())
    {
        return std::vector<std::string>{};
    }

    std::vector<LexicalRunCursor> cursors(runs.size());
    for (std::size_t index = 0; index < runs.size(); ++index)
    {
        cursors[index].input.open(runs[index], std::ios::binary);
        std::uint64_t count = 0;
        if (!cursors[index].input || !ReadU64Stream(cursors[index].input, count))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        cursors[index].remaining = count;
        if (count == 0)
        {
            continue;
        }
        if (!ReadRunTerm(cursors[index].input, cursors[index].remaining, cursors[index].term))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        cursors[index].active = true;
    }

    std::vector<std::string> dictionary;
    std::string previous;
    while (const std::optional<std::size_t> best = SmallestActiveRun(cursors))
    {
        LexicalRunCursor &cursor = cursors[*best];
        if (previous.empty() || previous != cursor.term)
        {
            if (dictionary.size() >= max_terms)
            {
                return std::unexpected(EngineError::LimitsExceeded);
            }
            previous = cursor.term;
            dictionary.push_back(cursor.term);
        }
        if (cursor.remaining == 0)
        {
            cursor.active = false;
            continue;
        }
        if (!ReadRunTerm(cursor.input, cursor.remaining, cursor.term))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
    }
    for (LexicalRunCursor &cursor : cursors)
    {
        if (cursor.remaining != 0 || !AtEnd(cursor.input))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
    }
    return dictionary;
}

static auto WriteLexicalHeaderAndDictionary(std::ostream &output, std::span<const std::string> dictionary,
                                            std::uint64_t line_count, std::uint64_t &written) -> bool
{
    std::vector<std::byte> prefix;
    prefix.reserve(16 + (dictionary.size() * 8));
    AppendU64(prefix, dictionary.size());
    AppendU64(prefix, line_count);
    AppendLexicalDictionaryBytes(prefix, dictionary);
    if (!WriteRaw(output, prefix.data(), prefix.size()))
    {
        return false;
    }
    written += prefix.size();
    return true;
}

namespace
{
enum class PostingWriteStatus : std::uint8_t
{
    Ok,
    TooLarge,
    IoError
};
} // namespace

static auto WriteLinePostings(std::ostream &output, std::span<const std::string> terms,
                              const std::unordered_map<std::string, std::uint32_t> &ids, std::uint64_t max_bytes,
                              std::uint64_t &written) -> PostingWriteStatus
{
    std::vector<std::byte> row;
    AppendLexicalLinePostings(row, terms, ids);
    if (written > max_bytes || max_bytes - written < row.size())
    {
        return PostingWriteStatus::TooLarge;
    }
    if (!WriteRaw(output, row.data(), row.size()))
    {
        return PostingWriteStatus::IoError;
    }
    written += row.size();
    return PostingWriteStatus::Ok;
}

static auto WriteLexicalSpill(std::istream &terms, std::uint64_t line_count, std::span<const std::string> dictionary,
                              const std::filesystem::path &spill_prefix, std::uint64_t max_bytes)
    -> std::expected<LexicalSpillFile, EngineError>
{
    auto lexical_path = CreateLexicalTemp(spill_prefix, "lexical");
    if (!lexical_path)
    {
        return std::unexpected(lexical_path.error());
    }
    std::ofstream lexical(*lexical_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!lexical)
    {
        RemovePath(*lexical_path);
        return std::unexpected(EngineError::FileOpenFailure);
    }

    std::uint64_t written = 0;
    const auto ids = LexicalTermIds(dictionary);
    if (!WriteLexicalHeaderAndDictionary(lexical, dictionary, line_count, written))
    {
        lexical.close();
        RemovePath(*lexical_path);
        return std::unexpected(EngineError::FileOpenFailure);
    }

    std::vector<std::string> row;
    for (std::uint64_t index = 0; index < line_count; ++index)
    {
        if (!ReadSpilledTermRow(terms, row))
        {
            lexical.close();
            RemovePath(*lexical_path);
            return std::unexpected(EngineError::FileOpenFailure);
        }
        switch (WriteLinePostings(lexical, row, ids, max_bytes, written))
        {
        case PostingWriteStatus::Ok:
            break;
        case PostingWriteStatus::TooLarge:
            lexical.close();
            RemovePath(*lexical_path);
            return std::unexpected(EngineError::LimitsExceeded);
        case PostingWriteStatus::IoError:
            lexical.close();
            RemovePath(*lexical_path);
            return std::unexpected(EngineError::FileOpenFailure);
        }
    }
    if (!AtEnd(terms))
    {
        lexical.close();
        RemovePath(*lexical_path);
        return std::unexpected(EngineError::CorruptDatabase);
    }
    lexical.close();
    if (!lexical || written > max_bytes)
    {
        RemovePath(*lexical_path);
        return std::unexpected(written > max_bytes ? EngineError::LimitsExceeded : EngineError::FileOpenFailure);
    }
    return LexicalSpillFile{.path = std::move(*lexical_path), .bytes = written};
}

auto EncodeLexicalSectionSpilled(const std::filesystem::path &terms_path, std::uint64_t line_count,
                                 const std::filesystem::path &spill_prefix, std::uint64_t memory_budget,
                                 const CorpusLimits &limits) -> std::expected<LexicalSpillFile, EngineError>
{
    if (memory_budget == 0)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const std::uint64_t chunk_budget = std::max<std::uint64_t>(memory_budget / 4, 1);

    std::ifstream terms(terms_path, std::ios::binary);
    if (!terms)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }

    auto runs = BuildUniqueTermRuns(terms, line_count, spill_prefix, chunk_budget);
    if (!runs)
    {
        return std::unexpected(runs.error());
    }
    auto dictionary = MergeSortedRuns(*runs, limits.lexical_terms);
    RemovePaths(*runs);
    if (!dictionary)
    {
        return std::unexpected(dictionary.error());
    }

    terms.clear();
    terms.seekg(0);
    if (!terms)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    return WriteLexicalSpill(terms, line_count, *dictionary, spill_prefix, limits.lexical_bytes);
}

static auto DecodeLexicalDictionary(std::span<const std::byte> bytes, std::uint64_t &cursor, std::uint64_t term_count,
                                    std::vector<std::string> &owned_terms, std::vector<std::string_view> &dictionary)
    -> std::expected<void, EngineError>
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

namespace
{
struct LexicalPostingLimits
{
    std::uint64_t term_count = 0;
    std::uint64_t posting_lines = 0;
};
} // namespace

static auto DecodeLexicalPostings(std::span<const std::byte> bytes, std::uint64_t &cursor,
                                  const LexicalPostingLimits &limits, std::vector<LexicalLinePostings> &postings)
    -> std::expected<void, EngineError>
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

auto DecodeLexicalSection(std::span<const std::byte> bytes, std::uint64_t line_count,
                          std::vector<std::string> &owned_terms, std::vector<std::string_view> &dictionary,
                          std::vector<LexicalLinePostings> &postings) -> std::expected<void, EngineError>
{
    std::uint64_t cursor = 0;
    std::uint64_t term_count = 0;
    std::uint64_t posting_lines = 0;
    if (!ReadU64At(bytes, cursor, term_count) || !ReadU64At(bytes, cursor, posting_lines) ||
        posting_lines != line_count)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (term_count > MAX_LEXICAL_TERMS)
    {
        return std::unexpected(EngineError::LimitsExceeded);
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
