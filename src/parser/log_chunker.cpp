// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/log_chunker.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <ios>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
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
        if (line.empty())
        {
            countSkip(stats, SkipReason::Empty);
            consume(ParsedChunk{.text = {},
                                .lineNumber = lineNumber,
                                .byteOffset = byteOffset,
                                .skipped = true,
                                .byteLength = fileBytes});
        }
        else
        {
            limitValue(line, maxLineLength, stats);
            consume(
                ParsedChunk{.text = line, .lineNumber = lineNumber, .byteOffset = byteOffset, .byteLength = fileBytes});
        }
        advanceOffset(byteOffset, fileBytes, !input.eof());
    }
    if (input.bad() || !input.eof())
    {
        return std::unexpected(ParseError::FileOpenFailure);
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

    const auto appendValue = [&](const nlohmann::json &document, uint64_t sourceLine, uint64_t byteOffset,
                                 uint64_t byteLength, bool arrayElement)
    {
        if (!document.contains(field) || !document.at(field).is_string())
        {
            countSkip(stats, SkipReason::MissingField);
            consume(ParsedChunk{.text = {},
                                .lineNumber = sourceLine,
                                .byteOffset = byteOffset,
                                .arrayElement = arrayElement,
                                .skipped = true,
                                .byteLength = byteLength});
            return;
        }
        std::string text = document.at(field).get<std::string>();
        limitValue(text, maxLineLength, stats);
        consume(ParsedChunk{.text = std::move(text),
                            .lineNumber = sourceLine,
                            .byteOffset = byteOffset,
                            .arrayElement = arrayElement,
                            .byteLength = byteLength});
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
                consume(ParsedChunk{.text = {},
                                    .lineNumber = lineNumber,
                                    .byteOffset = byteOffset,
                                    .skipped = true,
                                    .byteLength = fileBytes});
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
                    countSkip(stats, SkipReason::Malformed);
                    consume(ParsedChunk{.text = {},
                                        .lineNumber = lineNumber,
                                        .byteOffset = byteOffset,
                                        .skipped = true,
                                        .byteLength = fileBytes});
                    parsed = false;
                }
                if (parsed)
                {
                    appendValue(document, lineNumber, byteOffset, fileBytes, false);
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
        consume(ParsedChunk{.text = {}, .lineNumber = 1, .skipped = true});
        return {};
    }
    if (document.is_array())
    {
        uint64_t index = 0;
        for (const auto &entry : document)
        {
            ++index;
            appendValue(entry, index, 0, 0, true);
        }
        return {};
    }
    appendValue(document, 1, 0, 0, false);
    return {};
}

std::expected<void, ParseError> Chunker::forEach(const std::filesystem::path &path, size_t maxLineLength,
                                                 ParseStats *stats, std::string_view jsonField,
                                                 const std::function<void(ParsedChunk)> &consume)
{
    return forEachRecord(path, maxLineLength, stats, jsonField,
                         [&consume](ParsedChunk chunk)
                         {
                             if (!chunk.skipped)
                             {
                                 consume(std::move(chunk));
                             }
                         });
}

std::expected<void, ParseError> Chunker::forEachRecord(const std::filesystem::path &path, size_t maxLineLength,
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

std::expected<std::string, ParseError> Chunker::readRecord(const std::filesystem::path &path, uint64_t byteOffset,
                                                           uint64_t byteLength, uint64_t lineNumber, bool arrayElement,
                                                           std::string_view jsonField, size_t maxLineLength)
{
    if (path == "-")
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    std::ifstream input(path, std::ios::binary);
    if (!input || byteOffset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    const std::string extension = extensionLower(path);
    if (arrayElement && extension != ".json")
    {
        return std::unexpected(ParseError::UnsupportedFormat);
    }
    if (extension == ".json")
    {
        if (byteOffset != 0 || byteLength != 0)
        {
            return std::unexpected(ParseError::UnsupportedFormat);
        }
        try
        {
            const auto document = nlohmann::json::parse(input);
            const nlohmann::json *entry = &document;
            if (arrayElement)
            {
                if (!document.is_array() || lineNumber == 0 || lineNumber > document.size())
                {
                    return std::unexpected(ParseError::UnsupportedFormat);
                }
                entry = &document.at(lineNumber - 1);
            }
            else if (document.is_array() || lineNumber != 1)
            {
                return std::unexpected(ParseError::UnsupportedFormat);
            }
            const std::string field{jsonField};
            if (!entry->contains(field) || !entry->at(field).is_string())
            {
                return std::unexpected(ParseError::UnsupportedFormat);
            }
            std::string text = entry->at(field).get<std::string>();
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
    input.seekg(static_cast<std::streamoff>(byteOffset));
    if (!input)
    {
        return std::unexpected(ParseError::FileOpenFailure);
    }
    uint64_t readLength = byteLength;
    if (extension != ".jsonl" && maxLineLength <= (std::numeric_limits<size_t>::max() - 4) / 4)
    {
        readLength = std::min<uint64_t>(byteLength, (maxLineLength * 4) + 4);
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
        try
        {
            const auto document = nlohmann::json::parse(text);
            const std::string field{jsonField};
            if (!document.contains(field) || !document.at(field).is_string())
            {
                return std::unexpected(ParseError::UnsupportedFormat);
            }
            text = document.at(field).get<std::string>();
        }
        catch (const nlohmann::json::exception &)
        {
            return std::unexpected(ParseError::UnsupportedFormat);
        }
    }
    else if (readLength == byteLength)
    {
        static_cast<void>(stripCarriageReturn(text));
    }
    limitValue(text, maxLineLength, nullptr);
    return text;
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
