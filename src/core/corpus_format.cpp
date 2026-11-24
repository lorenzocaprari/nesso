// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "corpus_detail.hpp"

#include <core/corpus_index.hpp>
#include <core/sha256.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <istream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace nesso::core::detail
{

auto RangeInside(std::uint64_t offset, std::uint64_t length, std::uint64_t limit) -> bool
{
    return offset <= limit && length <= limit - offset;
}

auto AlignUp(std::uint64_t value, std::uint64_t alignment) -> std::uint64_t
{
    return (value + alignment - 1) & ~(alignment - 1);
}

auto WriteRaw(std::ostream &output, const void *data, std::size_t size) -> bool
{
    if (size == 0)
    {
        return true;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(output);
}

auto WriteU32(std::ostream &output, std::uint32_t value) -> bool
{
    const std::array<unsigned char, 4> bytes{static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8U),
                                             static_cast<unsigned char>(value >> 16U),
                                             static_cast<unsigned char>(value >> 24U)};
    return WriteRaw(output, bytes.data(), bytes.size());
}

auto WriteU64(std::ostream &output, std::uint64_t value) -> bool
{
    const std::array<unsigned char, 8> bytes{
        static_cast<unsigned char>(value),        static_cast<unsigned char>(value >> 8U),
        static_cast<unsigned char>(value >> 16U), static_cast<unsigned char>(value >> 24U),
        static_cast<unsigned char>(value >> 32U), static_cast<unsigned char>(value >> 40U),
        static_cast<unsigned char>(value >> 48U), static_cast<unsigned char>(value >> 56U)};
    return WriteRaw(output, bytes.data(), bytes.size());
}

auto ReadExact(std::istream &input, void *data, std::size_t size) -> bool
{
    if (size == 0)
    {
        return true;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    input.read(reinterpret_cast<char *>(data), static_cast<std::streamsize>(size));
    return std::cmp_equal(input.gcount(), size);
}

auto ReadU32Stream(std::istream &input, std::uint32_t &value) -> bool
{
    std::array<unsigned char, 4> bytes{};
    if (!ReadExact(input, bytes.data(), bytes.size()))
    {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

auto ReadU64Stream(std::istream &input, std::uint64_t &value) -> bool
{
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (!ReadU32Stream(input, low) || !ReadU32Stream(input, high))
    {
        return false;
    }
    value = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32U);
    return true;
}

auto ReadU32(std::span<const std::byte> bytes, std::uint64_t offset, std::uint32_t &value) -> bool
{
    if (!RangeInside(offset, sizeof(value), bytes.size()))
    {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        value |= std::to_integer<std::uint32_t>(bytes[offset++]) << shift;
    }
    return true;
}

auto ReadU64(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t &value) -> bool
{
    if (!RangeInside(offset, sizeof(value), bytes.size()))
    {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
    {
        value |= std::to_integer<std::uint64_t>(bytes[offset++]) << shift;
    }
    return true;
}

auto ViewBytes(std::span<const std::byte> bytes, std::uint64_t offset, std::uint64_t length) -> std::string_view
{
    const std::span<const std::byte> slice =
        bytes.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(length));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const char *>(slice.data()), slice.size()};
}

auto AtEnd(std::istream &input) -> bool
{
    return input.peek() == std::char_traits<char>::eof() && input.eof() && !input.bad();
}

auto CopyBytes(std::istream &input, std::ostream &output, std::uint64_t bytes) -> bool
{
    std::array<char, 64ULL * 1024ULL> buffer{};
    while (bytes > 0)
    {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(bytes, buffer.size()));
        if (!ReadExact(input, buffer.data(), count) || !WriteRaw(output, buffer.data(), count))
        {
            return false;
        }
        bytes -= count;
    }
    return AtEnd(input);
}

auto WritePadding(std::ostream &output, std::uint64_t count) -> bool
{
    constexpr std::array<char, 64> zeros{};
    while (count > 0)
    {
        const auto part = static_cast<std::size_t>(std::min<std::uint64_t>(count, zeros.size()));
        if (!WriteRaw(output, zeros.data(), part))
        {
            return false;
        }
        count -= part;
    }
    return true;
}

auto IsZero(std::span<const std::byte> bytes, std::uint64_t begin, std::uint64_t end) -> bool
{
    if (begin > end || end > bytes.size())
    {
        return false;
    }
    return std::ranges::all_of(bytes.subspan(begin, end - begin),
                               [](std::byte value) -> bool { return value == std::byte{0}; });
}

} // namespace nesso::core::detail

namespace nesso::core
{

static_assert(std::endian::native == std::endian::little);

auto ComputeCorpusChecksum(std::span<const std::byte> file) -> CorpusChecksum
{
    constexpr std::array<std::byte, CORPUS_CHECKSUM_BYTES> zeros{};
    const std::array<std::span<const std::byte>, 3> parts{file.first(CORPUS_CHECKSUM_OFFSET), zeros,
                                                          file.subspan(CORPUS_CHECKSUM_OFFSET + CORPUS_CHECKSUM_BYTES)};
    const Sha256Digest digest = ComputeSha256(parts);
    CorpusChecksum checksum{};
    std::ranges::transform(std::span{digest}.first(CORPUS_CHECKSUM_BYTES), checksum.begin(),
                           [](std::uint8_t value) -> std::byte { return std::byte{value}; });
    return checksum;
}

} // namespace nesso::core
