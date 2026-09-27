// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/core/corpus_index.hpp"

#include <fstream>

namespace core
{

static constexpr uint64_t MAX_TEXT_BYTES = 8ULL * 1024ULL * 1024ULL;
static constexpr uint64_t MAX_DIMENSIONS = 65536;
static constexpr uint64_t MAX_CHUNKS = 1'000'000;

#pragma pack(push, 1)
struct CorpusHeader
{
    std::array<uint8_t, 4> magic = CORPUS_MAGIC;
    uint32_t version = CORPUS_VERSION;
    uint64_t dimensions = 0;
    uint64_t chunkCount = 0;
};
#pragma pack(pop)

template <typename T> static bool writePod(std::ostream &output, const T &value)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(&value), static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(output);
}

template <typename T> static bool readPod(std::istream &input, T &value)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    input.read(reinterpret_cast<char *>(&value), static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(input);
}

static bool writeBytes(std::ostream &output, std::string_view bytes)
{
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

static std::expected<std::string, EngineError> readBytes(std::istream &input, uint64_t size)
{
    if (size > MAX_TEXT_BYTES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    std::string bytes(static_cast<size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(size));
    if (!input)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return bytes;
}

std::expected<void, EngineError> writeCorpusFile(const std::filesystem::path &path, std::span<const CorpusChunk> chunks)
{
    uint64_t dimensions = 0;
    if (!chunks.empty())
    {
        dimensions = chunks.front().embedding.size();
        if (dimensions == 0)
        {
            return std::unexpected(EngineError::DatabaseNotInitialized);
        }
        if (dimensions > MAX_DIMENSIONS)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        for (const CorpusChunk &chunk : chunks)
        {
            if (chunk.embedding.size() != dimensions)
            {
                return std::unexpected(EngineError::MismatchedDimensions);
            }
            if (chunk.chunk.source.size() > MAX_TEXT_BYTES || chunk.chunk.text.size() > MAX_TEXT_BYTES)
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }

    CorpusHeader header{};
    header.dimensions = dimensions;
    header.chunkCount = chunks.size();
    if (!writePod(output, header))
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }

    for (const CorpusChunk &chunk : chunks)
    {
        const uint64_t sourceSize = chunk.chunk.source.size();
        const uint64_t textSize = chunk.chunk.text.size();
        if (!writePod(output, chunk.chunk.lineNumber) || !writePod(output, sourceSize) || !writePod(output, textSize) ||
            !writeBytes(output, chunk.chunk.source) || !writeBytes(output, chunk.chunk.text))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        output.write(reinterpret_cast<const char *>(chunk.embedding.data()),
                     static_cast<std::streamsize>(chunk.embedding.size() * sizeof(float)));
        if (!output)
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
    }
    return {};
}

std::expected<std::vector<CorpusChunk>, EngineError> readCorpusFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }

    CorpusHeader header{};
    if (!readPod(input, header) || header.magic != CORPUS_MAGIC || header.version != CORPUS_VERSION)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (header.chunkCount > MAX_CHUNKS ||
        (header.chunkCount > 0 && (header.dimensions == 0 || header.dimensions > MAX_DIMENSIONS)))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (header.chunkCount == 0)
    {
        if (input.peek() != std::char_traits<char>::eof())
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        return std::vector<CorpusChunk>{};
    }

    std::vector<CorpusChunk> chunks;
    chunks.reserve(static_cast<size_t>(header.chunkCount));
    for (uint64_t index = 0; index < header.chunkCount; ++index)
    {
        CorpusChunk chunk;
        uint64_t sourceSize = 0;
        uint64_t textSize = 0;
        if (!readPod(input, chunk.chunk.lineNumber) || !readPod(input, sourceSize) || !readPod(input, textSize))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        auto source = readBytes(input, sourceSize);
        if (!source)
        {
            return std::unexpected(source.error());
        }
        auto text = readBytes(input, textSize);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        chunk.chunk.source = std::move(*source);
        chunk.chunk.text = std::move(*text);
        chunk.embedding.resize(static_cast<size_t>(header.dimensions));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        input.read(reinterpret_cast<char *>(chunk.embedding.data()),
                   static_cast<std::streamsize>(chunk.embedding.size() * sizeof(float)));
        if (!input)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        chunks.push_back(std::move(chunk));
    }
    if (input.peek() != std::char_traits<char>::eof())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return chunks;
}

} // namespace core
