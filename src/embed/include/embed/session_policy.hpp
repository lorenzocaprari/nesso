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

/// Peak RSS of one wide int8 session (up to 4 intra-op threads, arena shrunk
/// after each run) while indexing 100000 lines. Half of the memory limit is
/// divided by this when that layout is in use.
inline constexpr std::uintmax_t INT8_SESSION_BYTES = 443191296;

/// Per-session estimate for x86 budgets of 4 or less, which keep one intra-op
/// thread and do not shrink the arena.
inline constexpr std::uintmax_t INT8_NARROW_SESSION_BYTES = std::uintmax_t{480} << 20U;

/// Largest intra-op width of a wide int8 session.
inline constexpr size_t INT8_INTRA_OP_MAX = 4;

/// x86 budgets at or below this stay one intra-op thread per session.
inline constexpr size_t INT8_NARROW_BUDGET = 4;

/// GCC 15 emits two ComputeImpl copies in ORT 1.18.1 and only the intra-op 1
/// copy fuses multiply-add, so intra-op 1 gives different bytes than 2 or more.
/// aarch64 stays at 2 or more. Other architectures keep 1.
[[nodiscard]] constexpr int intraOpThreadFloor() noexcept
{
#ifdef __aarch64__
    return 2;
#else
    return 1;
#endif
}

struct MachineResources
{
    /// Physical cores in the affinity mask, then reduced by a cgroup cpu.max quota.
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
    /// Shrink the CPU arena after each run. x86 single-thread sessions leave it.
    bool shrinkArena = false;
};

/// `requestedThreads` of 0 selects the usable physical cores. A positive value is
/// that CPU budget. On x86, a budget of 4 or less is one intra-op thread and
/// that many sessions, and the arena is not shrunk. A larger x86 budget uses
/// `I = min(4, N/2)` and `S = max(1, N/I)` and shrinks the arena. aarch64 keeps
/// intra-op threads at least intraOpThreadFloor(). fp32 keeps one session and
/// uses the budget as its intra-op thread count, with the same floor.
[[nodiscard]] SessionPolicy sessionPolicy(bool quantized, size_t requestedThreads, const MachineResources &machine);

[[nodiscard]] std::string sessionPolicyText(size_t requestedThreads, const MachineResources &machine);

/// Empty unless an explicit int8 budget asks for more sessions than the memory cap.
[[nodiscard]] std::string overrideWarning(const SessionPolicy &policy, size_t requestedThreads);

/// MemAvailable from /proc/meminfo, in bytes. 0 when the key is missing or unreadable.
[[nodiscard]] std::uintmax_t memAvailableBytes(std::istream &meminfo);

[[nodiscard]] MachineResources readMachine(const std::filesystem::path &procRoot = "/proc",
                                           const std::filesystem::path &sysRoot = "/sys",
                                           const std::filesystem::path &cgroupRoot = "/sys/fs/cgroup");

} // namespace nesso::embed

#endif // EMBED_SESSION_POLICY_HPP
