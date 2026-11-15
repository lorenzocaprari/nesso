// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/nesso.hpp>

#include <nesso/error_map.hpp>
#include <nesso/trace.hpp>
#include <nesso_detail.hpp>

#include <core/corpus_index.hpp>
#include <parser/archive_source.hpp>
#include <parser/input_walk.hpp>
#include <parser/log_chunker.hpp>

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nesso
{

using detail::LineCounts;
using detail::MakeError;
using detail::MAX_LINE_LENGTH;
using detail::ParseFailure;
using detail::SameSource;
using detail::Scan;

static LineCounts MakeLineCounts(const nesso::parser::ParseStats &stats, std::string json_field)
{
    return {.skipped = stats.skipped_lines,
            .empty_lines = stats.empty_lines,
            .malformed_lines = stats.malformed_lines,
            .missing_field_lines = stats.missing_field_lines,
            .truncated = stats.truncated_lines,
            .binary_entries = stats.binary_entries,
            .json_field = std::move(json_field)};
}

static void RecordBundleSkips(Scan &scan, const std::filesystem::path &path, const nesso::parser::ParseStats &before,
                              const nesso::parser::ParseStats &after)
{
    if (!nesso::parser::IsArchivePath(path))
    {
        return;
    }
    BundleSkipSummary summary{.path = path.string(),
                              .directories = after.archive_directories - before.archive_directories,
                              .symlinks = after.archive_symlinks - before.archive_symlinks,
                              .hardlinks = after.archive_hardlinks - before.archive_hardlinks,
                              .other = after.archive_other_skipped - before.archive_other_skipped,
                              .binary = after.binary_entries - before.binary_entries};
    if (BundleSkipTotal(summary) > 0)
    {
        scan.bundle_skips.push_back(std::move(summary));
    }
}

static std::expected<void, Error> ScanStdin(Scan &scan, nesso::parser::ParseStats &stats, std::string_view json_field,
                                            const nesso::parser::ArchiveLimits &limits, bool for_index)
{
    const std::filesystem::path path{"-"};
    const auto keep = [&scan](nesso::parser::ParsedChunk chunk)
    {
        if (!chunk.skipped)
        {
            ++scan.chunks;
        }
        scan.stdin_chunks.push_back(std::move(chunk));
    };
    const auto streamed =
        for_index ? nesso::parser::Chunker::ForEachRecord(path, MAX_LINE_LENGTH, &stats, json_field, keep, limits)
                  : nesso::parser::Chunker::ForEach(path, MAX_LINE_LENGTH, &stats, json_field, keep, limits);
    if (!streamed)
    {
        return std::unexpected(ParseFailure(streamed.error(), path, MakeLineCounts(stats, std::string{json_field}),
                                            limits, stats.encrypted_entry, scan.bundle_skips));
    }
    if (for_index)
    {
        const auto after = nesso::core::SourceInfoFor(path, json_field);
        if (!after || !SameSource(scan.sources.back(), *after))
        {
            return std::unexpected(MakeError(ErrorKind::Parse, ErrorCause::SourceChanged, path,
                                             MakeLineCounts(stats, std::string{json_field}), scan.bundle_skips));
        }
    }
    return {};
}

namespace detail
{

bool SameSource(const nesso::core::SourceInfo &left, const nesso::core::SourceInfo &right)
{
    return left.tracked == right.tracked && left.size == right.size && left.modified_time == right.modified_time;
}

std::expected<Scan, Error> ScanFiles(std::span<const std::filesystem::path> files, std::string_view json_field,
                                     const nesso::parser::ArchiveLimits &limits, bool for_index)
{
    const ScopedStage stage{"parse"};
    nesso::parser::ParseStats stats{};
    Scan scan{};
    bool captured_stdin = false;
    for (const std::filesystem::path &path : files)
    {
        if (for_index)
        {
            auto source = nesso::core::SourceInfoFor(path, json_field);
            if (!source)
            {
                return std::unexpected(MakeError(ErrorKind::Parse, CauseOf(source.error()), path,
                                                 MakeLineCounts(stats, std::string{json_field})));
            }
            scan.sources.push_back(std::move(*source));
        }
        if (path == "-")
        {
            if (captured_stdin)
            {
                continue;
            }
            captured_stdin = true;
            if (const auto scanned = ScanStdin(scan, stats, json_field, limits, for_index); !scanned)
            {
                return std::unexpected(scanned.error());
            }
            continue;
        }

        const nesso::parser::ParseStats before = stats;
        const auto streamed = nesso::parser::Chunker::ForEach(
            path, MAX_LINE_LENGTH, &stats, json_field, [&scan](const nesso::parser::ParsedChunk &) { ++scan.chunks; },
            limits);
        if (!streamed)
        {
            RecordBundleSkips(scan, path, before, stats);
            return std::unexpected(ParseFailure(streamed.error(), path, MakeLineCounts(stats, std::string{json_field}),
                                                limits, stats.encrypted_entry, scan.bundle_skips));
        }
        RecordBundleSkips(scan, path, before, stats);
    }
    scan.counts = MakeLineCounts(stats, std::string{json_field});
    return scan;
}

std::expected<std::vector<std::filesystem::path>, Error>
ExpandRequestFiles(std::span<const std::filesystem::path> files)
{
    auto expanded = nesso::parser::ExpandInputs(files);
    if (!expanded)
    {
        return std::unexpected(MakeError(ErrorKind::Parse, ErrorCause::FileOpen, expanded.error().path, {}));
    }
    return std::move(*expanded);
}

} // namespace detail

} // namespace nesso
