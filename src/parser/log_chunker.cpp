// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/log_chunker.hpp>

#include <array>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

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
    if (containsNul(std::string_view(buffer.data(), static_cast<size_t>(input.gcount()))))
    {
        return std::unexpected(ParseError::BinaryFile);
    }
    input.clear();
    input.seekg(0);
    return input;
}

static std::expected<std::string, ParseError> readStdinChecked()
{
    std::string bytes(BINARY_PROBE_BYTES, '\0');
    std::cin.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<size_t>(std::cin.gcount()));
    if (containsNul(bytes))
    {
        return std::unexpected(ParseError::BinaryFile);
    }
    if (!std::cin.eof())
    {
        std::ostringstream rest;
        rest << std::cin.rdbuf();
        bytes += rest.str();
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
        if (line.empty())
        {
            countSkip(stats, SkipReason::Empty);
        }
        else
        {
            limitValue(line, maxLineLength, stats);
            consume(ParsedChunk{.text = line, .lineNumber = lineNumber, .byteOffset = byteOffset});
        }
        advanceOffset(byteOffset, fileBytes, !input.eof());
    }
    return {};
}

static std::expected<void, ParseError> readJsonStream(std::istream &input, bool jsonLines, ParseStats *stats,
                                                      std::string_view jsonField, size_t maxLineLength,
                                                      const std::function<void(ParsedChunk)> &consume)
{
    std::string line;
    uint64_t lineNumber = 0;
    const std::string field{jsonField};

    const auto appendValue =
        [&](const nlohmann::json &document, uint64_t sourceLine, uint64_t byteOffset, bool arrayElement)
    {
        if (!document.contains(field) || !document.at(field).is_string())
        {
            countSkip(stats, SkipReason::MissingField);
            return;
        }
        std::string text = document.at(field).get<std::string>();
        limitValue(text, maxLineLength, stats);
        consume(ParsedChunk{
            .text = std::move(text), .lineNumber = sourceLine, .byteOffset = byteOffset, .arrayElement = arrayElement});
    };

    if (jsonLines)
    {
        uint64_t byteOffset = 0;
        while (std::getline(input, line))
        {
            ++lineNumber;
            const bool hadCarriageReturn = stripCarriageReturn(line);
            const size_t fileBytes = line.size() + (hadCarriageReturn ? 1U : 0U);
            if (line.empty())
            {
                countSkip(stats, SkipReason::Empty);
            }
            else
            {
                try
                {
                    appendValue(nlohmann::json::parse(line), lineNumber, byteOffset, false);
                }
                catch (const nlohmann::json::exception &)
                {
                    countSkip(stats, SkipReason::Malformed);
                }
            }
            advanceOffset(byteOffset, fileBytes, !input.eof());
        }
        return {};
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
                appendValue(entry, index, 0, true);
            }
            return {};
        }
        appendValue(document, 1, 0, false);
        return {};
    }
    catch (const nlohmann::json::exception &)
    {
        countSkip(stats, SkipReason::Malformed);
        return {};
    }
}

std::expected<void, ParseError> Chunker::forEach(const std::filesystem::path &path, size_t maxLineLength,
                                                 ParseStats *stats, std::string_view jsonField,
                                                 const std::function<void(ParsedChunk)> &consume)
{
    if (path == "-")
    {
        auto bytes = readStdinChecked();
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        std::istringstream input{*bytes};
        return readLogStream(input, maxLineLength, stats, consume);
    }
    auto input = openChecked(path);
    if (!input)
    {
        return std::unexpected(input.error());
    }
    const std::string extension = extensionLower(path);
    if (extension == ".json" || extension == ".jsonl")
    {
        return readJsonStream(*input, extension == ".jsonl", stats, jsonField, maxLineLength, consume);
    }
    return readLogStream(*input, maxLineLength, stats, consume);
}

// cppcheck-suppress unusedFunction
std::expected<std::vector<ParsedChunk>, ParseError> Chunker::fromFile(const std::filesystem::path &path,
                                                                      size_t maxLineLength, ParseStats *stats,
                                                                      std::string_view jsonField)
{
    std::vector<ParsedChunk> chunks;
    const auto streamed = forEach(path, maxLineLength, stats, jsonField,
                                  [&chunks](ParsedChunk chunk) { chunks.push_back(std::move(chunk)); });
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
                                        [&chunks](ParsedChunk chunk) { chunks.push_back(std::move(chunk)); });
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
                                         [&chunks](ParsedChunk chunk) { chunks.push_back(std::move(chunk)); });
    if (!streamed)
    {
        return std::unexpected(streamed.error());
    }
    return chunks;
}

} // namespace nesso::parser
