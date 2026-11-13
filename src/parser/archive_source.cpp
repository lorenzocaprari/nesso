// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/archive_source.hpp>
#include <parser/gzip_reader.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace nesso::parser
{
namespace
{

struct ArchiveFree
{
    void operator()(archive *handle) const noexcept
    {
        if (handle != nullptr)
        {
            archive_read_free(handle);
        }
    }
};

} // namespace

static constexpr std::string_view PGP_ARMOR_HEADER = "-----BEGIN PGP MESSAGE-----";

static ArchiveFailure failure(ArchiveError code, std::string entry = {})
{
    return ArchiveFailure{.code = code, .entry = std::move(entry)};
}

static bool fileLooksLikeOpenPgp(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return false;
    }
    std::array<char, 64> buffer{};
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto counted = static_cast<std::size_t>(input.gcount());
    if (counted == 0)
    {
        return false;
    }
    return looksLikeOpenPgp(std::as_bytes(std::span(buffer).first(counted)));
}

static ArchiveFailure afterReadFailure(ArchiveError code, const std::filesystem::path &path)
{
    if ((code == ArchiveError::Corrupt || code == ArchiveError::OpenFailure) && fileLooksLikeOpenPgp(path))
    {
        return failure(ArchiveError::Encrypted);
    }
    return failure(code);
}

static ArchiveFailure afterOpenFailure(const std::filesystem::path &path)
{
    const std::ifstream probe(path, std::ios::binary);
    if (!probe)
    {
        return failure(ArchiveError::OpenFailure);
    }
    if (fileLooksLikeOpenPgp(path))
    {
        return failure(ArchiveError::Encrypted);
    }
    return failure(ArchiveError::Corrupt);
}

static void countSkippedEntry(archive_entry *entry, ArchiveSkipStats *skips)
{
    if (skips == nullptr)
    {
        return;
    }
    if (archive_entry_symlink(entry) != nullptr)
    {
        ++skips->symlinks;
        return;
    }
    if (archive_entry_hardlink(entry) != nullptr)
    {
        ++skips->hardlinks;
        return;
    }
    if (archive_entry_filetype(entry) == AE_IFDIR)
    {
        ++skips->directories;
        return;
    }
    ++skips->other;
}

bool looksLikeOpenPgp(std::span<const std::byte> bytes)
{
    if (bytes.size() >= PGP_ARMOR_HEADER.size())
    {
        bool armor = true;
        for (std::size_t index = 0; index < PGP_ARMOR_HEADER.size(); ++index)
        {
            if (static_cast<char>(bytes[index]) != PGP_ARMOR_HEADER[index])
            {
                armor = false;
                break;
            }
        }
        if (armor)
        {
            return true;
        }
    }
    if (bytes.empty())
    {
        return false;
    }
    const auto first = static_cast<unsigned>(bytes[0]);
    // New format: 11TTTTTT. Old format: 10TTTTLL (tag is four bits, so 18 is new-format only).
    if ((first & 0xC0U) == 0xC0U)
    {
        const unsigned tag = first & 0x3FU;
        return tag == 1U || tag == 3U || tag == 18U;
    }
    if ((first & 0xC0U) == 0x80U)
    {
        const unsigned tag = (first >> 2U) & 0x0FU;
        return tag == 1U || tag == 3U;
    }
    return false;
}

static std::string lowerExtension(const std::filesystem::path &path)
{
    std::string extension = path.extension().string();
    for (char &character : extension)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return extension;
}

// Archive order is the member order libarchive reports. Sorting by name would require holding
// every member, and a compressed stream cannot be rewound to one member. The order is a property
// of the file bytes, so a later pass and any thread count see the same sequence.
static std::string memberName(const char *raw)
{
    if (raw == nullptr)
    {
        return {};
    }
    std::string name{raw};
    for (char &character : name)
    {
        if (character == '\\')
        {
            character = '/';
        }
    }
    while (name.starts_with("./"))
    {
        name.erase(0, 2);
    }
    while (!name.empty() && name.back() == '/')
    {
        name.pop_back();
    }
    return name;
}

