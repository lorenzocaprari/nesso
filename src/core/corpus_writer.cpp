// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "core/core_types.hpp"
#include "corpus_detail.hpp"

#include <core/corpus_index.hpp>

#include <cstddef>
#include <ios>
#include <istream>
#include <memory>
#include <ostream>
#include <parser/archive_source.hpp>
#include <parser/gzip_reader.hpp>
#include <platform/assert.hpp>
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

using detail::AlignUp;
using detail::AtEnd;
using detail::CopyBytes;
using detail::EncodeLexicalSectionSpilled;
using detail::HEADER_BYTES;
using detail::LINE_ARRAY_ELEMENT;
using detail::LINE_RECORD_BYTES;
using detail::LINE_SKIPPED;
using detail::LINE_STORED_TEXT;
using detail::MAGIC;
using detail::MAX_TEXT_BYTES;
using detail::RangeInside;
using detail::ReadExact;
using detail::ReadU32Stream;
using detail::ReadU64Stream;
using detail::SerializePolicy;
using detail::SOURCE_RECORD_BYTES;
using detail::SOURCE_STORED_ONLY;
using detail::SOURCE_TRACKED;
using detail::TEXT_RECORD_BYTES;
using detail::ValidateVector;
using detail::WritePadding;
using detail::WriteRaw;
using detail::WriteU32;
using detail::WriteU64;

struct SpillFile
{
    std::filesystem::path path;
    std::ofstream output;
};

