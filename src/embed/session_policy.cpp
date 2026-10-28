// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/session_policy.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
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

static std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    {
        text.remove_suffix(1);
    }
    return text;
}

static bool parseBoundedInt(std::string_view text, int &value)
{
    if (text.empty())
    {
        return false;
    }
    int parsed = 0;
    for (const char digit : text)
    {
        if (digit < '0' || digit > '9' || parsed > (std::numeric_limits<int>::max() - (digit - '0')) / 10)
        {
            return false;
        }
        parsed = (parsed * 10) + (digit - '0');
    }
    value = parsed;
    return true;
}

// "0-3,5,8-9". An empty or malformed list is ignored by the caller.
static std::optional<std::set<int>> parseCpuList(std::string_view text)
{
    text = trim(text);
    if (text.empty())
    {
        return std::nullopt;
    }
    std::set<int> cpus;
    std::size_t cursor = 0;
    while (cursor <= text.size())
    {
        const std::size_t comma = text.find(',', cursor);
        const std::string_view token =
            text.substr(cursor, comma == std::string_view::npos ? text.size() - cursor : comma - cursor);
        const std::size_t dash = token.find('-');
        int first = 0;
        int last = 0;
        const bool one = dash == std::string_view::npos && parseBoundedInt(token, first);
        const bool range = dash != std::string_view::npos && parseBoundedInt(token.substr(0, dash), first) &&
                           parseBoundedInt(token.substr(dash + 1), last);
        if (!one && !range)
        {
            return std::nullopt;
        }
        if (one)
        {
            last = first;
        }
        if (last < first || last - first > 4096)
        {
            return std::nullopt;
        }
        for (int cpu = first; cpu <= last; ++cpu)
        {
            cpus.insert(cpu);
        }
        if (comma == std::string_view::npos)
        {
            break;
        }
        cursor = comma + 1;
    }
    return cpus.empty() ? std::nullopt : std::optional<std::set<int>>{cpus};
}

// cpu.max is "quota period" microseconds, or "max period". A positive quota
// below one period is one CPU. nullopt means this file sets no limit.
static std::optional<size_t> parseCpuMax(std::string_view text)
{
    text = trim(text);
    const std::size_t gap = text.find_first_of(" \t");
    if (gap == std::string_view::npos || gap == 0)
    {
        return std::nullopt;
    }
    const std::string_view quotaText = text.substr(0, gap);
    int period = 0;
    if (quotaText == "max" || !parseBoundedInt(trim(text.substr(gap)), period) || period <= 0)
    {
        return std::nullopt;
    }
    if (quotaText == "0")
    {
        return 0;
    }
    int quota = 0;
    if (!parseBoundedInt(quotaText, quota) || quota < 0)
    {
        return std::nullopt;
    }
    const int cpus = quota / period;
    return static_cast<size_t>(cpus == 0 ? 1 : cpus);
}

static std::optional<std::set<int>> allowedFromStatus(const std::filesystem::path &statusPath)
{
    std::ifstream status(statusPath);
    if (!status)
    {
        return std::nullopt;
    }
    constexpr std::string_view key = "Cpus_allowed_list:";
    std::string line;
    while (std::getline(status, line))
    {
        if (line.starts_with(key))
        {
            return parseCpuList(line.substr(key.size()));
        }
    }
    return std::nullopt;
}

static bool controllerIsCpu(std::string_view controllers)
{
    std::size_t cursor = 0;
    while (cursor <= controllers.size())
    {
        const std::size_t comma = controllers.find(',', cursor);
        const std::string_view name =
            controllers.substr(cursor, comma == std::string_view::npos ? controllers.size() - cursor : comma - cursor);
        if (name == "cpu")
        {
            return true;
        }
        if (comma == std::string_view::npos)
        {
            break;
        }
        cursor = comma + 1;
    }
    return false;
}

