// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "core/core_types.hpp"
#include "corpus_detail.hpp"

#include <core/corpus_index.hpp>
#include <cstddef>
#include <platform/assert.hpp>
#include <platform/file.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nesso::core
{

using detail::AlignUp;
using detail::DecodeLexicalSection;
using detail::HEADER_BYTES;
using detail::IsZero;
using detail::LexicalStatusOf;
using detail::LINE_ARRAY_ELEMENT;
using detail::LINE_RECORD_BYTES;
using detail::LINE_SKIPPED;
using detail::LINE_STORED_TEXT;
using detail::MAGIC;
using detail::MAX_DIMENSIONS;
using detail::MAX_DISTINCT;
using detail::MAX_LEXICAL_BYTES;
using detail::MAX_LINES;
using detail::MAX_POLICY_BYTES;
using detail::MAX_SOURCES;
using detail::MAX_TEXT_BYTES;
using detail::ParsePolicy;
using detail::RangeInside;
using detail::ReadU32;
using detail::ReadU64;
using detail::SOURCE_RECORD_BYTES;
using detail::SOURCE_STORED_ONLY;
using detail::SOURCE_TRACKED;
using detail::TEXT_RECORD_BYTES;
using detail::ValidatePolicy;
using detail::ViewBytes;

struct Layout
{
    std::uint32_t policy_bytes = 0;
    std::uint32_t dimensions = 0;
    std::uint64_t source_count = 0;
    std::uint64_t line_count = 0;
    std::uint64_t source_offset = 0;
    std::uint64_t line_offset = 0;
    std::uint64_t matrix_offset = 0;
    std::uint64_t distinct_count = 0;
    std::uint64_t distinct_offset = 0;
    std::uint64_t text_offset = 0;
    std::uint64_t stored_offset = 0;
    std::uint64_t lexical_offset = 0;
    std::uint64_t lexical_bytes = 0;
    std::uint64_t policy_offset = 0;
};
struct MappedCorpus::Impl
{
    nesso::platform::MappedFile file;
    CorpusPolicy policy;
    std::uint32_t dimensions = 0;
    std::vector<SourceInfo> sources;
    std::vector<CorpusLine> lines;
    std::vector<std::string_view> texts;
    std::span<const float> matrix;
    std::vector<std::string> lexical_term_storage;
    std::vector<std::string_view> lexical_dictionary;
    std::vector<LexicalLinePostings> lexical_postings;
    LexicalStatus lexical_status = LexicalStatus::Missing;
    std::string corpus_lexical_terms_version;
};

struct MappedShape
{
    std::size_t sources = 0;
    std::size_t texts = 0;
    std::size_t lines = 0;
    std::size_t matrix = 0;
    std::size_t postings = 0;
    std::size_t dictionary = 0;
    std::size_t stored_terms = 0;
};

static void AssertMappedShape(const MappedShape &shape, const Layout &layout, bool has_lexical)
{
    NESSO_ASSERT(shape.sources == layout.source_count);
    NESSO_ASSERT(shape.texts == layout.distinct_count);
    NESSO_ASSERT(shape.lines == layout.line_count);
    NESSO_ASSERT(shape.matrix == layout.distinct_count * layout.dimensions);
    if (layout.distinct_count != 0)
    {
        NESSO_ASSERT(layout.dimensions > 0);
    }
    if (has_lexical)
    {
        NESSO_ASSERT(shape.postings == layout.line_count);
        NESSO_ASSERT(shape.dictionary == shape.stored_terms);
    }
}

MappedCorpus::MappedCorpus(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MappedCorpus::~MappedCorpus() = default;
MappedCorpus::MappedCorpus(MappedCorpus &&) noexcept = default;
MappedCorpus &MappedCorpus::operator=(MappedCorpus &&) noexcept = default;

const CorpusPolicy &MappedCorpus::Policy() const noexcept { return impl_->policy; }
std::uint32_t MappedCorpus::Dimensions() const noexcept { return impl_->dimensions; }
std::span<const SourceInfo> MappedCorpus::Sources() const noexcept { return impl_->sources; }
std::span<const CorpusLine> MappedCorpus::Lines() const noexcept { return impl_->lines; }
std::span<const std::string_view> MappedCorpus::Texts() const noexcept { return impl_->texts; }
std::span<const float> MappedCorpus::Matrix() const noexcept { return impl_->matrix; }
std::span<const std::string_view> MappedCorpus::LexicalDictionary() const noexcept { return impl_->lexical_dictionary; }
std::span<const LexicalLinePostings> MappedCorpus::LexicalPostings() const noexcept { return impl_->lexical_postings; }
LexicalStatus MappedCorpus::GetLexicalStatus() const noexcept { return impl_->lexical_status; }
std::string_view MappedCorpus::CorpusLexicalTermsVersion() const noexcept
{
    return impl_->corpus_lexical_terms_version;
}

static bool ReadLayoutFields(std::span<const std::byte> bytes, Layout &layout)
{
    return bytes.size() >= HEADER_BYTES && ReadU32(bytes, 8, layout.policy_bytes) &&
           ReadU32(bytes, 12, layout.dimensions) && ReadU64(bytes, 16, layout.source_count) &&
           ReadU64(bytes, 24, layout.line_count) && ReadU64(bytes, 32, layout.source_offset) &&
           ReadU64(bytes, 40, layout.line_offset) && ReadU64(bytes, 48, layout.matrix_offset) &&
           ReadU64(bytes, 56, layout.distinct_count) && ReadU64(bytes, 64, layout.distinct_offset) &&
           ReadU64(bytes, 72, layout.text_offset) && ReadU64(bytes, 80, layout.stored_offset) &&
           ReadU64(bytes, 88, layout.lexical_offset) && ReadU64(bytes, 96, layout.lexical_bytes) &&
           ReadU64(bytes, 104, layout.policy_offset);
}

struct ParsedHeader
{
    Layout layout{};
    std::uint32_t version = 0;
};

static CorpusMapFailure MakeMapFailure(EngineError code, const std::optional<std::uint32_t> &version = std::nullopt)
{
    return {.code = code, .version = version};
}

static std::expected<ParsedHeader, CorpusMapFailure> MapCorpusHeader(std::span<const std::byte> bytes)
{
    std::uint32_t version = 0;
    if (bytes.size() < 8 || std::memcmp(bytes.data(), MAGIC.data(), MAGIC.size()) != 0 || !ReadU32(bytes, 4, version))
    {
        return std::unexpected(MakeMapFailure(EngineError::CorruptDatabase));
    }
    if (version != CORPUS_VERSION)
    {
        return std::unexpected(MakeMapFailure(EngineError::UnsupportedVersion, version));
    }
    if (bytes.size() < HEADER_BYTES)
    {
        return std::unexpected(MakeMapFailure(EngineError::CorruptDatabase, version));
    }
    const CorpusChecksum checksum = ComputeCorpusChecksum(bytes);
    if (!std::ranges::equal(checksum, bytes.subspan(CORPUS_CHECKSUM_OFFSET, CORPUS_CHECKSUM_BYTES)))
    {
        return std::unexpected(MakeMapFailure(EngineError::CorruptDatabase, version));
    }
    Layout layout;
    if (!ReadLayoutFields(bytes, layout))
    {
        return std::unexpected(MakeMapFailure(EngineError::CorruptDatabase, version));
    }
    return ParsedHeader{.layout = layout, .version = version};
}

static bool CorpusLimitsExceeded(const Layout &layout)
{
    return layout.policy_bytes > MAX_POLICY_BYTES || layout.dimensions > MAX_DIMENSIONS ||
           layout.source_count > MAX_SOURCES || layout.line_count > MAX_LINES || layout.distinct_count > MAX_DISTINCT ||
           (layout.distinct_count > 0 && layout.dimensions == 0) || layout.lexical_bytes > MAX_LEXICAL_BYTES;
}

static bool CorpusOffsetsBroken(const Layout &layout, std::uint64_t file_size)
{
    return layout.policy_offset != HEADER_BYTES ||
           layout.source_offset != AlignUp(HEADER_BYTES + layout.policy_bytes, 8) ||
           layout.source_offset > layout.line_offset || layout.line_offset > layout.distinct_offset ||
           layout.distinct_offset > layout.text_offset || layout.text_offset > layout.stored_offset ||
           layout.stored_offset > layout.matrix_offset || layout.matrix_offset > file_size ||
           layout.matrix_offset % 64 != 0;
}

static bool CorpusSectionsBroken(const Layout &layout, std::span<const std::byte> bytes)
{
    return layout.source_count > (layout.line_offset - layout.source_offset) / SOURCE_RECORD_BYTES ||
           layout.distinct_offset - layout.line_offset != layout.line_count * LINE_RECORD_BYTES ||
           layout.text_offset - layout.distinct_offset != layout.distinct_count * TEXT_RECORD_BYTES ||
           !RangeInside(layout.policy_offset, layout.policy_bytes, layout.source_offset) ||
           !IsZero(bytes, HEADER_BYTES + layout.policy_bytes, layout.source_offset);
}

static bool CorpusLexicalBroken(const Layout &layout, std::span<const std::byte> bytes, bool has_lexical)
{
    return has_lexical &&
           (layout.stored_offset > layout.lexical_offset || layout.lexical_offset > layout.matrix_offset ||
            !RangeInside(layout.lexical_offset, layout.lexical_bytes, layout.matrix_offset) ||
            layout.lexical_offset + layout.lexical_bytes > layout.matrix_offset ||
            !IsZero(bytes, layout.lexical_offset + layout.lexical_bytes, layout.matrix_offset));
}

static bool CorpusMatrixBroken(const Layout &layout, std::uint64_t file_size)
{
    const std::uint64_t matrix_bytes = layout.distinct_count * layout.dimensions * sizeof(float);
    return !RangeInside(layout.matrix_offset, matrix_bytes, file_size) ||
           layout.matrix_offset + matrix_bytes != file_size;
}

static std::expected<void, EngineError> ValidateCorpusLayout(const Layout &layout, std::span<const std::byte> bytes,
                                                             bool has_lexical)
{
    if (CorpusLimitsExceeded(layout) || CorpusOffsetsBroken(layout, bytes.size()) ||
        CorpusSectionsBroken(layout, bytes) || CorpusLexicalBroken(layout, bytes, has_lexical) ||
        CorpusMatrixBroken(layout, bytes.size()))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

struct SourceRecordFields
{
    std::uint64_t path_at = 0;
    std::uint32_t path_length = 0;
    std::uint32_t flags = 0;
    std::uint64_t size = 0;
    std::uint64_t modified = 0;
    std::uint64_t field_at = 0;
    std::uint32_t field_length = 0;
};

static bool SourceRecordRejected(std::span<const std::byte> bytes, std::uint64_t record, std::uint64_t source_cursor,
                                 std::uint64_t line_limit, SourceRecordFields &fields)
{
    std::uint32_t reserved = 0;
    return !ReadU64(bytes, record, fields.path_at) || !ReadU32(bytes, record + 8, fields.path_length) ||
           !ReadU32(bytes, record + 12, fields.flags) || !ReadU64(bytes, record + 16, fields.size) ||
           !ReadU64(bytes, record + 24, fields.modified) || !ReadU64(bytes, record + 32, fields.field_at) ||
           !ReadU32(bytes, record + 40, fields.field_length) || !ReadU32(bytes, record + 44, reserved) ||
           fields.path_length == 0 || fields.path_length > MAX_TEXT_BYTES || fields.field_length > MAX_TEXT_BYTES ||
           (fields.flags & ~(SOURCE_TRACKED | SOURCE_STORED_ONLY)) != 0 || reserved != 0 ||
           fields.path_at != source_cursor || !RangeInside(fields.path_at, fields.path_length, line_limit) ||
           fields.field_at != fields.path_at + fields.path_length ||
           !RangeInside(fields.field_at, fields.field_length, line_limit);
}

static std::expected<void, EngineError> LoadMappedSources(std::span<const std::byte> bytes, const Layout &layout,
                                                          std::vector<SourceInfo> &sources)
{
    sources.reserve(static_cast<std::size_t>(layout.source_count));
    std::uint64_t source_cursor = layout.source_offset + (layout.source_count * SOURCE_RECORD_BYTES);
    for (std::uint64_t index = 0; index < layout.source_count; ++index)
    {
        const std::uint64_t record = layout.source_offset + (index * SOURCE_RECORD_BYTES);
        SourceRecordFields fields;
        if (SourceRecordRejected(bytes, record, source_cursor, layout.line_offset, fields))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        SourceInfo source{.path = std::string{ViewBytes(bytes, fields.path_at, fields.path_length)},
                          .size = fields.size,
                          .modified_time = std::bit_cast<std::int64_t>(fields.modified),
                          .tracked = (fields.flags & SOURCE_TRACKED) != 0,
                          .json_field = std::string{ViewBytes(bytes, fields.field_at, fields.field_length)},
                          .stored_only = (fields.flags & SOURCE_STORED_ONLY) != 0};
        if (source.path.find('\0') != std::string::npos || source.json_field.find('\0') != std::string::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        sources.push_back(std::move(source));
        source_cursor = fields.field_at + fields.field_length;
    }
    if (source_cursor != layout.line_offset)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    NESSO_ASSERT(sources.size() == layout.source_count);
    return {};
}

static std::expected<void, EngineError> LoadMappedDistinctTexts(std::span<const std::byte> bytes, const Layout &layout,
                                                                std::vector<std::string_view> &texts)
{
    texts.reserve(static_cast<std::size_t>(layout.distinct_count));
    std::unordered_set<std::string_view> seen_texts;
    std::uint64_t text_cursor = layout.text_offset;
    for (std::uint64_t index = 0; index < layout.distinct_count; ++index)
    {
        const std::uint64_t record = layout.distinct_offset + (index * TEXT_RECORD_BYTES);
        std::uint64_t offset = 0;
        std::uint32_t length = 0;
        std::uint32_t reserved = 0;
        if (!ReadU64(bytes, record, offset) || !ReadU32(bytes, record + 8, length) ||
            !ReadU32(bytes, record + 12, reserved) || length > MAX_TEXT_BYTES || reserved != 0 ||
            offset != text_cursor || !RangeInside(offset, length, layout.stored_offset))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const std::string_view text = ViewBytes(bytes, offset, length);
        if (!seen_texts.insert(text).second)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        texts.push_back(text);
        text_cursor += length;
    }
    if (text_cursor != layout.stored_offset)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    NESSO_ASSERT(texts.size() == layout.distinct_count);
    return {};
}

struct LineRecordExtras
{
    std::uint64_t text_offset = 0;
    std::uint32_t text_length = 0;
    std::uint32_t flags = 0;
};

static bool LineRecordRejected(std::span<const std::byte> bytes, std::uint64_t record, std::uint64_t source_count,
                               std::uint64_t distinct_count, CorpusLine &line, LineRecordExtras &extras)
{
    std::uint64_t reserved = 0;
    return !ReadU64(bytes, record, line.line_number) || !ReadU64(bytes, record + 8, line.byte_offset) ||
           !ReadU64(bytes, record + 16, line.byte_length) || !ReadU32(bytes, record + 24, line.source_index) ||
           !ReadU32(bytes, record + 28, line.distinct_id) || !ReadU64(bytes, record + 32, extras.text_offset) ||
           !ReadU32(bytes, record + 40, extras.text_length) || !ReadU32(bytes, record + 44, extras.flags) ||
           !ReadU64(bytes, record + 48, reserved) || line.line_number == 0 || line.source_index >= source_count ||
           (extras.flags & ~(LINE_ARRAY_ELEMENT | LINE_STORED_TEXT | LINE_SKIPPED)) != 0 || reserved != 0 ||
           ((extras.flags & LINE_SKIPPED) != 0) != (line.distinct_id == NO_DISTINCT) ||
           (line.distinct_id != NO_DISTINCT && line.distinct_id >= distinct_count) ||
           extras.text_length > MAX_TEXT_BYTES;
}

struct StoredTextSpan
{
    std::uint32_t flags = 0;
    std::uint64_t text_offset = 0;
    std::uint32_t text_length = 0;
    std::uint64_t stored_limit = 0;
};

static std::expected<void, EngineError> TakeStoredLineText(CorpusLine &line, std::span<const std::byte> bytes,
                                                           const StoredTextSpan &span, std::uint64_t &stored_cursor)
{
    if ((span.flags & LINE_STORED_TEXT) != 0)
    {
        if (span.text_offset != stored_cursor || !RangeInside(span.text_offset, span.text_length, span.stored_limit))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        line.stored_text = ViewBytes(bytes, span.text_offset, span.text_length);
        stored_cursor += span.text_length;
    }
    else if (span.text_offset != 0 || span.text_length != 0)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

static void AssertLoadedLines(const Layout &layout, std::span<const SourceInfo> sources,
                              std::span<const CorpusLine> lines)
{
    NESSO_ASSERT(lines.size() == layout.line_count);
    NESSO_ASSERT(std::ranges::all_of(lines,
                                     [&](const CorpusLine &line)
                                     {
                                         return line.source_index < sources.size() &&
                                                (line.distinct_id == NO_DISTINCT ||
                                                 line.distinct_id < layout.distinct_count);
                                     }));
}

static std::expected<void, EngineError> LoadMappedLines(std::span<const std::byte> bytes, const Layout &layout,
                                                        bool has_lexical, std::span<const SourceInfo> sources,
                                                        std::vector<CorpusLine> &lines)
{
    lines.reserve(static_cast<std::size_t>(layout.line_count));
    std::vector<bool> used(static_cast<std::size_t>(layout.distinct_count));
    std::uint64_t stored_cursor = layout.stored_offset;
    for (std::uint64_t index = 0; index < layout.line_count; ++index)
    {
        const std::uint64_t record = layout.line_offset + (index * LINE_RECORD_BYTES);
        CorpusLine line;
        LineRecordExtras extras;
        if (LineRecordRejected(bytes, record, sources.size(), layout.distinct_count, line, extras))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        if (!lines.empty())
        {
            const CorpusLine &previous = lines.back();
            if (line.source_index < previous.source_index ||
                (line.source_index == previous.source_index && line.line_number <= previous.line_number))
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
        }
        const SourceInfo &source = sources[line.source_index];
        if ((source.tracked && !RangeInside(line.byte_offset, line.byte_length, source.size)) ||
            (source.stored_only && (extras.flags & LINE_STORED_TEXT) == 0))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const StoredTextSpan span{.flags = extras.flags,
                                  .text_offset = extras.text_offset,
                                  .text_length = extras.text_length,
                                  .stored_limit = has_lexical ? layout.lexical_offset : layout.matrix_offset};
        if (const auto stored = TakeStoredLineText(line, bytes, span, stored_cursor); !stored)
        {
            return std::unexpected(stored.error());
        }
        if (line.distinct_id != NO_DISTINCT)
        {
            used[line.distinct_id] = true;
        }
        line.array_element = (extras.flags & LINE_ARRAY_ELEMENT) != 0;
        lines.push_back(line);
    }
    if (std::ranges::any_of(used, [](bool value) { return !value; }) ||
        (has_lexical ? stored_cursor != layout.lexical_offset : !IsZero(bytes, stored_cursor, layout.matrix_offset)))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    AssertLoadedLines(layout, sources, lines);
    return {};
}

static std::expected<void, EngineError> DecodeMappedLexical(std::span<const std::byte> bytes, const Layout &layout,
                                                            std::vector<std::string> &owned_terms,
                                                            std::vector<std::string_view> &dictionary,
                                                            std::vector<LexicalLinePostings> &postings)
{
    return DecodeLexicalSection(
        bytes.subspan(static_cast<std::size_t>(layout.lexical_offset), static_cast<std::size_t>(layout.lexical_bytes)),
        layout.line_count, owned_terms, dictionary, postings);
}

static void ApplyLexicalPolicy(bool has_lexical, const CorpusPolicy &policy, std::string &terms_version,
                               LexicalStatus &status)
{
    if (policy.contains("lexical.terms"))
    {
        terms_version = policy.at("lexical.terms");
    }
    status = LexicalStatusOf(has_lexical, policy);
}

std::expected<MappedCorpusFile, CorpusMapFailure> MapCorpusFile(const std::filesystem::path &path)
{
    auto mapped = nesso::platform::MappedFile::Open(path);
    if (!mapped)
    {
        return std::unexpected(MakeMapFailure(mapped.error() == nesso::platform::FileError::OpenFailure
                                                  ? EngineError::FileOpenFailure
                                                  : EngineError::CorruptDatabase));
    }
    const std::span<const std::byte> bytes = mapped->Bytes();
    auto header = MapCorpusHeader(bytes);
    if (!header)
    {
        return std::unexpected(header.error());
    }
    const Layout &layout = header->layout;
    const std::uint32_t version = header->version;
    auto fail = [version](EngineError code) { return std::unexpected(MakeMapFailure(code, version)); };
    // Pre-postings v3 used lexicalOffset=lexicalBytes=0. Those corpora stay
    // readable for dense search; new writers always emit a non-empty section.
    const bool has_lexical = layout.lexical_offset != 0 || layout.lexical_bytes != 0;
    if (const auto valid = ValidateCorpusLayout(layout, bytes, has_lexical); !valid)
    {
        return fail(valid.error());
    }
    auto policy = ParsePolicy(ViewBytes(bytes, layout.policy_offset, layout.policy_bytes));
    if (!policy)
    {
        return fail(policy.error());
    }
    const auto dimensions = ValidatePolicy(*policy);
    if (!dimensions || *dimensions != layout.dimensions)
    {
        return fail(EngineError::CorruptDatabase);
    }

    auto impl = std::make_unique<MappedCorpus::Impl>();
    impl->policy = std::move(*policy);
    impl->dimensions = layout.dimensions;
    if (const auto sources = LoadMappedSources(bytes, layout, impl->sources); !sources)
    {
        return fail(sources.error());
    }
    if (const auto texts = LoadMappedDistinctTexts(bytes, layout, impl->texts); !texts)
    {
        return fail(texts.error());
    }
    if (const auto lines = LoadMappedLines(bytes, layout, has_lexical, impl->sources, impl->lines); !lines)
    {
        return fail(lines.error());
    }
    if (has_lexical)
    {
        if (const auto decoded = DecodeMappedLexical(bytes, layout, impl->lexical_term_storage,
                                                     impl->lexical_dictionary, impl->lexical_postings);
            !decoded)
        {
            return fail(decoded.error());
        }
    }
    ApplyLexicalPolicy(has_lexical, impl->policy, impl->corpus_lexical_terms_version, impl->lexical_status);

    impl->file = std::move(*mapped);
    const auto stable = impl->file.Bytes();
    const std::span<const std::byte> matrix_bytes = stable.subspan(static_cast<std::size_t>(layout.matrix_offset));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    impl->matrix = {reinterpret_cast<const float *>(matrix_bytes.data()), layout.distinct_count * layout.dimensions};
    AssertMappedShape(MappedShape{.sources = impl->sources.size(),
                                  .texts = impl->texts.size(),
                                  .lines = impl->lines.size(),
                                  .matrix = impl->matrix.size(),
                                  .postings = impl->lexical_postings.size(),
                                  .dictionary = impl->lexical_dictionary.size(),
                                  .stored_terms = impl->lexical_term_storage.size()},
                      layout, has_lexical);
    return MappedCorpusFile{.corpus = MappedCorpus{std::move(impl)}, .version = version};
}

std::expected<std::vector<CorpusChunk>, EngineError> ReadCorpusFile(const std::filesystem::path &path)
{
    auto mapped = MapCorpusFile(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error().code);
    }
    const MappedCorpus &corpus = mapped->corpus;
    NESSO_ASSERT(corpus.Matrix().size() == corpus.Texts().size() * corpus.Dimensions());
    if (!corpus.Texts().empty())
    {
        NESSO_ASSERT(corpus.Dimensions() > 0);
    }
    std::vector<CorpusChunk> chunks;
    for (const CorpusLine &line : corpus.Lines())
    {
        if (line.distinct_id == NO_DISTINCT)
        {
            continue;
        }
        const std::size_t dimensions = corpus.Dimensions();
        const auto embedding =
            corpus.Matrix().subspan(static_cast<std::size_t>(line.distinct_id) * dimensions, dimensions);
        chunks.push_back(
            CorpusChunk{.chunk = {.text = std::string{line.stored_text.value_or(corpus.Texts()[line.distinct_id])},
                                  .line_number = line.line_number,
                                  .source = corpus.Sources()[line.source_index].path,
                                  .byte_offset = line.byte_offset,
                                  .array_element = line.array_element,
                                  .byte_length = line.byte_length},
                        .embedding = {embedding.begin(), embedding.end()}});
    }
    return chunks;
}

} // namespace nesso::core
