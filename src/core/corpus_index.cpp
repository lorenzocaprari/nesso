// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/corpus_index.hpp>

#include <platform/file.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nesso::core
{
namespace
{

static_assert(std::endian::native == std::endian::little);

constexpr std::size_t HEADER_BYTES = 64;
constexpr std::size_t SOURCE_RECORD_BYTES = 32;
constexpr std::size_t LEGACY_SOURCE_RECORD_BYTES = 16;
constexpr std::size_t CHUNK_RECORD_BYTES = 32;
constexpr std::uint32_t MAX_MODEL_ID_BYTES = 1024;
constexpr std::uint64_t MAX_TEXT_BYTES = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MAX_DIMENSIONS = 65536;
constexpr std::uint64_t MAX_CHUNKS = 1'000'000;
constexpr std::uint64_t MAX_SOURCES = 1'000'000;
constexpr std::array<char, 4> MAGIC{'N', 'E', 'S', 'C'};
// Bit 63 marks a JSON array element. Older v2 files leave it clear, so they still read as lines.
constexpr std::uint64_t ARRAY_ELEMENT_BIT = std::uint64_t{1} << 63U;

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

static std::uint64_t storedLineNumber(const LogChunk &chunk)
{
    return chunk.arrayElement ? (chunk.lineNumber | ARRAY_ELEMENT_BIT) : chunk.lineNumber;
}

static void loadLineNumber(LogChunk &chunk, std::uint64_t stored)
{
    chunk.arrayElement = (stored & ARRAY_ELEMENT_BIT) != 0;
    chunk.lineNumber = stored & ~ARRAY_ELEMENT_BIT;
}

static SourceInfo sourceInfoFor(std::string_view path)
{
    SourceInfo info{.path = std::string(path)};
    if (path == "-")
    {
        return info;
    }

    std::error_code error;
    const std::uint64_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        return info;
    }
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error)
    {
        return info;
    }

    info.size = size;
    info.modifiedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
    info.tracked = true;
    return info;
}

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
    nesso::platform::MappedFile file;
    std::string modelId;
    std::uint32_t dimensions = 0;
    std::vector<SourceInfo> sources;
    std::vector<CorpusChunk> chunks;
    std::span<const float> matrix;
};

MappedCorpus::~MappedCorpus() = default;

MappedCorpus::MappedCorpus(MappedCorpus &&) noexcept = default;

MappedCorpus &MappedCorpus::operator=(MappedCorpus &&) noexcept = default;

