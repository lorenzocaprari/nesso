// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/session_policy.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>

namespace nesso::embed
{

static size_t atLeastOne(size_t count) { return count == 0 ? 1 : count; }

static std::uintmax_t physicalMemoryBytes()
{
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto pageSize = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || pageSize <= 0)
    {
        return 0;
    }
    return static_cast<std::uintmax_t>(pages) * static_cast<std::uintmax_t>(pageSize);
}

std::uintmax_t memAvailableBytes(std::istream &meminfo)
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

static size_t countPhysicalCores(const std::filesystem::path &cpuDir)
{
    std::set<std::pair<int, int>> cores;
    std::error_code error;
    for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(cpuDir, error))
    {
        const std::string name = entry.path().filename().string();
        if (name.size() < 4 || !name.starts_with("cpu"))
        {
            continue;
        }
        if (!std::ranges::all_of(name.substr(3), [](unsigned char digit) { return digit >= '0' && digit <= '9'; }))
        {
            continue;
        }
        std::ifstream packageFile(entry.path() / "topology/physical_package_id");
        std::ifstream coreFile(entry.path() / "topology/core_id");
        int packageId = 0;
        int coreId = 0;
        if (!(packageFile >> packageId) || !(coreFile >> coreId))
        {
            continue;
        }
        cores.emplace(packageId, coreId);
    }
    return cores.size();
}

MachineResources readMachine(const std::filesystem::path &procRoot, const std::filesystem::path &sysRoot)
{
    MachineResources machine;
    machine.logicalThreads = atLeastOne(std::thread::hardware_concurrency());
    std::ifstream meminfo(procRoot / "meminfo");
    machine.memoryLimitBytes = meminfo ? memAvailableBytes(meminfo) : 0;
    if (machine.memoryLimitBytes == 0)
    {
        machine.memoryLimitBytes = physicalMemoryBytes();
    }
    const size_t cores = countPhysicalCores(sysRoot / "devices/system/cpu");
    machine.physicalCores = cores == 0 ? machine.logicalThreads : cores;
    return machine;
}

SessionPolicy sessionPolicy(bool quantized, size_t requestedThreads, const MachineResources &machine)
{
    const size_t logical = atLeastOne(machine.logicalThreads);
    if (!quantized)
    {
        const size_t intra = requestedThreads == 0 ? logical : requestedThreads;
        return SessionPolicy{.quantized = false, .sessions = 1, .intraOpThreads = static_cast<int>(intra)};
    }

    // Leave half of the available memory for the rest of the process.
    size_t memoryCap = 0;
    if (machine.memoryLimitBytes > 0)
    {
        const std::uintmax_t fit = std::max<std::uintmax_t>((machine.memoryLimitBytes / 2) / INT8_SESSION_BYTES, 1);
        memoryCap = static_cast<size_t>(std::min<std::uintmax_t>(fit, std::numeric_limits<size_t>::max()));
    }
    size_t sessions = requestedThreads;
    if (sessions == 0)
    {
        sessions = atLeastOne(machine.physicalCores);
        if (memoryCap > 0)
        {
            sessions = std::min(sessions, memoryCap);
        }
    }
    return SessionPolicy{.quantized = true, .sessions = sessions, .intraOpThreads = 1, .memoryCap = memoryCap};
}

std::string sessionPolicyText(size_t requestedThreads, const MachineResources &machine)
{
    const SessionPolicy quantized = sessionPolicy(true, requestedThreads, machine);
    const SessionPolicy floating = sessionPolicy(false, requestedThreads, machine);
    const char *sessionWord = quantized.sessions == 1 ? "session" : "sessions";
    std::string text = std::format("int8: {} single-thread {}", quantized.sessions, sessionWord);
    if (quantized.memoryCap > 0)
    {
        text += std::format(" (estimated memory cap {})", quantized.memoryCap);
    }
    text += std::format("\nfp32: {} session, {} intra-op threads", floating.sessions, floating.intraOpThreads);
    return text;
}

std::string overrideWarning(const SessionPolicy &policy, size_t requestedThreads)
{
    if (!policy.quantized || requestedThreads == 0 || policy.memoryCap == 0 || requestedThreads <= policy.memoryCap)
    {
        return {};
    }
    return std::format("Warning: {} int8 sessions exceed the estimated memory limit of {}; continuing with {}.",
                       requestedThreads, policy.memoryCap, requestedThreads);
}

} // namespace nesso::embed
