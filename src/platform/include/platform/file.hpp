// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef PLATFORM_FILE_HPP
#define PLATFORM_FILE_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

namespace platform
{

enum class FileError : uint8_t
{
    OpenFailure,
    MapFailure,
    ReplaceFailure
};

/// Read-only mapping. The base is page-aligned, so a 64-byte-aligned offset
/// stays aligned for the distance kernels.
class MappedFile
{
  public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;
    MappedFile(MappedFile &&other) noexcept;
    MappedFile &operator=(MappedFile &&other) noexcept;

    [[nodiscard]] static std::expected<MappedFile, FileError> open(const std::filesystem::path &path);

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

  private:
    std::byte *data_ = nullptr;
    std::size_t size_ = 0;
};

/// Replaces `destination` with `source`. `source` is removed on success.
[[nodiscard]] std::expected<void, FileError> replaceFileAtomically(const std::filesystem::path &source,
                                                                   const std::filesystem::path &destination);

/// Peak resident set size in bytes, from getrusage. Zero when the query fails.
[[nodiscard]] std::int64_t peakResidentBytes() noexcept;

} // namespace platform

#endif // PLATFORM_FILE_HPP
