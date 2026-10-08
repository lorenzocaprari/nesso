// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/parser/log_chunker.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace parser
{

static bool hasExtension(const std::filesystem::path &path, std::string_view extension)
{
    return path.extension() == extension;
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

static void countSkip(ParseStats *stats)
{
    if (stats != nullptr)
    {
        ++stats->skippedLines;
    }
}

static void countTruncation(ParseStats *stats)
{
    if (stats != nullptr)
    {
        ++stats->truncatedLines;
    }
}

static void limitValue(std::string &text, size_t maxLineLength, ParseStats *stats)
{
    if (text.size() > maxLineLength)
    {
        text.resize(maxLineLength);
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

std::expected<std::vector<ParsedChunk>, ParseError> Chunker::fromFile(const std::filesystem::path &path,
                                                                      size_t maxLineLength, ParseStats *stats,
                                                                      std::string_view jsonField)
{
    if (hasExtension(path, ".log"))
    {
        return fromLogFile(path, maxLineLength, stats);
    }
    if (hasExtension(path, ".json") || hasExtension(path, ".jsonl"))
    {
        return fromJsonFile(path, stats, jsonField, maxLineLength);
    }
    return std::unexpected(ParseError::UnsupportedFormat);
}

std::expected<std::vector<ParsedChunk>, ParseError> Chunker::fromLogFile(const std::filesystem::path &path,
                                                                         size_t maxLineLength, ParseStats *stats)
{
    std::ifstream input(path);
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }

    std::vector<ParsedChunk> chunks;
    std::string line;
    uint64_t lineNumber = 0;
    uint64_t byteOffset = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        const bool hadCarriageReturn = stripCarriageReturn(line);
        const size_t fileBytes = line.size() + (hadCarriageReturn ? 1U : 0U);
        if (line.empty())
        {
            countSkip(stats);
        }
        else
        {
            limitValue(line, maxLineLength, stats);
            chunks.push_back({.text = line, .lineNumber = lineNumber, .byteOffset = byteOffset});
        }
        advanceOffset(byteOffset, fileBytes, !input.eof());
    }
    return chunks;
}

std::expected<std::vector<ParsedChunk>, ParseError> Chunker::fromJsonFile(const std::filesystem::path &path,
                                                                          ParseStats *stats, std::string_view jsonField,
                                                                          size_t maxLineLength)
{
    std::ifstream input(path);
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }

    std::vector<ParsedChunk> chunks;
    std::string line;
    uint64_t lineNumber = 0;
    const std::string field{jsonField};

    const auto appendValue = [&](const nlohmann::json &document, uint64_t sourceLine, uint64_t byteOffset)
    {
        if (!document.contains(field) || !document.at(field).is_string())
        {
            countSkip(stats);
            return;
        }
        std::string text = document.at(field).get<std::string>();
        limitValue(text, maxLineLength, stats);
        chunks.push_back({.text = std::move(text), .lineNumber = sourceLine, .byteOffset = byteOffset});
    };

    if (hasExtension(path, ".jsonl"))
    {
        uint64_t byteOffset = 0;
        while (std::getline(input, line))
        {
            ++lineNumber;
            const bool hadCarriageReturn = stripCarriageReturn(line);
            const size_t fileBytes = line.size() + (hadCarriageReturn ? 1U : 0U);
            if (line.empty())
            {
                countSkip(stats);
            }
            else
            {
                try
                {
                    appendValue(nlohmann::json::parse(line), lineNumber, byteOffset);
                }
                catch (const nlohmann::json::exception &)
                {
                    countSkip(stats);
                }
            }
            advanceOffset(byteOffset, fileBytes, !input.eof());
        }
        return chunks;
    }

    try
    {
        const auto document = nlohmann::json::parse(input);
        if (document.is_array())
        {
            uint64_t index = 0;
            for (const auto &entry : document)
            {
                ++index;
                appendValue(entry, index, 0);
            }
            return chunks;
        }
        appendValue(document, 1, 0);
        return chunks;
    }
    catch (const nlohmann::json::exception &)
    {
        countSkip(stats);
        return chunks;
    }
}

} // namespace parser
