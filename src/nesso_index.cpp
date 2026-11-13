// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso.hpp>

#include <error_map.hpp>
#include <nesso_detail.hpp>
#include <trace.hpp>

#include <core/corpus_index.hpp>
#include <core/dedup.hpp>
#include <core/lexical_terms.hpp>
#include <embed/embedder.hpp>
#include <parser/archive_source.hpp>
#include <parser/log_chunker.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nesso
{

using detail::embedDistinct;
using detail::expandRequestFiles;
using detail::LineCounts;
using detail::MakeCorpusPolicy;
using detail::makeError;
using detail::MAX_LINE_LENGTH;
using detail::noKeptLines;
using detail::parseFailure;
using detail::ProgressReporter;
using detail::sameSource;
using detail::Scan;
using detail::scanFiles;
using detail::streamBatch;

// Like visitChunks, but also yields skipped records so v3 line ordinals stay complete.
template <typename Consume>
static std::expected<void, nesso::parser::ParseError>
visitRecords(const std::filesystem::path &path, Scan &scan, bool &stdinUsed, std::string_view jsonField,
             const nesso::parser::ArchiveLimits &limits, const Consume &consume)
{
    if (path == "-")
    {
        if (!stdinUsed)
        {
            stdinUsed = true;
            for (nesso::parser::ParsedChunk &chunk : scan.stdinChunks)
            {
                consume(std::move(chunk));
            }
        }
        return {};
    }
    return nesso::parser::Chunker::forEachRecord(path, MAX_LINE_LENGTH, nullptr, jsonField, consume, limits);
}

// `note` selects the index wording in `message`. `EmptyInput` stays exit 1.
static std::unexpected<Error> nothingToIndex(const IndexRequest &request, const Scan &scanned)
{
    const ErrorCause cause = noKeptLines(scanned.counts) ? ErrorCause::EmptyFile : ErrorCause::NothingIndexable;
    const std::filesystem::path path = request.files.size() == 1 ? request.files.front() : std::filesystem::path{};
    Error error = makeError(ErrorKind::EmptyInput, cause, path, scanned.counts, scanned.bundleSkips);
    error.note = "check the input path or --json-field";
    return std::unexpected(std::move(error));
}

struct IndexSourceCursor
{
    bool archive = false;
    std::uint32_t activeSource = 0;
    bool storeLines = false;
};

struct IndexEmbedState
{
    const IndexRequest *request = nullptr;
    Scan *scanned = nullptr;
    const nesso::embed::Embedder *model = nullptr;
    nesso::core::CorpusWriter *writer = nullptr;
    nesso::core::DistinctIndex distinct;
    ProgressReporter *progress = nullptr;
    std::uint32_t reserved = 0;
    std::uint32_t vectorsWritten = 0;
    size_t linesSeen = 0;
    size_t linesReported = 0;
    size_t batch = 0;
    std::optional<Error> failed;
};

static std::expected<void, Error> guardStableSource(const std::filesystem::path &path, std::string_view jsonField,
                                                    const nesso::core::SourceInfo &expected, const LineCounts &counts)
{
    if (path == "-")
    {
        return {};
    }
    const auto current = nesso::core::sourceInfoFor(path, jsonField);
    if (!current || !sameSource(expected, *current))
    {
        return std::unexpected(makeError(ErrorKind::Parse, ErrorCause::SourceChanged, path, counts));
    }
    return {};
}

static std::expected<void, Error> flushIndexEmbeddings(IndexEmbedState &state)
{
    if (state.distinct.pending().empty())
    {
        return {};
    }
    const LineCounts &counts = state.scanned->counts;
    const size_t linesInFlush = state.linesSeen - state.linesReported;
    auto embeddings = embedDistinct(
        state.distinct.pending(), *state.model, *state.progress,
        {.linesBefore = state.linesReported, .linesInFlush = linesInFlush, .total = state.scanned->chunks}, counts);
    if (!embeddings)
    {
        return std::unexpected(embeddings.error());
    }
    for (const std::vector<float> &embedding : *embeddings)
    {
        const auto added = state.writer->addVector(state.vectorsWritten, embedding);
        if (!added)
        {
            return std::unexpected(
                makeError(ErrorKind::CorpusWrite, causeOf(added.error()), state.request->output, counts));
        }
        ++state.vectorsWritten;
    }
    state.distinct.clearPending();
    state.linesReported = state.linesSeen;
    state.progress->update(state.linesReported, state.scanned->chunks);
    return {};
}

static std::optional<Error> ingestIndexChunk(IndexEmbedState &state, IndexSourceCursor &cursor,
                                             nesso::parser::ParsedChunk chunk)
{
    const LineCounts &counts = state.scanned->counts;
    if (cursor.archive && chunk.entryStart)
    {
        nesso::core::SourceInfo entry{.path = chunk.source,
                                      .size = 0,
                                      .modifiedTime = 0,
                                      .tracked = false,
                                      .jsonField = std::string{state.request->jsonField},
                                      .storedOnly = true};
        const auto added = state.writer->addSource(std::move(entry));
        if (!added)
        {
            return makeError(ErrorKind::CorpusWrite, causeOf(added.error()), state.request->output, counts);
        }
        cursor.activeSource = *added;
        cursor.storeLines = true;
    }
    std::uint32_t id = nesso::core::NO_DISTINCT;
    if (!chunk.skipped)
    {
        id = state.distinct.add(chunk.text);
        if (id == state.reserved)
        {
            const auto added = state.writer->reserveDistinct(state.distinct.text(id));
            if (!added || *added != id)
            {
                return makeError(ErrorKind::CorpusWrite, added ? ErrorCause::CorruptFile : causeOf(added.error()),
                                 state.request->output, counts);
            }
            ++state.reserved;
        }
        ++state.linesSeen;
    }
    nesso::core::CorpusLine line{.lineNumber = chunk.lineNumber,
                                 .byteOffset = chunk.byteOffset,
                                 .byteLength = chunk.byteLength,
                                 .sourceIndex = cursor.activeSource,
                                 .distinctId = id,
                                 .arrayElement = chunk.arrayElement};
    if (cursor.storeLines)
    {
        line.storedText = chunk.text;
    }
    std::vector<std::string> terms = chunk.skipped ? std::vector<std::string>{} : nesso::core::lexicalTerms(chunk.text);
    if (const auto added = state.writer->addLines(std::span{&line, 1}, std::span{&terms, 1}); !added)
    {
        return makeError(ErrorKind::CorpusWrite, causeOf(added.error()), state.request->output, counts);
    }
    if (state.distinct.pending().size() == state.batch)
    {
        if (const auto flushed = flushIndexEmbeddings(state); !flushed)
        {
            return flushed.error();
        }
    }
    return std::nullopt;
}

static std::expected<void, Error> embedIndexFiles(IndexEmbedState &state)
{
    const ScopedStage stage{"embed"};
    bool stdinUsed = false;
    const LineCounts &counts = state.scanned->counts;
    for (size_t sourceIndex = 0; sourceIndex < state.request->files.size(); ++sourceIndex)
    {
        const std::filesystem::path &path = state.request->files[sourceIndex];
        const nesso::core::SourceInfo &source = state.scanned->sources[sourceIndex];
        if (const auto stable = guardStableSource(path, state.request->jsonField, source, counts); !stable)
        {
            return std::unexpected(stable.error());
        }
        IndexSourceCursor cursor{
            .archive = nesso::parser::isArchivePath(path), .activeSource = 0, .storeLines = source.storedOnly};
        if (!cursor.archive)
        {
            const auto sourceId = state.writer->addSource(source);
            if (!sourceId)
            {
                return std::unexpected(
                    makeError(ErrorKind::CorpusWrite, causeOf(sourceId.error()), state.request->output, counts));
            }
            cursor.activeSource = *sourceId;
        }
        const auto streamed =
            visitRecords(path, *state.scanned, stdinUsed, state.request->jsonField, state.request->archiveLimits,
                         [&](nesso::parser::ParsedChunk chunk)
                         {
                             if (state.failed)
                             {
                                 return;
                             }
                             if (auto error = ingestIndexChunk(state, cursor, std::move(chunk)))
                             {
                                 state.failed = std::move(error);
                             }
                         });
        if (!streamed)
        {
            return std::unexpected(parseFailure(streamed.error(), path, counts, state.request->archiveLimits));
        }
        if (state.failed)
        {
            return std::unexpected(*state.failed);
        }
        if (const auto stable = guardStableSource(path, state.request->jsonField, source, counts); !stable)
        {
            return std::unexpected(stable.error());
        }
    }
    if (const auto flushed = flushIndexEmbeddings(state); !flushed)
    {
        return std::unexpected(flushed.error());
    }
    return {};
}

std::expected<IndexSummary, Error> Nesso::index(const IndexRequest &input) const
{
    const TraceSession session;
    auto expanded = expandRequestFiles(input.files);
    if (!expanded)
    {
        return std::unexpected(expanded.error());
    }
    IndexRequest request = input;
    request.files = std::move(*expanded);
    for (const std::filesystem::path &path : request.files)
    {
        std::error_code error;
        if (path != "-" && std::filesystem::equivalent(path, request.output, error))
        {
            return std::unexpected(makeError(ErrorKind::CorpusWrite, ErrorCause::OutputIsInput, request.output, {}));
        }
    }
    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto scanned = scanFiles(request.files, request.jsonField, request.archiveLimits, true);
    if (!scanned)
    {
        return std::unexpected(scanned.error());
    }

    auto policy = MakeCorpusPolicy(**model, scanned->counts);
    if (!policy)
    {
        return std::unexpected(policy.error());
    }
    // create() rejects an unusable output path; only finish() replaces an existing corpus.
    auto writer = nesso::core::CorpusWriter::create(request.output, std::move(*policy));
    if (!writer)
    {
        return std::unexpected(
            makeError(ErrorKind::CorpusWrite, causeOf(writer.error()), request.output, scanned->counts));
    }
    if (scanned->chunks == 0)
    {
        return nothingToIndex(request, *scanned);
    }

    ProgressReporter progress(request.progress);
    IndexEmbedState state;
    state.request = &request;
    state.scanned = &*scanned;
    state.model = *model;
    state.writer = &*writer;
    state.progress = &progress;
    state.batch = streamBatch();
    state.distinct.reserve(scanned->chunks);
    if (const auto embedded = embedIndexFiles(state); !embedded)
    {
        return std::unexpected(embedded.error());
    }
    progress.finish();

    const LineCounts &counts = scanned->counts;
    const auto written = [&writer]
    {
        const ScopedStage stage{"corpus-write"};
        return writer->finish();
    }();
    if (!written)
    {
        return std::unexpected(makeError(ErrorKind::CorpusWrite, causeOf(written.error()), request.output, counts));
    }
    return IndexSummary{.chunks = scanned->chunks,
                        .skippedLines = counts.skipped,
                        .emptyLines = counts.emptyLines,
                        .malformedLines = counts.malformedLines,
                        .missingFieldLines = counts.missingFieldLines,
                        .truncatedLines = counts.truncated,
                        .binaryEntries = 0,
                        .jsonField = counts.jsonField,
                        .bundleSkips = scanned->bundleSkips};
}

} // namespace nesso
