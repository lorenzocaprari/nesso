// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/gzip_reader.hpp>

#include <array>
#include <fstream>
#include <memory>
#include <string>

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

bool isGzipPath(const std::filesystem::path &path)
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

std::expected<std::string, GzipError> readGzip(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(GzipError::OpenFailure);
    }

    z_stream stream{};
    // 16 selects the gzip wrapper. inflate stops at each member; the reset below continues so
    // concatenated members are one byte stream.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
    {
        return std::unexpected(GzipError::Corrupt);
    }
    // The deleter is the only use: inflateEnd must run on every return.
    // cppcheck-suppress unusedVariable
    [[maybe_unused]] const std::unique_ptr<z_stream, InflateEnd> guard(&stream);

    std::array<unsigned char, CHUNK_BYTES> compressed{};
    std::array<unsigned char, CHUNK_BYTES> plain{};
    std::string output;
    bool finishedMember = false;
    bool inputEnded = false;

    while (true)
    {
        if (stream.avail_in == 0)
        {
            if (inputEnded)
            {
                if (finishedMember)
                {
                    return output;
                }
                return std::unexpected(GzipError::Corrupt);
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            input.read(reinterpret_cast<char *>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
            if (input.bad())
            {
                return std::unexpected(GzipError::OpenFailure);
            }
            const std::streamsize count = input.gcount();
            if (count <= 0)
            {
                if (finishedMember)
                {
                    return output;
                }
                return std::unexpected(GzipError::Corrupt);
            }
            if (static_cast<std::size_t>(count) < compressed.size())
            {
                inputEnded = true;
            }
            stream.next_in = compressed.data();
            stream.avail_in = static_cast<unsigned int>(count);
        }

        stream.next_out = plain.data();
        stream.avail_out = static_cast<unsigned int>(plain.size());
        const unsigned int availableBefore = stream.avail_in;
        const int status = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = plain.size() - static_cast<std::size_t>(stream.avail_out);
        if (produced != 0)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            output.append(reinterpret_cast<const char *>(plain.data()), produced);
        }

        if (status == Z_STREAM_END)
        {
            finishedMember = true;
            if (inflateReset(&stream) != Z_OK)
            {
                return std::unexpected(GzipError::Corrupt);
            }
            continue;
        }
        if (status == Z_OK || status == Z_BUF_ERROR)
        {
            if (produced == 0 && stream.avail_in == availableBefore)
            {
                return std::unexpected(GzipError::Corrupt);
            }
            finishedMember = false;
            continue;
        }
        return std::unexpected(GzipError::Corrupt);
    }
}

} // namespace nesso::parser
