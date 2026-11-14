// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/archive_source.hpp>
#include <parser/gzip_reader.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <ios>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <utility>

namespace nesso::parser
{

static constexpr size_t BINARY_PROBE_BYTES = 4096;

static std::string extensionLower(const std::filesystem::path &path)
{
    std::string extension = path.extension().string();
    for (char &character : extension)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return extension;
}

static bool containsNul(std::string_view bytes) { return bytes.find('\0') != std::string_view::npos; }

static std::expected<std::ifstream, ParseError> openChecked(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    std::array<char, BINARY_PROBE_BYTES> buffer{};
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (input.bad() || (input.fail() && !input.eof()))
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    if (containsNul(std::string_view(buffer.data(), static_cast<size_t>(input.gcount()))))
    {
        return std::unexpected(ParseError::BinaryFile);
    }
    input.clear();
    input.seekg(0);
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    return input;
}

static std::expected<std::string, ParseError> readStdinChecked()
{
    std::array<char, BINARY_PROBE_BYTES> buffer{};
    std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (std::cin.bad() || (std::cin.fail() && !std::cin.eof()))
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    std::string bytes(buffer.data(), static_cast<size_t>(std::cin.gcount()));
    if (containsNul(bytes))
    {
        return std::unexpected(ParseError::BinaryFile);
    }
    while (std::cin)
    {
        std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        bytes.append(buffer.data(), static_cast<size_t>(std::cin.gcount()));
    }
    if (std::cin.bad() || !std::cin.eof())
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    return bytes;
}

static bool stripCarriageReturn(std::string &line)
{
    if (line.empty() || line.back() != '\r')
    {
        return false;
    }
    line.pop_back();
    return true;
}

static bool isBlank(std::string_view text) { return text.find_first_not_of(" \t\n\r\f\v") == std::string_view::npos; }

enum class SkipReason : uint8_t
{
    Empty,
    Malformed,
    MissingField
};

static void countSkip(ParseStats *stats, SkipReason reason)
{
    if (stats == nullptr)
    {
        return;
    }
    ++stats->skippedLines;
    switch (reason)
    {
    case SkipReason::Empty:
        ++stats->emptyLines;
        break;
    case SkipReason::Malformed:
        ++stats->malformedLines;
        break;
    case SkipReason::MissingField:
        ++stats->missingFieldLines;
        break;
    }
}

static void countTruncation(ParseStats *stats)
{
    if (stats != nullptr)
    {
        ++stats->truncatedLines;
    }
}

// Bytes in the code point at `index`. A lead byte with its continuation bytes counts as one code point. Any other byte
// counts on its own, as the tokenizer replaces it with U+FFFD.
static size_t codePointBytes(std::string_view text, size_t index)
{
    const auto lead = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index]));
    size_t length = 1;
    if ((lead & 0xE0U) == 0xC0U)
    {
        length = 2;
    }
    else if ((lead & 0xF0U) == 0xE0U)
    {
        length = 3;
    }
    else if ((lead & 0xF8U) == 0xF0U)
    {
        length = 4;
    }
    if (length == 1 || length > text.size() - index)
    {
        return 1;
    }
    for (size_t offset = 1; offset < length; ++offset)
    {
        const auto byte = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + offset]));
        if ((byte & 0xC0U) != 0x80U)
        {
            return 1;
        }
    }
    return length;
}

static void limitValue(std::string &text, size_t maxCodePoints, ParseStats *stats)
{
    if (text.size() <= maxCodePoints)
    {
        return;
    }
    size_t end = 0;
    for (size_t count = 0; count < maxCodePoints && end < text.size(); ++count)
    {
        end += codePointBytes(text, end);
    }
    if (end < text.size())
    {
        text.resize(end);
        countTruncation(stats);
    }
}