MappedCorpus::MappedCorpus(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

// cppcheck-suppress unusedFunction
std::string_view MappedCorpus::modelId() const noexcept { return impl_->modelId; }

std::uint32_t MappedCorpus::dimensions() const noexcept { return impl_->dimensions; }

std::span<const CorpusChunk> MappedCorpus::chunks() const noexcept { return impl_->chunks; }

std::span<const SourceInfo> MappedCorpus::sources() const noexcept { return impl_->sources; }

std::span<const float> MappedCorpus::matrix() const noexcept { return impl_->matrix; }

static bool writeRaw(std::ostream &out, const void *data, std::size_t size)
{
    if (size == 0)
    {
        return true;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

static bool writeU32(std::ostream &out, std::uint32_t value)
{
    const std::array<unsigned char, 4> bytes{static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8U),
                                             static_cast<unsigned char>(value >> 16U),
                                             static_cast<unsigned char>(value >> 24U)};
    return writeRaw(out, bytes.data(), bytes.size());
}

static bool writeU64(std::ostream &out, std::uint64_t value)
{
    const std::array<unsigned char, 8> bytes{
        static_cast<unsigned char>(value),        static_cast<unsigned char>(value >> 8U),
        static_cast<unsigned char>(value >> 16U), static_cast<unsigned char>(value >> 24U),
        static_cast<unsigned char>(value >> 32U), static_cast<unsigned char>(value >> 40U),
        static_cast<unsigned char>(value >> 48U), static_cast<unsigned char>(value >> 56U)};
    return writeRaw(out, bytes.data(), bytes.size());
}

static bool readExact(std::istream &in, void *data, std::size_t size)
{
    if (size == 0)
    {
        return true;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    in.read(reinterpret_cast<char *>(data), static_cast<std::streamsize>(size));
    return std::cmp_equal(in.gcount(), size);
}

static bool readU32Stream(std::istream &in, std::uint32_t &value)
{
    unsigned char b0 = 0;
    unsigned char b1 = 0;
    unsigned char b2 = 0;
    unsigned char b3 = 0;
    if (!readExact(in, &b0, 1) || !readExact(in, &b1, 1) || !readExact(in, &b2, 1) || !readExact(in, &b3, 1))
    {
        return false;
    }
    value = static_cast<std::uint32_t>(b0) | (static_cast<std::uint32_t>(b1) << 8U) |
            (static_cast<std::uint32_t>(b2) << 16U) | (static_cast<std::uint32_t>(b3) << 24U);
    return true;
}

static bool readU64Stream(std::istream &in, std::uint64_t &value)
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (!readU32Stream(in, low) || !readU32Stream(in, high))
    {
        return false;
    }
    value = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32U);
    return true;
}

struct SpilledChunk
{
    std::string source;
    std::uint64_t storedLine = 0;
    std::uint64_t byteOffset = 0;
    std::string text;
    std::vector<float> embedding;
};

static std::expected<bool, EngineError> readSpilled(std::istream &in, SpilledChunk &chunk)
{
    if (in.peek() == EOF)
    {
        return false;
    }
    std::uint32_t sourceLength = 0;
    if (!readU32Stream(in, sourceLength) || sourceLength > MAX_TEXT_BYTES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    chunk.source.assign(sourceLength, '\0');
    if (!readExact(in, chunk.source.data(), sourceLength) || !readU64Stream(in, chunk.storedLine) ||
        !readU64Stream(in, chunk.byteOffset))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    std::uint32_t textLength = 0;
    if (!readU32Stream(in, textLength) || textLength > MAX_TEXT_BYTES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    chunk.text.assign(textLength, '\0');
    std::uint32_t dimensions = 0;
    if (!readExact(in, chunk.text.data(), textLength) || !readU32Stream(in, dimensions) || dimensions > MAX_DIMENSIONS)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    chunk.embedding.resize(dimensions);
    if (!readExact(in, chunk.embedding.data(), dimensions * sizeof(float)))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return true;
}

CorpusWriter::~CorpusWriter()
{
    if (open_)
    {
        records_.close();
        std::error_code error;
        std::filesystem::remove(recordsPath_, error);
        open_ = false;
    }
}

// A defaulted move would copy open_, so both objects would delete the spill file.
CorpusWriter::CorpusWriter(CorpusWriter &&other) noexcept
    : destination_(std::move(other.destination_)), recordsPath_(std::move(other.recordsPath_)),
      records_(std::move(other.records_)), modelId_(std::move(other.modelId_)), dimensions_(other.dimensions_),
      chunkCount_(other.chunkCount_), open_(other.open_)
{
    other.open_ = false;
}

CorpusWriter &CorpusWriter::operator=(CorpusWriter &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    if (open_)
    {
        records_.close();
        std::error_code error;
        std::filesystem::remove(recordsPath_, error);
    }
    destination_ = std::move(other.destination_);
    recordsPath_ = std::move(other.recordsPath_);
    records_ = std::move(other.records_);
    modelId_ = std::move(other.modelId_);
    dimensions_ = other.dimensions_;
    chunkCount_ = other.chunkCount_;
    open_ = other.open_;
    other.open_ = false;
    return *this;
}

std::expected<CorpusWriter, EngineError> CorpusWriter::create(const std::filesystem::path &path,
                                                              std::string_view modelId)
{
    if (modelId.size() > MAX_MODEL_ID_BYTES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    std::error_code directoryError;
    if (std::filesystem::is_directory(path, directoryError))
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    CorpusWriter writer;
    writer.destination_ = path;
    writer.recordsPath_ = path.string() + ".records";
    writer.records_.open(writer.recordsPath_, std::ios::binary | std::ios::trunc);
    if (!writer.records_)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    writer.modelId_ = std::string{modelId};
    writer.open_ = true;
    return writer;
}

std::expected<void, EngineError> CorpusWriter::add(std::span<const CorpusChunk> chunks)
{
    if (chunks.empty())
    {
        return {};
    }
    const auto dimensions = static_cast<std::uint32_t>(chunks.front().embedding.size());
    if (dimensions == 0)
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (dimensions_ == 0)
    {
        dimensions_ = dimensions;
    }
    if (dimensions != dimensions_)
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (chunkCount_ > MAX_CHUNKS - chunks.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    for (const CorpusChunk &chunk : chunks)
    {
        if (chunk.embedding.size() != dimensions || chunk.chunk.text.size() > MAX_TEXT_BYTES ||
            chunk.chunk.source.size() > MAX_TEXT_BYTES)
        {
            return std::unexpected(chunk.embedding.size() == dimensions ? EngineError::CorruptDatabase
                                                                        : EngineError::MismatchedDimensions);
        }
        const std::uint64_t storedLine = storedLineNumber(chunk.chunk);
        if (!writeU32(records_, static_cast<std::uint32_t>(chunk.chunk.source.size())) ||
            !writeRaw(records_, chunk.chunk.source.data(), chunk.chunk.source.size()) ||
            !writeU64(records_, storedLine) || !writeU64(records_, chunk.chunk.byteOffset) ||
            !writeU32(records_, static_cast<std::uint32_t>(chunk.chunk.text.size())) ||
            !writeRaw(records_, chunk.chunk.text.data(), chunk.chunk.text.size()) || !writeU32(records_, dimensions) ||
            !writeRaw(records_, chunk.embedding.data(), chunk.embedding.size() * sizeof(float)))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        ++chunkCount_;
    }
    return {};
}

std::expected<void, EngineError> CorpusWriter::finish()
{
    records_.close();
    std::ifstream records(recordsPath_, std::ios::binary);
    if (!records)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }

    std::vector<SourceInfo> sources;
    std::unordered_map<std::string, std::uint32_t> sourceIndex;
    std::uint64_t textBytes = 0;
    std::uint64_t seen = 0;
    SpilledChunk spilled;
    while (true)
    {
        const auto more = readSpilled(records, spilled);
        if (!more)
        {
            return std::unexpected(more.error());
        }
        if (!*more)
        {
            break;
        }
        if (spilled.embedding.size() != dimensions_)
        {
            return std::unexpected(EngineError::MismatchedDimensions);
        }
        textBytes += spilled.text.size();
        const auto [it, inserted] = sourceIndex.emplace(spilled.source, static_cast<std::uint32_t>(sources.size()));
        if (inserted)
        {
            sources.push_back(sourceInfoFor(spilled.source));
        }
        ++seen;
        (void)it;
    }
    if (seen != chunkCount_ || sources.size() > MAX_SOURCES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    const std::uint64_t modelPad = (8 - (modelId_.size() % 8)) % 8;
    const std::uint64_t sourceTableOffset = HEADER_BYTES + modelId_.size() + modelPad;
    std::uint64_t sourceBlob = sourceTableOffset + (sources.size() * SOURCE_RECORD_BYTES);
    std::vector<std::uint64_t> sourceOffsets;
    sourceOffsets.reserve(sources.size());
    for (const SourceInfo &source : sources)
    {
        sourceOffsets.push_back(sourceBlob);
        sourceBlob += source.path.size();
    }
    const std::uint64_t chunkTableOffset = sourceBlob;
    const std::uint64_t textStart = chunkTableOffset + (chunkCount_ * CHUNK_RECORD_BYTES);
    const std::uint64_t matrixOffset = alignUp(textStart + textBytes, 64);

    const std::filesystem::path temporary = destination_.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    const auto fail = [&]()
    {
        output.close();
        std::filesystem::remove(temporary);
        return std::unexpected(EngineError::FileOpenFailure);
    };
    if (!writeRaw(output, MAGIC.data(), MAGIC.size()) || !writeU32(output, CORPUS_VERSION) ||
        !writeU32(output, static_cast<std::uint32_t>(modelId_.size())) || !writeU32(output, dimensions_) ||
        !writeU64(output, sources.size()) || !writeU64(output, chunkCount_) || !writeU64(output, sourceTableOffset) ||
        !writeU64(output, chunkTableOffset) || !writeU64(output, matrixOffset) || !writeU64(output, 0) ||
        !writeRaw(output, modelId_.data(), modelId_.size()))
    {
        return fail();
    }
    const std::vector<char> padding(static_cast<std::size_t>(modelPad), '\0');
    if (!writeRaw(output, padding.data(), padding.size()))
    {
        return fail();
    }
    for (std::size_t index = 0; index < sources.size(); ++index)
    {
        if (!writeU64(output, sourceOffsets[index]) ||
            !writeU32(output, static_cast<std::uint32_t>(sources[index].path.size())) ||
            !writeU32(output, sources[index].tracked ? 1U : 0U) || !writeU64(output, sources[index].size) ||
            !writeU64(output, static_cast<std::uint64_t>(sources[index].modifiedTime)))
        {
            return fail();
        }
    }
    for (const SourceInfo &source : sources)
    {
        if (!writeRaw(output, source.path.data(), source.path.size()))
        {
            return fail();
        }
    }

    records.clear();
    records.seekg(0);
    std::uint64_t textCursor = textStart;
    while (true)
    {
        const auto more = readSpilled(records, spilled);
        if (!more)
        {
            return std::unexpected(more.error());
        }
        if (!*more)
        {
            break;
        }
        const auto found = sourceIndex.find(spilled.source);
        if (found == sourceIndex.end() || !writeU64(output, spilled.storedLine) ||
            !writeU64(output, spilled.byteOffset) || !writeU32(output, found->second) ||
            !writeU32(output, static_cast<std::uint32_t>(spilled.text.size())) || !writeU64(output, textCursor))
        {
            return fail();
        }
        textCursor += spilled.text.size();
    }

    records.clear();
    records.seekg(0);
    while (true)
    {
        const auto more = readSpilled(records, spilled);
        if (!more)
        {
            return std::unexpected(more.error());
        }
        if (!*more)
        {
            break;
        }
        if (!writeRaw(output, spilled.text.data(), spilled.text.size()))
        {
            return fail();
        }
    }
    if (textCursor > matrixOffset)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const std::vector<char> matrixPad(static_cast<std::size_t>(matrixOffset - textCursor), '\0');
    if (!writeRaw(output, matrixPad.data(), matrixPad.size()))
    {
        return fail();
    }

    records.clear();
    records.seekg(0);
    while (true)
    {
        const auto more = readSpilled(records, spilled);
        if (!more)
        {
            return std::unexpected(more.error());
        }
        if (!*more)
        {
            break;
        }
        if (!writeRaw(output, spilled.embedding.data(), spilled.embedding.size() * sizeof(float)))
        {
            return fail();
        }
    }
    output.close();
    if (!output)
    {
        std::filesystem::remove(temporary);
        return std::unexpected(EngineError::FileOpenFailure);
    }
    records.close();
    std::filesystem::remove(recordsPath_);
    open_ = false;
    if (const auto replaced = nesso::platform::replaceFileAtomically(temporary, destination_); !replaced)
    {
        std::filesystem::remove(temporary);
        return std::unexpected(EngineError::FileOpenFailure);
    }
    return {};
}

// cppcheck-suppress unusedFunction
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

    std::vector<SourceInfo> sources;
    std::unordered_map<std::string_view, std::uint32_t> sourceIndex;
    std::vector<std::uint32_t> chunkSource;
    chunkSource.reserve(chunks.size());
    for (const CorpusChunk &chunk : chunks)
    {
        const auto [it, inserted] = sourceIndex.emplace(chunk.chunk.source, static_cast<std::uint32_t>(sources.size()));
        if (inserted)
        {
            sources.push_back(sourceInfoFor(chunk.chunk.source));
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
    for (const SourceInfo &source : sources)
    {
        sourceOffsets.push_back(sourceBlob);
        sourceBlob += source.path.size();
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
        appendU32(bytes, static_cast<std::uint32_t>(sources[index].path.size()));
        appendU32(bytes, sources[index].tracked ? 1U : 0U);
        appendU64(bytes, sources[index].size);
        appendU64(bytes, static_cast<std::uint64_t>(sources[index].modifiedTime));
    }
    for (const SourceInfo &source : sources)
    {
        appendBytes(bytes, source.path.data(), source.path.size());
    }
    for (std::size_t index = 0; index < chunks.size(); ++index)
    {
        const CorpusChunk &chunk = chunks[index];
        appendU64(bytes, storedLineNumber(chunk.chunk));
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
    if (const auto replaced = nesso::platform::replaceFileAtomically(temporary, path); !replaced)
    {
        std::filesystem::remove(temporary);
        return std::unexpected(EngineError::FileOpenFailure);
    }
    return {};
}

std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path)
{
    auto mapped = nesso::platform::MappedFile::open(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error() == nesso::platform::FileError::OpenFailure
                                   ? EngineError::FileOpenFailure
                                   : EngineError::CorruptDatabase);
    }
    const std::span<const std::byte> bytes = mapped->bytes();
    if (bytes.size() < HEADER_BYTES || std::memcmp(bytes.data(), MAGIC.data(), MAGIC.size()) != 0)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    Layout layout;
    std::uint32_t version = 0;
    if (!readU32(bytes, 4, version))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (version != CORPUS_VERSION)
    {
        return std::unexpected(EngineError::UnsupportedVersion);
    }
    if (!readU32(bytes, 8, layout.modelIdBytes) || !readU32(bytes, 12, layout.dimensions) ||
        !readU64(bytes, 16, layout.sourceCount) || !readU64(bytes, 24, layout.chunkCount) ||
        !readU64(bytes, 32, layout.sourceTableOffset) || !readU64(bytes, 40, layout.chunkTableOffset) ||
        !readU64(bytes, 48, layout.matrixOffset))
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
    // Offsets are file input. A value past the mapping is a corrupt corpus, not a broken invariant.
    if (layout.sourceTableOffset < HEADER_BYTES || layout.sourceTableOffset > layout.chunkTableOffset ||
        layout.chunkTableOffset > layout.matrixOffset || layout.matrixOffset > bytes.size() ||
        !rangeInside(layout.matrixOffset, matrixBytes, bytes.size()) ||
        layout.matrixOffset + matrixBytes != bytes.size() ||
        !rangeInside(HEADER_BYTES, layout.modelIdBytes, layout.sourceTableOffset))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    auto impl = std::make_unique<MappedCorpus::Impl>();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    impl->modelId.assign(reinterpret_cast<const char *>(bytes.data() + HEADER_BYTES), layout.modelIdBytes);
    impl->dimensions = layout.dimensions;

    const auto validSourceTable = [&](std::size_t recordBytes)
    {
        if (layout.sourceTableOffset > layout.chunkTableOffset ||
            layout.sourceCount > (layout.chunkTableOffset - layout.sourceTableOffset) / recordBytes)
        {
            return false;
        }
        const std::uint64_t sourceBlob = layout.sourceTableOffset + (layout.sourceCount * recordBytes);
        for (std::uint64_t index = 0; index < layout.sourceCount; ++index)
        {
            const std::uint64_t record = layout.sourceTableOffset + (index * recordBytes);
            std::uint64_t stringOffset = 0;
            std::uint32_t length = 0;
            if (!readU64(bytes, record, stringOffset) || !readU32(bytes, record + 8, length) ||
                length > MAX_TEXT_BYTES || !rangeInside(stringOffset, length, layout.chunkTableOffset) ||
                stringOffset < sourceBlob)
            {
                return false;
            }
        }
        return true;
    };

    const std::size_t sourceRecordBytes =
        validSourceTable(SOURCE_RECORD_BYTES) ? SOURCE_RECORD_BYTES : LEGACY_SOURCE_RECORD_BYTES;
    if (!validSourceTable(sourceRecordBytes))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    impl->sources.reserve(static_cast<std::size_t>(layout.sourceCount));
    for (std::uint64_t index = 0; index < layout.sourceCount; ++index)
    {
        const std::uint64_t record = layout.sourceTableOffset + (index * sourceRecordBytes);
        std::uint64_t stringOffset = 0;
        std::uint32_t length = 0;
        if (!readU64(bytes, record, stringOffset) || !readU32(bytes, record + 8, length))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        SourceInfo source;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        source.path.assign(reinterpret_cast<const char *>(bytes.data() + stringOffset), length);
        if (sourceRecordBytes == SOURCE_RECORD_BYTES)
        {
            std::uint32_t flags = 0;
            std::uint64_t modifiedTime = 0;
            if (!readU32(bytes, record + 12, flags) || !readU64(bytes, record + 16, source.size) ||
                !readU64(bytes, record + 24, modifiedTime) || (flags & ~1U) != 0)
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
            source.tracked = (flags & 1U) != 0;
            source.modifiedTime = static_cast<std::int64_t>(modifiedTime);
        }
        impl->sources.push_back(std::move(source));
    }

    impl->chunks.reserve(static_cast<std::size_t>(layout.chunkCount));
    for (std::uint64_t index = 0; index < layout.chunkCount; ++index)
    {
        const std::uint64_t record = layout.chunkTableOffset + (index * CHUNK_RECORD_BYTES);
        CorpusChunk chunk;
        std::uint32_t sourceIndex = 0;
        std::uint32_t textLength = 0;
        std::uint64_t textOffset = 0;
        std::uint64_t storedLine = 0;
        if (!readU64(bytes, record, storedLine) || !readU64(bytes, record + 8, chunk.chunk.byteOffset) ||
            !readU32(bytes, record + 16, sourceIndex) || !readU32(bytes, record + 20, textLength) ||
            !readU64(bytes, record + 24, textOffset) || sourceIndex >= impl->sources.size() ||
            textLength > MAX_TEXT_BYTES || !rangeInside(textOffset, textLength, layout.matrixOffset))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        loadLineNumber(chunk.chunk, storedLine);
        chunk.chunk.source = impl->sources[sourceIndex].path;
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

// cppcheck-suppress unusedFunction
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

} // namespace nesso::core
