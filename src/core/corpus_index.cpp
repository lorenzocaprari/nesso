// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/core/corpus_index.hpp"

#include <platform/file.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core
{
namespace
{

static_assert(std::endian::native == std::endian::little);

constexpr std::size_t HEADER_BYTES = 64;
constexpr std::size_t SOURCE_RECORD_BYTES = 16;
constexpr std::size_t CHUNK_RECORD_BYTES = 32;
constexpr std::uint32_t MAX_MODEL_ID_BYTES = 1024;
constexpr std::uint64_t MAX_TEXT_BYTES = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MAX_DIMENSIONS = 65536;
constexpr std::uint64_t MAX_CHUNKS = 1'000'000;
constexpr std::uint64_t MAX_SOURCES = 1'000'000;
constexpr std::array<char, 4> MAGIC{'N', 'E', 'S', 'C'};

struct Layout
{
    std::uint32_t modelIdBytes = 0;
    std::uint32_t dimensions = 0;
    std::uint64_t sourceCount = 0;
    std::uint64_t chunkCount = 0;
    std::uint64_t sourceTableOffset = 0;
    std::uint64_t chunkTableOffset = 0;
    std::uint64_t matrixOffset = 0;
};

} // namespace

static std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static void appendBytes(std::vector<std::byte> &out, const void *data, std::size_t size)
{
    const auto *bytes = static_cast<const std::byte *>(data);
    out.insert(out.end(), bytes, bytes + size);
}

static void appendU32(std::vector<std::byte> &out, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        out.push_back(static_cast<std::byte>(value >> shift));
    }
}

static void appendU64(std::vector<std::byte> &out, std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8)
    {
        out.push_back(static_cast<std::byte>(value >> shift));
    }
}

static bool readU32(std::span<const std::byte> bytes, std::uint64_t offset, std::uint32_t &value)
{
    if (bytes.size() < sizeof(std::uint32_t) || offset > bytes.size() - sizeof(std::uint32_t))
    {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset])) << shift;
        ++offset;
    }
    return true;
}

static bool readU64(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t &value)
{
    if (bytes.size() < sizeof(std::uint64_t) || offset > bytes.size() - sizeof(std::uint64_t))
    {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
    {
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset])) << shift;
        ++offset;
    }
    return true;
}

