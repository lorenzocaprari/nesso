// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <expected>
#include <filesystem>
#include <functional>
#include <ios>
#include <parser/archive_source.hpp>
#include <parser/gzip_reader.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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

static auto Failure(ArchiveError code, std::string entry = {}) -> ArchiveFailure
{
    return ArchiveFailure{.code = code, .entry = std::move(entry)};
}

static auto FileLooksLikeOpenPgp(const std::filesystem::path &path) -> bool
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
    return LooksLikeOpenPgp(std::as_bytes(std::span(buffer).first(counted)));
}

static auto AfterReadFailure(ArchiveError code, const std::filesystem::path &path) -> ArchiveFailure
{
    if ((code == ArchiveError::Corrupt || code == ArchiveError::OpenFailure) && FileLooksLikeOpenPgp(path))
    {
        return Failure(ArchiveError::Encrypted);
    }
    return Failure(code);
}

static auto AfterOpenFailure(const std::filesystem::path &path) -> ArchiveFailure
{
    const std::ifstream probe(path, std::ios::binary);
    if (!probe)
    {
        return Failure(ArchiveError::OpenFailure);
    }
    if (FileLooksLikeOpenPgp(path))
    {
        return Failure(ArchiveError::Encrypted);
    }
    return Failure(ArchiveError::Corrupt);
}

static void CountSkippedEntry(archive_entry *entry, ArchiveSkipStats *skips)
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

auto LooksLikeOpenPgp(std::span<const std::byte> bytes) -> bool
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
    // New format: 11TTTTTT. Old format: 10TTTTLL (tag is four bits, so 18 is
    // new-format only).
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

static auto LowerExtension(const std::filesystem::path &path) -> std::string
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

// Archive order is the member order libarchive reports. Sorting by name would
// require holding every member, and a compressed stream cannot be rewound to
// one member. The order is a property of the file bytes, so a later pass and
// any thread count see the same sequence.
static auto MemberName(const char *raw) -> std::string
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

static auto ExceedsRatio(std::uint64_t uncompressed, std::uint64_t compressed, std::uint64_t ratio) -> bool
{
    if (ratio == 0 || compressed == 0)
    {
        return uncompressed > 0;
    }
    const std::uint64_t whole = uncompressed / ratio;
    return whole > compressed || (whole == compressed && uncompressed % ratio != 0);
}

static auto Fits(std::uint64_t used, std::uint64_t extra, std::uint64_t limit) -> bool
{
    return extra <= limit && used <= limit - extra;
}

static auto ReadMember(archive *handle, std::string &payload, std::uint64_t declared, const ArchiveLimits &limits,
                       std::uint64_t accepted, std::uint64_t compressed) -> std::expected<void, ArchiveError>
{
    if (declared > limits.max_entry_bytes)
    {
        return std::unexpected(ArchiveError::EntryTooLarge);
    }
    if (declared > 0 && !Fits(accepted, declared, limits.max_total_bytes))
    {
        return std::unexpected(ArchiveError::TotalTooLarge);
    }
    if (declared > 0 && ExceedsRatio(accepted + declared, compressed, limits.max_ratio))
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
        if (!Fits(payload.size(), bytes, limits.max_entry_bytes))
        {
            return std::unexpected(ArchiveError::EntryTooLarge);
        }
        if (!Fits(accepted + payload.size(), bytes, limits.max_total_bytes))
        {
            return std::unexpected(ArchiveError::TotalTooLarge);
        }
        if (ExceedsRatio(accepted + payload.size() + bytes, compressed, limits.max_ratio))
        {
            return std::unexpected(ArchiveError::RatioExceeded);
        }
        payload.append(buffer.data(), static_cast<std::size_t>(bytes));
    }
}

static auto SkipMember(archive *handle) -> bool
{
    const int status = archive_read_data_skip(handle);
    return status == ARCHIVE_OK || status == ARCHIVE_WARN || status == ARCHIVE_EOF;
}

auto IsArchivePath(const std::filesystem::path &path) -> bool
{
    const std::string extension = LowerExtension(path);
    if (extension == ".tar" || extension == ".tgz" || extension == ".zip")
    {
        return true;
    }
    if (extension == ".gz" || extension == ".xz")
    {
        return LowerExtension(path.stem()) == ".tar";
    }
    return false;
}

static auto ArchiveCompressedSize(const std::filesystem::path &path) -> std::expected<std::uint64_t, ArchiveFailure>
{
    std::error_code error;
    const auto compressed_size = std::filesystem::file_size(path, error);
    if (error)
    {
        return std::unexpected(Failure(ArchiveError::OpenFailure));
    }
    return static_cast<std::uint64_t>(compressed_size);
}

