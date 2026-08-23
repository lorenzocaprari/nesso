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
                                                                         size_t maxLineLength)
{
    if (hasExtension(path, ".log"))
    {
        return fromLogFile(path, maxLineLength);
    }
    if (hasExtension(path, ".json") || hasExtension(path, ".jsonl"))
    {
        return fromJsonFile(path);
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
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (line.empty())
        {
            if (stats != nullptr)
            {
                ++stats->skippedLines;
            }
            continue;
        }
        if (line.size() > maxLineLength)
        {
            if (stats != nullptr)
            {
                ++stats->skippedLines;
            }
            continue;
        }
        chunks.push_back({.text = line, .lineNumber = lineNumber});
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

    const auto appendMessage = [&](const nlohmann::json &document, uint64_t sourceLine)
    {
        if (!document.contains("message") || !document.at("message").is_string())
        {
            if (stats != nullptr)
            {
                ++stats->skippedLines;
            }
            return;
        }
        chunks.push_back({.text = document.at("message").get<std::string>(), .lineNumber = sourceLine});
    };

    if (hasExtension(path, ".jsonl"))
    {
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (line.empty())
            {
                if (stats != nullptr)
                {
                    ++stats->skippedLines;
                }
                continue;
            }
            try
            {
                appendMessage(nlohmann::json::parse(line), lineNumber);
            }
            catch (const nlohmann::json::exception &)
            {
                if (stats != nullptr)
                {
                    ++stats->skippedLines;
                }
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
                appendMessage(entry, index);
            }
            return chunks;
        }
        appendMessage(document, 1);
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
