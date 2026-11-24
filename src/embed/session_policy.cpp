// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <embed/session_policy.hpp>

#include <platform/narrow_cast.hpp>

#include <algorithm>
#include <format>
#include <istream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace nesso::embed
{

static auto AtLeastOne(size_t count) -> size_t { return count == 0 ? 1 : count; }

auto MemAvailableBytes(std::istream &meminfo) -> std::uintmax_t
{
    constexpr std::string_view key = "MemAvailable:";
    std::string line;
    while (std::getline(meminfo, line))
    {
        if (!line.starts_with(key))
        {
            continue;
        }
        std::istringstream fields(line.substr(key.size()));
        std::uintmax_t kibibytes = 0;
        std::string unit;
        if (!(fields >> kibibytes >> unit) || unit != "kB" ||
            kibibytes > std::numeric_limits<std::uintmax_t>::max() / 1024)
        {
            return 0;
        }
        return kibibytes * 1024;
    }
    return 0;
}

auto MakeSessionPolicy(bool quantized, size_t requested_threads, const MachineResources &machine) -> SessionPolicy
{
    const size_t logical = AtLeastOne(machine.logical_threads);
    if (!quantized)
    {
        const size_t picked = requested_threads == 0 ? logical : requested_threads;
        const size_t intra = std::max(picked, static_cast<size_t>(IntraOpThreadFloor()));
        return SessionPolicy{.quantized = false,
                             .sessions = 1,
                             .intra_op_threads = nesso::platform::NarrowCast<int>(intra),
                             .memory_cap = 0,
                             .shrink_arena = true};
    }

    const size_t budget = requested_threads == 0 ? AtLeastOne(machine.physical_cores) : requested_threads;
    const bool arm64 = IntraOpThreadFloor() > 1;
    const bool narrow = !arm64 && budget <= INT8_NARROW_BUDGET;
    const std::uintmax_t per_session = narrow ? INT8_NARROW_SESSION_BYTES : INT8_SESSION_BYTES;
    // Leave half of the available memory for the rest of the process.
    size_t memory_cap = 0;
    if (machine.memory_limit_bytes > 0)
    {
        const std::uintmax_t fit = std::max<std::uintmax_t>((machine.memory_limit_bytes / 2) / per_session, 1);
        memory_cap = static_cast<size_t>(std::min<std::uintmax_t>(fit, std::numeric_limits<size_t>::max()));
    }
    size_t intra = 1;
    size_t sessions = 1;
    bool shrink = true;
    if (arm64)
    {
        intra = std::max(std::min(INT8_INTRA_OP_MAX, budget), static_cast<size_t>(IntraOpThreadFloor()));
        sessions = std::max<size_t>(1, budget / intra);
    }
    else if (narrow)
    {
        intra = 1;
        sessions = budget;
        shrink = false;
    }
    else
    {
        intra = std::min(INT8_INTRA_OP_MAX, budget / 2);
        sessions = std::max<size_t>(1, budget / intra);
    }
    if (requested_threads == 0 && memory_cap > 0)
    {
        sessions = std::min(sessions, memory_cap);
    }
    return SessionPolicy{.quantized = true,
                         .sessions = sessions,
                         .intra_op_threads = nesso::platform::NarrowCast<int>(intra),
                         .memory_cap = memory_cap,
                         .shrink_arena = shrink};
}

auto SessionPolicyText(size_t requested_threads, const MachineResources &machine) -> std::string
{
    const SessionPolicy quantized = MakeSessionPolicy(true, requested_threads, machine);
    const SessionPolicy floating = MakeSessionPolicy(false, requested_threads, machine);
    const char *session_word = quantized.sessions == 1 ? "session" : "sessions";
    const char *thread_word = quantized.intra_op_threads == 1 ? "thread" : "threads";
    std::string text = quantized.intra_op_threads == 1
                           ? std::format("int8: {} single-thread {}", quantized.sessions, session_word)
                           : std::format("int8: {} {}, {} intra-op {}", quantized.sessions, session_word,
                                         quantized.intra_op_threads, thread_word);
    if (quantized.memory_cap > 0)
    {
        text += std::format(" (estimated memory cap {})", quantized.memory_cap);
    }
    const char *fp_thread_word = floating.intra_op_threads == 1 ? "thread" : "threads";
    text +=
        std::format("\nfp32: {} session, {} intra-op {}", floating.sessions, floating.intra_op_threads, fp_thread_word);
    return text;
}

auto OverrideWarning(const SessionPolicy &policy, size_t requested_threads) -> std::string
{
    if (!policy.quantized || requested_threads == 0 || policy.memory_cap == 0 || policy.sessions <= policy.memory_cap)
    {
        return {};
    }
    return std::format("Warning: {} int8 sessions exceed the estimated memory "
                       "limit of {}; continuing with {}.",
                       policy.sessions, policy.memory_cap, policy.sessions);
}

} // namespace nesso::embed
