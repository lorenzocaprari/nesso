// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "platform/file.hpp"

#include <array>
#include <cstring>
#include <new>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <fcntl.h>

namespace platform
{

MappedFile::~MappedFile()
{
    if (data_ != nullptr)
    {
        munmap(data_, size_);
    }
}

MappedFile::MappedFile(MappedFile &&other) noexcept : data_(other.data_), size_(other.size_)
{
    other.data_ = nullptr;
    other.size_ = 0;
}

MappedFile &MappedFile::operator=(MappedFile &&other) noexcept
{
    if (this != &other)
    {
        if (data_ != nullptr)
        {
            munmap(data_, size_);
        }
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

std::expected<MappedFile, FileError> MappedFile::open(const std::filesystem::path &path)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        return std::unexpected(FileError::OpenFailure);
    }
    const off_t length = ::lseek(fd, 0, SEEK_END);
    if (length < 0)
    {
        ::close(fd);
        return std::unexpected(FileError::MapFailure);
    }
    MappedFile mapped;
    mapped.size_ = static_cast<std::size_t>(length);
    if (mapped.size_ == 0)
    {
        ::close(fd);
        return mapped;
    }
    void *const view = ::mmap(nullptr, mapped.size_, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (view == MAP_FAILED)
    {
        return std::unexpected(FileError::MapFailure);
    }
    mapped.data_ = static_cast<std::byte *>(view);
    return mapped;
}

std::span<const std::byte> MappedFile::bytes() const noexcept { return {data_, size_}; }

std::expected<void, FileError> replaceFileAtomically(const std::filesystem::path &source,
                                                     const std::filesystem::path &destination)
{
    if (::rename(source.c_str(), destination.c_str()) != 0)
    {
        return std::unexpected(FileError::ReplaceFailure);
    }
    return {};
}

std::int64_t peakResidentBytes() noexcept
{
    // glibc stores ru_maxrss in an anonymous union immediately after the two timevals.
    // Copy that object representation so the reported peak stays getrusage's value
    // (kilobytes on Linux) without naming the union member.
    static_assert(sizeof(std::int64_t) == 8);
    static_assert((sizeof(timeval) * 2) + sizeof(std::int64_t) <= sizeof(rusage));
    std::array<std::byte, sizeof(rusage)> raw{};
    auto *usage = new (raw.data()) rusage;
    if (getrusage(RUSAGE_SELF, usage) != 0)
    {
        return 0;
    }
    std::int64_t peakKb = 0;
    std::memcpy(&peakKb, raw.data() + (sizeof(timeval) * 2), sizeof(peakKb));
    return peakKb * 1024;
}

} // namespace platform
