// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef PARSER_LOG_CHUNKER_HPP
#define PARSER_LOG_CHUNKER_HPP

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace parser
{

enum class ParseError : uint8_t
{
    FileOpenFailure,
    UnsupportedFormat,
    BinaryFile
};

struct ParsedChunk
{
    std::string text;
    uint64_t lineNumber = 0;
    uint64_t byteOffset = 0;
    bool arrayElement = false;
};

/// Counts for one parse. `skippedLines` is the sum of the reason counters. `truncatedLines` were kept after cutting
/// the value to the maximum length.
struct ParseStats
{
    size_t skippedLines = 0;
    size_t emptyLines = 0;
    size_t malformedLines = 0;
    size_t missingFieldLines = 0;
    size_t truncatedLines = 0;
};

class Chunker
{
  public:
    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    fromFile(const std::filesystem::path &path, size_t maxLineLength = 4096, ParseStats *stats = nullptr,
             std::string_view jsonField = "message");

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    fromLogFile(const std::filesystem::path &path, size_t maxLineLength = 4096, ParseStats *stats = nullptr);

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    fromJsonFile(const std::filesystem::path &path, ParseStats *stats = nullptr, std::string_view jsonField = "message",
                 size_t maxLineLength = 4096);
};

} // namespace parser

#endif // PARSER_LOG_CHUNKER_HPP
