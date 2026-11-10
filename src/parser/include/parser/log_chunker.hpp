// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PARSER_LOG_CHUNKER_HPP
#define PARSER_LOG_CHUNKER_HPP

#include <parser/archive_source.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace nesso::parser
{

enum class ParseError : uint8_t
{
    FileOpenFailure,
    UnsupportedFormat,
    BinaryFile,
    CorruptGzip,
    CorruptArchive,
    ArchiveEntryLimit,
    ArchiveTotalLimit,
    ArchiveCountLimit,
    ArchiveRatioLimit
};

struct ParsedChunk
{
    std::string text;
    uint64_t lineNumber = 0;
    /// Raw line range, excluding the newline but including a trailing carriage return.
    /// JSON documents use zero ranges and a one-based object or array-element ordinal.
    uint64_t byteOffset = 0;
    bool arrayElement = false;
    bool skipped = false;
    uint64_t byteLength = 0;
    /// Set for a chunk read from an archive member. Empty for a plain file or stdin.
    // The brace is the default value. It is what lets a partial aggregate compile under
    // -Wmissing-field-initializers.
    // NOLINTNEXTLINE(readability-redundant-member-init)
    std::string source{};
    /// True on the first record of an archive member, including a skipped record.
    bool entryStart = false;
};

/// Counts for one parse. `skippedLines` is the sum of the reason counters. `emptyLines` includes ASCII-whitespace-only
/// lines and JSON string values. `truncatedLines` were kept after cutting the value to the maximum length.
/// `binaryEntries` counts archive members whose first 4 KiB contain a NUL byte. Those members are not lines.
struct ParseStats
{
    size_t skippedLines = 0;
    size_t emptyLines = 0;
    size_t malformedLines = 0;
    size_t missingFieldLines = 0;
    size_t truncatedLines = 0;
    size_t binaryEntries = 0;
};

class Chunker
{
  public:
    /// Invokes `consume` for each kept chunk and does not retain the chunk list.
    /// `maxLineLength` counts UTF-8 code points, and a longer value is cut after that many without splitting a
    /// sequence. A byte that does not start a complete sequence counts as one code point.
    [[nodiscard]] static std::expected<void, ParseError>
    forEach(const std::filesystem::path &path, size_t maxLineLength, ParseStats *stats, std::string_view jsonField,
            const std::function<void(ParsedChunk)> &consume, const ArchiveLimits &limits = {});

    /// Also visits skipped input records, in source order.
    [[nodiscard]] static std::expected<void, ParseError> forEachRecord(const std::filesystem::path &path,
                                                                       size_t maxLineLength, ParseStats *stats,
                                                                       std::string_view jsonField,
                                                                       const std::function<void(ParsedChunk)> &consume,
                                                                       const ArchiveLimits &limits = {});

    /// Re-read a kept file record using its source metadata, field and code-point limit.
    /// JSON values are decoded. Stdin and archive members cannot be re-read this way.
    [[nodiscard]] static std::expected<std::string, ParseError>
    readRecord(const std::filesystem::path &path, uint64_t byteOffset, uint64_t byteLength, uint64_t lineNumber,
               bool arrayElement, std::string_view jsonField, size_t maxLineLength);

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    fromFile(const std::filesystem::path &path, size_t maxLineLength = 4096, ParseStats *stats = nullptr,
             std::string_view jsonField = "message", const ArchiveLimits &limits = {});

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    fromLogFile(const std::filesystem::path &path, size_t maxLineLength = 4096, ParseStats *stats = nullptr);

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    fromJsonFile(const std::filesystem::path &path, ParseStats *stats = nullptr, std::string_view jsonField = "message",
                 size_t maxLineLength = 4096);
};

} // namespace nesso::parser

#endif // PARSER_LOG_CHUNKER_HPP
