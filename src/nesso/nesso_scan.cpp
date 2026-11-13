// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/nesso.hpp>

#include <nesso/error_map.hpp>
#include <nesso_detail.hpp>
#include <nesso/trace.hpp>

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
using detail::makeError;
using detail::MAX_LINE_LENGTH;
using detail::parseFailure;
using detail::sameSource;
using detail::Scan;

// NOLINTNEXTLINE(readability-identifier-naming)
static LineCounts MakeLineCounts(const nesso::parser::ParseStats &stats, std::string jsonField)
{
    return {.skipped = stats.skippedLines,
            .emptyLines = stats.emptyLines,
            .malformedLines = stats.malformedLines,
            .missingFieldLines = stats.missingFieldLines,
            .truncated = stats.truncatedLines,
            .binaryEntries = stats.binaryEntries,
            .jsonField = std::move(jsonField)};
}

static void recordBundleSkips(Scan &scan, const std::filesystem::path &path, const nesso::parser::ParseStats &before,
                              const nesso::parser::ParseStats &after)
{
    if (!nesso::parser::isArchivePath(path))
    {
        return;
    }
    BundleSkipSummary summary{.path = path.string(),
                              .directories = after.archiveDirectories - before.archiveDirectories,
                              .symlinks = after.archiveSymlinks - before.archiveSymlinks,
                              .hardlinks = after.archiveHardlinks - before.archiveHardlinks,
                              .other = after.archiveOtherSkipped - before.archiveOtherSkipped,
                              .binary = after.binaryEntries - before.binaryEntries};
    if (bundleSkipTotal(summary) > 0)
    {
        scan.bundleSkips.push_back(std::move(summary));
    }
}

static std::expected<void, Error> scanStdin(Scan &scan, nesso::parser::ParseStats &stats, std::string_view jsonField,
                                            const nesso::parser::ArchiveLimits &limits, bool forIndex)
{
    const std::filesystem::path path{"-"};
    const auto keep = [&scan](nesso::parser::ParsedChunk chunk)
    {
        if (!chunk.skipped)
        {
            ++scan.chunks;
        }
        scan.stdinChunks.push_back(std::move(chunk));
    };
    const auto streamed =
        forIndex ? nesso::parser::Chunker::forEachRecord(path, MAX_LINE_LENGTH, &stats, jsonField, keep, limits)
                 : nesso::parser::Chunker::forEach(path, MAX_LINE_LENGTH, &stats, jsonField, keep, limits);
    if (!streamed)
    {
        return std::unexpected(parseFailure(streamed.error(), path, MakeLineCounts(stats, std::string{jsonField}),
                                            limits, stats.encryptedEntry, scan.bundleSkips));
    }
    if (forIndex)
    {
        const auto after = nesso::core::sourceInfoFor(path, jsonField);
        if (!after || !sameSource(scan.sources.back(), *after))
        {
            return std::unexpected(makeError(ErrorKind::Parse, ErrorCause::SourceChanged, path,
                                             MakeLineCounts(stats, std::string{jsonField}), scan.bundleSkips));
        }
    }
    return {};
}

namespace detail
{

bool sameSource(const nesso::core::SourceInfo &left, const nesso::core::SourceInfo &right)
{
    return left.tracked == right.tracked && left.size == right.size && left.modifiedTime == right.modifiedTime;
}

std::expected<Scan, Error> scanFiles(std::span<const std::filesystem::path> files, std::string_view jsonField,
                                     const nesso::parser::ArchiveLimits &limits, bool forIndex)
{
    const ScopedStage stage{"parse"};
    nesso::parser::ParseStats stats{};
    Scan scan{};
    bool capturedStdin = false;
    for (const std::filesystem::path &path : files)
    {
        if (forIndex)
        {
            auto source = nesso::core::sourceInfoFor(path, jsonField);
            if (!source)
            {
                return std::unexpected(makeError(ErrorKind::Parse, causeOf(source.error()), path,
                                                 MakeLineCounts(stats, std::string{jsonField})));
            }
            scan.sources.push_back(std::move(*source));
        }
        if (path == "-")
        {
            if (capturedStdin)
            {
                continue;
            }
            capturedStdin = true;
            if (const auto scanned = scanStdin(scan, stats, jsonField, limits, forIndex); !scanned)
            {
                return std::unexpected(scanned.error());
            }
            continue;
        }

        const nesso::parser::ParseStats before = stats;
        const auto streamed = nesso::parser::Chunker::forEach(
            path, MAX_LINE_LENGTH, &stats, jsonField, [&scan](nesso::parser::ParsedChunk) { ++scan.chunks; }, limits);
        if (!streamed)
        {
            recordBundleSkips(scan, path, before, stats);
            return std::unexpected(parseFailure(streamed.error(), path, MakeLineCounts(stats, std::string{jsonField}),
                                                limits, stats.encryptedEntry, scan.bundleSkips));
        }
        recordBundleSkips(scan, path, before, stats);
    }
    scan.counts = MakeLineCounts(stats, std::string{jsonField});
    return scan;
}

std::expected<std::vector<std::filesystem::path>, Error>
expandRequestFiles(std::span<const std::filesystem::path> files)
{
    auto expanded = nesso::parser::expandInputs(files);
    if (!expanded)
    {
        return std::unexpected(makeError(ErrorKind::Parse, ErrorCause::FileOpen, expanded.error().path, {}));
    }
    return std::move(*expanded);
}

} // namespace detail

} // namespace nesso