static bool rangeInside(std::uint64_t offset, std::uint64_t length, std::uint64_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static std::expected<std::uint64_t, EngineError> checkedDimensions(std::span<const CorpusChunk> chunks,
                                                                   std::string_view modelId)
{
    if (modelId.size() > MAX_MODEL_ID_BYTES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (chunks.empty())
    {
        return 0;
    }
    const std::uint64_t dimensions = chunks.front().embedding.size();
    if (dimensions == 0)
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (dimensions > MAX_DIMENSIONS || chunks.size() > MAX_CHUNKS)
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
    return dimensions;
}

struct MappedCorpus::Impl
{
    platform::MappedFile file;
    std::string modelId;
    std::uint32_t dimensions = 0;
    std::vector<CorpusChunk> chunks;
    std::span<const float> matrix;
};

MappedCorpus::~MappedCorpus() = default;

MappedCorpus::MappedCorpus(MappedCorpus &&) noexcept = default;

MappedCorpus &MappedCorpus::operator=(MappedCorpus &&) noexcept = default;

MappedCorpus::MappedCorpus(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::string_view MappedCorpus::modelId() const noexcept { return impl_->modelId; }

std::uint32_t MappedCorpus::dimensions() const noexcept { return impl_->dimensions; }

std::span<const CorpusChunk> MappedCorpus::chunks() const noexcept { return impl_->chunks; }

std::span<const float> MappedCorpus::matrix() const noexcept { return impl_->matrix; }

std::expected<void, EngineError> writeCorpusFile(const std::filesystem::path &path, std::span<const CorpusChunk> chunks,
                                                 std::string_view modelId)
{
    const auto dimensions = checkedDimensions(chunks, modelId);
    if (!dimensions)
    {
        return std::unexpected(dimensions.error());
    }
    std::error_code directoryError;
    if (std::filesystem::is_directory(path, directoryError))
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }

    std::vector<std::string_view> sources;
    std::unordered_map<std::string_view, std::uint32_t> sourceIndex;
    std::vector<std::uint32_t> chunkSource;
    chunkSource.reserve(chunks.size());
    for (const CorpusChunk &chunk : chunks)
    {
        const auto [it, inserted] = sourceIndex.emplace(chunk.chunk.source, static_cast<std::uint32_t>(sources.size()));
        if (inserted)
        {
            sources.push_back(chunk.chunk.source);
        }
        chunkSource.push_back(it->second);
    }
    if (sources.size() > MAX_SOURCES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    const std::uint64_t modelPad = (8 - (modelId.size() % 8)) % 8;
    const std::uint64_t sourceTableOffset = HEADER_BYTES + modelId.size() + modelPad;
    std::uint64_t sourceBlob = sourceTableOffset + (sources.size() * SOURCE_RECORD_BYTES);
    std::vector<std::uint64_t> sourceOffsets;
    sourceOffsets.reserve(sources.size());
    for (const std::string_view source : sources)
    {
        sourceOffsets.push_back(sourceBlob);
        sourceBlob += source.size();
    }

    const std::uint64_t chunkTableOffset = sourceBlob;
    std::uint64_t textBlob = chunkTableOffset + (chunks.size() * CHUNK_RECORD_BYTES);
    std::vector<std::uint64_t> textOffsets;
    textOffsets.reserve(chunks.size());
    for (const CorpusChunk &chunk : chunks)
    {
        textOffsets.push_back(textBlob);
        textBlob += chunk.chunk.text.size();
    }
    const std::uint64_t matrixOffset = alignUp(textBlob, 64);

    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(matrixOffset) +
                  (chunks.size() * static_cast<std::size_t>(*dimensions) * sizeof(float)));
    appendBytes(bytes, MAGIC.data(), MAGIC.size());
    appendU32(bytes, CORPUS_VERSION);
    appendU32(bytes, static_cast<std::uint32_t>(modelId.size()));
    appendU32(bytes, static_cast<std::uint32_t>(*dimensions));
    appendU64(bytes, sources.size());
    appendU64(bytes, chunks.size());
    appendU64(bytes, sourceTableOffset);
    appendU64(bytes, chunkTableOffset);
    appendU64(bytes, matrixOffset);
    appendU64(bytes, 0);
    appendBytes(bytes, modelId.data(), modelId.size());
    bytes.resize(bytes.size() + static_cast<std::size_t>(modelPad));

    for (std::size_t index = 0; index < sources.size(); ++index)
    {
        appendU64(bytes, sourceOffsets[index]);
        appendU32(bytes, static_cast<std::uint32_t>(sources[index].size()));
        appendU32(bytes, 0);
    }
    for (const std::string_view source : sources)
    {
        appendBytes(bytes, source.data(), source.size());
    }
    for (std::size_t index = 0; index < chunks.size(); ++index)
    {
        const CorpusChunk &chunk = chunks[index];
        appendU64(bytes, chunk.chunk.lineNumber);
        appendU64(bytes, chunk.chunk.byteOffset);
        appendU32(bytes, chunkSource[index]);
        appendU32(bytes, static_cast<std::uint32_t>(chunk.chunk.text.size()));
        appendU64(bytes, textOffsets[index]);
    }
    for (const CorpusChunk &chunk : chunks)
    {
        appendBytes(bytes, chunk.chunk.text.data(), chunk.chunk.text.size());
    }
    bytes.resize(static_cast<std::size_t>(matrixOffset));
    for (const CorpusChunk &chunk : chunks)
    {
        appendBytes(bytes, chunk.embedding.data(), chunk.embedding.size() * sizeof(float));
    }

    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output)
        {
            std::filesystem::remove(temporary);
            return std::unexpected(EngineError::FileOpenFailure);
        }
    }
    if (const auto replaced = platform::replaceFileAtomically(temporary, path); !replaced)
    {
        std::filesystem::remove(temporary);
        return std::unexpected(EngineError::FileOpenFailure);
    }
    return {};
}

