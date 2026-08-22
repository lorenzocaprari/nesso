// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef MACH_PARSER_LOG_CHUNKER_HPP
#define MACH_PARSER_LOG_CHUNKER_HPP

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace mach_parser
{

enum class ParseError : uint8_t
{
    FileOpenFailure,
    UnsupportedFormat
};

struct ParsedChunk
{
    std::string text;
    uint64_t lineNumber = 0;
};

struct ParseStats
{
    size_t skippedLines = 0;
};

class LogChunker
{
  public:
    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError> fromFile(const std::filesystem::path &path,
                                                                                      size_t maxLineLength = 4096);

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    fromLogFile(const std::filesystem::path &path, size_t maxLineLength = 4096, ParseStats *stats = nullptr);

    [[nodiscard]] static std::expected<std::vector<ParsedChunk>, ParseError>
    fromJsonFile(const std::filesystem::path &path, ParseStats *stats = nullptr);
};

} // namespace mach_parser

#endif // MACH_PARSER_LOG_CHUNKER_HPP
