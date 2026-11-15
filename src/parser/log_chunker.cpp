// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <nlohmann/json_fwd.hpp>
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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nesso::parser
{

static constexpr size_t BINARY_PROBE_BYTES = 4096;

static std::string ExtensionLower(const std::filesystem::path &path)
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

static bool ContainsNul(std::string_view bytes) { return bytes.find('\0') != std::string_view::npos; }

static std::expected<std::ifstream, ParseError> OpenChecked(const std::filesystem::path &path)
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
    if (ContainsNul(std::string_view(buffer.data(), static_cast<size_t>(input.gcount()))))
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

static std::expected<std::string, ParseError> ReadStdinChecked()
{
    std::array<char, BINARY_PROBE_BYTES> buffer{};
    std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (std::cin.bad() || (std::cin.fail() && !std::cin.eof()))
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    std::string bytes(buffer.data(), static_cast<size_t>(std::cin.gcount()));
    if (ContainsNul(bytes))
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

static bool StripCarriageReturn(std::string &line)
{
    if (line.empty() || line.back() != '\r')
    {
        return false;
    }
    line.pop_back();
    return true;
}

static bool IsBlank(std::string_view text) { return text.find_first_not_of(" \t\n\r\f\v") == std::string_view::npos; }

enum class SkipReason : uint8_t
{
    Empty,
    Malformed,
    MissingField
};

static void CountSkip(ParseStats *stats, SkipReason reason)
{
    if (stats == nullptr)
    {
        return;
    }
    ++stats->skipped_lines;
    switch (reason)
    {
    case SkipReason::Empty:
        ++stats->empty_lines;
        break;
    case SkipReason::Malformed:
        ++stats->malformed_lines;
        break;
    case SkipReason::MissingField:
        ++stats->missing_field_lines;
        break;
    }
}

static void CountTruncation(ParseStats *stats)
{
    if (stats != nullptr)
    {
        ++stats->truncated_lines;
    }
}

// Bytes in the code point at `index`. A lead byte with its continuation bytes
// counts as one code point. Any other byte counts on its own, as the tokenizer
// replaces it with U+FFFD.
static size_t CodePointBytes(std::string_view text, size_t index)
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

static void LimitValue(std::string &text, size_t max_code_points, ParseStats *stats)
{
    if (text.size() <= max_code_points)
    {
        return;
    }
    size_t end = 0;
    for (size_t count = 0; count < max_code_points && end < text.size(); ++count)
    {
        end += CodePointBytes(text, end);
    }
    if (end < text.size())
    {
        text.resize(end);
        CountTruncation(stats);
    }
}

static void AdvanceOffset(uint64_t &byte_offset, size_t file_bytes, bool delimiter_pending)
{
    byte_offset += file_bytes;
    if (delimiter_pending)
    {
        ++byte_offset;
    }
}

static std::expected<void, ParseError> ReadLogStream(std::istream &input, size_t max_line_length, ParseStats *stats,
                                                     const std::function<void(ParsedChunk)> &consume)
{
    std::string line;
    uint64_t line_number = 0;
    uint64_t byte_offset = 0;
    while (std::getline(input, line))
    {
        ++line_number;
        const bool had_carriage_return = StripCarriageReturn(line);
        const size_t file_bytes = line.size() + (had_carriage_return ? 1U : 0U);
        if (IsBlank(line))
        {
            CountSkip(stats, SkipReason::Empty);
            consume(ParsedChunk{.text = {},
                                .line_number = line_number,
                                .byte_offset = byte_offset,
                                .skipped = true,
                                .byte_length = file_bytes,
                                .source = {}});
        }
        else
        {
            LimitValue(line, max_line_length, stats);
            consume(ParsedChunk{.text = line,
                                .line_number = line_number,
                                .byte_offset = byte_offset,
                                .byte_length = file_bytes,
                                .source = {}});
        }
        AdvanceOffset(byte_offset, file_bytes, !input.eof());
    }
    if (input.bad() || !input.eof())
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    return {};
}

static void ConsumeSkipped(ParseStats *stats, SkipReason reason, ParsedChunk loc,
                           const std::function<void(ParsedChunk)> &consume)
{
    CountSkip(stats, reason);
    loc.text.clear();
    loc.skipped = true;
    consume(std::move(loc));
}

static void AppendJsonField(const nlohmann::json &document, const std::string &field, ParsedChunk loc,
                            size_t max_line_length, ParseStats *stats, const std::function<void(ParsedChunk)> &consume)
{
    if (!document.contains(field) || !document.at(field).is_string())
    {
        ConsumeSkipped(stats, SkipReason::MissingField, std::move(loc), consume);
        return;
    }
    std::string text = document.at(field).get<std::string>();
    if (IsBlank(text))
    {
        ConsumeSkipped(stats, SkipReason::Empty, std::move(loc), consume);
        return;
    }
    LimitValue(text, max_line_length, stats);
    loc.text = std::move(text);
    consume(std::move(loc));
}

static std::expected<void, ParseError> ReadJsonLinesStream(std::istream &input, ParseStats *stats,
                                                           const std::string &field, size_t max_line_length,
                                                           const std::function<void(ParsedChunk)> &consume)
{
    std::string line;
    uint64_t line_number = 0;
    uint64_t byte_offset = 0;
    while (std::getline(input, line))
    {
        ++line_number;
        const bool had_carriage_return = StripCarriageReturn(line);
        const size_t file_bytes = line.size() + (had_carriage_return ? 1U : 0U);
        if (IsBlank(line))
        {
            ConsumeSkipped(
                stats, SkipReason::Empty,
                {.text = {}, .line_number = line_number, .byte_offset = byte_offset, .byte_length = file_bytes},
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
                ConsumeSkipped(
                    stats, SkipReason::Malformed,
                    {.text = {}, .line_number = line_number, .byte_offset = byte_offset, .byte_length = file_bytes},
                    consume);
                parsed = false;
            }
            if (parsed)
            {
                AppendJsonField(
                    document, field,
                    {.text = {}, .line_number = line_number, .byte_offset = byte_offset, .byte_length = file_bytes},
                    max_line_length, stats, consume);
            }
        }
        AdvanceOffset(byte_offset, file_bytes, !input.eof());
    }
    if (input.bad() || !input.eof())
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    return {};
}

static std::expected<void, ParseError> ReadJsonDocumentStream(std::istream &input, ParseStats *stats,
                                                              const std::string &field, size_t max_line_length,
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
        CountSkip(stats, SkipReason::Malformed);
        consume(ParsedChunk{.text = {}, .line_number = 1, .skipped = true, .source = {}});
        return {};
    }
    if (document.is_array())
    {
        uint64_t index = 0;
        for (const auto &entry : document)
        {
            ++index;
            AppendJsonField(entry, field, {.text = {}, .line_number = index, .array_element = true}, max_line_length,
                            stats, consume);
        }
        return {};
    }
    AppendJsonField(document, field, {.text = {}, .line_number = 1}, max_line_length, stats, consume);
    return {};
}

static std::expected<void, ParseError> ReadJsonStream(std::istream &input, bool json_lines, ParseStats *stats,
                                                      std::string_view json_field, size_t max_line_length,
                                                      const std::function<void(ParsedChunk)> &consume)
{
    const std::string field{json_field};
    if (json_lines)
    {
        return ReadJsonLinesStream(input, stats, field, max_line_length, consume);
    }
    return ReadJsonDocumentStream(input, stats, field, max_line_length, consume);
}

static ParseError GzipParseError(GzipError error)
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

static ParseError ArchiveParseError(const ArchiveFailure &error)
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
static std::string FormatExtension(const std::filesystem::path &path)
{
    if (IsGzipPath(path))
    {
        return ExtensionLower(path.stem());
    }
    return ExtensionLower(path);
}

static std::expected<void, ParseError> DispatchStream(std::istream &input, const std::string &extension,
                                                      size_t max_line_length, ParseStats *stats,
                                                      std::string_view json_field,
                                                      const std::function<void(ParsedChunk)> &consume)
{
    if (extension == ".json" || extension == ".jsonl")
    {
        return ReadJsonStream(input, extension == ".jsonl", stats, json_field, max_line_length, consume);
    }
    return ReadLogStream(input, max_line_length, stats, consume);
}

static std::expected<const nlohmann::json *, ParseError> SelectJsonRecordEntry(const nlohmann::json &document,
                                                                               uint64_t line_number, bool array_element)
{
    if (array_element)
    {
        if (!document.is_array() || line_number == 0 || line_number > document.size())
        {
            return std::unexpected(ParseError::UnsupportedFormat);
        }
        return &document.at(line_number - 1);
    }
    if (document.is_array() || line_number != 1)
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    return &document;
}

static std::expected<std::string, ParseError> ReadOpenedJsonRecord(std::istream &input, const ParsedChunk &key,
                                                                   std::string_view json_field, size_t max_line_length)
{
    if (key.byte_offset != 0 || key.byte_length != 0)
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    try
    {
        const auto document = nlohmann::json::parse(input);
        const auto entry = SelectJsonRecordEntry(document, key.line_number, key.array_element);
        if (!entry)
        {
            return std::unexpected(entry.error());
        }
        const std::string field{json_field};
        if (!(*entry)->contains(field) || !(*entry)->at(field).is_string())
        {
            return std::unexpected(ParseError::UnsupportedFormat);
        }
        std::string text = (*entry)->at(field).get<std::string>();
        LimitValue(text, max_line_length, nullptr);
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

static std::expected<void, ParseError> DecodeJsonlField(std::string &text, std::string_view json_field)
{
    try
    {
        const auto document = nlohmann::json::parse(text);
        const std::string field{json_field};
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

static std::expected<std::string, ParseError> ReadOpenedRangedRecord(std::istream &input, const std::string &extension,
                                                                     const ByteRange &range,
                                                                     std::string_view json_field,
                                                                     size_t max_line_length)
{
    input.seekg(static_cast<std::streamoff>(range.offset));
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    uint64_t read_length = range.length;
    if (extension != ".jsonl" && max_line_length <= (std::numeric_limits<size_t>::max() - 4) / 4)
    {
        read_length = std::min<uint64_t>(range.length, (max_line_length * 4) + 4);
    }
    if (!std::in_range<std::streamsize>(read_length) || !std::in_range<size_t>(read_length))
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    std::string text(static_cast<size_t>(read_length), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!std::cmp_equal(input.gcount(), text.size()))
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    if (extension == ".jsonl")
    {
        if (const auto decoded = DecodeJsonlField(text, json_field); !decoded)
        {
            return std::unexpected(decoded.error());
        }
    }
    else if (read_length == range.length)
    {
        static_cast<void>(StripCarriageReturn(text));
    }
    LimitValue(text, max_line_length, nullptr);
    return text;
}

static std::expected<std::string, ParseError> ReadOpenedRecord(std::istream &input, const std::string &extension,
                                                               const ParsedChunk &key, std::string_view json_field,
                                                               size_t max_line_length)
{
    if (!input || key.byte_offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    if (key.array_element && extension != ".json")
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    if (extension == ".json")
    {
        return ReadOpenedJsonRecord(input, key, json_field, max_line_length);
    }
    return ReadOpenedRangedRecord(input, extension, {.offset = key.byte_offset, .length = key.byte_length}, json_field,
                                  max_line_length);
}

std::expected<void, ParseError> Chunker::ForEach(const std::filesystem::path &path, size_t max_line_length,
                                                 ParseStats *stats, std::string_view json_field,
                                                 const std::function<void(ParsedChunk)> &consume,
                                                 const ArchiveLimits &limits)
{
    return ForEachRecord(
        path, max_line_length, stats, json_field,
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
    size_t max_line_length = 0;
    // NOLINTNEXTLINE(readability-redundant-member-init)
    std::string json_field{};
};

struct ArchiveMemberContent
{
    std::string_view name;
    std::string_view bytes;
};

static void IngestArchiveMember(const ArchiveMemberContent &member, const ArchiveMemberIngest &opts, ParseStats *stats,
                                const std::function<void(ParsedChunk)> &consume, std::optional<ParseError> &failed)
{
    if (failed)
    {
        return;
    }
    const std::size_t probe = std::min(member.bytes.size(), BINARY_PROBE_BYTES);
    if (ContainsNul(member.bytes.substr(0, probe)))
    {
        if (stats != nullptr)
        {
            ++stats->binary_entries;
        }
        return;
    }
    const std::string label = opts.path.string() + ":" + std::string{member.name};
    bool first = true;
    std::istringstream input{std::string{member.bytes}};
    const auto streamed = DispatchStream(input, FormatExtension(std::filesystem::path{std::string{member.name}}),
                                         opts.max_line_length, stats, opts.json_field,
                                         [&](ParsedChunk chunk)
                                         {
                                             chunk.source = label;
                                             chunk.entry_start = first;
                                             first = false;
                                             consume(std::move(chunk));
                                         });
    if (!streamed)
    {
        failed = streamed.error();
    }
}

static std::expected<void, ParseError> ForEachArchivePath(const std::filesystem::path &path, size_t max_line_length,
                                                          ParseStats *stats, std::string_view json_field,
                                                          const std::function<void(ParsedChunk)> &consume,
                                                          const ArchiveLimits &limits)
{
    std::optional<ParseError> failed;
    ArchiveSkipStats skips{};
    const ArchiveMemberIngest opts{
        .path = path, .max_line_length = max_line_length, .json_field = std::string{json_field}};
    const auto read = ReadArchive(
        path, limits, [&](std::string_view name, std::string_view bytes)
        { IngestArchiveMember({.name = name, .bytes = bytes}, opts, stats, consume, failed); }, &skips);
    if (stats != nullptr)
    {
        stats->archive_directories += skips.directories;
        stats->archive_symlinks += skips.symlinks;
        stats->archive_hardlinks += skips.hardlinks;
        stats->archive_other_skipped += skips.other;
    }
    if (!read)
    {
        if (stats != nullptr && read.error().code == ArchiveError::Encrypted)
        {
            stats->encrypted_entry = read.error().entry;
        }
        return std::unexpected(ArchiveParseError(read.error()));
    }
    if (failed)
    {
        return std::unexpected(*failed);
    }
    return {};
}

static std::expected<void, ParseError> ForEachStdinPath(size_t max_line_length, ParseStats *stats,
                                                        const std::function<void(ParsedChunk)> &consume)
{
    auto bytes = ReadStdinChecked();
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    std::istringstream input{*bytes};
    return ReadLogStream(input, max_line_length, stats, consume);
}

static std::expected<void, ParseError> ForEachGzipPath(const std::filesystem::path &path, size_t max_line_length,
                                                       ParseStats *stats, std::string_view json_field,
                                                       const std::function<void(ParsedChunk)> &consume)
{
    auto bytes = ReadGzip(path);
    if (!bytes)
    {
        return std::unexpected(GzipParseError(bytes.error()));
    }
    const std::size_t probe = std::min(bytes->size(), BINARY_PROBE_BYTES);
    if (ContainsNul(std::string_view(*bytes).substr(0, probe)))
    {
        return std::unexpected(ParseError::BinaryFile);
    }
    std::istringstream input{*bytes};
    return DispatchStream(input, FormatExtension(path), max_line_length, stats, json_field, consume);
}

static std::expected<void, ParseError> ForEachPlainPath(const std::filesystem::path &path, size_t max_line_length,
                                                        ParseStats *stats, std::string_view json_field,
                                                        const std::function<void(ParsedChunk)> &consume)
{
    auto input = OpenChecked(path);
    if (!input)
    {
        return std::unexpected(input.error());
    }
    return DispatchStream(*input, ExtensionLower(path), max_line_length, stats, json_field, consume);
}

std::expected<void, ParseError> Chunker::ForEachRecord(const std::filesystem::path &path, size_t max_line_length,
                                                       ParseStats *stats, std::string_view json_field,
                                                       const std::function<void(ParsedChunk)> &consume,
                                                       const ArchiveLimits &limits)
{
    if (IsArchivePath(path))
    {
        return ForEachArchivePath(path, max_line_length, stats, json_field, consume, limits);
    }
    if (path == "-")
    {
        return ForEachStdinPath(max_line_length, stats, consume);
    }
    if (IsGzipPath(path))
    {
        return ForEachGzipPath(path, max_line_length, stats, json_field, consume);
    }
    return ForEachPlainPath(path, max_line_length, stats, json_field, consume);
}

std::expected<std::string, ParseError> Chunker::ReadRecord(const std::filesystem::path &path, const ParsedChunk &key,
                                                           std::string_view json_field, size_t max_line_length)
{
    if (path == "-" || IsArchivePath(path))
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    if (IsGzipPath(path))
    {
        auto bytes = ReadGzip(path);
        if (!bytes)
        {
            return std::unexpected(GzipParseError(bytes.error()));
        }
        std::istringstream input{std::move(*bytes)};
        return ReadOpenedRecord(input, FormatExtension(path), key, json_field, max_line_length);
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    return ReadOpenedRecord(input, ExtensionLower(path), key, json_field, max_line_length);
}

// cppcheck-suppress unusedFunction
std::expected<std::vector<ParsedChunk>, ParseError> Chunker::FromFile(const std::filesystem::path &path,
                                                                      size_t max_line_length, ParseStats *stats,
                                                                      std::string_view json_field,
                                                                      const ArchiveLimits &limits)
{
    std::vector<ParsedChunk> chunks;
    const auto streamed = ForEach(
        path, max_line_length, stats, json_field, [&chunks](ParsedChunk chunk) { chunks.push_back(std::move(chunk)); },
        limits);
    if (!streamed)
    {
        return std::unexpected(streamed.error());
    }
    return chunks;
}

// cppcheck-suppress unusedFunction
std::expected<std::vector<ParsedChunk>, ParseError> Chunker::FromLogFile(const std::filesystem::path &path,
                                                                         size_t max_line_length, ParseStats *stats)
{
    auto input = OpenChecked(path);
    if (!input)
    {
        return std::unexpected(input.error());
    }
    std::vector<ParsedChunk> chunks;
    const auto streamed = ReadLogStream(*input, max_line_length, stats,
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
std::expected<std::vector<ParsedChunk>, ParseError> Chunker::FromJsonFile(const std::filesystem::path &path,
                                                                          ParseStats *stats,
                                                                          std::string_view json_field,
                                                                          size_t max_line_length)
{
    auto input = OpenChecked(path);
    if (!input)
    {
        return std::unexpected(input.error());
    }
    std::vector<ParsedChunk> chunks;
    const auto streamed = ReadJsonStream(*input, ExtensionLower(path) == ".jsonl", stats, json_field, max_line_length,
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
