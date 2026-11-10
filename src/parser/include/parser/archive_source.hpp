// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PARSER_ARCHIVE_SOURCE_HPP
#define PARSER_ARCHIVE_SOURCE_HPP

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
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
    RatioExceeded,
    Encrypted
};

/// Failure from `readArchive`. `entry` is set for an encrypted zip member; empty for whole-file OpenPGP.
struct ArchiveFailure
{
    ArchiveError code = ArchiveError::Corrupt;
    // NOLINTNEXTLINE(readability-redundant-member-init)
    std::string entry{};
};

/// Counts of members skipped without reading as text (directories, links, other non-regular).
struct ArchiveSkipStats
{
    size_t directories = 0;
    size_t symlinks = 0;
    size_t hardlinks = 0;
    size_t other = 0;
};

/// True when `bytes` are an armored PGP message or the first packet is PKESK, SKESK, or SEIPD.
[[nodiscard]] bool looksLikeOpenPgp(std::span<const std::byte> bytes);

/// True for `.tar`, `.tgz`, `.tar.gz`, `.tar.xz`, and `.zip`, compared without regard to case.
[[nodiscard]] bool isArchivePath(const std::filesystem::path &path);

/// Read regular-file members of a tar, tar.gz, tgz, tar.xz, or zip archive.
/// Members are visited in archive order. That order is fixed by the file bytes, so it does not
/// depend on thread count. Nothing is written to disk: member names are labels only, including
/// absolute paths and names that contain `..`. A `.gz` member is inflated in memory before the
/// callback. Directories, symlinks, and hard links are skipped. When `skips` is set, those skips
/// are counted. An encrypted zip member or a whole-file OpenPGP payload (checked only after open
/// or read fails) returns `ArchiveError::Encrypted`.
[[nodiscard]] std::expected<void, ArchiveFailure>
readArchive(const std::filesystem::path &path, const ArchiveLimits &limits,
            const std::function<void(std::string_view name, std::string_view bytes)> &consume,
            ArchiveSkipStats *skips = nullptr);

} // namespace nesso::parser

#endif // PARSER_ARCHIVE_SOURCE_HPP
