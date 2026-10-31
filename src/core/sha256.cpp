// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/sha256.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <span>
#include <string_view>

namespace nesso::core
{
namespace
{

constexpr std::array<std::uint32_t, 64> ROUND_CONSTANTS{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

class Sha256
{
  public:
    void update(std::span<const std::byte> bytes)
    {
        totalBytes_ += bytes.size();
        while (!bytes.empty())
        {
            const std::size_t count = std::min(bytes.size(), buffer_.size() - buffered_);
            std::ranges::copy(bytes.first(count), buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
            buffered_ += count;
            bytes = bytes.subspan(count);
            if (buffered_ == buffer_.size())
            {
                transform(buffer_);
                buffered_ = 0;
            }
        }
    }

    [[nodiscard]] Sha256Digest finish()
    {
        const std::uint64_t bits = totalBytes_ * 8U;
        const auto buffer = std::span{buffer_};
        buffer[buffered_++] = std::byte{0x80};
        if (buffered_ > 56)
        {
            std::ranges::fill(buffer.subspan(buffered_), std::byte{0});
            transform(buffer_);
            buffered_ = 0;
        }
        std::ranges::fill(buffer.subspan(buffered_, 56 - buffered_), std::byte{0});
        for (std::size_t index = 0; index < 8; ++index)
        {
            buffer[56 + index] = static_cast<std::byte>(bits >> ((7U - index) * 8U));
        }
        transform(buffer_);

        Sha256Digest digest{};
        const auto digestBytes = std::span{digest};
        const auto stateWords = std::span{state_};
        for (std::size_t index = 0; index < state_.size(); ++index)
        {
            for (std::size_t byte = 0; byte < 4; ++byte)
            {
                digestBytes[(index * 4) + byte] = static_cast<std::uint8_t>(stateWords[index] >> ((3U - byte) * 8U));
            }
        }
        return digest;
    }

  private:
    void transform(std::span<const std::byte, 64> block)
    {
        std::array<std::uint32_t, 64> words{};
        const auto schedule = std::span{words};
        for (std::size_t index = 0; index < 16; ++index)
        {
            for (std::size_t byte = 0; byte < 4; ++byte)
            {
                schedule[index] = (schedule[index] << 8U) | std::to_integer<std::uint32_t>(block[(index * 4) + byte]);
            }
        }
        for (std::size_t index = 16; index < words.size(); ++index)
        {
            const auto first =
                std::rotr(schedule[index - 15], 7) ^ std::rotr(schedule[index - 15], 18) ^ (schedule[index - 15] >> 3U);
            const auto second =
                std::rotr(schedule[index - 2], 17) ^ std::rotr(schedule[index - 2], 19) ^ (schedule[index - 2] >> 10U);
            schedule[index] = schedule[index - 16] + first + schedule[index - 7] + second;
        }

        auto [a, b, c, d, e, f, g, h] = state_;
        const auto constants = std::span{ROUND_CONSTANTS};
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choose = (e & f) ^ (~e & g);
            const auto temp1 = h + sum1 + choose + constants[index] + schedule[index];
            const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        const std::array<std::uint32_t, 8> result{a, b, c, d, e, f, g, h};
        const auto stateWords = std::span{state_};
        const auto resultWords = std::span{result};
        for (std::size_t index = 0; index < state_.size(); ++index)
        {
            stateWords[index] += resultWords[index];
        }
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::byte, 64> buffer_{};
    std::uint64_t totalBytes_ = 0;
    std::size_t buffered_ = 0;
};

} // namespace

Sha256Digest sha256(std::span<const std::byte> bytes)
{
    Sha256 hash;
    hash.update(bytes);
    return hash.finish();
}

std::expected<Sha256Digest, HashError> sha256File(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::unexpected(HashError::OpenFailure);
    }
    Sha256 hash;
    std::array<char, 65'536> buffer{};
    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(input.gcount());
        hash.update(std::as_bytes(std::span{buffer}.first(count)));
    }
    if (input.bad() || !input.eof())
    {
        return std::unexpected(HashError::ReadFailure);
    }
    return hash.finish();
}

std::string hexSha256(const Sha256Digest &digest)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest)
    {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

} // namespace nesso::core
