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

std::expected<std::vector<ParsedChunk>, ParseError> LogChunker::fromFile(const std::filesystem::path &path,
                                                                         size_t maxLineLength, ParseStats *stats)
{
    if (hasExtension(path, ".log"))
    {
        return fromLogFile(path, maxLineLength, stats);
    }
    if (hasExtension(path, ".json") || hasExtension(path, ".jsonl"))
    {
        return fromJsonFile(path, stats);
    }
    return std::unexpected(ParseError::UnsupportedFormat);
}

std::expected<std::vector<ParsedChunk>, ParseError> LogChunker::fromLogFile(const std::filesystem::path &path,
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
        if (line.empty() || line.size() > maxLineLength)
        {
            if (stats != nullptr)
            {
                ++stats->skippedLines;
            }
        }
        else
        {
            chunks.push_back({.text = line, .lineNumber = lineNumber, .byteOffset = byteOffset});
        }
        byteOffset += line.size();
        if (!input.eof())
        {
            ++byteOffset;
        }
    }
    return chunks;
}

std::expected<std::vector<ParsedChunk>, ParseError> LogChunker::fromJsonFile(const std::filesystem::path &path,
                                                                             ParseStats *stats)
{
    std::ifstream input(path);
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }

    std::vector<ParsedChunk> chunks;
    std::string line;
    uint64_t lineNumber = 0;

    const auto appendMessage = [&](const nlohmann::json &document, uint64_t sourceLine, uint64_t byteOffset)
    {
        if (!document.contains("message") || !document.at("message").is_string())
        {
            if (stats != nullptr)
            {
                ++stats->skippedLines;
            }
            return;
        }
        chunks.push_back(
            {.text = document.at("message").get<std::string>(), .lineNumber = sourceLine, .byteOffset = byteOffset});
    };

    if (hasExtension(path, ".jsonl"))
    {
        uint64_t byteOffset = 0;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (line.empty())
            {
                if (stats != nullptr)
                {
                    ++stats->skippedLines;
                }
            }
            else
            {
                try
                {
                    appendMessage(nlohmann::json::parse(line), lineNumber, byteOffset);
                }
                catch (const nlohmann::json::exception &)
                {
                    if (stats != nullptr)
                    {
                        ++stats->skippedLines;
                    }
                }
            }
            byteOffset += line.size();
            if (!input.eof())
            {
                ++byteOffset;
            }
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
                appendMessage(entry, index, 0);
            }
            return chunks;
        }
        appendMessage(document, 1, 0);
        return chunks;
    }
    catch (const nlohmann::json::exception &)
    {
        if (stats != nullptr)
        {
            ++stats->skippedLines;
        }
        return chunks;
    }
}

} // namespace parser
