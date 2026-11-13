// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/lexical_terms.hpp>
#include <core/sha256.hpp>

#include <parser/archive_source.hpp>
#include <parser/gzip_reader.hpp>
#include <platform/file.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace nesso::core
{

static_assert(std::endian::native == std::endian::little);

constexpr std::uint64_t HEADER_BYTES = 128;
constexpr std::uint64_t SOURCE_RECORD_BYTES = 48;
constexpr std::uint64_t LINE_RECORD_BYTES = 56;
constexpr std::uint64_t TEXT_RECORD_BYTES = 16;
constexpr std::uint64_t MAX_POLICY_BYTES = 64ULL * 1024ULL;
constexpr std::uint64_t MAX_TEXT_BYTES = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MAX_SOURCES = 1'000'000;
constexpr std::uint64_t MAX_LINES = 10'000'000;
constexpr std::uint64_t MAX_DISTINCT = 1'000'000;
constexpr std::uint64_t MAX_LEXICAL_TERMS = 4'000'000;
constexpr std::uint64_t MAX_LEXICAL_BYTES = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t MAX_DIMENSIONS = 65536;
constexpr std::array<char, 4> MAGIC{'N', 'E', 'S', 'C'};
constexpr std::uint32_t SOURCE_TRACKED = 1U;
constexpr std::uint32_t SOURCE_STORED_ONLY = 2U;
constexpr std::uint32_t LINE_ARRAY_ELEMENT = 1U;
constexpr std::uint32_t LINE_STORED_TEXT = 2U;
constexpr std::uint32_t LINE_SKIPPED = 4U;

struct Layout
{
    std::uint32_t policyBytes = 0;
    std::uint32_t dimensions = 0;
    std::uint64_t sourceCount = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t sourceOffset = 0;
    std::uint64_t lineOffset = 0;
    std::uint64_t matrixOffset = 0;
    std::uint64_t distinctCount = 0;
    std::uint64_t distinctOffset = 0;
    std::uint64_t textOffset = 0;
    std::uint64_t storedOffset = 0;
    std::uint64_t lexicalOffset = 0;
    std::uint64_t lexicalBytes = 0;
    std::uint64_t policyOffset = 0;
};

struct SpillFile
{
    std::filesystem::path path;
    std::ofstream output;
};

struct SpilledLine
{
    std::uint64_t lineNumber = 0;
    std::uint64_t byteOffset = 0;
    std::uint64_t byteLength = 0;
    std::uint32_t sourceIndex = 0;
    std::uint32_t distinctId = NO_DISTINCT;
    std::uint32_t flags = 0;
    std::string storedText;
};

static bool rangeInside(std::uint64_t offset, std::uint64_t length, std::uint64_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

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

static bool readVarint(std::span<const std::byte> bytes, std::uint64_t &cursor, std::uint64_t &value)
{
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7)
    {
        if (cursor >= bytes.size())
        {
            return false;
        }
        const auto piece = static_cast<std::uint64_t>(bytes[cursor++]);
        value |= (piece & 0x7FU) << shift;
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

static bool writeRaw(std::ostream &output, const void *data, std::size_t size)
{
    if (size == 0)
    {
        return true;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(output);
}

static bool writeU32(std::ostream &output, std::uint32_t value)
{
    const std::array<unsigned char, 4> bytes{static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8U),
                                             static_cast<unsigned char>(value >> 16U),
                                             static_cast<unsigned char>(value >> 24U)};
    return writeRaw(output, bytes.data(), bytes.size());
}

static bool writeU64(std::ostream &output, std::uint64_t value)
{
    const std::array<unsigned char, 8> bytes{
        static_cast<unsigned char>(value),        static_cast<unsigned char>(value >> 8U),
        static_cast<unsigned char>(value >> 16U), static_cast<unsigned char>(value >> 24U),
        static_cast<unsigned char>(value >> 32U), static_cast<unsigned char>(value >> 40U),
        static_cast<unsigned char>(value >> 48U), static_cast<unsigned char>(value >> 56U)};
    return writeRaw(output, bytes.data(), bytes.size());
}

static bool readExact(std::istream &input, void *data, std::size_t size)
{
    if (size == 0)
    {
        return true;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    input.read(reinterpret_cast<char *>(data), static_cast<std::streamsize>(size));
    return std::cmp_equal(input.gcount(), size);
}

static bool readU32Stream(std::istream &input, std::uint32_t &value)
{
    std::array<unsigned char, 4> bytes{};
    if (!readExact(input, bytes.data(), bytes.size()))
    {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

static bool readU64Stream(std::istream &input, std::uint64_t &value)
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (!readU32Stream(input, low) || !readU32Stream(input, high))
    {
        return false;
    }
    value = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32U);
    return true;
}

static bool readU32(std::span<const std::byte> bytes, std::uint64_t offset, std::uint32_t &value)
{
    if (!rangeInside(offset, sizeof(value), bytes.size()))
    {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        value |= std::to_integer<std::uint32_t>(bytes[offset++]) << shift;
    }
    return true;
}

static bool readU64(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t &value)
{
    if (!rangeInside(offset, sizeof(value), bytes.size()))
    {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
    {
        value |= std::to_integer<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

static std::string_view viewBytes(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t length)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const char *>(bytes.data() + offset), static_cast<std::size_t>(length)};
}

static bool validKey(std::string_view key)
{
    return !key.empty() && std::ranges::all_of(key,
                                               [](char character)
                                               {
                                                   return (character >= 'a' && character <= 'z') ||
                                                          (character >= '0' && character <= '9') || character == '.' ||
                                                          character == '-' || character == '_';
                                               });
}

static bool validHash(std::string_view value)
{
    return value.size() == 64 &&
           std::ranges::all_of(
               value, [](char character)
               { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
}

static bool parseNumber(std::string_view text, std::uint32_t &value)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::to_string(value) == text;
}

static std::expected<std::uint32_t, EngineError> validatePolicy(const CorpusPolicy &policy)
{
    constexpr std::array<std::string_view, 10> required{
        "batch",        "dedup",      "dimensions", "mask",        "model.id", "model.quantization",
        "model.sha256", "tokens.max", "truncate",   "vocab.sha256"};
    for (const std::string_view key : required)
    {
        if (!policy.contains(key))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
    }
    std::uint64_t size = 0;
    for (const auto &[key, value] : policy)
    {
        if (!validKey(key) || value.empty() || value.find_first_of("\r\n\0", 0, 3) != std::string::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        size += key.size() + value.size() + 2;
        if (size > MAX_POLICY_BYTES)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
    }
    std::uint32_t dimensions = 0;
    std::uint32_t maxTokens = 0;
    const auto &quantization = policy.at("model.quantization");
    if (!parseNumber(policy.at("dimensions"), dimensions) || dimensions > MAX_DIMENSIONS ||
        !parseNumber(policy.at("tokens.max"), maxTokens) || maxTokens == 0 || policy.at("model.id").size() > 1024 ||
        !validHash(policy.at("model.sha256")) || !validHash(policy.at("vocab.sha256")) ||
        (quantization != "int8" && quantization != "fp32"))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return dimensions;
}

static LexicalStatus lexicalStatusOf(bool hasLexical, const CorpusPolicy &policy)
{
    if (!hasLexical)
    {
        return LexicalStatus::Missing;
    }
    const bool termsOk = policy.contains("lexical.terms") && policy.at("lexical.terms") == LEXICAL_TERMS_VERSION;
    const bool bm25Ok = policy.contains("bm25") && policy.at("bm25") == BM25_POLICY;
    if (!termsOk || !bm25Ok)
    {
        return LexicalStatus::VersionMismatch;
    }
    return LexicalStatus::Ok;
}

static std::expected<void, EngineError> validateVector(std::span<const float> embedding, std::uint32_t dimensions)
{
    if (embedding.empty())
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (embedding.size() != dimensions)
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (!std::ranges::all_of(embedding, [](float value) { return std::isfinite(value); }))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

static std::string serializePolicy(const CorpusPolicy &policy)
{
    std::string text;
    for (const auto &[key, value] : policy)
    {
        text += key;
        text += '=';
        text += value;
        text += '\n';
    }
    return text;
}

static std::expected<CorpusPolicy, EngineError> parsePolicy(std::string_view text)
{
    CorpusPolicy policy;
    std::string previous;
    while (!text.empty())
    {
        const auto newline = text.find('\n');
        if (newline == std::string_view::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const std::string_view record = text.substr(0, newline);
        const auto separator = record.find('=');
        if (separator == std::string_view::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        std::string key{record.substr(0, separator)};
        if (!previous.empty() && key <= previous)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        previous = key;
        policy.emplace(std::move(key), std::string{record.substr(separator + 1)});
        text.remove_prefix(newline + 1);
    }
    if (!validatePolicy(policy))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return policy;
}

static std::expected<SpillFile, EngineError> createSpill(const std::filesystem::path &destination,
                                                         std::string_view suffix)
{
    static std::atomic<std::uint64_t> counter{0};
    for (unsigned attempt = 0; attempt < 16; ++attempt)
    {
        const auto number = counter.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path path = destination.string() + "." + std::string{suffix} + "." +
                                           std::to_string(getpid()) + "." + std::to_string(number);
        errno = 0;
        std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::noreplace);
        if (output)
        {
            return SpillFile{.path = path, .output = std::move(output)};
        }
        if (errno != EEXIST)
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
    }
    return std::unexpected(EngineError::FileOpenFailure);
}

static void removeFile(const std::filesystem::path &path)
{
    if (!path.empty())
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
}

static bool readSpilledLine(std::istream &input, SpilledLine &line)
{
    std::uint32_t length = 0;
    if (!readU64Stream(input, line.lineNumber) || !readU64Stream(input, line.byteOffset) ||
        !readU64Stream(input, line.byteLength) || !readU32Stream(input, line.sourceIndex) ||
        !readU32Stream(input, line.distinctId) || !readU32Stream(input, line.flags) || !readU32Stream(input, length) ||
        length > MAX_TEXT_BYTES)
    {
        return false;
    }
    line.storedText.assign(length, '\0');
    return readExact(input, line.storedText.data(), length);
}

static bool readSpilledText(std::istream &input, std::string &text)
{
    std::uint32_t length = 0;
    if (!readU32Stream(input, length) || length > MAX_TEXT_BYTES)
    {
        return false;
    }
    text.assign(length, '\0');
    return readExact(input, text.data(), length);
}

static bool rewind(std::istream &input)
{
    input.clear();
    input.seekg(0);
    return static_cast<bool>(input);
}

static bool atEnd(std::istream &input)
{
    return input.peek() == std::char_traits<char>::eof() && input.eof() && !input.bad();
}

static bool copyBytes(std::istream &input, std::ostream &output, std::uint64_t bytes)
{
    std::array<char, 64ULL * 1024ULL> buffer{};
    while (bytes > 0)
    {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(bytes, buffer.size()));
        if (!readExact(input, buffer.data(), count) || !writeRaw(output, buffer.data(), count))
        {
            return false;
        }
        bytes -= count;
    }
    return atEnd(input);
}

static bool writePadding(std::ostream &output, std::uint64_t count)
{
    constexpr std::array<char, 64> zeros{};
    while (count > 0)
    {
        const auto part = static_cast<std::size_t>(std::min<std::uint64_t>(count, zeros.size()));
        if (!writeRaw(output, zeros.data(), part))
        {
            return false;
        }
        count -= part;
    }
    return true;
}

static bool isZero(std::span<const std::byte> bytes, std::uint64_t begin, std::uint64_t end)
{
    if (begin > end || end > bytes.size())
    {
        return false;
    }
    return std::ranges::all_of(bytes.subspan(begin, end - begin),
                               [](std::byte value) { return value == std::byte{0}; });
}

CorpusChecksum corpusChecksum(std::span<const std::byte> file)
{
    constexpr std::array<std::byte, CORPUS_CHECKSUM_BYTES> zeros{};
    const std::array<std::span<const std::byte>, 3> parts{file.first(CORPUS_CHECKSUM_OFFSET), zeros,
                                                          file.subspan(CORPUS_CHECKSUM_OFFSET + CORPUS_CHECKSUM_BYTES)};
    const Sha256Digest digest = sha256(parts);
    CorpusChecksum checksum{};
    std::ranges::transform(std::span{digest}.first(CORPUS_CHECKSUM_BYTES), checksum.begin(),
                           [](std::uint8_t value) { return std::byte{value}; });
    return checksum;
}

static bool sealCorpus(const std::filesystem::path &path)
{
    CorpusChecksum checksum{};
    {
        auto mapped = nesso::platform::MappedFile::open(path);
        if (!mapped || mapped->bytes().size() < HEADER_BYTES)
        {
            return false;
        }
        checksum = corpusChecksum(mapped->bytes());
    }
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(static_cast<std::streamoff>(CORPUS_CHECKSUM_OFFSET));
    if (!writeRaw(file, checksum.data(), checksum.size()))
    {
        return false;
    }
    file.close();
    return !file.fail();
}

std::expected<SourceInfo, EngineError> sourceInfoFor(const std::filesystem::path &path, std::string_view jsonField)
{
    SourceInfo source{.path = path.string(), .jsonField = std::string{jsonField}};
    if (path == "-")
    {
        source.storedOnly = true;
        return source;
    }
    std::error_code error;
    source.size = std::filesystem::file_size(path, error);
    if (error)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    source.modifiedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
    if (nesso::parser::isArchivePath(path))
    {
        // The bundle is one file for the staleness check. Each member is its own stored source.
        source.tracked = true;
        return source;
    }
    if (nesso::parser::isGzipPath(path))
    {
        // A gzip member is not a file-offset range. Store each line, as stdin and archive members do.
        source.storedOnly = true;
        return source;
    }
    source.tracked = true;
    return source;
}

static std::vector<std::byte> encodeLexicalSection(std::span<const std::vector<std::string>> lineTerms)
{
    std::vector<std::string> dictionary;
    {
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
    }
    std::ranges::sort(dictionary, termByteLess);
    std::unordered_map<std::string, std::uint32_t> ids;
    ids.reserve(dictionary.size());
    for (std::uint32_t index = 0; index < dictionary.size(); ++index)
    {
        ids.emplace(dictionary[index], index);
    }

    std::vector<std::byte> out;
    out.reserve(16 + (dictionary.size() * 8));
    appendU64(out, dictionary.size());
    appendU64(out, lineTerms.size());
    for (const std::string &term : dictionary)
    {
        appendVarint(out, term.size());
        for (const char byte : term)
        {
            out.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
        }
    }
    for (const std::vector<std::string> &terms : lineTerms)
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
        if (!readVarint(bytes, cursor, length) || length > MAX_TEXT_BYTES || !rangeInside(cursor, length, bytes.size()))
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

static std::expected<void, EngineError> decodeLexicalPostings(std::span<const std::byte> bytes, std::uint64_t &cursor,
                                                              std::uint64_t termCount, std::uint64_t postingLines,
                                                              std::vector<LexicalLinePostings> &postings)
{
    for (std::uint64_t line = 0; line < postingLines; ++line)
    {
        std::uint64_t unique = 0;
        if (!readVarint(bytes, cursor, unique) || unique > termCount)
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
            if (!readVarint(bytes, cursor, delta) || !readVarint(bytes, cursor, frequency) || frequency == 0 ||
                delta > termCount || previousId + delta >= termCount || (index > 0 && delta == 0))
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
        if (!readVarint(bytes, cursor, length) || length != lengthSum || length > MAX_TEXT_BYTES)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        row.length = static_cast<std::uint32_t>(length);
    }
    return {};
}

static std::expected<void, EngineError> decodeLexicalSection(std::span<const std::byte> bytes, std::uint64_t lineCount,
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
    if (const auto decoded = decodeLexicalPostings(bytes, cursor, termCount, postingLines, postings); !decoded)
    {
        return std::unexpected(decoded.error());
    }
    if (cursor != bytes.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

struct CorpusWriter::Impl
{
    std::filesystem::path destination;
    std::filesystem::path temporary;
    CorpusPolicy policy;
    std::uint32_t dimensions = 0;
    SpillFile lines;
    SpillFile texts;
    SpillFile vectors;
    SpillFile terms;
    std::vector<SourceInfo> sources;
    std::unordered_set<std::string> distinctTexts;
    std::vector<std::uint32_t> uses;
    std::uint32_t vectorCount = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t textBytes = 0;
    std::uint64_t storedBytes = 0;
    std::uint32_t lastSource = 0;
    std::uint64_t lastLine = 0;
    bool hasLastLine = false;
    bool failed = false;
    bool finished = false;
};

CorpusWriter::CorpusWriter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
void CorpusWriter::discardSpills(Impl *impl) noexcept
{
    if (impl == nullptr)
    {
        return;
    }
    impl->lines.output.close();
    impl->texts.output.close();
    impl->vectors.output.close();
    impl->terms.output.close();
    removeFile(impl->lines.path);
    removeFile(impl->texts.path);
    removeFile(impl->vectors.path);
    removeFile(impl->terms.path);
    removeFile(impl->temporary);
}

CorpusWriter::~CorpusWriter() { discardSpills(impl_.get()); }
CorpusWriter::CorpusWriter(CorpusWriter &&) noexcept = default;
CorpusWriter &CorpusWriter::operator=(CorpusWriter &&other) noexcept
{
    if (this != &other)
    {
        discardSpills(impl_.get());
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<CorpusWriter, EngineError> CorpusWriter::create(const std::filesystem::path &path, CorpusPolicy policy)
{
    const auto dimensions = validatePolicy(policy);
    if (!dimensions)
    {
        return std::unexpected(dimensions.error());
    }
    std::error_code error;
    if (std::filesystem::is_directory(path, error))
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    auto impl = std::make_unique<Impl>();
    impl->destination = path;
    impl->policy = std::move(policy);
    impl->dimensions = *dimensions;
    auto lines = createSpill(path, "lines");
    if (!lines)
    {
        return std::unexpected(lines.error());
    }
    impl->lines = std::move(*lines);
    auto texts = createSpill(path, "texts");
    if (!texts)
    {
        return std::unexpected(texts.error());
    }
    impl->texts = std::move(*texts);
    auto vectors = createSpill(path, "vectors");
    if (!vectors)
    {
        return std::unexpected(vectors.error());
    }
    impl->vectors = std::move(*vectors);
    auto terms = createSpill(path, "terms");
    if (!terms)
    {
        return std::unexpected(terms.error());
    }
    impl->terms = std::move(*terms);
    return CorpusWriter{std::move(impl)};
}

std::expected<std::uint32_t, EngineError> CorpusWriter::addSource(SourceInfo source)
{
    if (impl_->failed || impl_->finished || impl_->sources.size() >= MAX_SOURCES || source.path.empty() ||
        source.path.size() > MAX_TEXT_BYTES || source.jsonField.size() > MAX_TEXT_BYTES ||
        source.path.find('\0') != std::string::npos || source.jsonField.find('\0') != std::string::npos)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const auto id = static_cast<std::uint32_t>(impl_->sources.size());
    impl_->sources.push_back(std::move(source));
    return id;
}

std::expected<std::uint32_t, EngineError> CorpusWriter::reserveDistinct(std::string_view text)
{
    if (impl_->failed || impl_->finished || impl_->uses.size() >= MAX_DISTINCT || text.size() > MAX_TEXT_BYTES ||
        impl_->distinctTexts.contains(std::string{text}))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (!writeU32(impl_->texts.output, static_cast<std::uint32_t>(text.size())) ||
        !writeRaw(impl_->texts.output, text.data(), text.size()))
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    const auto id = static_cast<std::uint32_t>(impl_->uses.size());
    impl_->distinctTexts.emplace(text);
    impl_->uses.push_back(0);
    impl_->textBytes += text.size();
    return id;
}

std::expected<void, EngineError> CorpusWriter::addVector(std::uint32_t id, std::span<const float> embedding)
{
    if (impl_->failed || impl_->finished || id != impl_->vectorCount || id >= impl_->uses.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (const auto valid = validateVector(embedding, impl_->dimensions); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!writeRaw(impl_->vectors.output, embedding.data(), embedding.size_bytes()))
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    ++impl_->vectorCount;
    return {};
}

std::expected<std::uint32_t, EngineError> CorpusWriter::addDistinct(std::string_view text,
                                                                    std::span<const float> embedding)
{
    if (impl_->vectorCount != impl_->uses.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (const auto valid = validateVector(embedding, impl_->dimensions); !valid)
    {
        return std::unexpected(valid.error());
    }
    const auto id = reserveDistinct(text);
    if (!id)
    {
        return std::unexpected(id.error());
    }
    if (const auto added = addVector(*id, embedding); !added)
    {
        return std::unexpected(added.error());
    }
    return *id;
}

static bool addLineRejected(const CorpusLine &line, std::span<const SourceInfo> sources,
                           std::span<const std::uint32_t> uses, std::span<const std::string> terms,
                           std::uint32_t lastSource, std::uint64_t lastLine, bool hasLastLine)
{
    if (line.sourceIndex >= sources.size() || line.lineNumber == 0 ||
        (line.distinctId != NO_DISTINCT && line.distinctId >= uses.size()) ||
        (line.storedText && line.storedText->size() > MAX_TEXT_BYTES) ||
        (line.distinctId == NO_DISTINCT && !terms.empty()) ||
        (hasLastLine &&
         (line.sourceIndex < lastSource || (line.sourceIndex == lastSource && line.lineNumber <= lastLine))))
    {
        return true;
    }
    for (const std::string &term : terms)
    {
        if (term.empty() || term.size() > MAX_TEXT_BYTES || term.find('\0') != std::string::npos)
        {
            return true;
        }
    }
    const SourceInfo &source = sources[line.sourceIndex];
    return (source.storedOnly && !line.storedText) ||
           (source.tracked && !rangeInside(line.byteOffset, line.byteLength, source.size));
}

static std::expected<void, EngineError> validateAddLines(std::span<const SourceInfo> sources,
                                                         std::span<const std::uint32_t> uses,
                                                         std::span<const CorpusLine> lines,
                                                         std::span<const std::vector<std::string>> lineTerms,
                                                         std::uint32_t &lastSource, std::uint64_t &lastLine,
                                                         bool &hasLastLine)
{
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const CorpusLine &line = lines[index];
        if (addLineRejected(line, sources, uses, lineTerms[index], lastSource, lastLine, hasLastLine))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        lastSource = line.sourceIndex;
        lastLine = line.lineNumber;
        hasLastLine = true;
    }
    return {};
}

static std::expected<void, EngineError> spillAddLines(std::ostream &linesOut, std::ostream &termsOut,
                                                      std::span<std::uint32_t> uses, std::uint64_t &storedBytes,
                                                      std::uint64_t &lineCount, std::span<const CorpusLine> lines,
                                                      std::span<const std::vector<std::string>> lineTerms)
{
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const CorpusLine &line = lines[index];
        const std::uint32_t flags = (line.arrayElement ? LINE_ARRAY_ELEMENT : 0U) |
                                    (line.storedText ? LINE_STORED_TEXT : 0U) |
                                    (line.distinctId == NO_DISTINCT ? LINE_SKIPPED : 0U);
        const std::string_view text = line.storedText.value_or(std::string_view{});
        if (!writeU64(linesOut, line.lineNumber) || !writeU64(linesOut, line.byteOffset) ||
            !writeU64(linesOut, line.byteLength) || !writeU32(linesOut, line.sourceIndex) ||
            !writeU32(linesOut, line.distinctId) || !writeU32(linesOut, flags) ||
            !writeU32(linesOut, static_cast<std::uint32_t>(text.size())) ||
            !writeRaw(linesOut, text.data(), text.size()))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        if (!writeU32(termsOut, static_cast<std::uint32_t>(lineTerms[index].size())))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        for (const std::string &term : lineTerms[index])
        {
            if (!writeU32(termsOut, static_cast<std::uint32_t>(term.size())) ||
                !writeRaw(termsOut, term.data(), term.size()))
            {
                return std::unexpected(EngineError::FileOpenFailure);
            }
        }
        if (line.distinctId != NO_DISTINCT)
        {
            ++uses[line.distinctId];
        }
        storedBytes += text.size();
        ++lineCount;
    }
    return {};
}

std::expected<void, EngineError> CorpusWriter::addLines(std::span<const CorpusLine> lines,
                                                        std::span<const std::vector<std::string>> lineTerms)
{
    if (impl_->failed || impl_->finished || lines.size() != lineTerms.size() ||
        lines.size() > MAX_LINES - impl_->lineCount)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    auto lastSource = impl_->lastSource;
    auto lastLine = impl_->lastLine;
    bool hasLastLine = impl_->hasLastLine;
    if (const auto valid = validateAddLines(impl_->sources, impl_->uses, lines, lineTerms, lastSource, lastLine,
                                            hasLastLine);
        !valid)
    {
        return std::unexpected(valid.error());
    }
    if (const auto spilled = spillAddLines(impl_->lines.output, impl_->terms.output, impl_->uses, impl_->storedBytes,
                                           impl_->lineCount, lines, lineTerms);
        !spilled)
    {
        impl_->failed = true;
        return std::unexpected(spilled.error());
    }
    impl_->lastSource = lastSource;
    impl_->lastLine = lastLine;
    impl_->hasLastLine = hasLastLine;
    return {};
}

struct CorpusOffsets
{
    std::uint64_t sourceOffset = 0;
    std::uint64_t lineOffset = 0;
    std::uint64_t distinctOffset = 0;
    std::uint64_t textOffset = 0;
    std::uint64_t storedOffset = 0;
    std::uint64_t lexicalOffset = 0;
    std::uint64_t lexicalBytes = 0;
    std::uint64_t matrixOffset = 0;
    std::uint64_t matrixBytes = 0;
};

static std::expected<std::vector<std::vector<std::string>>, EngineError> readSpilledTerms(std::istream &terms,
                                                                                          std::uint64_t lineCount)
{
    std::vector<std::vector<std::string>> lineTerms;
    lineTerms.reserve(static_cast<std::size_t>(lineCount));
    for (std::uint64_t index = 0; index < lineCount; ++index)
    {
        std::uint32_t count = 0;
        if (!readU32Stream(terms, count))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        std::vector<std::string> row;
        row.reserve(count);
        for (std::uint32_t termIndex = 0; termIndex < count; ++termIndex)
        {
            std::uint32_t length = 0;
            if (!readU32Stream(terms, length) || length > MAX_TEXT_BYTES)
            {
                return std::unexpected(EngineError::FileOpenFailure);
            }
            std::string term(length, '\0');
            if (length != 0 && !terms.read(term.data(), static_cast<std::streamsize>(length)))
            {
                return std::unexpected(EngineError::FileOpenFailure);
            }
            row.push_back(std::move(term));
        }
        lineTerms.push_back(std::move(row));
    }
    if (!atEnd(terms))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return lineTerms;
}

static CorpusOffsets corpusLayoutOffsets(std::span<const SourceInfo> sources, std::uint64_t lineCount,
                                         std::uint64_t distinctCount, std::uint64_t textBytes,
                                         std::uint64_t storedBytes, std::uint32_t dimensions,
                                         std::uint64_t policyBytes, std::uint64_t lexicalBytes)
{
    CorpusOffsets offsets;
    offsets.sourceOffset = alignUp(HEADER_BYTES + policyBytes, 8);
    offsets.lineOffset = offsets.sourceOffset + (sources.size() * SOURCE_RECORD_BYTES);
    for (const SourceInfo &source : sources)
    {
        offsets.lineOffset += source.path.size() + source.jsonField.size();
    }
    offsets.distinctOffset = offsets.lineOffset + (lineCount * LINE_RECORD_BYTES);
    offsets.textOffset = offsets.distinctOffset + (distinctCount * TEXT_RECORD_BYTES);
    offsets.storedOffset = offsets.textOffset + textBytes;
    offsets.lexicalOffset = offsets.storedOffset + storedBytes;
    offsets.lexicalBytes = lexicalBytes;
    offsets.matrixOffset = alignUp(offsets.lexicalOffset + offsets.lexicalBytes, 64);
    offsets.matrixBytes = distinctCount * static_cast<std::uint64_t>(dimensions) * sizeof(float);
    return offsets;
}

static bool writeCorpusHeader(std::ostream &output, std::string_view policy, std::uint32_t dimensions,
                              std::uint64_t sourceCount, std::uint64_t lineCount, std::uint64_t distinctCount,
                              const CorpusOffsets &offsets)
{
    return writeRaw(output, MAGIC.data(), MAGIC.size()) && writeU32(output, CORPUS_VERSION) &&
           writeU32(output, static_cast<std::uint32_t>(policy.size())) && writeU32(output, dimensions) &&
           writeU64(output, sourceCount) && writeU64(output, lineCount) && writeU64(output, offsets.sourceOffset) &&
           writeU64(output, offsets.lineOffset) && writeU64(output, offsets.matrixOffset) &&
           writeU64(output, distinctCount) && writeU64(output, offsets.distinctOffset) &&
           writeU64(output, offsets.textOffset) && writeU64(output, offsets.storedOffset) &&
           writeU64(output, offsets.lexicalOffset) && writeU64(output, offsets.lexicalBytes) &&
           writeU64(output, HEADER_BYTES) && writeU64(output, 0) && writeU64(output, 0) &&
           writeRaw(output, policy.data(), policy.size()) &&
           writePadding(output, offsets.sourceOffset - HEADER_BYTES - policy.size());
}

static bool writeCorpusSources(std::ostream &output, std::span<const SourceInfo> sources, std::uint64_t sourceOffset)
{
    std::uint64_t sourceCursor = sourceOffset + (sources.size() * SOURCE_RECORD_BYTES);
    for (const SourceInfo &source : sources)
    {
        const std::uint32_t flags =
            (source.tracked ? SOURCE_TRACKED : 0U) | (source.storedOnly ? SOURCE_STORED_ONLY : 0U);
        if (!writeU64(output, sourceCursor) || !writeU32(output, static_cast<std::uint32_t>(source.path.size())) ||
            !writeU32(output, flags) || !writeU64(output, source.size) ||
            !writeU64(output, std::bit_cast<std::uint64_t>(source.modifiedTime)) ||
            !writeU64(output, sourceCursor + source.path.size()) ||
            !writeU32(output, static_cast<std::uint32_t>(source.jsonField.size())) || !writeU32(output, 0))
        {
            return false;
        }
        sourceCursor += source.path.size() + source.jsonField.size();
    }
    for (const SourceInfo &source : sources)
    {
        if (!writeRaw(output, source.path.data(), source.path.size()) ||
            !writeRaw(output, source.jsonField.data(), source.jsonField.size()))
        {
            return false;
        }
    }
    return true;
}

static bool writeCorpusLineTable(std::istream &lines, std::ostream &output, std::uint64_t lineCount,
                                 std::uint64_t &storedCursor)
{
    SpilledLine line;
    for (std::uint64_t index = 0; index < lineCount; ++index)
    {
        if (!readSpilledLine(lines, line))
        {
            return false;
        }
        const bool hasText = (line.flags & LINE_STORED_TEXT) != 0;
        if (!writeU64(output, line.lineNumber) || !writeU64(output, line.byteOffset) ||
            !writeU64(output, line.byteLength) || !writeU32(output, line.sourceIndex) ||
            !writeU32(output, line.distinctId) || !writeU64(output, hasText ? storedCursor : 0) ||
            !writeU32(output, static_cast<std::uint32_t>(line.storedText.size())) || !writeU32(output, line.flags) ||
            !writeU64(output, 0))
        {
            return false;
        }
        storedCursor += line.storedText.size();
    }
    return atEnd(lines);
}

static bool writeCorpusDistinctTexts(std::istream &texts, std::istream &lines, std::ostream &output,
                                     std::size_t distinctCount, std::uint64_t textOffset, std::uint64_t storedOffset)
{
    std::uint64_t textCursor = textOffset;
    std::string text;
    for (std::size_t index = 0; index < distinctCount; ++index)
    {
        if (!readSpilledText(texts, text) || !writeU64(output, textCursor) ||
            !writeU32(output, static_cast<std::uint32_t>(text.size())) || !writeU32(output, 0))
        {
            return false;
        }
        textCursor += text.size();
    }
    if (!atEnd(texts) || textCursor != storedOffset || !rewind(texts))
    {
        return false;
    }
    for (std::size_t index = 0; index < distinctCount; ++index)
    {
        if (!readSpilledText(texts, text) || !writeRaw(output, text.data(), text.size()))
        {
            return false;
        }
    }
    return atEnd(texts) && rewind(lines);
}

static bool writeCorpusStoredAndMatrix(std::istream &lines, std::istream &vectors, std::ostream &output,
                                       std::span<const std::byte> lexical, std::uint64_t lineCount,
                                       std::uint64_t storedCursor, std::uint64_t storedOffset,
                                       std::uint64_t storedBytes, std::uint64_t lexicalOffset,
                                       std::uint64_t lexicalBytes, std::uint64_t matrixOffset,
                                       std::uint64_t matrixBytes)
{
    SpilledLine line;
    for (std::uint64_t index = 0; index < lineCount; ++index)
    {
        if (!readSpilledLine(lines, line) || !writeRaw(output, line.storedText.data(), line.storedText.size()))
        {
            return false;
        }
    }
    return atEnd(lines) && storedCursor == storedOffset + storedBytes &&
           writeRaw(output, lexical.data(), lexical.size()) &&
           writePadding(output, matrixOffset - (lexicalOffset + lexicalBytes)) &&
           copyBytes(vectors, output, matrixBytes);
}

std::expected<void, EngineError> CorpusWriter::finish()
{
    if (impl_->failed || impl_->finished || impl_->vectorCount != impl_->uses.size() ||
        std::ranges::any_of(impl_->uses, [](std::uint32_t uses) { return uses == 0; }))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    impl_->lines.output.close();
    impl_->texts.output.close();
    impl_->vectors.output.close();
    impl_->terms.output.close();
    if (!impl_->lines.output || !impl_->texts.output || !impl_->vectors.output || !impl_->terms.output)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    std::ifstream lines(impl_->lines.path, std::ios::binary);
    std::ifstream texts(impl_->texts.path, std::ios::binary);
    std::ifstream vectors(impl_->vectors.path, std::ios::binary);
    std::ifstream terms(impl_->terms.path, std::ios::binary);
    if (!lines || !texts || !vectors || !terms)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    auto lineTerms = readSpilledTerms(terms, impl_->lineCount);
    if (!lineTerms)
    {
        impl_->failed = true;
        return std::unexpected(lineTerms.error());
    }
    const std::vector<std::byte> lexical = encodeLexicalSection(*lineTerms);
    if (lexical.size() > MAX_LEXICAL_BYTES)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::CorruptDatabase);
    }
    auto temporary = createSpill(impl_->destination, "tmp");
    if (!temporary)
    {
        impl_->failed = true;
        return std::unexpected(temporary.error());
    }
    impl_->temporary = temporary->path;
    std::ofstream &output = temporary->output;
    const auto fail = [&]() -> std::expected<void, EngineError>
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    };

    const std::string policy = serializePolicy(impl_->policy);
    const CorpusOffsets offsets =
        corpusLayoutOffsets(impl_->sources, impl_->lineCount, impl_->uses.size(), impl_->textBytes, impl_->storedBytes,
                            impl_->dimensions, policy.size(), lexical.size());
    if (!writeCorpusHeader(output, policy, impl_->dimensions, impl_->sources.size(), impl_->lineCount,
                           impl_->uses.size(), offsets))
    {
        return fail();
    }
    if (!writeCorpusSources(output, impl_->sources, offsets.sourceOffset))
    {
        return fail();
    }
    std::uint64_t storedCursor = offsets.storedOffset;
    if (!writeCorpusLineTable(lines, output, impl_->lineCount, storedCursor))
    {
        return fail();
    }
    if (!writeCorpusDistinctTexts(texts, lines, output, impl_->uses.size(), offsets.textOffset, offsets.storedOffset))
    {
        return fail();
    }
    if (!writeCorpusStoredAndMatrix(lines, vectors, output, lexical, impl_->lineCount, storedCursor,
                                    offsets.storedOffset, impl_->storedBytes, offsets.lexicalOffset,
                                    offsets.lexicalBytes, offsets.matrixOffset, offsets.matrixBytes))
    {
        return fail();
    }
    output.close();
    if (!output || !sealCorpus(impl_->temporary))
    {
        return fail();
    }
    if (const auto replaced = nesso::platform::replaceFileAtomically(impl_->temporary, impl_->destination); !replaced)
    {
        return fail();
    }
    impl_->temporary.clear();
    impl_->finished = true;
    return {};
}

struct MappedCorpus::Impl
{
    nesso::platform::MappedFile file;
    CorpusPolicy policy;
    std::uint32_t dimensions = 0;
    std::vector<SourceInfo> sources;
    std::vector<CorpusLine> lines;
    std::vector<std::string_view> texts;
    std::span<const float> matrix;
    std::vector<std::string> lexicalTermStorage;
    std::vector<std::string_view> lexicalDictionary;
    std::vector<LexicalLinePostings> lexicalPostings;
    LexicalStatus lexicalStatus = LexicalStatus::Missing;
    std::string corpusLexicalTermsVersion;
};

MappedCorpus::MappedCorpus(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MappedCorpus::~MappedCorpus() = default;
MappedCorpus::MappedCorpus(MappedCorpus &&) noexcept = default;
MappedCorpus &MappedCorpus::operator=(MappedCorpus &&) noexcept = default;

const CorpusPolicy &MappedCorpus::policy() const noexcept { return impl_->policy; }
std::uint32_t MappedCorpus::dimensions() const noexcept { return impl_->dimensions; }
std::span<const SourceInfo> MappedCorpus::sources() const noexcept { return impl_->sources; }
std::span<const CorpusLine> MappedCorpus::lines() const noexcept { return impl_->lines; }
std::span<const std::string_view> MappedCorpus::texts() const noexcept { return impl_->texts; }
std::span<const float> MappedCorpus::matrix() const noexcept { return impl_->matrix; }
std::span<const std::string_view> MappedCorpus::lexicalDictionary() const noexcept { return impl_->lexicalDictionary; }
std::span<const LexicalLinePostings> MappedCorpus::lexicalPostings() const noexcept { return impl_->lexicalPostings; }
LexicalStatus MappedCorpus::lexicalStatus() const noexcept { return impl_->lexicalStatus; }
std::string_view MappedCorpus::corpusLexicalTermsVersion() const noexcept { return impl_->corpusLexicalTermsVersion; }

std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path,
                                                       std::uint32_t *observedVersion)
{
    auto mapped = nesso::platform::MappedFile::open(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error() == nesso::platform::FileError::OpenFailure
                                   ? EngineError::FileOpenFailure
                                   : EngineError::CorruptDatabase);
    }
    const std::span<const std::byte> bytes = mapped->bytes();
    std::uint32_t version = 0;
    if (bytes.size() < 8 || std::memcmp(bytes.data(), MAGIC.data(), MAGIC.size()) != 0 || !readU32(bytes, 4, version))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (observedVersion != nullptr)
    {
        *observedVersion = version;
    }
    if (version != CORPUS_VERSION)
    {
        return std::unexpected(EngineError::UnsupportedVersion);
    }
    if (bytes.size() < HEADER_BYTES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const CorpusChecksum checksum = corpusChecksum(bytes);
    if (!std::ranges::equal(checksum, bytes.subspan(CORPUS_CHECKSUM_OFFSET, CORPUS_CHECKSUM_BYTES)))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    Layout layout;
    if (bytes.size() < HEADER_BYTES || !readU32(bytes, 8, layout.policyBytes) ||
        !readU32(bytes, 12, layout.dimensions) || !readU64(bytes, 16, layout.sourceCount) ||
        !readU64(bytes, 24, layout.lineCount) || !readU64(bytes, 32, layout.sourceOffset) ||
        !readU64(bytes, 40, layout.lineOffset) || !readU64(bytes, 48, layout.matrixOffset) ||
        !readU64(bytes, 56, layout.distinctCount) || !readU64(bytes, 64, layout.distinctOffset) ||
        !readU64(bytes, 72, layout.textOffset) || !readU64(bytes, 80, layout.storedOffset) ||
        !readU64(bytes, 88, layout.lexicalOffset) || !readU64(bytes, 96, layout.lexicalBytes) ||
        !readU64(bytes, 104, layout.policyOffset))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    // Pre-postings v3 used lexicalOffset=lexicalBytes=0. Those corpora stay
    // readable for dense search; new writers always emit a non-empty section.
    const bool hasLexical = layout.lexicalOffset != 0 || layout.lexicalBytes != 0;
    if (layout.policyBytes > MAX_POLICY_BYTES || layout.dimensions > MAX_DIMENSIONS ||
        layout.sourceCount > MAX_SOURCES || layout.lineCount > MAX_LINES || layout.distinctCount > MAX_DISTINCT ||
        (layout.distinctCount > 0 && layout.dimensions == 0) || layout.lexicalBytes > MAX_LEXICAL_BYTES ||
        layout.policyOffset != HEADER_BYTES || layout.sourceOffset != alignUp(HEADER_BYTES + layout.policyBytes, 8) ||
        layout.sourceOffset > layout.lineOffset || layout.lineOffset > layout.distinctOffset ||
        layout.distinctOffset > layout.textOffset || layout.textOffset > layout.storedOffset ||
        layout.storedOffset > layout.matrixOffset || layout.matrixOffset > bytes.size() ||
        layout.matrixOffset % 64 != 0 ||
        layout.sourceCount > (layout.lineOffset - layout.sourceOffset) / SOURCE_RECORD_BYTES ||
        layout.distinctOffset - layout.lineOffset != layout.lineCount * LINE_RECORD_BYTES ||
        layout.textOffset - layout.distinctOffset != layout.distinctCount * TEXT_RECORD_BYTES ||
        !rangeInside(layout.policyOffset, layout.policyBytes, layout.sourceOffset) ||
        !isZero(bytes, HEADER_BYTES + layout.policyBytes, layout.sourceOffset) ||
        (hasLexical && (layout.storedOffset > layout.lexicalOffset || layout.lexicalOffset > layout.matrixOffset ||
                        !rangeInside(layout.lexicalOffset, layout.lexicalBytes, layout.matrixOffset) ||
                        layout.lexicalOffset + layout.lexicalBytes > layout.matrixOffset ||
                        !isZero(bytes, layout.lexicalOffset + layout.lexicalBytes, layout.matrixOffset))))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const std::uint64_t matrixBytes = layout.distinctCount * layout.dimensions * sizeof(float);
    if (!rangeInside(layout.matrixOffset, matrixBytes, bytes.size()) ||
        layout.matrixOffset + matrixBytes != bytes.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    auto policy = parsePolicy(viewBytes(bytes, layout.policyOffset, layout.policyBytes));
    if (!policy)
    {
        return std::unexpected(policy.error());
    }
    const auto dimensions = validatePolicy(*policy);
    if (!dimensions || *dimensions != layout.dimensions)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    auto impl = std::make_unique<MappedCorpus::Impl>();
    impl->policy = std::move(*policy);
    impl->dimensions = layout.dimensions;
    impl->sources.reserve(static_cast<std::size_t>(layout.sourceCount));
    std::uint64_t sourceCursor = layout.sourceOffset + (layout.sourceCount * SOURCE_RECORD_BYTES);
    for (std::uint64_t index = 0; index < layout.sourceCount; ++index)
    {
        const std::uint64_t record = layout.sourceOffset + (index * SOURCE_RECORD_BYTES);
        std::uint64_t pathOffset = 0;
        std::uint32_t pathLength = 0;
        std::uint32_t flags = 0;
        std::uint64_t size = 0;
        std::uint64_t modified = 0;
        std::uint64_t fieldOffset = 0;
        std::uint32_t fieldLength = 0;
        std::uint32_t reserved = 0;
        if (!readU64(bytes, record, pathOffset) || !readU32(bytes, record + 8, pathLength) ||
            !readU32(bytes, record + 12, flags) || !readU64(bytes, record + 16, size) ||
            !readU64(bytes, record + 24, modified) || !readU64(bytes, record + 32, fieldOffset) ||
            !readU32(bytes, record + 40, fieldLength) || !readU32(bytes, record + 44, reserved) || pathLength == 0 ||
            pathLength > MAX_TEXT_BYTES || fieldLength > MAX_TEXT_BYTES ||
            (flags & ~(SOURCE_TRACKED | SOURCE_STORED_ONLY)) != 0 || reserved != 0 || pathOffset != sourceCursor ||
            !rangeInside(pathOffset, pathLength, layout.lineOffset) || fieldOffset != pathOffset + pathLength ||
            !rangeInside(fieldOffset, fieldLength, layout.lineOffset))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        SourceInfo source{.path = std::string{viewBytes(bytes, pathOffset, pathLength)},
                          .size = size,
                          .modifiedTime = std::bit_cast<std::int64_t>(modified),
                          .tracked = (flags & SOURCE_TRACKED) != 0,
                          .jsonField = std::string{viewBytes(bytes, fieldOffset, fieldLength)},
                          .storedOnly = (flags & SOURCE_STORED_ONLY) != 0};
        if (source.path.find('\0') != std::string::npos || source.jsonField.find('\0') != std::string::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        impl->sources.push_back(std::move(source));
        sourceCursor = fieldOffset + fieldLength;
    }
    if (sourceCursor != layout.lineOffset)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    impl->texts.reserve(static_cast<std::size_t>(layout.distinctCount));
    std::unordered_set<std::string_view> seenTexts;
    std::uint64_t textCursor = layout.textOffset;
    for (std::uint64_t index = 0; index < layout.distinctCount; ++index)
    {
        const std::uint64_t record = layout.distinctOffset + (index * TEXT_RECORD_BYTES);
        std::uint64_t offset = 0;
        std::uint32_t length = 0;
        std::uint32_t reserved = 0;
        if (!readU64(bytes, record, offset) || !readU32(bytes, record + 8, length) ||
            !readU32(bytes, record + 12, reserved) || length > MAX_TEXT_BYTES || reserved != 0 ||
            offset != textCursor || !rangeInside(offset, length, layout.storedOffset))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const std::string_view text = viewBytes(bytes, offset, length);
        if (!seenTexts.insert(text).second)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        impl->texts.push_back(text);
        textCursor += length;
    }
    if (textCursor != layout.storedOffset)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    impl->lines.reserve(static_cast<std::size_t>(layout.lineCount));
    std::vector<bool> used(static_cast<std::size_t>(layout.distinctCount));
    std::uint64_t storedCursor = layout.storedOffset;
    for (std::uint64_t index = 0; index < layout.lineCount; ++index)
    {
        const std::uint64_t record = layout.lineOffset + (index * LINE_RECORD_BYTES);
        CorpusLine line;
        std::uint64_t textOffset = 0;
        std::uint32_t textLength = 0;
        std::uint32_t flags = 0;
        std::uint64_t reserved = 0;
        if (!readU64(bytes, record, line.lineNumber) || !readU64(bytes, record + 8, line.byteOffset) ||
            !readU64(bytes, record + 16, line.byteLength) || !readU32(bytes, record + 24, line.sourceIndex) ||
            !readU32(bytes, record + 28, line.distinctId) || !readU64(bytes, record + 32, textOffset) ||
            !readU32(bytes, record + 40, textLength) || !readU32(bytes, record + 44, flags) ||
            !readU64(bytes, record + 48, reserved) || line.lineNumber == 0 ||
            line.sourceIndex >= impl->sources.size() ||
            (flags & ~(LINE_ARRAY_ELEMENT | LINE_STORED_TEXT | LINE_SKIPPED)) != 0 || reserved != 0 ||
            ((flags & LINE_SKIPPED) != 0) != (line.distinctId == NO_DISTINCT) ||
            (line.distinctId != NO_DISTINCT && line.distinctId >= layout.distinctCount) || textLength > MAX_TEXT_BYTES)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        if (!impl->lines.empty())
        {
            const CorpusLine &previous = impl->lines.back();
            if (line.sourceIndex < previous.sourceIndex ||
                (line.sourceIndex == previous.sourceIndex && line.lineNumber <= previous.lineNumber))
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
        }
        const SourceInfo &source = impl->sources[line.sourceIndex];
        if ((source.tracked && !rangeInside(line.byteOffset, line.byteLength, source.size)) ||
            (source.storedOnly && (flags & LINE_STORED_TEXT) == 0))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const std::uint64_t storedLimit = hasLexical ? layout.lexicalOffset : layout.matrixOffset;
        if ((flags & LINE_STORED_TEXT) != 0)
        {
            if (textOffset != storedCursor || !rangeInside(textOffset, textLength, storedLimit))
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
            line.storedText = viewBytes(bytes, textOffset, textLength);
            storedCursor += textLength;
        }
        else if (textOffset != 0 || textLength != 0)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        if (line.distinctId != NO_DISTINCT)
        {
            used[line.distinctId] = true;
        }
        line.arrayElement = (flags & LINE_ARRAY_ELEMENT) != 0;
        impl->lines.push_back(line);
    }
    if (std::ranges::any_of(used, [](bool value) { return !value; }) ||
        (hasLexical ? storedCursor != layout.lexicalOffset : !isZero(bytes, storedCursor, layout.matrixOffset)))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (hasLexical)
    {
        if (const auto decoded = decodeLexicalSection(bytes.subspan(static_cast<std::size_t>(layout.lexicalOffset),
                                                                    static_cast<std::size_t>(layout.lexicalBytes)),
                                                      layout.lineCount, impl->lexicalTermStorage,
                                                      impl->lexicalDictionary, impl->lexicalPostings);
            !decoded)
        {
            return std::unexpected(decoded.error());
        }
    }
    if (impl->policy.contains("lexical.terms"))
    {
        impl->corpusLexicalTermsVersion = impl->policy.at("lexical.terms");
    }
    impl->lexicalStatus = lexicalStatusOf(hasLexical, impl->policy);

    impl->file = std::move(*mapped);
    const auto stable = impl->file.bytes();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    impl->matrix = {reinterpret_cast<const float *>(stable.data() + layout.matrixOffset),
                    layout.distinctCount * layout.dimensions};
    return MappedCorpus{std::move(impl)};
}

std::expected<std::vector<CorpusChunk>, EngineError> readCorpusFile(const std::filesystem::path &path)
{
    auto mapped = mapCorpusFile(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error());
    }
    std::vector<CorpusChunk> chunks;
    for (const CorpusLine &line : mapped->lines())
    {
        if (line.distinctId == NO_DISTINCT)
        {
            continue;
        }
        const std::size_t dimensions = mapped->dimensions();
        const auto embedding =
            mapped->matrix().subspan(static_cast<std::size_t>(line.distinctId) * dimensions, dimensions);
        chunks.push_back(
            CorpusChunk{.chunk = {.text = std::string{line.storedText.value_or(mapped->texts()[line.distinctId])},
                                  .lineNumber = line.lineNumber,
                                  .source = mapped->sources()[line.sourceIndex].path,
                                  .byteOffset = line.byteOffset,
                                  .arrayElement = line.arrayElement,
                                  .byteLength = line.byteLength},
                        .embedding = {embedding.begin(), embedding.end()}});
    }
    return chunks;
}

} // namespace nesso::core
