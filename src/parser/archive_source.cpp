// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/archive_source.hpp>
#include <parser/gzip_reader.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
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

std::expected<void, ArchiveError>
readArchive(const std::filesystem::path &path, const ArchiveLimits &limits,
            const std::function<void(std::string_view name, std::string_view bytes)> &consume)
{
    std::error_code error;
    const auto compressedSize = std::filesystem::file_size(path, error);
    if (error)
    {
        return std::unexpected(ArchiveError::OpenFailure);
    }
    if (compressedSize == 0)
    {
        return {};
    }
    const auto compressed = static_cast<std::uint64_t>(compressedSize);

    // open_filename only opens the archive path. Member names are never passed to the filesystem.
    const std::unique_ptr<archive, ArchiveFree> reader{archive_read_new()};
    if (!reader)
    {
        return std::unexpected(ArchiveError::Corrupt);
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
        const std::ifstream probe(path, std::ios::binary);
        if (!probe)
        {
            return std::unexpected(ArchiveError::OpenFailure);
        }
        return std::unexpected(ArchiveError::Corrupt);
    }

    std::uint64_t accepted = 0;
    std::uint64_t entries = 0;
    std::string payload;
    while (true)
    {
        archive_entry *entry = nullptr;
        const int status = archive_read_next_header(handle, &entry);
        if (status == ARCHIVE_EOF)
        {
            return {};
        }
        if (status == ARCHIVE_RETRY)
        {
            continue;
        }
        if ((status != ARCHIVE_OK && status != ARCHIVE_WARN) || entry == nullptr)
        {
            return std::unexpected(ArchiveError::Corrupt);
        }

        const auto type = archive_entry_filetype(entry);
        const bool regular =
            type == AE_IFREG && archive_entry_hardlink(entry) == nullptr && archive_entry_symlink(entry) == nullptr;
        const std::string name = regular ? memberName(archive_entry_pathname(entry)) : std::string{};
        if (!regular || name.empty())
        {
            if (!skipMember(handle))
            {
                return std::unexpected(ArchiveError::Corrupt);
            }
            continue;
        }

        ++entries;
        if (entries > limits.maxEntries)
        {
            return std::unexpected(ArchiveError::TooManyEntries);
        }

        std::uint64_t declared = 0;
        if (archive_entry_size_is_set(entry) != 0)
        {
            const la_int64_t size = archive_entry_size(entry);
            if (size > 0)
            {
                declared = static_cast<std::uint64_t>(size);
            }
        }
        const auto readError = readMember(handle, payload, declared, limits, accepted, compressed);
        if (!readError)
        {
            return std::unexpected(readError.error());
        }

        if (isGzipPath(name))
        {
            auto inflated = readGzipBytes(payload);
            if (!inflated)
            {
                return std::unexpected(inflated.error() == GzipError::OpenFailure ? ArchiveError::OpenFailure
                                                                                  : ArchiveError::CorruptGzip);
            }
            payload = std::move(*inflated);
            if (payload.size() > limits.maxEntryBytes)
            {
                return std::unexpected(ArchiveError::EntryTooLarge);
            }
        }
        if (!fits(accepted, payload.size(), limits.maxTotalBytes))
        {
            return std::unexpected(ArchiveError::TotalTooLarge);
        }
        if (exceedsRatio(accepted + payload.size(), compressed, limits.maxRatio))
        {
            return std::unexpected(ArchiveError::RatioExceeded);
        }
        accepted += payload.size();
        consume(name, payload);
    }
}

} // namespace nesso::parser