struct SpilledLine
{
    std::uint64_t line_number = 0;
    std::uint64_t byte_offset = 0;
    std::uint64_t byte_length = 0;
    std::uint32_t source_index = 0;
    std::uint32_t distinct_id = NO_DISTINCT;
    std::uint32_t flags = 0;
    std::string stored_text;
};
static std::expected<SpillFile, EngineError> CreateSpill(const std::filesystem::path &destination,
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

static void RemoveFile(const std::filesystem::path &path)
{
    if (!path.empty())
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
}

static bool ReadSpilledLine(std::istream &input, SpilledLine &line)
{
    std::uint32_t length = 0;
    if (!ReadU64Stream(input, line.line_number) || !ReadU64Stream(input, line.byte_offset) ||
        !ReadU64Stream(input, line.byte_length) || !ReadU32Stream(input, line.source_index) ||
        !ReadU32Stream(input, line.distinct_id) || !ReadU32Stream(input, line.flags) || !ReadU32Stream(input, length) ||
        length > MAX_TEXT_BYTES)
    {
        return false;
    }
    line.stored_text.assign(length, '\0');
    return ReadExact(input, line.stored_text.data(), length);
}

static bool ReadSpilledText(std::istream &input, std::string &text)
{
    std::uint32_t length = 0;
    if (!ReadU32Stream(input, length) || length > MAX_TEXT_BYTES)
    {
        return false;
    }
    text.assign(length, '\0');
    return ReadExact(input, text.data(), length);
}

static bool Rewind(std::istream &input)
{
    input.clear();
    input.seekg(0);
    return static_cast<bool>(input);
}
static bool SealCorpus(const std::filesystem::path &path)
{
    CorpusChecksum checksum{};
    {
        auto mapped = nesso::platform::MappedFile::Open(path);
        if (!mapped || mapped->Bytes().size() < HEADER_BYTES)
        {
            return false;
        }
        checksum = ComputeCorpusChecksum(mapped->Bytes());
    }
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(static_cast<std::streamoff>(CORPUS_CHECKSUM_OFFSET));
    if (!WriteRaw(file, checksum.data(), checksum.size()))
    {
        return false;
    }
    file.close();
    return !file.fail();
}

std::expected<SourceInfo, EngineError> SourceInfoFor(const std::filesystem::path &path, std::string_view json_field)
{
    SourceInfo source{.path = path.string(), .json_field = std::string{json_field}};
    if (path == "-")
    {
        source.stored_only = true;
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
    source.modified_time = std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
    if (nesso::parser::IsArchivePath(path))
    {
        // The bundle is one file for the staleness check. Each member is its own
        // stored source.
        source.tracked = true;
        return source;
    }
    if (nesso::parser::IsGzipPath(path))
    {
        // A gzip member is not a file-offset range. Store each line, as stdin and
        // archive members do.
        source.stored_only = true;
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
    detail::CorpusLimits limits;
    std::uint32_t dimensions = 0;
    SpillFile lines;
    SpillFile texts;
    SpillFile vectors;
    SpillFile terms;
    std::filesystem::path lexical_spill;
    std::vector<SourceInfo> sources;
    std::unordered_set<std::string> distinct_texts;
    std::vector<std::uint32_t> uses;
    std::uint32_t vector_count = 0;
    std::uint64_t line_count = 0;
    std::uint64_t text_bytes = 0;
    std::uint64_t stored_bytes = 0;
    std::uint32_t last_source = 0;
    std::uint64_t last_line = 0;
    bool has_last_line = false;
    bool failed = false;
    bool finished = false;
};

CorpusWriter::CorpusWriter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
void CorpusWriter::DiscardSpills(Impl *impl) noexcept
{
    if (impl == nullptr)
    {
        return;
    }
    impl->lines.output.close();
    impl->texts.output.close();
    impl->vectors.output.close();
    impl->terms.output.close();
    RemoveFile(impl->lines.path);
    RemoveFile(impl->texts.path);
    RemoveFile(impl->vectors.path);
    RemoveFile(impl->terms.path);
    RemoveFile(impl->lexical_spill);
    RemoveFile(impl->temporary);
}

CorpusWriter::~CorpusWriter() { DiscardSpills(impl_.get()); }
CorpusWriter::CorpusWriter(CorpusWriter &&) noexcept = default;
CorpusWriter &CorpusWriter::operator=(CorpusWriter &&other) noexcept
{
    if (this != &other)
    {
        DiscardSpills(impl_.get());
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::expected<CorpusWriter, EngineError> CorpusWriter::Create(const std::filesystem::path &path, CorpusPolicy policy)
{
    return detail::CorpusWriterAccess::Create(path, std::move(policy), {});
}

std::expected<CorpusWriter, EngineError>
detail::CorpusWriterAccess::Create(const std::filesystem::path &path, CorpusPolicy policy, const CorpusLimits &limits)
{
    const auto dimensions = ValidatePolicy(policy);
    if (!dimensions)
    {
        return std::unexpected(dimensions.error());
    }
    std::error_code error;
    if (std::filesystem::is_directory(path, error))
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }
    auto impl = std::make_unique<CorpusWriter::Impl>();
    impl->destination = path;
    impl->policy = std::move(policy);
    impl->limits = limits;
    impl->dimensions = *dimensions;
    auto lines = CreateSpill(path, "lines");
    if (!lines)
    {
        return std::unexpected(lines.error());
    }
    impl->lines = std::move(*lines);
    auto texts = CreateSpill(path, "texts");
    if (!texts)
    {
        return std::unexpected(texts.error());
    }
    impl->texts = std::move(*texts);
    auto vectors = CreateSpill(path, "vectors");
    if (!vectors)
    {
        return std::unexpected(vectors.error());
    }
    impl->vectors = std::move(*vectors);
    auto terms = CreateSpill(path, "terms");
    if (!terms)
    {
        return std::unexpected(terms.error());
    }
    impl->terms = std::move(*terms);
    return CorpusWriter{std::move(impl)};
}

std::expected<std::uint32_t, EngineError> CorpusWriter::AddSource(SourceInfo source)
{
    if (impl_->failed || impl_->finished)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (impl_->sources.size() >= impl_->limits.sources)
    {
        return std::unexpected(EngineError::LimitsExceeded);
    }
    if (source.path.empty() || source.path.size() > MAX_TEXT_BYTES || source.json_field.size() > MAX_TEXT_BYTES ||
        source.path.find('\0') != std::string::npos || source.json_field.find('\0') != std::string::npos)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const auto source_id = static_cast<std::uint32_t>(impl_->sources.size());
    impl_->sources.push_back(std::move(source));
    return source_id;
}

std::expected<std::uint32_t, EngineError> CorpusWriter::ReserveDistinct(std::string_view text)
{
    if (impl_->failed || impl_->finished)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (impl_->uses.size() >= impl_->limits.distinct)
    {
        return std::unexpected(EngineError::LimitsExceeded);
    }
    if (text.size() > MAX_TEXT_BYTES || impl_->distinct_texts.contains(std::string{text}))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (!WriteU32(impl_->texts.output, static_cast<std::uint32_t>(text.size())) ||
        !WriteRaw(impl_->texts.output, text.data(), text.size()))
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    const auto distinct_id = static_cast<std::uint32_t>(impl_->uses.size());
    impl_->distinct_texts.emplace(text);
    impl_->uses.push_back(0);
    impl_->text_bytes += text.size();
    return distinct_id;
}

std::expected<void, EngineError> CorpusWriter::AddVector(std::uint32_t distinct_id, std::span<const float> embedding)
{
    if (impl_->failed || impl_->finished || distinct_id != impl_->vector_count || distinct_id >= impl_->uses.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (const auto valid = ValidateVector(embedding, impl_->dimensions); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (!WriteRaw(impl_->vectors.output, embedding.data(), embedding.size_bytes()))
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    ++impl_->vector_count;
    return {};
}

std::expected<std::uint32_t, EngineError> CorpusWriter::AddDistinct(std::string_view text,
                                                                    std::span<const float> embedding)
{
    if (impl_->vector_count != impl_->uses.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (const auto valid = ValidateVector(embedding, impl_->dimensions); !valid)
    {
        return std::unexpected(valid.error());
    }
    const auto distinct_id = ReserveDistinct(text);
    if (!distinct_id)
    {
        return std::unexpected(distinct_id.error());
    }
    if (const auto added = AddVector(*distinct_id, embedding); !added)
    {
        return std::unexpected(added.error());
    }
    return *distinct_id;
}

struct LastLineCursor
{
    std::uint32_t last_source = 0;
    std::uint64_t last_line = 0;
    bool has_last_line = false;
};

static bool AddLineRejected(const CorpusLine &line, std::span<const SourceInfo> sources,
                            std::span<const std::uint32_t> uses, std::span<const std::string> terms,
                            const LastLineCursor &cursor)
{
    if (line.source_index >= sources.size() || line.line_number == 0 ||
        (line.distinct_id != NO_DISTINCT && line.distinct_id >= uses.size()) ||
        (line.stored_text && line.stored_text->size() > MAX_TEXT_BYTES) ||
        (line.distinct_id == NO_DISTINCT && !terms.empty()) ||
        (cursor.has_last_line && (line.source_index < cursor.last_source ||
                                  (line.source_index == cursor.last_source && line.line_number <= cursor.last_line))))
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
    const SourceInfo &source = sources[line.source_index];
    return (source.stored_only && !line.stored_text) ||
           (source.tracked && !RangeInside(line.byte_offset, line.byte_length, source.size));
}

static std::expected<void, EngineError> ValidateAddLines(std::span<const SourceInfo> sources,
                                                         std::span<const std::uint32_t> uses,
                                                         std::span<const CorpusLine> lines,
                                                         std::span<const std::vector<std::string>> line_terms,
                                                         LastLineCursor &cursor)
{
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const CorpusLine &line = lines[index];
        if (AddLineRejected(line, sources, uses, line_terms[index], cursor))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        cursor.last_source = line.source_index;
        cursor.last_line = line.line_number;
        cursor.has_last_line = true;
    }
    return {};
}

struct SpillCounters
{
    std::uint64_t stored_bytes = 0;
    std::uint64_t line_count = 0;
};

static std::expected<void, EngineError> SpillAddLines(std::ostream &lines_out, std::ostream &terms_out,
                                                      std::span<std::uint32_t> uses, SpillCounters &counters,
                                                      std::span<const CorpusLine> lines,
                                                      std::span<const std::vector<std::string>> line_terms)
{
    NESSO_ASSERT(lines.size() == line_terms.size());
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const CorpusLine &line = lines[index];
        const std::uint32_t flags = (line.array_element ? LINE_ARRAY_ELEMENT : 0U) |
                                    (line.stored_text ? LINE_STORED_TEXT : 0U) |
                                    (line.distinct_id == NO_DISTINCT ? LINE_SKIPPED : 0U);
        const std::string_view text = line.stored_text.value_or(std::string_view{});
        if (!WriteU64(lines_out, line.line_number) || !WriteU64(lines_out, line.byte_offset) ||
            !WriteU64(lines_out, line.byte_length) || !WriteU32(lines_out, line.source_index) ||
            !WriteU32(lines_out, line.distinct_id) || !WriteU32(lines_out, flags) ||
            !WriteU32(lines_out, static_cast<std::uint32_t>(text.size())) ||
            !WriteRaw(lines_out, text.data(), text.size()))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        if (!WriteU32(terms_out, static_cast<std::uint32_t>(line_terms[index].size())))
        {
            return std::unexpected(EngineError::FileOpenFailure);
        }
        for (const std::string &term : line_terms[index])
        {
            if (!WriteU32(terms_out, static_cast<std::uint32_t>(term.size())) ||
                !WriteRaw(terms_out, term.data(), term.size()))
            {
                return std::unexpected(EngineError::FileOpenFailure);
            }
        }
        if (line.distinct_id != NO_DISTINCT)
        {
            ++uses[line.distinct_id];
        }
        counters.stored_bytes += text.size();
        ++counters.line_count;
    }
    return {};
}

std::expected<void, EngineError> CorpusWriter::AddLines(std::span<const CorpusLine> lines,
                                                        std::span<const std::vector<std::string>> line_terms)
{
    if (impl_->failed || impl_->finished)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (lines.size() > impl_->limits.lines - impl_->line_count)
    {
        return std::unexpected(EngineError::LimitsExceeded);
    }
    if (lines.size() != line_terms.size())
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    LastLineCursor cursor{
        .last_source = impl_->last_source, .last_line = impl_->last_line, .has_last_line = impl_->has_last_line};
    if (const auto valid = ValidateAddLines(impl_->sources, impl_->uses, lines, line_terms, cursor); !valid)
    {
        return std::unexpected(valid.error());
    }
    SpillCounters counters{.stored_bytes = impl_->stored_bytes, .line_count = impl_->line_count};
    if (const auto spilled =
            SpillAddLines(impl_->lines.output, impl_->terms.output, impl_->uses, counters, lines, line_terms);
        !spilled)
    {
        impl_->failed = true;
        return std::unexpected(spilled.error());
    }
    impl_->stored_bytes = counters.stored_bytes;
    impl_->line_count = counters.line_count;
    impl_->last_source = cursor.last_source;
    impl_->last_line = cursor.last_line;
    impl_->has_last_line = cursor.has_last_line;
    return {};
}

struct CorpusOffsets
{
    std::uint64_t source_offset = 0;
    std::uint64_t line_offset = 0;
    std::uint64_t distinct_offset = 0;
    std::uint64_t text_offset = 0;
    std::uint64_t stored_offset = 0;
    std::uint64_t lexical_offset = 0;
    std::uint64_t lexical_bytes = 0;
    std::uint64_t matrix_offset = 0;
    std::uint64_t matrix_bytes = 0;
};

struct CorpusLayoutSizes
{
    std::uint64_t line_count = 0;
    std::uint64_t distinct_count = 0;
    std::uint64_t text_bytes = 0;
    std::uint64_t stored_bytes = 0;
    std::uint32_t dimensions = 0;
    std::uint64_t policy_bytes = 0;
    std::uint64_t lexical_bytes = 0;
};

static CorpusOffsets CorpusLayoutOffsets(std::span<const SourceInfo> sources, const CorpusLayoutSizes &sizes)
{
    CorpusOffsets offsets;
    offsets.source_offset = AlignUp(HEADER_BYTES + sizes.policy_bytes, 8);
    offsets.line_offset = offsets.source_offset + (sources.size() * SOURCE_RECORD_BYTES);
    for (const SourceInfo &source : sources)
    {
        offsets.line_offset += source.path.size() + source.json_field.size();
    }
    offsets.distinct_offset = offsets.line_offset + (sizes.line_count * LINE_RECORD_BYTES);
    offsets.text_offset = offsets.distinct_offset + (sizes.distinct_count * TEXT_RECORD_BYTES);
    offsets.stored_offset = offsets.text_offset + sizes.text_bytes;
    offsets.lexical_offset = offsets.stored_offset + sizes.stored_bytes;
    offsets.lexical_bytes = sizes.lexical_bytes;
    offsets.matrix_offset = AlignUp(offsets.lexical_offset + offsets.lexical_bytes, 64);
    offsets.matrix_bytes = sizes.distinct_count * static_cast<std::uint64_t>(sizes.dimensions) * sizeof(float);
    return offsets;
}

struct CorpusHeaderCounts
{
    std::uint32_t dimensions = 0;
    std::uint64_t source_count = 0;
    std::uint64_t line_count = 0;
    std::uint64_t distinct_count = 0;
};

static bool WriteCorpusHeader(std::ostream &output, std::string_view policy, const CorpusHeaderCounts &counts,
                              const CorpusOffsets &offsets)
{
    return WriteRaw(output, MAGIC.data(), MAGIC.size()) && WriteU32(output, CORPUS_VERSION) &&
           WriteU32(output, static_cast<std::uint32_t>(policy.size())) && WriteU32(output, counts.dimensions) &&
           WriteU64(output, counts.source_count) && WriteU64(output, counts.line_count) &&
           WriteU64(output, offsets.source_offset) && WriteU64(output, offsets.line_offset) &&
           WriteU64(output, offsets.matrix_offset) && WriteU64(output, counts.distinct_count) &&
           WriteU64(output, offsets.distinct_offset) && WriteU64(output, offsets.text_offset) &&
           WriteU64(output, offsets.stored_offset) && WriteU64(output, offsets.lexical_offset) &&
           WriteU64(output, offsets.lexical_bytes) && WriteU64(output, HEADER_BYTES) && WriteU64(output, 0) &&
           WriteU64(output, 0) && WriteRaw(output, policy.data(), policy.size()) &&
           WritePadding(output, offsets.source_offset - HEADER_BYTES - policy.size());
}

static bool WriteCorpusSources(std::ostream &output, std::span<const SourceInfo> sources, std::uint64_t source_offset)
{
    std::uint64_t source_cursor = source_offset + (sources.size() * SOURCE_RECORD_BYTES);
    for (const SourceInfo &source : sources)
    {
        const std::uint32_t flags =
            (source.tracked ? SOURCE_TRACKED : 0U) | (source.stored_only ? SOURCE_STORED_ONLY : 0U);
        if (!WriteU64(output, source_cursor) || !WriteU32(output, static_cast<std::uint32_t>(source.path.size())) ||
            !WriteU32(output, flags) || !WriteU64(output, source.size) ||
            !WriteU64(output, std::bit_cast<std::uint64_t>(source.modified_time)) ||
            !WriteU64(output, source_cursor + source.path.size()) ||
            !WriteU32(output, static_cast<std::uint32_t>(source.json_field.size())) || !WriteU32(output, 0))
        {
            return false;
        }
        source_cursor += source.path.size() + source.json_field.size();
    }
    for (const SourceInfo &source : sources)
    {
        if (!WriteRaw(output, source.path.data(), source.path.size()) ||
            !WriteRaw(output, source.json_field.data(), source.json_field.size()))
        {
            return false;
        }
    }
    return true;
}

static bool WriteCorpusLineTable(std::istream &lines, std::ostream &output, std::uint64_t line_count,
                                 std::uint64_t &stored_cursor)
{
    SpilledLine line;
    for (std::uint64_t index = 0; index < line_count; ++index)
    {
        if (!ReadSpilledLine(lines, line))
        {
            return false;
        }
        const bool has_text = (line.flags & LINE_STORED_TEXT) != 0;
        if (!WriteU64(output, line.line_number) || !WriteU64(output, line.byte_offset) ||
            !WriteU64(output, line.byte_length) || !WriteU32(output, line.source_index) ||
            !WriteU32(output, line.distinct_id) || !WriteU64(output, has_text ? stored_cursor : 0) ||
            !WriteU32(output, static_cast<std::uint32_t>(line.stored_text.size())) || !WriteU32(output, line.flags) ||
            !WriteU64(output, 0))
        {
            return false;
        }
        stored_cursor += line.stored_text.size();
    }
    return AtEnd(lines);
}

struct DistinctTextLayout
{
    std::size_t distinct_count = 0;
    std::uint64_t text_offset = 0;
    std::uint64_t stored_offset = 0;
};

static bool WriteCorpusDistinctTexts(std::istream &texts, std::istream &lines, std::ostream &output,
                                     const DistinctTextLayout &layout)
{
    std::uint64_t text_cursor = layout.text_offset;
    std::string text;
    for (std::size_t index = 0; index < layout.distinct_count; ++index)
    {
        if (!ReadSpilledText(texts, text) || !WriteU64(output, text_cursor) ||
            !WriteU32(output, static_cast<std::uint32_t>(text.size())) || !WriteU32(output, 0))
        {
            return false;
        }
        text_cursor += text.size();
    }
    if (!AtEnd(texts) || text_cursor != layout.stored_offset || !Rewind(texts))
    {
        return false;
    }
    for (std::size_t index = 0; index < layout.distinct_count; ++index)
    {
        if (!ReadSpilledText(texts, text) || !WriteRaw(output, text.data(), text.size()))
        {
            return false;
        }
    }
    return AtEnd(texts) && Rewind(lines);
}

struct StoredMatrixCounts
{
    std::uint64_t line_count = 0;
    std::uint64_t stored_cursor = 0;
    std::uint64_t stored_bytes = 0;
};

static bool WriteCorpusStoredAndMatrix(std::istream &lines, std::istream &vectors, std::istream &lexical,
                                       std::ostream &output, const CorpusOffsets &offsets,
                                       const StoredMatrixCounts &counts)
{
    SpilledLine line;
    for (std::uint64_t index = 0; index < counts.line_count; ++index)
    {
        if (!ReadSpilledLine(lines, line) || !WriteRaw(output, line.stored_text.data(), line.stored_text.size()))
        {
            return false;
        }
    }
    return AtEnd(lines) && counts.stored_cursor == offsets.stored_offset + counts.stored_bytes &&
           CopyBytes(lexical, output, offsets.lexical_bytes) && AtEnd(lexical) &&
           WritePadding(output, offsets.matrix_offset - (offsets.lexical_offset + offsets.lexical_bytes)) &&
           CopyBytes(vectors, output, offsets.matrix_bytes);
}

struct CorpusEmitMeta
{
    std::span<const SourceInfo> sources;
    std::uint32_t dimensions = 0;
    std::uint64_t line_count = 0;
    std::uint64_t distinct_count = 0;
    std::uint64_t text_bytes = 0;
    std::uint64_t stored_bytes = 0;
    std::uint64_t lexical_bytes = 0;
};

struct CorpusSpillStreams
{
    std::istream *lines = nullptr;
    std::istream *texts = nullptr;
    std::istream *vectors = nullptr;
    std::istream *lexical = nullptr;
    std::ostream *output = nullptr;
};

static bool WriteCorpusSections(const CorpusSpillStreams &streams, const CorpusPolicy &policy,
                                const CorpusEmitMeta &meta)
{
    const std::string policy_text = SerializePolicy(policy);
    const CorpusLayoutSizes sizes{.line_count = meta.line_count,
                                  .distinct_count = meta.distinct_count,
                                  .text_bytes = meta.text_bytes,
                                  .stored_bytes = meta.stored_bytes,
                                  .dimensions = meta.dimensions,
                                  .policy_bytes = policy_text.size(),
                                  .lexical_bytes = meta.lexical_bytes};
    const CorpusOffsets offsets = CorpusLayoutOffsets(meta.sources, sizes);
    const CorpusHeaderCounts counts{.dimensions = meta.dimensions,
                                    .source_count = meta.sources.size(),
                                    .line_count = meta.line_count,
                                    .distinct_count = meta.distinct_count};
    if (!WriteCorpusHeader(*streams.output, policy_text, counts, offsets) ||
        !WriteCorpusSources(*streams.output, meta.sources, offsets.source_offset))
    {
        return false;
    }
    std::uint64_t stored_cursor = offsets.stored_offset;
    if (!WriteCorpusLineTable(*streams.lines, *streams.output, meta.line_count, stored_cursor) ||
        !WriteCorpusDistinctTexts(*streams.texts, *streams.lines, *streams.output,
                                  {.distinct_count = static_cast<std::size_t>(meta.distinct_count),
                                   .text_offset = offsets.text_offset,
                                   .stored_offset = offsets.stored_offset}))
    {
        return false;
    }
    return WriteCorpusStoredAndMatrix(
        *streams.lines, *streams.vectors, *streams.lexical, *streams.output, offsets,
        {.line_count = meta.line_count, .stored_cursor = stored_cursor, .stored_bytes = meta.stored_bytes});
}

std::expected<void, EngineError> CorpusWriter::Finish()
{
    if (impl_->failed || impl_->finished || impl_->vector_count != impl_->uses.size() ||
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
    if (!lines || !texts || !vectors)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    auto lexical = EncodeLexicalSectionSpilled(impl_->terms.path, impl_->line_count, impl_->destination,
                                               detail::CORPUS_WRITER_MEMORY_BUDGET_BYTES, impl_->limits);
    if (!lexical)
    {
        impl_->failed = true;
        return std::unexpected(lexical.error());
    }
    impl_->lexical_spill = lexical->path;
    if (lexical->bytes > impl_->limits.lexical_bytes)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::LimitsExceeded);
    }
    std::ifstream lexical_input(lexical->path, std::ios::binary);
    if (!lexical_input)
    {
        impl_->failed = true;
        return std::unexpected(EngineError::FileOpenFailure);
    }
    auto temporary = CreateSpill(impl_->destination, "tmp");
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
                              .line_count = impl_->line_count,
                              .distinct_count = impl_->uses.size(),
                              .text_bytes = impl_->text_bytes,
                              .stored_bytes = impl_->stored_bytes,
                              .lexical_bytes = lexical->bytes};
    if (!WriteCorpusSections(
            {.lines = &lines, .texts = &texts, .vectors = &vectors, .lexical = &lexical_input, .output = &output},
            impl_->policy, meta))
    {
        return fail();
    }
    output.close();
    if (!output || !SealCorpus(impl_->temporary))
    {
        return fail();
    }
    if (const auto replaced = nesso::platform::ReplaceFileAtomically(impl_->temporary, impl_->destination); !replaced)
    {
        return fail();
    }
    impl_->temporary.clear();
    impl_->finished = true;
    return {};
}

} // namespace nesso::core