static void advanceOffset(uint64_t &byteOffset, size_t fileBytes, bool delimiterPending)
{
    byteOffset += fileBytes;
    if (delimiterPending)
    {
        ++byteOffset;
    }
}

static std::expected<void, ParseError> readLogStream(std::istream &input, size_t maxLineLength, ParseStats *stats,
                                                     const std::function<void(ParsedChunk)> &consume)
{
    std::string line;
    uint64_t lineNumber = 0;
    uint64_t byteOffset = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        const bool hadCarriageReturn = stripCarriageReturn(line);
        const size_t fileBytes = line.size() + (hadCarriageReturn ? 1U : 0U);
        if (isBlank(line))
        {
            countSkip(stats, SkipReason::Empty);
            consume(ParsedChunk{.text = {},
                                .lineNumber = lineNumber,
                                .byteOffset = byteOffset,
                                .skipped = true,
                                .byteLength = fileBytes,
                                .source = {}});
        }
        else
        {
            limitValue(line, maxLineLength, stats);
            consume(ParsedChunk{.text = line,
                                .lineNumber = lineNumber,
                                .byteOffset = byteOffset,
                                .byteLength = fileBytes,
                                .source = {}});
        }
        advanceOffset(byteOffset, fileBytes, !input.eof());
    }
    if (input.bad() || !input.eof())
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    return {};
}

static void consumeSkipped(ParseStats *stats, SkipReason reason, ParsedChunk loc,
                           const std::function<void(ParsedChunk)> &consume)
{
    countSkip(stats, reason);
    loc.text.clear();
    loc.skipped = true;
    consume(std::move(loc));
}

static void appendJsonField(const nlohmann::json &document, const std::string &field, ParsedChunk loc,
                            size_t maxLineLength, ParseStats *stats, const std::function<void(ParsedChunk)> &consume)
{
    if (!document.contains(field) || !document.at(field).is_string())
    {
        consumeSkipped(stats, SkipReason::MissingField, std::move(loc), consume);
        return;
    }
    std::string text = document.at(field).get<std::string>();
    if (isBlank(text))
    {
        consumeSkipped(stats, SkipReason::Empty, std::move(loc), consume);
        return;
    }
    limitValue(text, maxLineLength, stats);
    loc.text = std::move(text);
    consume(std::move(loc));
}

static std::expected<void, ParseError> readJsonLinesStream(std::istream &input, ParseStats *stats,
                                                           const std::string &field, size_t maxLineLength,
                                                           const std::function<void(ParsedChunk)> &consume)
{
    std::string line;
    uint64_t lineNumber = 0;
    uint64_t byteOffset = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        const bool hadCarriageReturn = stripCarriageReturn(line);
        const size_t fileBytes = line.size() + (hadCarriageReturn ? 1U : 0U);
        if (isBlank(line))
        {
            consumeSkipped(stats, SkipReason::Empty,
                           {.text = {}, .lineNumber = lineNumber, .byteOffset = byteOffset, .byteLength = fileBytes},
                           consume);
        }
        else
        {
            nlohmann::json document;
            bool parsed = true;
            try
            {
                document = nlohmann::json::parse(line);
            }
            catch (const nlohmann::json::exception &)
            {
                consumeSkipped(
                    stats, SkipReason::Malformed,
                    {.text = {}, .lineNumber = lineNumber, .byteOffset = byteOffset, .byteLength = fileBytes}, consume);
                parsed = false;
            }
            if (parsed)
            {
                appendJsonField(
                    document, field,
                    {.text = {}, .lineNumber = lineNumber, .byteOffset = byteOffset, .byteLength = fileBytes},
                    maxLineLength, stats, consume);
            }
        }
        advanceOffset(byteOffset, fileBytes, !input.eof());
    }
    if (input.bad() || !input.eof())
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    return {};
}

