// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_SESSION_POLICY_HPP
#define EMBED_SESSION_POLICY_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>

namespace nesso::embed
{

/// Estimated memory one int8 session needs: ~440 MiB of activations for one
/// 32x256 batch plus ~40 MiB per session, from Python ORT measurements of the
/// same int8 model. The C++ build may differ.
inline constexpr std::uintmax_t INT8_SESSION_BYTES = std::uintmax_t{480} << 20U;

struct MachineResources
{
    size_t physicalCores = 1;
    size_t logicalThreads = 1;
    /// Memory the process can use: MemAvailable, else total RAM. 0 is unknown.
    std::uintmax_t memoryLimitBytes = 0;
};

struct SessionPolicy
{
    bool quantized = false;
    size_t sessions = 1;
    int intraOpThreads = 1;
    /// int8 sessions that fit the memory limit. 0 when the limit is unknown or the model is fp32.
    size_t memoryCap = 0;
};

/// `requestedThreads` of 0 selects the machine default. An explicit count overrides it.
[[nodiscard]] SessionPolicy sessionPolicy(bool quantized, size_t requestedThreads, const MachineResources &machine);

[[nodiscard]] std::string sessionPolicyText(size_t requestedThreads, const MachineResources &machine);

/// Empty unless an explicit int8 session count exceeds the memory cap.
[[nodiscard]] std::string overrideWarning(const SessionPolicy &policy, size_t requestedThreads);

/// MemAvailable from /proc/meminfo, in bytes. 0 when the key is missing or unreadable.
[[nodiscard]] std::uintmax_t memAvailableBytes(std::istream &meminfo);

[[nodiscard]] MachineResources readMachine(const std::filesystem::path &procRoot = "/proc",
                                           const std::filesystem::path &sysRoot = "/sys");

} // namespace nesso::embed

#endif // EMBED_SESSION_POLICY_HPP