static bool exceedsRatio(std::uint64_t uncompressed, std::uint64_t compressed, std::uint64_t ratio)
{
    if (ratio == 0 || compressed == 0)
    {
        return uncompressed > 0;
    }
    const std::uint64_t whole = uncompressed / ratio;
    return whole > compressed || (whole == compressed && uncompressed % ratio != 0);
}

static bool fits(std::uint64_t used, std::uint64_t extra, std::uint64_t limit)
{
    return extra <= limit && used <= limit - extra;
}

static std::expected<void, ArchiveError> readMember(archive *handle, std::string &payload, std::uint64_t declared,
                                                    const ArchiveLimits &limits, std::uint64_t accepted,
                                                    std::uint64_t compressed)
{
    if (declared > limits.maxEntryBytes)
    {
        return std::unexpected(ArchiveError::EntryTooLarge);
    }
    if (declared > 0 && !fits(accepted, declared, limits.maxTotalBytes))
    {
        return std::unexpected(ArchiveError::TotalTooLarge);
    }
    if (declared > 0 && exceedsRatio(accepted + declared, compressed, limits.maxRatio))
    {
        return std::unexpected(ArchiveError::RatioExceeded);
    }

    std::array<char, static_cast<std::size_t>(16) * 1024> buffer{};
    payload.clear();
    if (declared > 0 && declared <= payload.max_size())
    {
        payload.reserve(static_cast<std::size_t>(declared));
    }
    while (true)
    {
        const la_ssize_t count = archive_read_data(handle, buffer.data(), buffer.size());
        if (count == 0)
        {
            return {};
        }
        if (count < 0)
        {
            return std::unexpected(ArchiveError::Corrupt);
        }
        const auto bytes = static_cast<std::uint64_t>(count);
        if (!fits(payload.size(), bytes, limits.maxEntryBytes))
        {
            return std::unexpected(ArchiveError::EntryTooLarge);
        }
        if (!fits(accepted + payload.size(), bytes, limits.maxTotalBytes))
        {
            return std::unexpected(ArchiveError::TotalTooLarge);
        }
        if (exceedsRatio(accepted + payload.size() + bytes, compressed, limits.maxRatio))
        {
            return std::unexpected(ArchiveError::RatioExceeded);
        }
        payload.append(buffer.data(), static_cast<std::size_t>(bytes));
    }
}

static bool skipMember(archive *handle)
{
    const int status = archive_read_data_skip(handle);
    return status == ARCHIVE_OK || status == ARCHIVE_WARN || status == ARCHIVE_EOF;
}

bool isArchivePath(const std::filesystem::path &path)
{
    const std::string extension = lowerExtension(path);
    if (extension == ".tar" || extension == ".tgz" || extension == ".zip")
    {
        return true;
    }
    if (extension == ".gz" || extension == ".xz")
    {
        return lowerExtension(path.stem()) == ".tar";
    }
    return false;
}

static std::expected<std::uint64_t, ArchiveFailure> archiveCompressedSize(const std::filesystem::path &path)
{
    std::error_code error;
    const auto compressedSize = std::filesystem::file_size(path, error);
    if (error)
    {
        return std::unexpected(failure(ArchiveError::OpenFailure));
    }
    return static_cast<std::uint64_t>(compressedSize);
}

static std::expected<std::unique_ptr<archive, ArchiveFree>, ArchiveFailure>
openArchiveReader(const std::filesystem::path &path)
{
    // open_filename only opens the archive path. Member names are never passed to the filesystem.
    std::unique_ptr<archive, ArchiveFree> reader{archive_read_new()};
    if (!reader)
    {
        return std::unexpected(afterReadFailure(ArchiveError::Corrupt, path));
    }
    archive *handle = reader.get();
    archive_read_support_filter_none(handle);
    archive_read_support_filter_gzip(handle);
    archive_read_support_filter_xz(handle);
    archive_read_support_format_tar(handle);
    archive_read_support_format_zip(handle);
    archive_read_support_format_empty(handle);
    if (archive_read_open_filename(handle, path.c_str(), 10240) != ARCHIVE_OK)
    {
        return std::unexpected(afterOpenFailure(path));
    }
    return reader;
}