static auto OpenArchiveReader(const std::filesystem::path &path)
    -> std::expected<std::unique_ptr<archive, ArchiveFree>, ArchiveFailure>
{
    // open_filename only opens the archive path. Member names are never passed to
    // the filesystem.
    std::unique_ptr<archive, ArchiveFree> reader{archive_read_new()};
    if (!reader)
    {
        return std::unexpected(AfterReadFailure(ArchiveError::Corrupt, path));
    }
    archive *handle = reader.get();
    archive_read_support_filter_none(handle);
    archive_read_support_filter_gzip(handle);
    archive_read_support_filter_xz(handle);
    archive_read_support_format_tar(handle);
    archive_read_support_format_zip(handle);
    archive_read_support_format_empty(handle);
    // path::c_str() is wchar_t* on Windows; libarchive exposes a wide opener.
#ifdef _WIN32
    const int open_rc = archive_read_open_filename_w(handle, path.c_str(), 10240);
#else
    const int open_rc = archive_read_open_filename(handle, path.c_str(), 10240);
#endif
    if (open_rc != ARCHIVE_OK)
    {
        return std::unexpected(AfterOpenFailure(path));
    }
    return reader;
}

namespace
{
enum class ArchiveHeaderKind : std::uint8_t
{
    End,
    Retry,
    Ready
};
} // namespace

namespace
{
struct ArchiveHeaderStep
{
    ArchiveHeaderKind kind = ArchiveHeaderKind::End;
    archive_entry *entry = nullptr;
};
} // namespace

static auto AdvanceArchiveHeader(archive *handle, const std::filesystem::path &path)
    -> std::expected<ArchiveHeaderStep, ArchiveFailure>
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
        return std::unexpected(AfterReadFailure(ArchiveError::Corrupt, path));
    }
    return ArchiveHeaderStep{.kind = ArchiveHeaderKind::Ready, .entry = entry};
}

static auto IsRegularArchiveMember(archive_entry *entry) -> bool
{
    const auto type = archive_entry_filetype(entry);
    return type == AE_IFREG && archive_entry_hardlink(entry) == nullptr && archive_entry_symlink(entry) == nullptr;
}

static auto SkipNonRegularMember(archive *handle, archive_entry *entry, ArchiveSkipStats *skips,
                                 const std::filesystem::path &path) -> std::expected<void, ArchiveFailure>
{
    CountSkippedEntry(entry, skips);
    if (!SkipMember(handle))
    {
        return std::unexpected(AfterReadFailure(ArchiveError::Corrupt, path));
    }
    return {};
}

static auto RejectEncryptedHeader(archive_entry *entry, const std::string &name) -> std::expected<void, ArchiveFailure>
{
    if (archive_entry_is_encrypted(entry) != 0)
    {
        return std::unexpected(Failure(ArchiveError::Encrypted, name));
    }
    return {};
}

static auto RejectTooManyEntries(std::uint64_t entries, std::uint64_t max_entries)
    -> std::expected<void, ArchiveFailure>
{
    if (entries > max_entries)
    {
        return std::unexpected(Failure(ArchiveError::TooManyEntries));
    }
    return {};
}

static auto DeclaredMemberSize(archive_entry *entry) -> std::uint64_t
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

namespace
{
struct MemberLoadBudget
{
    std::uint64_t declared = 0;
    std::uint64_t accepted = 0;
    std::uint64_t compressed = 0;
};
} // namespace

namespace
{
struct MemberIdentity
{
    // NOLINTNEXTLINE(readability-redundant-member-init)
    std::string name{};
    std::filesystem::path path;
};
} // namespace

static auto LoadMemberPayload(archive *handle, std::string &payload, const MemberLoadBudget &budget,
                              const ArchiveLimits &limits, const MemberIdentity &member)
    -> std::expected<void, ArchiveFailure>
{
    const auto read_error = ReadMember(handle, payload, budget.declared, limits, budget.accepted, budget.compressed);
    if (read_error)
    {
        return {};
    }
    // When the format knows entries are encrypted, a data read failure is
    // encryption, not a generic corrupt archive (avoids dumping libarchive's
    // crypto message).
    if (read_error.error() == ArchiveError::Corrupt && archive_read_has_encrypted_entries(handle) > 0)
    {
        return std::unexpected(Failure(ArchiveError::Encrypted, member.name));
    }
    return std::unexpected(AfterReadFailure(read_error.error(), member.path));
}

