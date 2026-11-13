// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "corpus_detail.hpp"

#include <core/corpus_index.hpp>

#include <parser/archive_source.hpp>
#include <parser/gzip_reader.hpp>
#include <platform/file.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nesso::core
{

using namespace detail;

struct SpillFile
{
    std::filesystem::path path;
    std::ofstream output;
};

struct SpilledLine
{
    std::uint64_t lineNumber = 0;
    std::uint64_t byteOffset = 0;
    std::uint64_t byteLength = 0;
    std::uint32_t sourceIndex = 0;
    std::uint32_t distinctId = NO_DISTINCT;
    std::uint32_t flags = 0;
    std::string storedText;
};
static std::expected<SpillFile, EngineError> createSpill(const std::filesystem::path &destination,
                                                         std::string_view suffix)
{
    static std::atomic<std::uint64_t> counter{0};
    for (unsigned attempt = 0; attempt < 16; ++attempt)
    {
        const auto number = counter.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path path = destination.string() + "." + std::string{suffix} + "." +
                                           std::to_string(getpid()) + "." + std::to_string(number);
        errno = 0;
        std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::noreplace);
        if (output)
        {
            return SpillFile{.path = path, .output = std::move(output)};
        }
        if (errno != EEXIST)
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
    }
    return std::unexpected(EngineError::FileOpenFailure);
}

static void removeFile(const std::filesystem::path &path)
{
    if (!path.empty())
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
}

static bool readSpilledLine(std::istream &input, SpilledLine &line)
{
    std::uint32_t length = 0;
    if (!readU64Stream(input, line.lineNumber) || !readU64Stream(input, line.byteOffset) ||
        !readU64Stream(input, line.byteLength) || !readU32Stream(input, line.sourceIndex) ||
        !readU32Stream(input, line.distinctId) || !readU32Stream(input, line.flags) || !readU32Stream(input, length) ||
        length > MAX_TEXT_BYTES)
    {
        return false;
    }
    line.storedText.assign(length, '\0');
    return readExact(input, line.storedText.data(), length);
}

static bool readSpilledText(std::istream &input, std::string &text)
{
    std::uint32_t length = 0;
    if (!readU32Stream(input, length) || length > MAX_TEXT_BYTES)
    {
        return false;
    }
    text.assign(length, '\0');
    return readExact(input, text.data(), length);
}

static bool rewind(std::istream &input)
{
    input.clear();
    input.seekg(0);
    return static_cast<bool>(input);
}
static bool sealCorpus(const std::filesystem::path &path)
{
    CorpusChecksum checksum{};
    {
        auto mapped = nesso::platform::MappedFile::open(path);
        if (!mapped || mapped->bytes().size() < HEADER_BYTES)
        {
            return false;
        }
        checksum = ComputeCorpusChecksum(mapped->bytes());
    }
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(static_cast<std::streamoff>(CORPUS_CHECKSUM_OFFSET));
    if (!writeRaw(file, checksum.data(), checksum.size()))
    {
        return false;
    }
    file.close();
    return !file.fail();
}

std::expected<SourceInfo, EngineError> sourceInfoFor(const std::filesystem::path &path, std::string_view jsonField)
{
    SourceInfo source{.path = path.string(), .jsonField = std::string{jsonField}};
    if (path == "-")
    {
        source.storedOnly = true;
        return source;
    }
    std::error_code error;
    source.size = std::filesystem::file_size(path, error);
    if (error)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    source.modifiedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
    if (nesso::parser::isArchivePath(path))
    {
        // The bundle is one file for the staleness check. Each member is its own stored source.
        source.tracked = true;
        return source;
    }
    if (nesso::parser::isGzipPath(path))
    {
        // A gzip member is not a file-offset range. Store each line, as stdin and archive members do.
        source.storedOnly = true;
        return source;
    }
    source.tracked = true;
    return source;
}
struct CorpusWriter::Impl
{
    std::filesystem::path destination;
    std::filesystem::path temporary;
    CorpusPolicy policy;
    std::uint32_t dimensions = 0;
    SpillFile lines;
    SpillFile texts;
    SpillFile vectors;
    SpillFile terms;
    std::vector<SourceInfo> sources;
    std::unordered_set<std::string> distinctTexts;
    std::vector<std::uint32_t> uses;
    std::uint32_t vectorCount = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t textBytes = 0;
    std::uint64_t storedBytes = 0;
    std::uint32_t lastSource = 0;
    std::uint64_t lastLine = 0;
    bool hasLastLine = false;
    bool failed = false;
    bool finished = false;
};

