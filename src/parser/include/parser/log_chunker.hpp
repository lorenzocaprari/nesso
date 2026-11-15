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
    EncryptedArchive,
    ArchiveEntryLimit,
    ArchiveTotalLimit,
    ArchiveCountLimit,
    ArchiveRatioLimit
};

struct ParsedChunk
{
    std::string text;
    uint64_t line_number = 0;
    /// Raw line range, excluding the newline but including a trailing carriage
    /// return. JSON documents use zero ranges and a one-based object or
    /// array-element ordinal.
    uint64_t byte_offset = 0;
    bool array_element = false;
    bool skipped = false;
    uint64_t byte_length = 0;
    /// Set for a chunk read from an archive member. Empty for a plain file or
    /// stdin.
    // The brace is the default value. It is what lets a partial aggregate compile
    // under -Wmissing-field-initializers.
    // NOLINTNEXTLINE(readability-redundant-member-init)
    std::string source{};
    /// True on the first record of an archive member, including a skipped record.
    bool entry_start = false;
};

/// Counts for one parse. `skippedLines` is the sum of the reason counters.
/// `emptyLines` includes ASCII-whitespace-only lines and JSON string values.
/// `truncatedLines` were kept after cutting the value to the maximum length.
/// `binaryEntries` counts archive members whose first 4 KiB contain a NUL byte.
/// Those members are not lines. Archive directory/symlink/hardlink/other skips
/// are counted when reading a bundle. `encryptedEntry` is set when the failure
/// is an encrypted zip member.
struct ParseStats
{
    size_t skipped_lines = 0;
    size_t empty_lines = 0;
    size_t malformed_lines = 0;
    size_t missing_field_lines = 0;
    size_t truncated_lines = 0;
    size_t binary_entries = 0;
    size_t archive_directories = 0;
    size_t archive_symlinks = 0;
    size_t archive_hardlinks = 0;
    size_t archive_other_skipped = 0;
    // NOLINTNEXTLINE(readability-redundant-member-init)
    std::string encrypted_entry{};
};

class Chunker
{
  public:
    /// Invokes `consume` for each kept chunk and does not retain the chunk list.
    /// `maxLineLength` counts UTF-8 code points, and a longer value is cut after
    /// that many without splitting a sequence. A byte that does not start a
    /// complete sequence counts as one code point.
    [[nodiscard]] static std::expected<void, ParseError>
    ForEach(const std::filesystem::path &path, size_t max_line_length, ParseStats *stats, std::string_view json_field,
            const std::function<void(ParsedChunk)> &consume, const ArchiveLimits &limits = {});

    /// Also visits skipped input records, in source order.
    [[nodiscard]] static std::expected<void, ParseError> ForEachRecord(const std::filesystem::path &path,
                                                                       size_t max_line_length, ParseStats *stats,
                                                                       std::string_view json_field,
                                                                       const std::function<void(ParsedChunk)> &consume,
                                                                       const ArchiveLimits &limits = {});

    /// Re-read a kept file record using its source metadata, field and code-point
    /// limit. JSON values are decoded. Stdin and archive members cannot be
    /// re-read this way. `key` supplies lineNumber, byteOffset, byteLength, and
    /// arrayElement.
    [[nodiscard]] static std::expected<std::string, ParseError> ReadRecord(const std::filesystem::path &path,
                                                                           const ParsedChunk &key,
                                                                           std::string_view json_field,
                                                                           size_t max_line_length);

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    FromFile(const std::filesystem::path &path, size_t max_line_length = 4096, ParseStats *stats = nullptr,
             std::string_view json_field = "message", const ArchiveLimits &limits = {});

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    FromLogFile(const std::filesystem::path &path, size_t max_line_length = 4096, ParseStats *stats = nullptr);

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    FromJsonFile(const std::filesystem::path &path, ParseStats *stats = nullptr,
                 std::string_view json_field = "message", size_t max_line_length = 4096);
};

} // namespace nesso::parser

#endif // PARSER_LOG_CHUNKER_HPP