enum class ArchiveHeaderKind : std::uint8_t
{
    End,
    Retry,
    Ready
};

struct ArchiveHeaderStep
{
    ArchiveHeaderKind kind = ArchiveHeaderKind::End;
    archive_entry *entry = nullptr;
};

static std::expected<ArchiveHeaderStep, ArchiveFailure> advanceArchiveHeader(archive *handle,
                                                                             const std::filesystem::path &path)
{
    archive_entry *entry = nullptr;
    const int status = archive_read_next_header(handle, &entry);
    if (status == ARCHIVE_EOF)
    {
        return ArchiveHeaderStep{.kind = ArchiveHeaderKind::End};
    }
    if (status == ARCHIVE_RETRY)
    {
        return ArchiveHeaderStep{.kind = ArchiveHeaderKind::Retry};
    }
    if ((status != ARCHIVE_OK && status != ARCHIVE_WARN) || entry == nullptr)
    {
        return std::unexpected(afterReadFailure(ArchiveError::Corrupt, path));
    }
    return ArchiveHeaderStep{.kind = ArchiveHeaderKind::Ready, .entry = entry};
}

static bool isRegularArchiveMember(archive_entry *entry)
{
    const auto type = archive_entry_filetype(entry);
    return type == AE_IFREG && archive_entry_hardlink(entry) == nullptr && archive_entry_symlink(entry) == nullptr;
}

static std::expected<void, ArchiveFailure> skipNonRegularMember(archive *handle, archive_entry *entry,
                                                                ArchiveSkipStats *skips,
                                                                const std::filesystem::path &path)
{
    countSkippedEntry(entry, skips);
    if (!skipMember(handle))
    {
        return std::unexpected(afterReadFailure(ArchiveError::Corrupt, path));
    }
    return {};
}

static std::expected<void, ArchiveFailure> rejectEncryptedHeader(archive_entry *entry, const std::string &name)
{
    if (archive_entry_is_encrypted(entry) != 0)
    {
        return std::unexpected(failure(ArchiveError::Encrypted, name));
    }
    return {};
}

static std::expected<void, ArchiveFailure> rejectTooManyEntries(std::uint64_t entries, std::uint64_t maxEntries)
{
    if (entries > maxEntries)
    {
        return std::unexpected(failure(ArchiveError::TooManyEntries));
    }
    return {};
}

static std::uint64_t declaredMemberSize(archive_entry *entry)
{
    if (archive_entry_size_is_set(entry) == 0)
    {
        return 0;
    }
    const la_int64_t size = archive_entry_size(entry);
    if (size <= 0)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(size);
}

static std::expected<void, ArchiveFailure> loadMemberPayload(archive *handle, std::string &payload,
                                                             std::uint64_t declared, const ArchiveLimits &limits,
                                                             std::uint64_t accepted, std::uint64_t compressed,
                                                             const std::string &name, const std::filesystem::path &path)
{
    const auto readError = readMember(handle, payload, declared, limits, accepted, compressed);
    if (readError)
    {
        return {};
    }
    // When the format knows entries are encrypted, a data read failure is encryption,
    // not a generic corrupt archive (avoids dumping libarchive's crypto message).
    if (readError.error() == ArchiveError::Corrupt && archive_read_has_encrypted_entries(handle) > 0)
    {
        return std::unexpected(failure(ArchiveError::Encrypted, name));
    }
    return std::unexpected(afterReadFailure(readError.error(), path));
}

static std::expected<void, ArchiveFailure> inflateNestedGzipMember(std::string &payload, const std::string &name,
                                                                   const ArchiveLimits &limits,
                                                                   const std::filesystem::path &path)
{
    if (!isGzipPath(name))
    {
        return {};
    }
    auto inflated = readGzipBytes(payload);
    if (!inflated)
    {
        const ArchiveError code =
            inflated.error() == GzipError::OpenFailure ? ArchiveError::OpenFailure : ArchiveError::CorruptGzip;
        return std::unexpected(afterReadFailure(code, path));
    }
    payload = std::move(*inflated);
    if (payload.size() > limits.maxEntryBytes)
    {
        return std::unexpected(failure(ArchiveError::EntryTooLarge));
    }
    return {};
}

