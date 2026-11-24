// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM_FILE_HPP
#define PLATFORM_FILE_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

namespace nesso::platform
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
    auto operator=(const MappedFile &) -> MappedFile & = delete;
    MappedFile(MappedFile &&other) noexcept;
    auto operator=(MappedFile &&other) noexcept -> MappedFile &;

    [[nodiscard]] static auto Open(const std::filesystem::path &path) -> std::expected<MappedFile, FileError>;

    [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>;

  private:
    std::byte *data_ = nullptr;
    std::size_t size_ = 0;
};

/// Replaces `destination` with `source`. `source` is removed on success.
[[nodiscard]] auto ReplaceFileAtomically(const std::filesystem::path &source, const std::filesystem::path &destination)
    -> std::expected<void, FileError>;

/// Peak resident set size in bytes, from getrusage. Zero when the query fails.
[[nodiscard]] auto PeakResidentBytes() noexcept -> std::int64_t;

} // namespace nesso::platform

#endif // PLATFORM_FILE_HPP
