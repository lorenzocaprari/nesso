// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "corpus_detail.hpp"

#include <core/corpus_index.hpp>
#include <platform/file.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nesso::core
{

using namespace detail;

struct Layout
{
    std::uint32_t policyBytes = 0;
    std::uint32_t dimensions = 0;
    std::uint64_t sourceCount = 0;
    std::uint64_t lineCount = 0;
    std::uint64_t sourceOffset = 0;
    std::uint64_t lineOffset = 0;
    std::uint64_t matrixOffset = 0;
    std::uint64_t distinctCount = 0;
    std::uint64_t distinctOffset = 0;
    std::uint64_t textOffset = 0;
    std::uint64_t storedOffset = 0;
    std::uint64_t lexicalOffset = 0;
    std::uint64_t lexicalBytes = 0;
    std::uint64_t policyOffset = 0;
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
    std::vector<std::string> lexicalTermStorage;
    std::vector<std::string_view> lexicalDictionary;
    std::vector<LexicalLinePostings> lexicalPostings;
    LexicalStatus lexicalStatus = LexicalStatus::Missing;
    std::string corpusLexicalTermsVersion;
};

MappedCorpus::MappedCorpus(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MappedCorpus::~MappedCorpus() = default;
MappedCorpus::MappedCorpus(MappedCorpus &&) noexcept = default;
MappedCorpus &MappedCorpus::operator=(MappedCorpus &&) noexcept = default;

const CorpusPolicy &MappedCorpus::policy() const noexcept { return impl_->policy; }
std::uint32_t MappedCorpus::dimensions() const noexcept { return impl_->dimensions; }
std::span<const SourceInfo> MappedCorpus::sources() const noexcept { return impl_->sources; }
std::span<const CorpusLine> MappedCorpus::lines() const noexcept { return impl_->lines; }
std::span<const std::string_view> MappedCorpus::texts() const noexcept { return impl_->texts; }
std::span<const float> MappedCorpus::matrix() const noexcept { return impl_->matrix; }
std::span<const std::string_view> MappedCorpus::lexicalDictionary() const noexcept { return impl_->lexicalDictionary; }
std::span<const LexicalLinePostings> MappedCorpus::lexicalPostings() const noexcept { return impl_->lexicalPostings; }
// NOLINTNEXTLINE(readability-identifier-naming)
LexicalStatus MappedCorpus::GetLexicalStatus() const noexcept { return impl_->lexicalStatus; }
std::string_view MappedCorpus::corpusLexicalTermsVersion() const noexcept { return impl_->corpusLexicalTermsVersion; }

static bool readLayoutFields(std::span<const std::byte> bytes, Layout &layout)
{
    return bytes.size() >= HEADER_BYTES && readU32(bytes, 8, layout.policyBytes) &&
           readU32(bytes, 12, layout.dimensions) && readU64(bytes, 16, layout.sourceCount) &&
           readU64(bytes, 24, layout.lineCount) && readU64(bytes, 32, layout.sourceOffset) &&
           readU64(bytes, 40, layout.lineOffset) && readU64(bytes, 48, layout.matrixOffset) &&
           readU64(bytes, 56, layout.distinctCount) && readU64(bytes, 64, layout.distinctOffset) &&
           readU64(bytes, 72, layout.textOffset) && readU64(bytes, 80, layout.storedOffset) &&
           readU64(bytes, 88, layout.lexicalOffset) && readU64(bytes, 96, layout.lexicalBytes) &&
           readU64(bytes, 104, layout.policyOffset);
}

static std::expected<Layout, EngineError> mapCorpusHeader(std::span<const std::byte> bytes,
                                                          std::uint32_t *observedVersion)
{
    std::uint32_t version = 0;
    if (bytes.size() < 8 || std::memcmp(bytes.data(), MAGIC.data(), MAGIC.size()) != 0 || !readU32(bytes, 4, version))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    if (observedVersion != nullptr)
    {
        *observedVersion = version;
    }
    if (version != CORPUS_VERSION)
    {
        return std::unexpected(EngineError::UnsupportedVersion);
    }
    if (bytes.size() < HEADER_BYTES)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    const CorpusChecksum checksum = ComputeCorpusChecksum(bytes);
    if (!std::ranges::equal(checksum, bytes.subspan(CORPUS_CHECKSUM_OFFSET, CORPUS_CHECKSUM_BYTES)))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    Layout layout;
    if (!readLayoutFields(bytes, layout))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return layout;
}

static bool corpusLimitsExceeded(const Layout &layout)
{
    return layout.policyBytes > MAX_POLICY_BYTES || layout.dimensions > MAX_DIMENSIONS ||
           layout.sourceCount > MAX_SOURCES || layout.lineCount > MAX_LINES || layout.distinctCount > MAX_DISTINCT ||
           (layout.distinctCount > 0 && layout.dimensions == 0) || layout.lexicalBytes > MAX_LEXICAL_BYTES;
}

static bool corpusOffsetsBroken(const Layout &layout, std::uint64_t fileSize)
{
    return layout.policyOffset != HEADER_BYTES ||
           layout.sourceOffset != alignUp(HEADER_BYTES + layout.policyBytes, 8) ||
           layout.sourceOffset > layout.lineOffset || layout.lineOffset > layout.distinctOffset ||
           layout.distinctOffset > layout.textOffset || layout.textOffset > layout.storedOffset ||
           layout.storedOffset > layout.matrixOffset || layout.matrixOffset > fileSize || layout.matrixOffset % 64 != 0;
}

static bool corpusSectionsBroken(const Layout &layout, std::span<const std::byte> bytes)
{
    return layout.sourceCount > (layout.lineOffset - layout.sourceOffset) / SOURCE_RECORD_BYTES ||
           layout.distinctOffset - layout.lineOffset != layout.lineCount * LINE_RECORD_BYTES ||
           layout.textOffset - layout.distinctOffset != layout.distinctCount * TEXT_RECORD_BYTES ||
           !rangeInside(layout.policyOffset, layout.policyBytes, layout.sourceOffset) ||
           !isZero(bytes, HEADER_BYTES + layout.policyBytes, layout.sourceOffset);
}

static bool corpusLexicalBroken(const Layout &layout, std::span<const std::byte> bytes, bool hasLexical)
{
    return hasLexical && (layout.storedOffset > layout.lexicalOffset || layout.lexicalOffset > layout.matrixOffset ||
                          !rangeInside(layout.lexicalOffset, layout.lexicalBytes, layout.matrixOffset) ||
                          layout.lexicalOffset + layout.lexicalBytes > layout.matrixOffset ||
                          !isZero(bytes, layout.lexicalOffset + layout.lexicalBytes, layout.matrixOffset));
}

static bool corpusMatrixBroken(const Layout &layout, std::uint64_t fileSize)
{
    const std::uint64_t matrixBytes = layout.distinctCount * layout.dimensions * sizeof(float);
    return !rangeInside(layout.matrixOffset, matrixBytes, fileSize) || layout.matrixOffset + matrixBytes != fileSize;
}

static std::expected<void, EngineError> validateCorpusLayout(const Layout &layout, std::span<const std::byte> bytes,
                                                             bool hasLexical)
{
    if (corpusLimitsExceeded(layout) || corpusOffsetsBroken(layout, bytes.size()) ||
        corpusSectionsBroken(layout, bytes) || corpusLexicalBroken(layout, bytes, hasLexical) ||
        corpusMatrixBroken(layout, bytes.size()))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

static bool sourceRecordRejected(std::span<const std::byte> bytes, std::uint64_t record, std::uint64_t sourceCursor,
                                 std::uint64_t lineLimit, std::uint64_t &pathAt, std::uint32_t &pathLength,
                                 std::uint32_t &flags, std::uint64_t &size, std::uint64_t &modified,
                                 std::uint64_t &fieldAt, std::uint32_t &fieldLength)
{
    std::uint32_t reserved = 0;
    return !readU64(bytes, record, pathAt) || !readU32(bytes, record + 8, pathLength) ||
           !readU32(bytes, record + 12, flags) || !readU64(bytes, record + 16, size) ||
           !readU64(bytes, record + 24, modified) || !readU64(bytes, record + 32, fieldAt) ||
           !readU32(bytes, record + 40, fieldLength) || !readU32(bytes, record + 44, reserved) || pathLength == 0 ||
           pathLength > MAX_TEXT_BYTES || fieldLength > MAX_TEXT_BYTES ||
           (flags & ~(SOURCE_TRACKED | SOURCE_STORED_ONLY)) != 0 || reserved != 0 || pathAt != sourceCursor ||
           !rangeInside(pathAt, pathLength, lineLimit) || fieldAt != pathAt + pathLength ||
           !rangeInside(fieldAt, fieldLength, lineLimit);
}

static std::expected<void, EngineError> loadMappedSources(std::span<const std::byte> bytes, const Layout &layout,
                                                          std::vector<SourceInfo> &sources)
{
    sources.reserve(static_cast<std::size_t>(layout.sourceCount));
    std::uint64_t sourceCursor = layout.sourceOffset + (layout.sourceCount * SOURCE_RECORD_BYTES);
    for (std::uint64_t index = 0; index < layout.sourceCount; ++index)
    {
        const std::uint64_t record = layout.sourceOffset + (index * SOURCE_RECORD_BYTES);
        std::uint64_t pathOffset = 0;
        std::uint32_t pathLength = 0;
        std::uint32_t flags = 0;
        std::uint64_t size = 0;
        std::uint64_t modified = 0;
        std::uint64_t fieldOffset = 0;
        std::uint32_t fieldLength = 0;
        if (sourceRecordRejected(bytes, record, sourceCursor, layout.lineOffset, pathOffset, pathLength, flags, size,
                                 modified, fieldOffset, fieldLength))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        SourceInfo source{.path = std::string{viewBytes(bytes, pathOffset, pathLength)},
                          .size = size,
                          .modifiedTime = std::bit_cast<std::int64_t>(modified),
                          .tracked = (flags & SOURCE_TRACKED) != 0,
                          .jsonField = std::string{viewBytes(bytes, fieldOffset, fieldLength)},
                          .storedOnly = (flags & SOURCE_STORED_ONLY) != 0};
        if (source.path.find('\0') != std::string::npos || source.jsonField.find('\0') != std::string::npos)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        sources.push_back(std::move(source));
        sourceCursor = fieldOffset + fieldLength;
    }
    if (sourceCursor != layout.lineOffset)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

static std::expected<void, EngineError> loadMappedDistinctTexts(std::span<const std::byte> bytes, const Layout &layout,
                                                                std::vector<std::string_view> &texts)
{
    texts.reserve(static_cast<std::size_t>(layout.distinctCount));
    std::unordered_set<std::string_view> seenTexts;
    std::uint64_t textCursor = layout.textOffset;
    for (std::uint64_t index = 0; index < layout.distinctCount; ++index)
    {
        const std::uint64_t record = layout.distinctOffset + (index * TEXT_RECORD_BYTES);
        std::uint64_t offset = 0;
        std::uint32_t length = 0;
        std::uint32_t reserved = 0;
        if (!readU64(bytes, record, offset) || !readU32(bytes, record + 8, length) ||
            !readU32(bytes, record + 12, reserved) || length > MAX_TEXT_BYTES || reserved != 0 ||
            offset != textCursor || !rangeInside(offset, length, layout.storedOffset))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const std::string_view text = viewBytes(bytes, offset, length);
        if (!seenTexts.insert(text).second)
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        texts.push_back(text);
        textCursor += length;
    }
    if (textCursor != layout.storedOffset)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

static bool lineRecordRejected(std::span<const std::byte> bytes, std::uint64_t record, std::uint64_t sourceCount,
                               std::uint64_t distinctCount, CorpusLine &line, std::uint64_t &textOffset,
                               std::uint32_t &textLength, std::uint32_t &flags)
{
    std::uint64_t reserved = 0;
    return !readU64(bytes, record, line.lineNumber) || !readU64(bytes, record + 8, line.byteOffset) ||
           !readU64(bytes, record + 16, line.byteLength) || !readU32(bytes, record + 24, line.sourceIndex) ||
           !readU32(bytes, record + 28, line.distinctId) || !readU64(bytes, record + 32, textOffset) ||
           !readU32(bytes, record + 40, textLength) || !readU32(bytes, record + 44, flags) ||
           !readU64(bytes, record + 48, reserved) || line.lineNumber == 0 || line.sourceIndex >= sourceCount ||
           (flags & ~(LINE_ARRAY_ELEMENT | LINE_STORED_TEXT | LINE_SKIPPED)) != 0 || reserved != 0 ||
           ((flags & LINE_SKIPPED) != 0) != (line.distinctId == NO_DISTINCT) ||
           (line.distinctId != NO_DISTINCT && line.distinctId >= distinctCount) || textLength > MAX_TEXT_BYTES;
}

static std::expected<void, EngineError> takeStoredLineText(CorpusLine &line, std::span<const std::byte> bytes,
                                                           std::uint32_t flags, std::uint64_t textOffset,
                                                           std::uint32_t textLength, std::uint64_t storedLimit,
                                                           std::uint64_t &storedCursor)
{
    if ((flags & LINE_STORED_TEXT) != 0)
    {
        if (textOffset != storedCursor || !rangeInside(textOffset, textLength, storedLimit))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        line.storedText = viewBytes(bytes, textOffset, textLength);
        storedCursor += textLength;
    }
    else if (textOffset != 0 || textLength != 0)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

static std::expected<void, EngineError> loadMappedLines(std::span<const std::byte> bytes, const Layout &layout,
                                                        bool hasLexical, std::span<const SourceInfo> sources,
                                                        std::vector<CorpusLine> &lines)
{
    lines.reserve(static_cast<std::size_t>(layout.lineCount));
    std::vector<bool> used(static_cast<std::size_t>(layout.distinctCount));
    std::uint64_t storedCursor = layout.storedOffset;
    for (std::uint64_t index = 0; index < layout.lineCount; ++index)
    {
        const std::uint64_t record = layout.lineOffset + (index * LINE_RECORD_BYTES);
        CorpusLine line;
        std::uint64_t textOffset = 0;
        std::uint32_t textLength = 0;
        std::uint32_t flags = 0;
        if (lineRecordRejected(bytes, record, sources.size(), layout.distinctCount, line, textOffset, textLength,
                               flags))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        if (!lines.empty())
        {
            const CorpusLine &previous = lines.back();
            if (line.sourceIndex < previous.sourceIndex ||
                (line.sourceIndex == previous.sourceIndex && line.lineNumber <= previous.lineNumber))
            {
                return std::unexpected(EngineError::CorruptDatabase);
            }
        }
        const SourceInfo &source = sources[line.sourceIndex];
        if ((source.tracked && !rangeInside(line.byteOffset, line.byteLength, source.size)) ||
            (source.storedOnly && (flags & LINE_STORED_TEXT) == 0))
        {
            return std::unexpected(EngineError::CorruptDatabase);
        }
        const std::uint64_t storedLimit = hasLexical ? layout.lexicalOffset : layout.matrixOffset;
        if (const auto stored =
                takeStoredLineText(line, bytes, flags, textOffset, textLength, storedLimit, storedCursor);
            !stored)
        {
            return std::unexpected(stored.error());
        }
        if (line.distinctId != NO_DISTINCT)
        {
            used[line.distinctId] = true;
        }
        line.arrayElement = (flags & LINE_ARRAY_ELEMENT) != 0;
        lines.push_back(line);
    }
    if (std::ranges::any_of(used, [](bool value) { return !value; }) ||
        (hasLexical ? storedCursor != layout.lexicalOffset : !isZero(bytes, storedCursor, layout.matrixOffset)))
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }
    return {};
}

static std::expected<void, EngineError> attachLexicalSection(std::span<const std::byte> bytes, const Layout &layout,
                                                             bool hasLexical, const CorpusPolicy &policy,
                                                             std::vector<std::string> &ownedTerms,
                                                             std::vector<std::string_view> &dictionary,
                                                             std::vector<LexicalLinePostings> &postings,
                                                             std::string &termsVersion, LexicalStatus &status)
{
    if (hasLexical)
    {
        if (const auto decoded = decodeLexicalSection(bytes.subspan(static_cast<std::size_t>(layout.lexicalOffset),
                                                                    static_cast<std::size_t>(layout.lexicalBytes)),
                                                      layout.lineCount, ownedTerms, dictionary, postings);
            !decoded)
        {
            return std::unexpected(decoded.error());
        }
    }
    if (policy.contains("lexical.terms"))
    {
        termsVersion = policy.at("lexical.terms");
    }
    status = lexicalStatusOf(hasLexical, policy);
    return {};
}

std::expected<MappedCorpus, EngineError> mapCorpusFile(const std::filesystem::path &path,
                                                       std::uint32_t *observedVersion)
{
    auto mapped = nesso::platform::MappedFile::open(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error() == nesso::platform::FileError::OpenFailure
                                   ? EngineError::FileOpenFailure
                                   : EngineError::CorruptDatabase);
    }
    const std::span<const std::byte> bytes = mapped->bytes();
    auto layout = mapCorpusHeader(bytes, observedVersion);
    if (!layout)
    {
        return std::unexpected(layout.error());
    }
    // Pre-postings v3 used lexicalOffset=lexicalBytes=0. Those corpora stay
    // readable for dense search; new writers always emit a non-empty section.
    const bool hasLexical = layout->lexicalOffset != 0 || layout->lexicalBytes != 0;
    if (const auto valid = validateCorpusLayout(*layout, bytes, hasLexical); !valid)
    {
        return std::unexpected(valid.error());
    }
    auto policy = parsePolicy(viewBytes(bytes, layout->policyOffset, layout->policyBytes));
    if (!policy)
    {
        return std::unexpected(policy.error());
    }
    const auto dimensions = validatePolicy(*policy);
    if (!dimensions || *dimensions != layout->dimensions)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    auto impl = std::make_unique<MappedCorpus::Impl>();
    impl->policy = std::move(*policy);
    impl->dimensions = layout->dimensions;
    if (const auto sources = loadMappedSources(bytes, *layout, impl->sources); !sources)
    {
        return std::unexpected(sources.error());
    }
    if (const auto texts = loadMappedDistinctTexts(bytes, *layout, impl->texts); !texts)
    {
        return std::unexpected(texts.error());
    }
    if (const auto lines = loadMappedLines(bytes, *layout, hasLexical, impl->sources, impl->lines); !lines)
    {
        return std::unexpected(lines.error());
    }
    if (const auto lexical = attachLexicalSection(bytes, *layout, hasLexical, impl->policy, impl->lexicalTermStorage,
                                                  impl->lexicalDictionary, impl->lexicalPostings,
                                                  impl->corpusLexicalTermsVersion, impl->lexicalStatus);
        !lexical)
    {
        return std::unexpected(lexical.error());
    }

    impl->file = std::move(*mapped);
    const auto stable = impl->file.bytes();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    impl->matrix = {reinterpret_cast<const float *>(stable.data() + layout->matrixOffset),
                    layout->distinctCount * layout->dimensions};
    return MappedCorpus{std::move(impl)};
}

std::expected<std::vector<CorpusChunk>, EngineError> readCorpusFile(const std::filesystem::path &path)
{
    auto mapped = mapCorpusFile(path);
    if (!mapped)
    {
        return std::unexpected(mapped.error());
    }
    std::vector<CorpusChunk> chunks;
    for (const CorpusLine &line : mapped->lines())
    {
        if (line.distinctId == NO_DISTINCT)
        {
            continue;
        }
        const std::size_t dimensions = mapped->dimensions();
        const auto embedding =
            mapped->matrix().subspan(static_cast<std::size_t>(line.distinctId) * dimensions, dimensions);
        chunks.push_back(
            CorpusChunk{.chunk = {.text = std::string{line.storedText.value_or(mapped->texts()[line.distinctId])},
                                  .lineNumber = line.lineNumber,
                                  .source = mapped->sources()[line.sourceIndex].path,
                                  .byteOffset = line.byteOffset,
                                  .arrayElement = line.arrayElement,
                                  .byteLength = line.byteLength},
                        .embedding = {embedding.begin(), embedding.end()}});
    }
    return chunks;
}

} // namespace nesso::core