static auto InflateNestedGzipMember(std::string &payload, const std::string &name, const ArchiveLimits &limits,
                                    const std::filesystem::path &path) -> std::expected<void, ArchiveFailure>
{
    if (!IsGzipPath(name))
    {
        return {};
    }
    auto inflated = ReadGzipBytes(payload);
    if (!inflated)
    {
        const ArchiveError code =
            inflated.error() == GzipError::OpenFailure ? ArchiveError::OpenFailure : ArchiveError::CorruptGzip;
        return std::unexpected(AfterReadFailure(code, path));
    }
    payload = std::move(*inflated);
    if (payload.size() > limits.max_entry_bytes)
    {
        return std::unexpected(Failure(ArchiveError::EntryTooLarge));
    }
    return {};
}

static auto AcceptMemberAgainstLimits(std::uint64_t accepted, std::uint64_t payload_size, std::uint64_t compressed,
                                      const ArchiveLimits &limits) -> std::expected<void, ArchiveFailure>
{
    if (!Fits(accepted, payload_size, limits.max_total_bytes))
    {
        return std::unexpected(Failure(ArchiveError::TotalTooLarge));
    }
    if (ExceedsRatio(accepted + payload_size, compressed, limits.max_ratio))
    {
        return std::unexpected(Failure(ArchiveError::RatioExceeded));
    }
    return {};
}

namespace
{
enum class ArchiveMemberAction : std::uint8_t
{
    Skip,
    Accept
};
} // namespace

namespace
{
struct ArchiveMemberSession
{
    ArchiveLimits limits;
    std::uint64_t compressed = 0;
    std::filesystem::path path;
};
} // namespace

namespace
{
struct ArchiveAccumulate
{
    std::uint64_t accepted = 0;
    std::uint64_t entries = 0;
    // NOLINTNEXTLINE(readability-redundant-member-init)
    std::string payload{};
};
} // namespace

// One regular member: encrypt/count/limits, load (and optional nested gzip),
// then accept against totals.
static auto ProcessArchiveMember(archive *handle, archive_entry *entry, ArchiveSkipStats *skips,
                                 const ArchiveMemberSession &session, ArchiveAccumulate &acc,
                                 const std::function<void(std::string_view name, std::string_view bytes)> &consume)
    -> std::expected<ArchiveMemberAction, ArchiveFailure>
{
    const bool regular = IsRegularArchiveMember(entry);
    const std::string name = regular ? MemberName(archive_entry_pathname(entry)) : std::string{};
    if (!regular || name.empty())
    {
        if (const auto skipped = SkipNonRegularMember(handle, entry, skips, session.path); !skipped)
        {
            return std::unexpected(skipped.error());
        }
        return ArchiveMemberAction::Skip;
    }

    if (const auto encrypted = RejectEncryptedHeader(entry, name); !encrypted)
    {
        return std::unexpected(encrypted.error());
    }

    ++acc.entries;
    if (const auto count = RejectTooManyEntries(acc.entries, session.limits.max_entries); !count)
    {
        return std::unexpected(count.error());
    }

    const MemberLoadBudget budget{
        .declared = DeclaredMemberSize(entry), .accepted = acc.accepted, .compressed = session.compressed};
    const MemberIdentity member{.name = name, .path = session.path};
    if (const auto loaded = LoadMemberPayload(handle, acc.payload, budget, session.limits, member); !loaded)
    {
        return std::unexpected(loaded.error());
    }
    if (const auto inflated = InflateNestedGzipMember(acc.payload, name, session.limits, session.path); !inflated)
    {
        return std::unexpected(inflated.error());
    }
    if (const auto accepted_limits =
            AcceptMemberAgainstLimits(acc.accepted, acc.payload.size(), session.compressed, session.limits);
        !accepted_limits)
    {
        return std::unexpected(accepted_limits.error());
    }
    acc.accepted += acc.payload.size();
    consume(name, acc.payload);
    return ArchiveMemberAction::Accept;
}

auto ReadArchive(const std::filesystem::path &path, const ArchiveLimits &limits,
                 const std::function<void(std::string_view name, std::string_view bytes)> &consume,
                 ArchiveSkipStats *skips) -> std::expected<void, ArchiveFailure>
{
    const auto compressed = ArchiveCompressedSize(path);
    if (!compressed)
    {
        return std::unexpected(compressed.error());
    }
    if (*compressed == 0)
    {
        return {};
    }

    auto reader = OpenArchiveReader(path);
    if (!reader)
    {
        return std::unexpected(reader.error());
    }
    archive *handle = reader->get();

    const ArchiveMemberSession session{.limits = limits, .compressed = *compressed, .path = path};
    ArchiveAccumulate acc;
    while (true)
    {
        const auto step = AdvanceArchiveHeader(handle, path);
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
        if (const auto processed = ProcessArchiveMember(handle, step->entry, skips, session, acc, consume); !processed)
        {
            return std::unexpected(processed.error());
        }
    }
}

} // namespace nesso::parser
