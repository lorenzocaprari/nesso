// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <nesso/nesso.hpp>

#include <nesso/error_map.hpp>
#include <nesso/trace.hpp>
#include <nesso_detail.hpp>

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
#include <system_error>
#include <utility>
#include <vector>

namespace nesso
{

using detail::EmbedDistinct;
using detail::ExpandRequestFiles;
using detail::LineCounts;
using detail::MakeCorpusPolicy;
using detail::MakeError;
using detail::MAX_LINE_LENGTH;
using detail::NoKeptLines;
using detail::ParseFailure;
using detail::ProgressReporter;
using detail::SameSource;
using detail::Scan;
using detail::ScanFiles;
using detail::StreamBatch;

// Like VisitChunks, but also yields skipped records so v3 line ordinals stay
// complete.
template <typename Consume>
static std::expected<void, nesso::parser::ParseError>
VisitRecords(const std::filesystem::path &path, Scan &scan, bool &stdin_used, std::string_view json_field,
             const nesso::parser::ArchiveLimits &limits, const Consume &consume)
{
    if (path == "-")
    {
        if (!stdin_used)
        {
            stdin_used = true;
            for (const nesso::parser::ParsedChunk &chunk : scan.stdin_chunks)
            {
                consume(chunk);
            }
        }
        return {};
    }
    return nesso::parser::Chunker::ForEachRecord(path, MAX_LINE_LENGTH, nullptr, json_field, consume, limits);
}

// `note` selects the index wording in `message`. `EmptyInput` stays exit 1.
static std::unexpected<Error> NothingToIndex(const IndexRequest &request, const Scan &scanned)
{
    const ErrorCause cause = NoKeptLines(scanned.counts) ? ErrorCause::EmptyFile : ErrorCause::NothingIndexable;
    const std::filesystem::path path = request.files.size() == 1 ? request.files.front() : std::filesystem::path{};
    Error error = MakeError(ErrorKind::EmptyInput, cause, path, scanned.counts, scanned.bundle_skips);
    error.note = "check the input path or --json-field";
    return std::unexpected(std::move(error));
}

namespace
{
struct IndexSourceCursor
{
    bool archive = false;
    std::uint32_t active_source = 0;
    bool store_lines = false;
};
} // namespace

namespace
{
struct IndexEmbedState
{
    const IndexRequest *request = nullptr;
    Scan *scanned = nullptr;
    const nesso::embed::Embedder *model = nullptr;
    nesso::core::CorpusWriter *writer = nullptr;
    nesso::core::DistinctIndex distinct;
    ProgressReporter *progress = nullptr;
    std::uint32_t reserved = 0;
    std::uint32_t vectors_written = 0;
    size_t lines_seen = 0;
    size_t lines_reported = 0;
    size_t batch = 0;
    std::optional<Error> failed;
};
} // namespace

static std::expected<void, Error> GuardStableSource(const std::filesystem::path &path, std::string_view json_field,
                                                    const nesso::core::SourceInfo &expected, const LineCounts &counts)
{
    if (path == "-")
    {
        return {};
    }
    const auto current = nesso::core::SourceInfoFor(path, json_field);
    if (!current || !SameSource(expected, *current))
    {
        return std::unexpected(MakeError(ErrorKind::Parse, ErrorCause::SourceChanged, path, counts));
    }
    return {};
}

static std::expected<void, Error> FlushIndexEmbeddings(IndexEmbedState &state)
{
    if (state.distinct.Pending().empty())
    {
        return {};
    }
    const LineCounts &counts = state.scanned->counts;
    const size_t lines_in_flush = state.lines_seen - state.lines_reported;
    auto embeddings = EmbedDistinct(
        state.distinct.Pending(), *state.model, *state.progress,
        {.lines_before = state.lines_reported, .lines_in_flush = lines_in_flush, .total = state.scanned->chunks},
        counts);
    if (!embeddings)
    {
        return std::unexpected(embeddings.error());
    }
    for (const std::vector<float> &embedding : *embeddings)
    {
        const auto added = state.writer->AddVector(state.vectors_written, embedding);
        if (!added)
        {
            return std::unexpected(
                MakeError(ErrorKind::CorpusWrite, CauseOf(added.error()), state.request->output, counts));
        }
        ++state.vectors_written;
    }
    state.distinct.ClearPending();
    state.lines_reported = state.lines_seen;
    state.progress->Update(state.lines_reported, state.scanned->chunks);
    return {};
}

static std::optional<Error> IngestIndexChunk(IndexEmbedState &state, IndexSourceCursor &cursor,
                                             const nesso::parser::ParsedChunk &chunk)
{
    const LineCounts &counts = state.scanned->counts;
    if (cursor.archive && chunk.entry_start)
    {
        nesso::core::SourceInfo entry{.path = chunk.source,
                                      .size = 0,
                                      .modified_time = 0,
                                      .tracked = false,
                                      .json_field = std::string{state.request->json_field},
                                      .stored_only = true};
        const auto added = state.writer->AddSource(std::move(entry));
        if (!added)
        {
            return MakeError(ErrorKind::CorpusWrite, CauseOf(added.error()), state.request->output, counts);
        }
        cursor.active_source = *added;
        cursor.store_lines = true;
    }
    std::uint32_t entry_id = nesso::core::NO_DISTINCT;
    if (!chunk.skipped)
    {
        entry_id = state.distinct.Add(chunk.text);
        if (entry_id == state.reserved)
        {
            const auto added = state.writer->ReserveDistinct(state.distinct.Text(entry_id));
            if (!added || *added != entry_id)
            {
                return MakeError(ErrorKind::CorpusWrite, added ? ErrorCause::CorruptFile : CauseOf(added.error()),
                                 state.request->output, counts);
            }
            ++state.reserved;
        }
        ++state.lines_seen;
    }
    nesso::core::CorpusLine line{.line_number = chunk.line_number,
                                 .byte_offset = chunk.byte_offset,
                                 .byte_length = chunk.byte_length,
                                 .source_index = cursor.active_source,
                                 .distinct_id = entry_id,
                                 .array_element = chunk.array_element};
    if (cursor.store_lines)
    {
        line.stored_text = chunk.text;
    }
    std::vector<std::string> terms = chunk.skipped ? std::vector<std::string>{} : nesso::core::LexicalTerms(chunk.text);
    if (const auto added = state.writer->AddLines(std::span{&line, 1}, std::span{&terms, 1}); !added)
    {
        return MakeError(ErrorKind::CorpusWrite, CauseOf(added.error()), state.request->output, counts);
    }
    if (state.distinct.Pending().size() == state.batch)
    {
        if (const auto flushed = FlushIndexEmbeddings(state); !flushed)
        {
            return flushed.error();
        }
    }
    return std::nullopt;
}

static std::expected<void, Error> EmbedIndexFiles(IndexEmbedState &state)
{
    const ScopedStage stage{"embed"};
    bool stdin_used = false;
    const LineCounts &counts = state.scanned->counts;
    for (size_t source_index = 0; source_index < state.request->files.size(); ++source_index)
    {
        const std::filesystem::path &path = state.request->files[source_index];
        const nesso::core::SourceInfo &source = state.scanned->sources[source_index];
        if (const auto stable = GuardStableSource(path, state.request->json_field, source, counts); !stable)
        {
            return std::unexpected(stable.error());
        }
        IndexSourceCursor cursor{
            .archive = nesso::parser::IsArchivePath(path), .active_source = 0, .store_lines = source.stored_only};
        if (!cursor.archive)
        {
            const auto source_id = state.writer->AddSource(source);
            if (!source_id)
            {
                return std::unexpected(
                    MakeError(ErrorKind::CorpusWrite, CauseOf(source_id.error()), state.request->output, counts));
            }
            cursor.active_source = *source_id;
        }
        const auto streamed =
            VisitRecords(path, *state.scanned, stdin_used, state.request->json_field, state.request->archive_limits,
                         [&](const nesso::parser::ParsedChunk &chunk)
                         {
                             if (state.failed)
                             {
                                 return;
                             }
                             if (auto error = IngestIndexChunk(state, cursor, chunk))
                             {
                                 state.failed = std::move(error);
                             }
                         });
        if (!streamed)
        {
            return std::unexpected(ParseFailure(streamed.error(), path, counts, state.request->archive_limits));
        }
        if (state.failed)
        {
            return std::unexpected(*state.failed);
        }
        if (const auto stable = GuardStableSource(path, state.request->json_field, source, counts); !stable)
        {
            return std::unexpected(stable.error());
        }
    }
    if (const auto flushed = FlushIndexEmbeddings(state); !flushed)
    {
        return std::unexpected(flushed.error());
    }
    return {};
}

std::expected<IndexSummary, Error> Nesso::Index(const IndexRequest &input) const
{
    const TraceSession session;
    auto expanded = ExpandRequestFiles(input.files);
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
            return std::unexpected(MakeError(ErrorKind::CorpusWrite, ErrorCause::OutputIsInput, request.output, {}));
        }
    }
    const auto model = Embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto scanned = ScanFiles(request.files, request.json_field, request.archive_limits, true);
    if (!scanned)
    {
        return std::unexpected(scanned.error());
    }

    auto policy = MakeCorpusPolicy(**model, scanned->counts);
    if (!policy)
    {
        return std::unexpected(policy.error());
    }
    // create() rejects an unusable output path; only finish() replaces an
    // existing corpus.
    auto writer = nesso::core::CorpusWriter::Create(request.output, std::move(*policy));
    if (!writer)
    {
        return std::unexpected(
            MakeError(ErrorKind::CorpusWrite, CauseOf(writer.error()), request.output, scanned->counts));
    }
    if (scanned->chunks == 0)
    {
        return NothingToIndex(request, *scanned);
    }

    ProgressReporter progress(request.progress);
    IndexEmbedState state;
    state.request = &request;
    state.scanned = &*scanned;
    state.model = *model;
    state.writer = &*writer;
    state.progress = &progress;
    state.batch = StreamBatch();
    state.distinct.Reserve(scanned->chunks);
    if (const auto embedded = EmbedIndexFiles(state); !embedded)
    {
        return std::unexpected(embedded.error());
    }
    progress.Finish();

    const LineCounts &counts = scanned->counts;
    const auto written = [&writer]
    {
        const ScopedStage stage{"corpus-write"};
        return writer->Finish();
    }();
    if (!written)
    {
        return std::unexpected(MakeError(ErrorKind::CorpusWrite, CauseOf(written.error()), request.output, counts));
    }
    return IndexSummary{.chunks = scanned->chunks,
                        .skipped_lines = counts.skipped,
                        .empty_lines = counts.empty_lines,
                        .malformed_lines = counts.malformed_lines,
                        .missing_field_lines = counts.missing_field_lines,
                        .truncated_lines = counts.truncated,
                        .binary_entries = 0,
                        .json_field = counts.json_field,
                        .bundle_skips = scanned->bundle_skips};
}

} // namespace nesso