CorpusWriter::CorpusWriter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
void CorpusWriter::discardSpills(Impl *impl) noexcept
{
    if (impl == nullptr)
    {
        return;
    }
    impl->lines.output.close();
    impl->texts.output.close();
    impl->vectors.output.close();
    impl->terms.output.close();
    removeFile(impl->lines.path);
    removeFile(impl->texts.path);
    removeFile(impl->vectors.path);
    removeFile(impl->terms.path);
    removeFile(impl->temporary);
}

CorpusWriter::~CorpusWriter() { discardSpills(impl_.get()); }
CorpusWriter::CorpusWriter(CorpusWriter &&) noexcept = default;
CorpusWriter &CorpusWriter::operator=(CorpusWriter &&other) noexcept
{
    if (this != &other)
    {
        discardSpills(impl_.get());
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<CorpusWriter, EngineError> CorpusWriter::create(const std::filesystem::path &path, CorpusPolicy policy)
{
    const auto dimensions = validatePolicy(policy);
    if (!dimensions)
    {
        return std::unexpected(dimensions.error());
    }
    std::error_code error;
    if (std::filesystem::is_directory(path, error))
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    auto impl = std::make_unique<Impl>();
    impl->destination = path;
    impl->policy = std::move(policy);
    impl->dimensions = *dimensions;
    auto lines = createSpill(path, "lines");
    if (!lines)
    {
        return std::unexpected(lines.error());
    }
    impl->lines = std::move(*lines);
    auto texts = createSpill(path, "texts");
    if (!texts)
    {
        return std::unexpected(texts.error());
    }
    impl->texts = std::move(*texts);
    auto vectors = createSpill(path, "vectors");
    if (!vectors)
    {
        return std::unexpected(vectors.error());
    }
    impl->vectors = std::move(*vectors);
    auto terms = createSpill(path, "terms");
    if (!terms)
    {
        return std::unexpected(terms.error());
    }
    impl->terms = std::move(*terms);
    return CorpusWriter{std::move(impl)};
}

std::expected<std::uint32_t, EngineError> CorpusWriter::addSource(SourceInfo source)
{
    if (impl_->failed || impl_->finished || impl_->sources.size() >= MAX_SOURCES || source.path.empty() ||
        source.path.size() > MAX_TEXT_BYTES || source.jsonField.size() > MAX_TEXT_BYTES ||
        source.path.find('\0') != std::string::npos || source.jsonField.find('\0') != std::string::npos)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const auto id = static_cast<std::uint32_t>(impl_->sources.size());
    impl_->sources.push_back(std::move(source));
    return id;
}

std::expected<std::uint32_t, EngineError> CorpusWriter::reserveDistinct(std::string_view text)
{
    if (impl_->failed || impl_->finished || impl_->uses.size() >= MAX_DISTINCT || text.size() > MAX_TEXT_BYTES ||
        impl_->distinctTexts.contains(std::string{text}))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (!writeU32(impl_->texts.output, static_cast<std::uint32_t>(text.size())) ||
        !writeRaw(impl_->texts.output, text.data(), text.size()))
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    const auto id = static_cast<std::uint32_t>(impl_->uses.size());
    impl_->distinctTexts.emplace(text);
    impl_->uses.push_back(0);
    impl_->textBytes += text.size();
    return id;
}

std::expected<void, EngineError> CorpusWriter::addVector(std::uint32_t id, std::span<const float> embedding)
{
    if (impl_->failed || impl_->finished || id != impl_->vectorCount || id >= impl_->uses.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (const auto valid = validateVector(embedding, impl_->dimensions); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!writeRaw(impl_->vectors.output, embedding.data(), embedding.size_bytes()))
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    ++impl_->vectorCount;
    return {};
}

std::expected<std::uint32_t, EngineError> CorpusWriter::addDistinct(std::string_view text,
                                                                    std::span<const float> embedding)
{
    if (impl_->vectorCount != impl_->uses.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (const auto valid = validateVector(embedding, impl_->dimensions); !valid)
    {
        return std::unexpected(valid.error());
    }
    const auto id = reserveDistinct(text);
    if (!id)
    {
        return std::unexpected(id.error());
    }
    if (const auto added = addVector(*id, embedding); !added)
    {
        return std::unexpected(added.error());
    }
    return *id;
}

struct LastLineCursor
{
    std::uint32_t lastSource = 0;
    std::uint64_t lastLine = 0;
    bool hasLastLine = false;
};

static bool addLineRejected(const CorpusLine &line, std::span<const SourceInfo> sources,
                            std::span<const std::uint32_t> uses, std::span<const std::string> terms,
                            const LastLineCursor &cursor)
{
    if (line.sourceIndex >= sources.size() || line.lineNumber == 0 ||
        (line.distinctId != NO_DISTINCT && line.distinctId >= uses.size()) ||
        (line.storedText && line.storedText->size() > MAX_TEXT_BYTES) ||
        (line.distinctId == NO_DISTINCT && !terms.empty()) ||
        (cursor.hasLastLine && (line.sourceIndex < cursor.lastSource ||
                                (line.sourceIndex == cursor.lastSource && line.lineNumber <= cursor.lastLine))))
    {
        return true;
    }
    for (const std::string &term : terms)
    {
        if (term.empty() || term.size() > MAX_TEXT_BYTES || term.find('\0') != std::string::npos)
        {
            return true;
        }
    }
    const SourceInfo &source = sources[line.sourceIndex];
    return (source.storedOnly && !line.storedText) ||
           (source.tracked && !rangeInside(line.byteOffset, line.byteLength, source.size));
}

static std::expected<void, EngineError> validateAddLines(std::span<const SourceInfo> sources,
                                                         std::span<const std::uint32_t> uses,
                                                         std::span<const CorpusLine> lines,
                                                         std::span<const std::vector<std::string>> lineTerms,
                                                         LastLineCursor &cursor)
{
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const CorpusLine &line = lines[index];
        if (addLineRejected(line, sources, uses, lineTerms[index], cursor))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        cursor.lastSource = line.sourceIndex;
        cursor.lastLine = line.lineNumber;
        cursor.hasLastLine = true;
    }
    return {};
}

struct SpillCounters
{
    std::uint64_t storedBytes = 0;
    std::uint64_t lineCount = 0;
};

static std::expected<void, EngineError> spillAddLines(std::ostream &linesOut, std::ostream &termsOut,
                                                      std::span<std::uint32_t> uses, SpillCounters &counters,
                                                      std::span<const CorpusLine> lines,
                                                      std::span<const std::vector<std::string>> lineTerms)
{
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const CorpusLine &line = lines[index];
        const std::uint32_t flags = (line.arrayElement ? LINE_ARRAY_ELEMENT : 0U) |
                                    (line.storedText ? LINE_STORED_TEXT : 0U) |
                                    (line.distinctId == NO_DISTINCT ? LINE_SKIPPED : 0U);
        const std::string_view text = line.storedText.value_or(std::string_view{});
        if (!writeU64(linesOut, line.lineNumber) || !writeU64(linesOut, line.byteOffset) ||
            !writeU64(linesOut, line.byteLength) || !writeU32(linesOut, line.sourceIndex) ||
            !writeU32(linesOut, line.distinctId) || !writeU32(linesOut, flags) ||
            !writeU32(linesOut, static_cast<std::uint32_t>(text.size())) ||
            !writeRaw(linesOut, text.data(), text.size()))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        if (!writeU32(termsOut, static_cast<std::uint32_t>(lineTerms[index].size())))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        for (const std::string &term : lineTerms[index])
        {
            if (!writeU32(termsOut, static_cast<std::uint32_t>(term.size())) ||
                !writeRaw(termsOut, term.data(), term.size()))
            {
                return std::unexpected(EngineError::FileOpenFailure);
            }
        }
        if (line.distinctId != NO_DISTINCT)
        {
            ++uses[line.distinctId];
        }
        counters.storedBytes += text.size();
        ++counters.lineCount;
    }
    return {};
}

std::expected<void, EngineError> CorpusWriter::addLines(std::span<const CorpusLine> lines,
                                                        std::span<const std::vector<std::string>> lineTerms)
{
    if (impl_->failed || impl_->finished || lines.size() != lineTerms.size() ||
        lines.size() > MAX_LINES - impl_->lineCount)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    LastLineCursor cursor{
        .lastSource = impl_->lastSource, .lastLine = impl_->lastLine, .hasLastLine = impl_->hasLastLine};
    if (const auto valid = validateAddLines(impl_->sources, impl_->uses, lines, lineTerms, cursor); !valid)
    {
        return std::unexpected(valid.error());
    }
    SpillCounters counters{.storedBytes = impl_->storedBytes, .lineCount = impl_->lineCount};
    if (const auto spilled =
            spillAddLines(impl_->lines.output, impl_->terms.output, impl_->uses, counters, lines, lineTerms);
        !spilled)
    {
        impl_->failed = true;
        return std::unexpected(spilled.error());
    }
    impl_->storedBytes = counters.storedBytes;
    impl_->lineCount = counters.lineCount;
    impl_->lastSource = cursor.lastSource;
    impl_->lastLine = cursor.lastLine;
    impl_->hasLastLine = cursor.hasLastLine;
    return {};
}

struct CorpusOffsets
{
    std::uint64_t sourceOffset = 0;
    std::uint64_t lineOffset = 0;
    std::uint64_t distinctOffset = 0;
    std::uint64_t textOffset = 0;
    std::uint64_t storedOffset = 0;
    std::uint64_t lexicalOffset = 0;
    std::uint64_t lexicalBytes = 0;
    std::uint64_t matrixOffset = 0;
    std::uint64_t matrixBytes = 0;
};

static std::expected<std::vector<std::vector<std::string>>, EngineError> readSpilledTerms(std::istream &terms,
                                                                                          std::uint64_t lineCount)
{
    std::vector<std::vector<std::string>> lineTerms;
    lineTerms.reserve(static_cast<std::size_t>(lineCount));
    for (std::uint64_t index = 0; index < lineCount; ++index)
    {
        std::uint32_t count = 0;
        if (!readU32Stream(terms, count))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        std::vector<std::string> row;
        row.reserve(count);
        for (std::uint32_t termIndex = 0; termIndex < count; ++termIndex)
        {
            std::uint32_t length = 0;
            if (!readU32Stream(terms, length) || length > MAX_TEXT_BYTES)
            {
                return std::unexpected(EngineError::FileOpenFailure);
            }
            std::string term(length, '\0');
            if (length != 0 && !terms.read(term.data(), static_cast<std::streamsize>(length)))
            {
                return std::unexpected(EngineError::FileOpenFailure);
            }
            row.push_back(std::move(term));
        }
        lineTerms.push_back(std::move(row));
    }
    if (!atEnd(terms))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return lineTerms;
}

struct CorpusLayoutSizes
{
    std::uint64_t lineCount = 0;
    std::uint64_t distinctCount = 0;
    std::uint64_t textBytes = 0;
    std::uint64_t storedBytes = 0;
    std::uint32_t dimensions = 0;
    std::uint64_t policyBytes = 0;
    std::uint64_t lexicalBytes = 0;
};

static CorpusOffsets corpusLayoutOffsets(std::span<const SourceInfo> sources, const CorpusLayoutSizes &sizes)
{
    CorpusOffsets offsets;
    offsets.sourceOffset = alignUp(HEADER_BYTES + sizes.policyBytes, 8);
    offsets.lineOffset = offsets.sourceOffset + (sources.size() * SOURCE_RECORD_BYTES);
    for (const SourceInfo &source : sources)
    {
        offsets.lineOffset += source.path.size() + source.jsonField.size();
    }
    offsets.distinctOffset = offsets.lineOffset + (sizes.lineCount * LINE_RECORD_BYTES);
    offsets.textOffset = offsets.distinctOffset + (sizes.distinctCount * TEXT_RECORD_BYTES);
    offsets.storedOffset = offsets.textOffset + sizes.textBytes;
    offsets.lexicalOffset = offsets.storedOffset + sizes.storedBytes;
    offsets.lexicalBytes = sizes.lexicalBytes;
    offsets.matrixOffset = alignUp(offsets.lexicalOffset + offsets.lexicalBytes, 64);
    offsets.matrixBytes = sizes.distinctCount * static_cast<std::uint64_t>(sizes.dimensions) * sizeof(float);
    return offsets;
}

struct CorpusHeaderCounts
{
    std::uint32_t dimensions = 0;
    std::uint64_t sourceCount = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t distinctCount = 0;
};

static bool writeCorpusHeader(std::ostream &output, std::string_view policy, const CorpusHeaderCounts &counts,
                              const CorpusOffsets &offsets)
{
    return writeRaw(output, MAGIC.data(), MAGIC.size()) && writeU32(output, CORPUS_VERSION) &&
           writeU32(output, static_cast<std::uint32_t>(policy.size())) && writeU32(output, counts.dimensions) &&
           writeU64(output, counts.sourceCount) && writeU64(output, counts.lineCount) &&
           writeU64(output, offsets.sourceOffset) && writeU64(output, offsets.lineOffset) &&
           writeU64(output, offsets.matrixOffset) && writeU64(output, counts.distinctCount) &&
           writeU64(output, offsets.distinctOffset) && writeU64(output, offsets.textOffset) &&
           writeU64(output, offsets.storedOffset) && writeU64(output, offsets.lexicalOffset) &&
           writeU64(output, offsets.lexicalBytes) && writeU64(output, HEADER_BYTES) && writeU64(output, 0) &&
           writeU64(output, 0) && writeRaw(output, policy.data(), policy.size()) &&
           writePadding(output, offsets.sourceOffset - HEADER_BYTES - policy.size());
}

static bool writeCorpusSources(std::ostream &output, std::span<const SourceInfo> sources, std::uint64_t sourceOffset)
{
    std::uint64_t sourceCursor = sourceOffset + (sources.size() * SOURCE_RECORD_BYTES);
    for (const SourceInfo &source : sources)
    {
        const std::uint32_t flags =
            (source.tracked ? SOURCE_TRACKED : 0U) | (source.storedOnly ? SOURCE_STORED_ONLY : 0U);
        if (!writeU64(output, sourceCursor) || !writeU32(output, static_cast<std::uint32_t>(source.path.size())) ||
            !writeU32(output, flags) || !writeU64(output, source.size) ||
            !writeU64(output, std::bit_cast<std::uint64_t>(source.modifiedTime)) ||
            !writeU64(output, sourceCursor + source.path.size()) ||
            !writeU32(output, static_cast<std::uint32_t>(source.jsonField.size())) || !writeU32(output, 0))
        {
            return false;
        }
        sourceCursor += source.path.size() + source.jsonField.size();
    }
    for (const SourceInfo &source : sources)
    {
        if (!writeRaw(output, source.path.data(), source.path.size()) ||
            !writeRaw(output, source.jsonField.data(), source.jsonField.size()))
        {
            return false;
        }
    }
    return true;
}

static bool writeCorpusLineTable(std::istream &lines, std::ostream &output, std::uint64_t lineCount,
                                 std::uint64_t &storedCursor)
{
    SpilledLine line;
    for (std::uint64_t index = 0; index < lineCount; ++index)
    {
        if (!readSpilledLine(lines, line))
        {
            return false;
        }
        const bool hasText = (line.flags & LINE_STORED_TEXT) != 0;
        if (!writeU64(output, line.lineNumber) || !writeU64(output, line.byteOffset) ||
            !writeU64(output, line.byteLength) || !writeU32(output, line.sourceIndex) ||
            !writeU32(output, line.distinctId) || !writeU64(output, hasText ? storedCursor : 0) ||
            !writeU32(output, static_cast<std::uint32_t>(line.storedText.size())) || !writeU32(output, line.flags) ||
            !writeU64(output, 0))
        {
            return false;
        }
        storedCursor += line.storedText.size();
    }
    return atEnd(lines);
}

static bool writeCorpusDistinctTexts(std::istream &texts, std::istream &lines, std::ostream &output,
                                     std::size_t distinctCount, std::uint64_t textOffset, std::uint64_t storedOffset)
{
    std::uint64_t textCursor = textOffset;
    std::string text;
    for (std::size_t index = 0; index < distinctCount; ++index)
    {
        if (!readSpilledText(texts, text) || !writeU64(output, textCursor) ||
            !writeU32(output, static_cast<std::uint32_t>(text.size())) || !writeU32(output, 0))
        {
            return false;
        }
        textCursor += text.size();
    }
    if (!atEnd(texts) || textCursor != storedOffset || !rewind(texts))
    {
        return false;
    }
    for (std::size_t index = 0; index < distinctCount; ++index)
    {
        if (!readSpilledText(texts, text) || !writeRaw(output, text.data(), text.size()))
        {
            return false;
        }
    }
    return atEnd(texts) && rewind(lines);
}

struct StoredMatrixCounts
{
    std::uint64_t lineCount = 0;
    std::uint64_t storedCursor = 0;
    std::uint64_t storedBytes = 0;
};

static bool writeCorpusStoredAndMatrix(std::istream &lines, std::istream &vectors, std::ostream &output,
                                       std::span<const std::byte> lexical, const CorpusOffsets &offsets,
                                       StoredMatrixCounts counts)
{
    SpilledLine line;
    for (std::uint64_t index = 0; index < counts.lineCount; ++index)
    {
        if (!readSpilledLine(lines, line) || !writeRaw(output, line.storedText.data(), line.storedText.size()))
        {
            return false;
        }
    }
    return atEnd(lines) && counts.storedCursor == offsets.storedOffset + counts.storedBytes &&
           writeRaw(output, lexical.data(), lexical.size()) &&
           writePadding(output, offsets.matrixOffset - (offsets.lexicalOffset + offsets.lexicalBytes)) &&
           copyBytes(vectors, output, offsets.matrixBytes);
}

struct CorpusEmitMeta
{
    std::span<const SourceInfo> sources;
    std::uint32_t dimensions = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t distinctCount = 0;
    std::uint64_t textBytes = 0;
    std::uint64_t storedBytes = 0;
    std::span<const std::byte> lexical;
};

static bool writeCorpusSections(std::istream &lines, std::istream &texts, std::istream &vectors, std::ostream &output,
                                const CorpusPolicy &policy, const CorpusEmitMeta &meta)
{
    const std::string policyText = serializePolicy(policy);
    const CorpusLayoutSizes sizes{.lineCount = meta.lineCount,
                                  .distinctCount = meta.distinctCount,
                                  .textBytes = meta.textBytes,
                                  .storedBytes = meta.storedBytes,
                                  .dimensions = meta.dimensions,
                                  .policyBytes = policyText.size(),
                                  .lexicalBytes = meta.lexical.size()};
    const CorpusOffsets offsets = corpusLayoutOffsets(meta.sources, sizes);
    const CorpusHeaderCounts counts{.dimensions = meta.dimensions,
                                    .sourceCount = meta.sources.size(),
                                    .lineCount = meta.lineCount,
                                    .distinctCount = meta.distinctCount};
    if (!writeCorpusHeader(output, policyText, counts, offsets) ||
        !writeCorpusSources(output, meta.sources, offsets.sourceOffset))
    {
        return false;
    }
    std::uint64_t storedCursor = offsets.storedOffset;
    if (!writeCorpusLineTable(lines, output, meta.lineCount, storedCursor) ||
        !writeCorpusDistinctTexts(texts, lines, output, meta.distinctCount, offsets.textOffset, offsets.storedOffset))
    {
        return false;
    }
    return writeCorpusStoredAndMatrix(
        lines, vectors, output, meta.lexical, offsets,
        {.lineCount = meta.lineCount, .storedCursor = storedCursor, .storedBytes = meta.storedBytes});
}

std::expected<void, EngineError> CorpusWriter::finish()
{
    if (impl_->failed || impl_->finished || impl_->vectorCount != impl_->uses.size() ||
        std::ranges::any_of(impl_->uses, [](std::uint32_t uses) { return uses == 0; }))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    impl_->lines.output.close();
    impl_->texts.output.close();
    impl_->vectors.output.close();
    impl_->terms.output.close();
    if (!impl_->lines.output || !impl_->texts.output || !impl_->vectors.output || !impl_->terms.output)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    std::ifstream lines(impl_->lines.path, std::ios::binary);
    std::ifstream texts(impl_->texts.path, std::ios::binary);
    std::ifstream vectors(impl_->vectors.path, std::ios::binary);
    std::ifstream terms(impl_->terms.path, std::ios::binary);
    if (!lines || !texts || !vectors || !terms)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    auto lineTerms = readSpilledTerms(terms, impl_->lineCount);
    if (!lineTerms)
    {
        impl_->failed = true;
        return std::unexpected(lineTerms.error());
    }
    const std::vector<std::byte> lexical = encodeLexicalSection(*lineTerms);
    if (lexical.size() > MAX_LEXICAL_BYTES)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::CorruptDatabase);
    }
    auto temporary = createSpill(impl_->destination, "tmp");
    if (!temporary)
    {
        impl_->failed = true;
        return std::unexpected(temporary.error());
    }
    impl_->temporary = temporary->path;
    std::ofstream &output = temporary->output;
    const auto fail = [&]() -> std::expected<void, EngineError>
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    };

    const CorpusEmitMeta meta{.sources = impl_->sources,
                              .dimensions = impl_->dimensions,
                              .lineCount = impl_->lineCount,
                              .distinctCount = impl_->uses.size(),
                              .textBytes = impl_->textBytes,
                              .storedBytes = impl_->storedBytes,
                              .lexical = lexical};
    if (!writeCorpusSections(lines, texts, vectors, output, impl_->policy, meta))
    {
        return fail();
    }
    output.close();
    if (!output || !sealCorpus(impl_->temporary))
    {
        return fail();
    }
    if (const auto replaced = nesso::platform::replaceFileAtomically(impl_->temporary, impl_->destination); !replaced)
    {
        return fail();
    }
    impl_->temporary.clear();
    impl_->finished = true;
    return {};
}

} // namespace nesso::core