static std::expected<void, ArchiveFailure> acceptMemberAgainstLimits(std::uint64_t accepted, std::uint64_t payloadSize,
                                                                     std::uint64_t compressed,
                                                                     const ArchiveLimits &limits)
{
    if (!fits(accepted, payloadSize, limits.maxTotalBytes))
    {
        return std::unexpected(failure(ArchiveError::TotalTooLarge));
    }
    if (exceedsRatio(accepted + payloadSize, compressed, limits.maxRatio))
    {
        return std::unexpected(failure(ArchiveError::RatioExceeded));
    }
    return {};
}

enum class ArchiveMemberAction : std::uint8_t
{
    Skip,
    Accept
};

// One regular member: encrypt/count/limits, load (and optional nested gzip), then accept against totals.
static std::expected<ArchiveMemberAction, ArchiveFailure>
processArchiveMember(archive *handle, archive_entry *entry, ArchiveSkipStats *skips, const ArchiveLimits &limits,
                     std::uint64_t &accepted, std::uint64_t &entries, std::uint64_t compressed, std::string &payload,
                     const std::filesystem::path &path,
                     const std::function<void(std::string_view name, std::string_view bytes)> &consume)
{
    const bool regular = isRegularArchiveMember(entry);
    const std::string name = regular ? memberName(archive_entry_pathname(entry)) : std::string{};
    if (!regular || name.empty())
    {
        if (const auto skipped = skipNonRegularMember(handle, entry, skips, path); !skipped)
        {
            return std::unexpected(skipped.error());
        }
        return ArchiveMemberAction::Skip;
    }

    if (const auto encrypted = rejectEncryptedHeader(entry, name); !encrypted)
    {
        return std::unexpected(encrypted.error());
    }

    ++entries;
    if (const auto count = rejectTooManyEntries(entries, limits.maxEntries); !count)
    {
        return std::unexpected(count.error());
    }

    if (const auto loaded =
            loadMemberPayload(handle, payload, declaredMemberSize(entry), limits, accepted, compressed, name, path);
        !loaded)
    {
        return std::unexpected(loaded.error());
    }
    if (const auto inflated = inflateNestedGzipMember(payload, name, limits, path); !inflated)
    {
        return std::unexpected(inflated.error());
    }
    if (const auto acceptedLimits = acceptMemberAgainstLimits(accepted, payload.size(), compressed, limits);
        !acceptedLimits)
    {
        return std::unexpected(acceptedLimits.error());
    }
    accepted += payload.size();
    consume(name, payload);
    return ArchiveMemberAction::Accept;
}

std::expected<void, ArchiveFailure>
readArchive(const std::filesystem::path &path, const ArchiveLimits &limits,
            const std::function<void(std::string_view name, std::string_view bytes)> &consume, ArchiveSkipStats *skips)
{
    const auto compressed = archiveCompressedSize(path);
    if (!compressed)
    {
        return std::unexpected(compressed.error());
    }
    if (*compressed == 0)
    {
        return {};
    }

    auto reader = openArchiveReader(path);
    if (!reader)
    {
        return std::unexpected(reader.error());
    }
    archive *handle = reader->get();

    std::uint64_t accepted = 0;
    std::uint64_t entries = 0;
    std::string payload;
    while (true)
    {
        const auto step = advanceArchiveHeader(handle, path);
        if (!step)
        {
            return std::unexpected(step.error());
        }
        if (step->kind == ArchiveHeaderKind::End)
        {
            return {};
        }
        if (step->kind == ArchiveHeaderKind::Retry)
        {
            continue;
        }
        if (const auto processed = processArchiveMember(handle, step->entry, skips, limits, accepted, entries,
                                                        *compressed, payload, path, consume);
            !processed)
        {
            return std::unexpected(processed.error());
        }
    }
}

} // namespace nesso::parser