static std::optional<size_t> tightestQuota(const std::filesystem::path &root, std::filesystem::path relative,
                                           const char *fileName)
{
    std::optional<size_t> tightest;
    while (true)
    {
        const std::filesystem::path file = relative.empty() ? root / fileName : root / relative / fileName;
        std::ifstream input(file);
        std::string line;
        if (input && std::getline(input, line))
        {
            if (const std::optional<size_t> parsed = parseCpuMax(line))
            {
                tightest = tightest ? std::min(*tightest, *parsed) : parsed;
            }
        }
        if (relative.empty())
        {
            break;
        }
        relative = relative.parent_path();
    }
    return tightest;
}

static std::string_view stripSlash(std::string_view path)
{
    while (path.starts_with('/'))
    {
        path.remove_prefix(1);
    }
    return path;
}

static std::optional<size_t> cgroupCpuLimit(const std::filesystem::path &procRoot,
                                            const std::filesystem::path &cgroupRoot)
{
    std::ifstream cgroup(procRoot / "self/cgroup");
    if (!cgroup)
    {
        return std::nullopt;
    }
    std::optional<std::filesystem::path> v2Path;
    std::optional<std::filesystem::path> v1Path;
    std::string line;
    while (std::getline(cgroup, line))
    {
        if (line.starts_with("0::"))
        {
            v2Path = std::filesystem::path{std::string{stripSlash(line.substr(3))}};
            continue;
        }
        const std::size_t first = line.find(':');
        const std::size_t second = first == std::string::npos ? std::string::npos : line.find(':', first + 1);
        if (second == std::string::npos)
        {
            continue;
        }
        if (controllerIsCpu(line.substr(first + 1, second - first - 1)))
        {
            v1Path = std::filesystem::path{std::string{stripSlash(line.substr(second + 1))}};
        }
    }
    std::optional<size_t> tightest;
    if (v2Path)
    {
        tightest = tightestQuota(cgroupRoot, *v2Path, "cpu.max");
    }
    if (v1Path)
    {
        // v1 stores the quota and the period in two files. Rejoin them into the cpu.max shape.
        std::filesystem::path relative = *v1Path;
        const std::filesystem::path controller = cgroupRoot / "cpu";
        while (true)
        {
            const std::filesystem::path directory = relative.empty() ? controller : controller / relative;
            std::ifstream quotaFile(directory / "cpu.cfs_quota_us");
            std::ifstream periodFile(directory / "cpu.cfs_period_us");
            std::string quota;
            std::string period;
            if (quotaFile && periodFile && std::getline(quotaFile, quota) && std::getline(periodFile, period))
            {
                const std::string quotaText = trim(quota) == "-1" ? std::string{"max"} : std::string{trim(quota)};
                const std::string joined = quotaText + " " + std::string{trim(period)};
                if (const std::optional<size_t> parsed = parseCpuMax(joined))
                {
                    tightest = tightest ? std::min(*tightest, *parsed) : parsed;
                }
            }
            if (relative.empty())
            {
                break;
            }
            relative = relative.parent_path();
        }
    }
    return tightest;
}

static size_t countPhysicalCores(const std::filesystem::path &cpuDir, const std::optional<std::set<int>> &allowed)
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
        int cpuIndex = 0;
        if (!parseBoundedInt(name.substr(3), cpuIndex))
        {
            continue;
        }
        if (allowed && !allowed->contains(cpuIndex))
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

MachineResources readMachine(const std::filesystem::path &procRoot, const std::filesystem::path &sysRoot,
                             const std::filesystem::path &cgroupRoot)
{
    MachineResources machine;
    machine.logicalThreads = atLeastOne(std::thread::hardware_concurrency());
    std::ifstream meminfo(procRoot / "meminfo");
    machine.memoryLimitBytes = meminfo ? memAvailableBytes(meminfo) : 0;
    if (machine.memoryLimitBytes == 0)
    {
        machine.memoryLimitBytes = physicalMemoryBytes();
    }
    const std::optional<std::set<int>> allowed = allowedFromStatus(procRoot / "self/status");
    size_t cores = countPhysicalCores(sysRoot / "devices/system/cpu", allowed);
    if (cores == 0)
    {
        cores = allowed ? allowed->size() : machine.logicalThreads;
    }
    if (const std::optional<size_t> quota = cgroupCpuLimit(procRoot, cgroupRoot))
    {
        cores = std::min(cores, *quota);
    }
    machine.physicalCores = atLeastOne(cores);
    return machine;
}