std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path)
{
    auto mapped = platform::MappedFile::open(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error() == platform::FileError::OpenFailure ? EngineError::FileOpenFailure
                                                                                  : EngineError::CorruptDatabase);
    }
    const std::span<const std::byte> bytes = mapped->bytes();
    if (bytes.size() < HEADER_BYTES || std::memcmp(bytes.data(), MAGIC.data(), MAGIC.size()) != 0)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    Layout layout;
    std::uint32_t version = 0;
    if (!readU32(bytes, 4, version) || version != CORPUS_VERSION || !readU32(bytes, 8, layout.modelIdBytes) ||
        !readU32(bytes, 12, layout.dimensions) || !readU64(bytes, 16, layout.sourceCount) ||
        !readU64(bytes, 24, layout.chunkCount) || !readU64(bytes, 32, layout.sourceTableOffset) ||
        !readU64(bytes, 40, layout.chunkTableOffset) || !readU64(bytes, 48, layout.matrixOffset))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (layout.modelIdBytes > MAX_MODEL_ID_BYTES || layout.sourceCount > MAX_SOURCES ||
        layout.chunkCount > MAX_CHUNKS || layout.dimensions > MAX_DIMENSIONS ||
        (layout.chunkCount > 0 && layout.dimensions == 0) || (layout.chunkCount == 0 && layout.dimensions != 0) ||
        layout.matrixOffset % 64 != 0)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (layout.dimensions != 0 &&
        layout.chunkCount > std::numeric_limits<std::uint64_t>::max() / layout.dimensions / sizeof(float))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const std::uint64_t matrixBytes = layout.chunkCount * layout.dimensions * sizeof(float);
    if (!rangeInside(layout.matrixOffset, matrixBytes, bytes.size()) ||
        layout.matrixOffset + matrixBytes != bytes.size() ||
        !rangeInside(HEADER_BYTES, layout.modelIdBytes, layout.sourceTableOffset))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    auto impl = std::make_unique<MappedCorpus::Impl>();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    impl->modelId.assign(reinterpret_cast<const char *>(bytes.data() + HEADER_BYTES), layout.modelIdBytes);
    impl->dimensions = layout.dimensions;

    std::vector<std::string> sources;
    sources.reserve(static_cast<std::size_t>(layout.sourceCount));
    for (std::uint64_t index = 0; index < layout.sourceCount; ++index)
    {
        const std::uint64_t record = layout.sourceTableOffset + (index * SOURCE_RECORD_BYTES);
        std::uint64_t stringOffset = 0;
        std::uint32_t length = 0;
        if (!readU64(bytes, record, stringOffset) || !readU32(bytes, record + 8, length) || length > MAX_TEXT_BYTES ||
            !rangeInside(stringOffset, length, layout.matrixOffset))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        sources.emplace_back(reinterpret_cast<const char *>(bytes.data() + stringOffset), length);
    }

    impl->chunks.reserve(static_cast<std::size_t>(layout.chunkCount));
    for (std::uint64_t index = 0; index < layout.chunkCount; ++index)
    {
        const std::uint64_t record = layout.chunkTableOffset + (index * CHUNK_RECORD_BYTES);
        CorpusChunk chunk;
        std::uint32_t sourceIndex = 0;
        std::uint32_t textLength = 0;
        std::uint64_t textOffset = 0;
        if (!readU64(bytes, record, chunk.chunk.lineNumber) || !readU64(bytes, record + 8, chunk.chunk.byteOffset) ||
            !readU32(bytes, record + 16, sourceIndex) || !readU32(bytes, record + 20, textLength) ||
            !readU64(bytes, record + 24, textOffset) || sourceIndex >= sources.size() || textLength > MAX_TEXT_BYTES ||
            !rangeInside(textOffset, textLength, layout.matrixOffset))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        chunk.chunk.source = sources[sourceIndex];
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        chunk.chunk.text.assign(reinterpret_cast<const char *>(bytes.data() + textOffset), textLength);
        impl->chunks.push_back(std::move(chunk));
    }

    impl->file = std::move(*mapped);
    const std::span<const std::byte> stable = impl->file.bytes();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    impl->matrix = {reinterpret_cast<const float *>(stable.data() + layout.matrixOffset),
                    static_cast<std::size_t>(layout.chunkCount * layout.dimensions)};
    return MappedCorpus{std::move(impl)};
}

std::expected<std::vector<CorpusChunk>, EngineError> readCorpusFile(const std::filesystem::path &path)
{
    auto mapped = mapCorpusFile(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error());
    }
    std::vector<CorpusChunk> chunks(mapped->chunks().begin(), mapped->chunks().end());
    const std::span<const float> matrix = mapped->matrix();
    const std::size_t dimensions = mapped->dimensions();
    for (std::size_t index = 0; index < chunks.size(); ++index)
    {
        const auto begin = matrix.begin() + static_cast<std::ptrdiff_t>(index * dimensions);
        chunks[index].embedding.assign(begin, begin + static_cast<std::ptrdiff_t>(dimensions));
    }
    return chunks;
}

} // namespace core
