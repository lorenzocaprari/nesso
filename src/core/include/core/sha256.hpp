// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_SHA256_HPP
#define CORE_SHA256_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace nesso::core
{

using Sha256Digest = std::array<std::uint8_t, 32>;

enum class HashError : std::uint8_t
{
    OpenFailure,
    ReadFailure
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::byte> bytes);
/// Digest of `parts` hashed in order, as if they were one contiguous buffer.
[[nodiscard]] Sha256Digest sha256(std::span<const std::span<const std::byte>> parts);
[[nodiscard]] std::expected<Sha256Digest, HashError> sha256File(const std::filesystem::path &path);
[[nodiscard]] std::string hexSha256(const Sha256Digest &digest);

} // namespace nesso::core

#endif // CORE_SHA256_HPP
