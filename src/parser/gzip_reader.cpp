// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <expected>
#include <filesystem>
#include <ios>
#include <parser/gzip_reader.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <zconf.h>

// next_in is const, so the compressed buffer does not need a const cast.
#define ZLIB_CONST
#include <zlib.h>

namespace nesso::parser
{
namespace
{

struct InflateEnd
{
    void operator()(z_stream *stream) const noexcept { inflateEnd(stream); }
};

constexpr std::size_t CHUNK_BYTES = static_cast<std::size_t>(16) * 1024;

} // namespace

namespace
{
enum class PullStatus : std::uint8_t
{
    Bytes,
    End,
    Error
};
} // namespace

static std::expected<void, GzipError> InitGzipInflate(z_stream &stream)
{
    // 16 selects the gzip wrapper. inflate stops at each member; the reset below
    // continues so concatenated members are one byte stream.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
    {
        return std::unexpected(GzipError::Corrupt);
    }
    return {};
}

template <typename Pull>
static std::expected<bool, GzipError> RefillCompressedInput(Pull &pull, z_stream &stream,
                                                            std::array<unsigned char, CHUNK_BYTES> &compressed,
                                                            bool &input_ended, bool finished_member)
{
    if (input_ended)
    {
        if (finished_member)
        {
            return true;
        }
        return std::unexpected(GzipError::Corrupt);
    }
    unsigned int count = 0;
    const PullStatus pulled = pull(compressed.data(), static_cast<unsigned int>(compressed.size()), count);
    if (pulled == PullStatus::Error)
    {
        return std::unexpected(GzipError::OpenFailure);
    }
    if (pulled == PullStatus::End || count == 0)
    {
        if (finished_member)
        {
            return true;
        }
        return std::unexpected(GzipError::Corrupt);
    }
    if (count < compressed.size())
    {
        input_ended = true;
    }
    stream.next_in = compressed.data();
    stream.avail_in = count;
    return false;
}

static void AppendInflateOutput(std::string &output, const std::array<unsigned char, CHUNK_BYTES> &plain,
                                std::size_t produced)
{
    if (produced == 0)
    {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.append(reinterpret_cast<const char *>(plain.data()), produced);
}

static std::expected<void, GzipError> HandleInflateStatus(z_stream &stream, int status, std::size_t produced,
                                                          unsigned int available_before, bool &finished_member)
{
    if (status == Z_STREAM_END)
    {
        finished_member = true;
        if (inflateReset(&stream) != Z_OK)
        {
            return std::unexpected(GzipError::Corrupt);
        }
        return {};
    }
    if (status == Z_OK || status == Z_BUF_ERROR)
    {
        if (produced == 0 && stream.avail_in == available_before)
        {
            return std::unexpected(GzipError::Corrupt);
        }
        finished_member = false;
        return {};
    }
    return std::unexpected(GzipError::Corrupt);
}

bool IsGzipPath(const std::filesystem::path &path)
{
    std::string extension = path.extension().string();
    for (char &character : extension)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return extension == ".gz";
}

// `pull` fills `dest` with the next compressed bytes. `End` is a clean EOF.
// `Error` is a read failure.
template <typename Pull> static std::expected<std::string, GzipError> InflateGzip(Pull pull)
{
    z_stream stream{};
    if (const auto init = InitGzipInflate(stream); !init)
    {
        return std::unexpected(init.error());
    }
    // The deleter is the only use: inflateEnd must run on every return.
    // cppcheck-suppress unusedVariable
    [[maybe_unused]] const std::unique_ptr<z_stream, InflateEnd> guard(&stream);

    std::array<unsigned char, CHUNK_BYTES> compressed{};
    std::array<unsigned char, CHUNK_BYTES> plain{};
    std::string output;
    bool finished_member = false;
    bool input_ended = false;

    while (true)
    {
        if (stream.avail_in == 0)
        {
            const auto refill = RefillCompressedInput(pull, stream, compressed, input_ended, finished_member);
            if (!refill)
            {
                return std::unexpected(refill.error());
            }
            if (*refill)
            {
                return output;
            }
        }

        stream.next_out = plain.data();
        stream.avail_out = static_cast<unsigned int>(plain.size());
        const unsigned int available_before = stream.avail_in;
        const int status = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = plain.size() - static_cast<std::size_t>(stream.avail_out);
        AppendInflateOutput(output, plain, produced);

        if (const auto handled = HandleInflateStatus(stream, status, produced, available_before, finished_member);
            !handled)
        {
            return std::unexpected(handled.error());
        }
    }
}

std::expected<std::string, GzipError> ReadGzip(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(GzipError::OpenFailure);
    }
    return InflateGzip(
        [&input](unsigned char *dest, unsigned int capacity, unsigned int &count)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            input.read(reinterpret_cast<char *>(dest), static_cast<std::streamsize>(capacity));
            if (input.bad())
            {
                return PullStatus::Error;
            }
            const std::streamsize got = input.gcount();
            if (got <= 0)
            {
                count = 0;
                return PullStatus::End;
            }
            count = static_cast<unsigned int>(got);
            return PullStatus::Bytes;
        });
}

std::expected<std::string, GzipError> ReadGzipBytes(std::string_view bytes)
{
    std::size_t offset = 0;
    return InflateGzip(
        [&bytes, &offset](unsigned char *dest, unsigned int capacity, unsigned int &count)
        {
            if (offset >= bytes.size())
            {
                count = 0;
                return PullStatus::End;
            }
            const std::size_t room = bytes.size() - offset;
            const std::size_t take = std::min(room, static_cast<std::size_t>(capacity));
            const std::span<const char> compressed{bytes.data(), bytes.size()};
            std::memcpy(dest, compressed.subspan(offset).data(), take);
            offset += take;
            count = static_cast<unsigned int>(take);
            return PullStatus::Bytes;
        });
}

} // namespace nesso::parser