static std::expected<void, ParseError> readJsonDocumentStream(std::istream &input, ParseStats *stats,
                                                              const std::string &field, size_t maxLineLength,
                                                              const std::function<void(ParsedChunk)> &consume)
{
    nlohmann::json document;
    try
    {
        document = nlohmann::json::parse(input);
    }
    catch (const std::ios_base::failure &)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    catch (const nlohmann::json::exception &)
    {
        if (input.bad())
        {
            return std::unexpected(ParseError::FileOpenFailure);
        }
        countSkip(stats, SkipReason::Malformed);
        consume(ParsedChunk{.text = {}, .lineNumber = 1, .skipped = true, .source = {}});
        return {};
    }
    if (document.is_array())
    {
        uint64_t index = 0;
        for (const auto &entry : document)
        {
            ++index;
            appendJsonField(entry, field, {.text = {}, .lineNumber = index, .arrayElement = true}, maxLineLength, stats,
                            consume);
        }
        return {};
    }
    appendJsonField(document, field, {.text = {}, .lineNumber = 1}, maxLineLength, stats, consume);
    return {};
}

static std::expected<void, ParseError> readJsonStream(std::istream &input, bool jsonLines, ParseStats *stats,
                                                      std::string_view jsonField, size_t maxLineLength,
                                                      const std::function<void(ParsedChunk)> &consume)
{
    const std::string field{jsonField};
    if (jsonLines)
    {
        return readJsonLinesStream(input, stats, field, maxLineLength, consume);
    }
    return readJsonDocumentStream(input, stats, field, maxLineLength, consume);
}

static ParseError gzipParseError(GzipError error)
{
    switch (error)
    {
    case GzipError::OpenFailure:
        return ParseError::FileOpenFailure;
    case GzipError::Corrupt:
        return ParseError::CorruptGzip;
    }
    std::unreachable();
}

static ParseError archiveParseError(const ArchiveFailure &error)
{
    switch (error.code)
    {
    case ArchiveError::OpenFailure:
        return ParseError::FileOpenFailure;
    case ArchiveError::Corrupt:
        return ParseError::CorruptArchive;
    case ArchiveError::CorruptGzip:
        return ParseError::CorruptGzip;
    case ArchiveError::Encrypted:
        return ParseError::EncryptedArchive;
    case ArchiveError::EntryTooLarge:
        return ParseError::ArchiveEntryLimit;
    case ArchiveError::TotalTooLarge:
        return ParseError::ArchiveTotalLimit;
    case ArchiveError::TooManyEntries:
        return ParseError::ArchiveCountLimit;
    case ArchiveError::RatioExceeded:
        return ParseError::ArchiveRatioLimit;
    }
    std::unreachable();
}

// `.gz` is one layer. The extension under it selects lines, JSON, or JSONL.
static std::string formatExtension(const std::filesystem::path &path)
{
    if (isGzipPath(path))
    {
        return extensionLower(path.stem());
    }
    return extensionLower(path);
}

static std::expected<void, ParseError> dispatchStream(std::istream &input, const std::string &extension,
                                                      size_t maxLineLength, ParseStats *stats,
                                                      std::string_view jsonField,
                                                      const std::function<void(ParsedChunk)> &consume)
{
    if (extension == ".json" || extension == ".jsonl")
    {
        return readJsonStream(input, extension == ".jsonl", stats, jsonField, maxLineLength, consume);
    }
    return readLogStream(input, maxLineLength, stats, consume);
}

static std::expected<const nlohmann::json *, ParseError> selectJsonRecordEntry(const nlohmann::json &document,
                                                                               uint64_t lineNumber, bool arrayElement)
{
    if (arrayElement)
    {
        if (!document.is_array() || lineNumber == 0 || lineNumber > document.size())
        {
            return std::unexpected(ParseError::UnsupportedFormat);
        }
        return &document.at(lineNumber - 1);
    }
    if (document.is_array() || lineNumber != 1)
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    return &document;
}