SessionPolicy sessionPolicy(bool quantized, size_t requestedThreads, const MachineResources &machine)
{
    const size_t logical = atLeastOne(machine.logicalThreads);
    if (!quantized)
    {
        const size_t picked = requestedThreads == 0 ? logical : requestedThreads;
        const size_t intra = std::max(picked, static_cast<size_t>(intraOpThreadFloor()));
        return SessionPolicy{.quantized = false,
                             .sessions = 1,
                             .intraOpThreads = static_cast<int>(intra),
                             .memoryCap = 0,
                             .shrinkArena = true};
    }

    const size_t budget = requestedThreads == 0 ? atLeastOne(machine.physicalCores) : requestedThreads;
    const bool arm64 = intraOpThreadFloor() > 1;
    const bool narrow = !arm64 && budget <= INT8_NARROW_BUDGET;
    const std::uintmax_t perSession = narrow ? INT8_NARROW_SESSION_BYTES : INT8_SESSION_BYTES;
    // Leave half of the available memory for the rest of the process.
    size_t memoryCap = 0;
    if (machine.memoryLimitBytes > 0)
    {
        const std::uintmax_t fit = std::max<std::uintmax_t>((machine.memoryLimitBytes / 2) / perSession, 1);
        memoryCap = static_cast<size_t>(std::min<std::uintmax_t>(fit, std::numeric_limits<size_t>::max()));
    }
    size_t intra = 1;
    size_t sessions = 1;
    bool shrink = true;
    if (arm64)
    {
        intra = std::max(std::min(INT8_INTRA_OP_MAX, budget), static_cast<size_t>(intraOpThreadFloor()));
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
    if (requestedThreads == 0 && memoryCap > 0)
    {
        sessions = std::min(sessions, memoryCap);
    }
    return SessionPolicy{.quantized = true,
                         .sessions = sessions,
                         .intraOpThreads = static_cast<int>(intra),
                         .memoryCap = memoryCap,
                         .shrinkArena = shrink};
}

std::string sessionPolicyText(size_t requestedThreads, const MachineResources &machine)
{
    const SessionPolicy quantized = sessionPolicy(true, requestedThreads, machine);
    const SessionPolicy floating = sessionPolicy(false, requestedThreads, machine);
    const char *sessionWord = quantized.sessions == 1 ? "session" : "sessions";
    const char *threadWord = quantized.intraOpThreads == 1 ? "thread" : "threads";
    std::string text = quantized.intraOpThreads == 1
                           ? std::format("int8: {} single-thread {}", quantized.sessions, sessionWord)
                           : std::format("int8: {} {}, {} intra-op {}", quantized.sessions, sessionWord,
                                         quantized.intraOpThreads, threadWord);
    if (quantized.memoryCap > 0)
    {
        text += std::format(" (estimated memory cap {})", quantized.memoryCap);
    }
    const char *fpThreadWord = floating.intraOpThreads == 1 ? "thread" : "threads";
    text += std::format("\nfp32: {} session, {} intra-op {}", floating.sessions, floating.intraOpThreads, fpThreadWord);
    return text;
}

std::string overrideWarning(const SessionPolicy &policy, size_t requestedThreads)
{
    if (!policy.quantized || requestedThreads == 0 || policy.memoryCap == 0 || policy.sessions <= policy.memoryCap)
    {
        return {};
    }
    return std::format("Warning: {} int8 sessions exceed the estimated memory limit of {}; continuing with {}.",
                       policy.sessions, policy.memoryCap, policy.sessions);
}

} // namespace nesso::embed
