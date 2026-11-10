// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PARSER_ARCHIVE_SOURCE_HPP
#define PARSER_ARCHIVE_SOURCE_HPP

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string_view>

namespace nesso::parser
{

/// Caps for one archive read. The defaults are the production limits.
/// `maxRatio` is uncompressed payload bytes divided by the archive's size on disk.
inline constexpr std::uint64_t ARCHIVE_MAX_ENTRY_BYTES = 256ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t ARCHIVE_MAX_TOTAL_BYTES = 2ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t ARCHIVE_MAX_ENTRIES = 100000;
inline constexpr std::uint64_t ARCHIVE_MAX_RATIO = 1000;

struct ArchiveLimits
{
    std::uint64_t maxEntryBytes = ARCHIVE_MAX_ENTRY_BYTES;
    std::uint64_t maxTotalBytes = ARCHIVE_MAX_TOTAL_BYTES;
    std::uint64_t maxEntries = ARCHIVE_MAX_ENTRIES;
    std::uint64_t maxRatio = ARCHIVE_MAX_RATIO;
};

enum class ArchiveError : std::uint8_t
{
    OpenFailure,
    Corrupt,
    CorruptGzip,
    EntryTooLarge,
    TotalTooLarge,
    TooManyEntries,
    RatioExceeded
};

/// True for `.tar`, `.tgz`, `.tar.gz`, `.tar.xz`, and `.zip`, compared without regard to case.
[[nodiscard]] bool isArchivePath(const std::filesystem::path &path);

/// Read regular-file members of a tar, tar.gz, tgz, tar.xz, or zip archive.
/// Members are visited in archive order. That order is fixed by the file bytes, so it does not
/// depend on thread count. Nothing is written to disk: member names are labels only, including
/// absolute paths and names that contain `..`. A `.gz` member is inflated in memory before the
/// callback. Directories, symlinks, and hard links are skipped.
[[nodiscard]] std::expected<void, ArchiveError>
readArchive(const std::filesystem::path &path, const ArchiveLimits &limits,
            const std::function<void(std::string_view name, std::string_view bytes)> &consume);

} // namespace nesso::parser

#endif // PARSER_ARCHIVE_SOURCE_HPP