static std::expected<std::string, ParseError> readOpenedJsonRecord(std::istream &input, const ParsedChunk &key,
                                                                   std::string_view jsonField, size_t maxLineLength)
{
    if (key.byteOffset != 0 || key.byteLength != 0)
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    try
    {
        const auto document = nlohmann::json::parse(input);
        const auto entry = selectJsonRecordEntry(document, key.lineNumber, key.arrayElement);
        if (!entry)
        {
            return std::unexpected(entry.error());
        }
        const std::string field{jsonField};
        if (!(*entry)->contains(field) || !(*entry)->at(field).is_string())
        {
            return std::unexpected(ParseError::UnsupportedFormat);
        }
        std::string text = (*entry)->at(field).get<std::string>();
        limitValue(text, maxLineLength, nullptr);
        return text;
    }
    catch (const std::ios_base::failure &)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    catch (const nlohmann::json::exception &)
    {
        return std::unexpected(input.bad() ? ParseError::FileOpenFailure : ParseError::UnsupportedFormat);
    }
}

static std::expected<void, ParseError> decodeJsonlField(std::string &text, std::string_view jsonField)
{
    try
    {
        const auto document = nlohmann::json::parse(text);
        const std::string field{jsonField};
        if (!document.contains(field) || !document.at(field).is_string())
        {
            return std::unexpected(ParseError::UnsupportedFormat);
        }
        text = document.at(field).get<std::string>();
        return {};
    }
    catch (const nlohmann::json::exception &)
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
}

struct ByteRange
{
    uint64_t offset = 0;
    uint64_t length = 0;
};

static std::expected<std::string, ParseError> readOpenedRangedRecord(std::istream &input, const std::string &extension,
                                                                     const ByteRange &range, std::string_view jsonField,
                                                                     size_t maxLineLength)
{
    input.seekg(static_cast<std::streamoff>(range.offset));
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    uint64_t readLength = range.length;
    if (extension != ".jsonl" && maxLineLength <= (std::numeric_limits<size_t>::max() - 4) / 4)
    {
        readLength = std::min<uint64_t>(range.length, (maxLineLength * 4) + 4);
    }
    if (!std::in_range<std::streamsize>(readLength) || !std::in_range<size_t>(readLength))
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    std::string text(static_cast<size_t>(readLength), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!std::cmp_equal(input.gcount(), text.size()))
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    if (extension == ".jsonl")
    {
        if (const auto decoded = decodeJsonlField(text, jsonField); !decoded)
        {
            return std::unexpected(decoded.error());
        }
    }
    else if (readLength == range.length)
    {
        static_cast<void>(stripCarriageReturn(text));
    }
    limitValue(text, maxLineLength, nullptr);
    return text;
}

