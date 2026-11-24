// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/nesso.hpp>
#include <nesso/overview.hpp>

#include <nesso_detail.hpp>

#include <core/event_types.hpp>
#include <parser/archive_source.hpp>
#include <parser/log_chunker.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace nesso
{

using detail::LineCounts;
using detail::MakeError;
using detail::MakeLineCounts;
using detail::MAX_LINE_LENGTH;
using detail::NoKeptLines;
using detail::ParseFailure;
using detail::RecordBundleSkips;
using detail::Scan;

static auto TooManyEventTypes(const std::filesystem::path &path, LineCounts counts,
                              std::vector<BundleSkipSummary> bundle_skips, size_t max_types) -> Error
{
    Error error =
        MakeError(ErrorKind::Parse, ErrorCause::TooManyEventTypes, path, std::move(counts), std::move(bundle_skips));
    error.note = std::format("more than {} distinct event types", max_types);
    return error;
}

static auto MakeResults(const nesso::core::EventTypeTally &tally, const OverviewRequest &request, LineCounts counts,
                        std::vector<BundleSkipSummary> bundle_skips) -> OverviewResults
{
    const auto types = tally.Types();
    OverviewResults results{.rows = {},
                            .lines = tally.Lines(),
                            .types = types.size(),
                            .skipped_lines = counts.skipped,
                            .empty_lines = counts.empty_lines,
                            .malformed_lines = counts.malformed_lines,
                            .missing_field_lines = counts.missing_field_lines,
                            .truncated_lines = counts.truncated,
                            .binary_entries = counts.binary_entries,
                            .json_field = std::move(counts.json_field),
                            .bundle_skips = std::move(bundle_skips)};
    const std::vector<size_t> ranked = nesso::core::RankEventTypes(types, request.order, request.top_k);
    results.rows.reserve(ranked.size());
    for (const size_t index : ranked)
    {
        const nesso::core::EventTypeStats &stats = types[index];
        results.rows.push_back(EventTypeRow{.count = stats.count,
                                            .source = stats.source,
                                            .line = stats.line,
                                            .array_element = stats.array_element,
                                            .text = stats.text,
                                            .key = stats.key,
                                            .first_seen = stats.first_seen,
                                            .last_seen = stats.last_seen});
    }
    return results;
}

auto Overview(const OverviewRequest &request) -> std::expected<OverviewResults, Error>
{
    auto expanded = detail::ExpandRequestFiles(request.files);
    if (!expanded)
    {
        return std::unexpected(expanded.error());
    }
    const std::vector<std::filesystem::path> &files = *expanded;
    nesso::parser::ParseStats stats{};
    // Only the bundle skip summaries are used.
    Scan scan{};
    nesso::core::EventTypeTally tally{request.max_types};
    bool stdin_used = false;
    for (const std::filesystem::path &path : files)
    {
        if (path == "-")
        {
            if (stdin_used)
            {
                continue;
            }
            stdin_used = true;
        }
        const bool archive = nesso::parser::IsArchivePath(path);
        const std::string label = path.string();
        bool full = false;
        const nesso::parser::ParseStats before = stats;
        const auto streamed = nesso::parser::Chunker::ForEach(
            path, MAX_LINE_LENGTH, &stats, request.json_field,
            [&](const nesso::parser::ParsedChunk &chunk) -> void
            {
                if (full)
                {
                    return;
                }
                full = !tally.Add({.source = archive ? chunk.source : label,
                                   .line = chunk.line_number,
                                   .array_element = chunk.array_element,
                                   .text = chunk.text});
            },
            request.archive_limits);
        RecordBundleSkips(scan, path, before, stats);
        if (!streamed)
        {
            return std::unexpected(ParseFailure(streamed.error(), path,
                                                MakeLineCounts(stats, std::string{request.json_field}),
                                                request.archive_limits, stats.encrypted_entry, scan.bundle_skips));
        }
        if (full)
        {
            return std::unexpected(TooManyEventTypes(path, MakeLineCounts(stats, std::string{request.json_field}),
                                                     scan.bundle_skips, request.max_types));
        }
    }

    LineCounts counts = MakeLineCounts(stats, std::string{request.json_field});
    if (tally.Lines() == 0)
    {
        const ErrorCause cause = NoKeptLines(counts) ? ErrorCause::EmptyFile : ErrorCause::NothingIndexable;
        const std::filesystem::path path = files.size() == 1 ? files.front() : std::filesystem::path{};
        return std::unexpected(MakeError(ErrorKind::EmptyInput, cause, path, std::move(counts), scan.bundle_skips));
    }
    return MakeResults(tally, request, std::move(counts), std::move(scan.bundle_skips));
}

} // namespace nesso