static std::expected<std::string, ParseError> readOpenedRecord(std::istream &input, const std::string &extension,
                                                               const ParsedChunk &key, std::string_view jsonField,
                                                               size_t maxLineLength)
{
    if (!input || key.byteOffset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    if (key.arrayElement && extension != ".json")
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    if (extension == ".json")
    {
        return readOpenedJsonRecord(input, key, jsonField, maxLineLength);
    }
    return readOpenedRangedRecord(input, extension, {.offset = key.byteOffset, .length = key.byteLength}, jsonField,
                                  maxLineLength);
}

std::expected<void, ParseError> Chunker::forEach(const std::filesystem::path &path, size_t maxLineLength,
                                                 ParseStats *stats, std::string_view jsonField,
                                                 const std::function<void(ParsedChunk)> &consume,
                                                 const ArchiveLimits &limits)
{
    return forEachRecord(
        path, maxLineLength, stats, jsonField,
        [&consume](ParsedChunk chunk)
        {
            if (!chunk.skipped)
            {
                consume(std::move(chunk));
            }
        },
        limits);
}

struct ArchiveMemberIngest
{
    std::filesystem::path path;
    size_t maxLineLength = 0;
    // NOLINTNEXTLINE(readability-redundant-member-init)
    std::string jsonField{};
};

struct ArchiveMemberContent
{
    std::string_view name;
    std::string_view bytes;
};

static void ingestArchiveMember(const ArchiveMemberContent &member, const ArchiveMemberIngest &opts, ParseStats *stats,
                                const std::function<void(ParsedChunk)> &consume, std::optional<ParseError> &failed)
{
    if (failed)
    {
        return;
    }
    const std::size_t probe = std::min(member.bytes.size(), BINARY_PROBE_BYTES);
    if (containsNul(member.bytes.substr(0, probe)))
    {
        if (stats != nullptr)
        {
            ++stats->binaryEntries;
        }
        return;
    }
    const std::string label = opts.path.string() + ":" + std::string{member.name};
    bool first = true;
    std::istringstream input{std::string{member.bytes}};
    const auto streamed = dispatchStream(input, formatExtension(std::filesystem::path{std::string{member.name}}),
                                         opts.maxLineLength, stats, opts.jsonField,
                                         [&](ParsedChunk chunk)
                                         {
                                             chunk.source = label;
                                             chunk.entryStart = first;
                                             first = false;
                                             consume(std::move(chunk));
                                         });
    if (!streamed)
    {
        failed = streamed.error();
    }
}

static std::expected<void, ParseError> forEachArchivePath(const std::filesystem::path &path, size_t maxLineLength,
                                                          ParseStats *stats, std::string_view jsonField,
                                                          const std::function<void(ParsedChunk)> &consume,
                                                          const ArchiveLimits &limits)
{
    std::optional<ParseError> failed;
    ArchiveSkipStats skips{};
    const ArchiveMemberIngest opts{.path = path, .maxLineLength = maxLineLength, .jsonField = std::string{jsonField}};
    const auto read = readArchive(
        path, limits, [&](std::string_view name, std::string_view bytes)
        { ingestArchiveMember({.name = name, .bytes = bytes}, opts, stats, consume, failed); }, &skips);
    if (stats != nullptr)
    {
        stats->archiveDirectories += skips.directories;
        stats->archiveSymlinks += skips.symlinks;
        stats->archiveHardlinks += skips.hardlinks;
        stats->archiveOtherSkipped += skips.other;
    }
    if (!read)
    {
        if (stats != nullptr && read.error().code == ArchiveError::Encrypted)
        {
            stats->encryptedEntry = read.error().entry;
        }
        return std::unexpected(archiveParseError(read.error()));
    }
    if (failed)
    {
        return std::unexpected(*failed);
    }
    return {};
}

static std::expected<void, ParseError> forEachStdinPath(size_t maxLineLength, ParseStats *stats,
                                                        const std::function<void(ParsedChunk)> &consume)
{
    auto bytes = readStdinChecked();
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::istringstream input{*bytes};
    return readLogStream(input, maxLineLength, stats, consume);
}

static std::expected<void, ParseError> forEachGzipPath(const std::filesystem::path &path, size_t maxLineLength,
                                                       ParseStats *stats, std::string_view jsonField,
                                                       const std::function<void(ParsedChunk)> &consume)
{
    auto bytes = readGzip(path);
    if (!bytes)
    {
        return std::unexpected(gzipParseError(bytes.error()));
    }
    const std::size_t probe = std::min(bytes->size(), BINARY_PROBE_BYTES);
    if (containsNul(std::string_view(*bytes).substr(0, probe)))
    {
        return std::unexpected(ParseError::BinaryFile);
    }
    std::istringstream input{*bytes};
    return dispatchStream(input, formatExtension(path), maxLineLength, stats, jsonField, consume);
}

static std::expected<void, ParseError> forEachPlainPath(const std::filesystem::path &path, size_t maxLineLength,
                                                        ParseStats *stats, std::string_view jsonField,
                                                        const std::function<void(ParsedChunk)> &consume)
{
    auto input = openChecked(path);
    if (!input)
    {
        return std::unexpected(input.error());
    }
    return dispatchStream(*input, extensionLower(path), maxLineLength, stats, jsonField, consume);
}

std::expected<void, ParseError> Chunker::forEachRecord(const std::filesystem::path &path, size_t maxLineLength,
                                                       ParseStats *stats, std::string_view jsonField,
                                                       const std::function<void(ParsedChunk)> &consume,
                                                       const ArchiveLimits &limits)
{
    if (isArchivePath(path))
    {
        return forEachArchivePath(path, maxLineLength, stats, jsonField, consume, limits);
    }
    if (path == "-")
    {
        return forEachStdinPath(maxLineLength, stats, consume);
    }
    if (isGzipPath(path))
    {
        return forEachGzipPath(path, maxLineLength, stats, jsonField, consume);
    }
    return forEachPlainPath(path, maxLineLength, stats, jsonField, consume);
}

std::expected<std::string, ParseError> Chunker::readRecord(const std::filesystem::path &path, const ParsedChunk &key,
                                                           std::string_view jsonField, size_t maxLineLength)
{
    if (path == "-" || isArchivePath(path))
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    if (isGzipPath(path))
    {
        auto bytes = readGzip(path);
        if (!bytes)
        {
            return std::unexpected(gzipParseError(bytes.error()));
        }
        std::istringstream input{std::move(*bytes)};
        return readOpenedRecord(input, formatExtension(path), key, jsonField, maxLineLength);
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    return readOpenedRecord(input, extensionLower(path), key, jsonField, maxLineLength);
}

// cppcheck-suppress unusedFunction
std::expected<std::vector<ParsedChunk>, ParseError> Chunker::fromFile(const std::filesystem::path &path,
                                                                      size_t maxLineLength, ParseStats *stats,
                                                                      std::string_view jsonField,
                                                                      const ArchiveLimits &limits)
{
    std::vector<ParsedChunk> chunks;
    const auto streamed = forEach(
        path, maxLineLength, stats, jsonField, [&chunks](ParsedChunk chunk) { chunks.push_back(std::move(chunk)); },
        limits);
    if (!streamed)
    {
        return std::unexpected(streamed.error());
    }
    return chunks;
}

// cppcheck-suppress unusedFunction
std::expected<std::vector<ParsedChunk>, ParseError> Chunker::fromLogFile(const std::filesystem::path &path,
                                                                         size_t maxLineLength, ParseStats *stats)
{
    auto input = openChecked(path);
    if (!input)
    {
        return std::unexpected(input.error());
    }
    std::vector<ParsedChunk> chunks;
    const auto streamed = readLogStream(*input, maxLineLength, stats,
                                        [&chunks](ParsedChunk chunk)
                                        {
                                            if (!chunk.skipped)
                                            {
                                                chunks.push_back(std::move(chunk));
                                            }
                                        });
    if (!streamed)
    {
        return std::unexpected(streamed.error());
    }
    return chunks;
}

// cppcheck-suppress unusedFunction
std::expected<std::vector<ParsedChunk>, ParseError> Chunker::fromJsonFile(const std::filesystem::path &path,
                                                                          ParseStats *stats, std::string_view jsonField,
                                                                          size_t maxLineLength)
{
    auto input = openChecked(path);
    if (!input)
    {
        return std::unexpected(input.error());
    }
    std::vector<ParsedChunk> chunks;
    const auto streamed = readJsonStream(*input, extensionLower(path) == ".jsonl", stats, jsonField, maxLineLength,
                                         [&chunks](ParsedChunk chunk)
                                         {
                                             if (!chunk.skipped)
                                             {
                                                 chunks.push_back(std::move(chunk));
                                             }
                                         });
    if (!streamed)
    {
        return std::unexpected(streamed.error());
    }
    return chunks;
}

} // namespace nesso::parser
