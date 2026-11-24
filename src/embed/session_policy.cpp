// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <embed/session_policy.hpp>

#include <filesystem>
#include <istream>
#include <platform/narrow_cast.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>

namespace nesso::embed
{

static auto AtLeastOne(size_t count) -> size_t { return count == 0 ? 1 : count; }

static auto PhysicalMemoryBytes() -> std::uintmax_t
{
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page_size <= 0)
    {
        return 0;
    }
    return static_cast<std::uintmax_t>(pages) * static_cast<std::uintmax_t>(page_size);
}

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

static auto Trim(std::string_view text) -> std::string_view
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

static auto ParseBoundedInt(std::string_view text, int &value) -> bool
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
static auto ParseCpuList(std::string_view text) -> std::optional<std::set<int>>
{
    text = Trim(text);
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
        const bool one = dash == std::string_view::npos && ParseBoundedInt(token, first);
        const bool range = dash != std::string_view::npos && ParseBoundedInt(token.substr(0, dash), first) &&
                           ParseBoundedInt(token.substr(dash + 1), last);
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
static auto ParseCpuMax(std::string_view text) -> std::optional<size_t>
{
    text = Trim(text);
    const std::size_t gap = text.find_first_of(" \t");
    if (gap == std::string_view::npos || gap == 0)
    {
        return std::nullopt;
    }
    const std::string_view quota_text = text.substr(0, gap);
    int period = 0;
    if (quota_text == "max" || !ParseBoundedInt(Trim(text.substr(gap)), period) || period <= 0)
    {
        return std::nullopt;
    }
    if (quota_text == "0")
    {
        return 0;
    }
    int quota = 0;
    if (!ParseBoundedInt(quota_text, quota) || quota < 0)
    {
        return std::nullopt;
    }
    const int cpus = quota / period;
    return static_cast<size_t>(cpus == 0 ? 1 : cpus);
}

static auto AllowedFromStatus(const std::filesystem::path &status_path) -> std::optional<std::set<int>>
{
    std::ifstream status(status_path);
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
            return ParseCpuList(line.substr(key.size()));
        }
    }
    return std::nullopt;
}

namespace
{
enum class CgroupController : std::uint8_t
{
    Cpu,
    Memory,
};
} // namespace

static auto ControllerHas(std::string_view controllers, CgroupController wanted) -> bool
{
    const std::string_view wanted_name = wanted == CgroupController::Cpu ? "cpu" : "memory";
    std::size_t cursor = 0;
    while (cursor <= controllers.size())
    {
        const std::size_t comma = controllers.find(',', cursor);
        const std::string_view name =
            controllers.substr(cursor, comma == std::string_view::npos ? controllers.size() - cursor : comma - cursor);
        if (name == wanted_name)
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

static auto TightestQuota(const std::filesystem::path &root, std::filesystem::path relative, const char *file_name)
    -> std::optional<size_t>
{
    std::optional<size_t> tightest;
    while (true)
    {
        const std::filesystem::path file = relative.empty() ? root / file_name : root / relative / file_name;
        std::ifstream input(file);
        std::string line;
        if (input && std::getline(input, line))
        {
            if (const std::optional<size_t> parsed = ParseCpuMax(line))
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

static auto StripSlash(std::string_view path) -> std::string_view
{
    while (path.starts_with('/'))
    {
        path.remove_prefix(1);
    }
    return path;
}

static void MinQuota(std::optional<size_t> &tightest, size_t parsed)
{
    tightest = tightest ? std::min(*tightest, parsed) : std::optional<size_t>{parsed};
}

static void MinHeadroom(std::optional<std::uintmax_t> &tightest, std::uintmax_t headroom)
{
    tightest = tightest ? std::min(*tightest, headroom) : std::optional<std::uintmax_t>{headroom};
}

namespace
{
struct CgroupPaths
{
    std::optional<std::filesystem::path> v2_path;
    std::optional<std::filesystem::path> v1_cpu_path;
    std::optional<std::filesystem::path> v1_memory_path;
};
} // namespace

static auto ReadCgroupPaths(const std::filesystem::path &proc_root) -> std::optional<CgroupPaths>
{
    std::ifstream cgroup(proc_root / "self/cgroup");
    if (!cgroup)
    {
        return std::nullopt;
    }
    CgroupPaths paths;
    std::string line;
    while (std::getline(cgroup, line))
    {
        if (line.starts_with("0::"))
        {
            paths.v2_path = std::filesystem::path{std::string{StripSlash(line.substr(3))}};
            continue;
        }
        const std::size_t first = line.find(':');
        const std::size_t second = first == std::string::npos ? std::string::npos : line.find(':', first + 1);
        if (second == std::string::npos)
        {
            continue;
        }
        // Bind to line (not string::substr) so the view outlives the checks.
        const std::string_view line_view = line;
        const std::string_view controllers = line_view.substr(first + 1, second - first - 1);
        const std::filesystem::path relative{std::string{StripSlash(line_view.substr(second + 1))}};
        if (ControllerHas(controllers, CgroupController::Cpu))
        {
            paths.v1_cpu_path = relative;
        }
        if (ControllerHas(controllers, CgroupController::Memory))
        {
            paths.v1_memory_path = relative;
        }
    }
    return paths;
}

static auto ParseUintBytes(std::string_view text) -> std::optional<std::uintmax_t>
{
    text = Trim(text);
    if (text.empty())
    {
        return std::nullopt;
    }
    std::uintmax_t value = 0;
    for (const char digit : text)
    {
        if (digit < '0' || digit > '9' ||
            value > (std::numeric_limits<std::uintmax_t>::max() - static_cast<unsigned>(digit - '0')) / 10)
        {
            return std::nullopt;
        }
        value = (value * 10) + static_cast<unsigned>(digit - '0');
    }
    return value;
}

static auto ReadUintFile(const std::filesystem::path &path) -> std::optional<std::uintmax_t>
{
    std::ifstream input(path);
    std::string line;
    if (!input || !std::getline(input, line))
    {
        return std::nullopt;
    }
    return ParseUintBytes(line);
}

namespace
{
struct CgroupV1Walk
{
    std::filesystem::path cgroup_root;
    std::filesystem::path relative;
};
} // namespace

namespace
{
struct ProcCgroupRoots
{
    std::filesystem::path proc_root;
    std::filesystem::path cgroup_root;
};
} // namespace

// nullopt when memory.max is "max", missing, or unreadable.
static auto V2MemoryHeadroomAt(const std::filesystem::path &directory) -> std::optional<std::uintmax_t>
{
    std::ifstream max_file(directory / "memory.max");
    std::string max_line;
    if (!max_file || !std::getline(max_file, max_line))
    {
        return std::nullopt;
    }
    const std::string_view max_text = Trim(max_line);
    if (max_text == "max")
    {
        return std::nullopt;
    }
    const std::optional<std::uintmax_t> limit = ParseUintBytes(max_text);
    if (!limit)
    {
        return std::nullopt;
    }
    const std::uintmax_t current = ReadUintFile(directory / "memory.current").value_or(0);
    return *limit > current ? *limit - current : 0;
}

// Remaining bytes under the tightest finite memory.max on the walk. nullopt
// when every ancestor is "max" or unreadable.
static auto TightestV2MemoryHeadroom(const std::filesystem::path &root, std::filesystem::path relative)
    -> std::optional<std::uintmax_t>
{
    std::optional<std::uintmax_t> tightest;
    while (true)
    {
        const std::filesystem::path directory = relative.empty() ? root : root / relative;
        if (const std::optional<std::uintmax_t> headroom = V2MemoryHeadroomAt(directory))
        {
            MinHeadroom(tightest, *headroom);
        }
        if (relative.empty())
        {
            break;
        }
        relative = relative.parent_path();
    }
    return tightest;
}

static auto V1MemoryLimitIsUnlimited(std::uintmax_t limit, std::uintmax_t physical_bytes) -> bool
{
    constexpr std::uintmax_t unlimited_floor = std::uintmax_t{1} << 62U;
    if (limit >= unlimited_floor)
    {
        return true;
    }
    return physical_bytes > 0 && limit >= physical_bytes;
}

static auto TightestV1MemoryHeadroom(const CgroupV1Walk &walk, std::uintmax_t physical_bytes)
    -> std::optional<std::uintmax_t>
{
    std::optional<std::uintmax_t> tightest;
    const std::filesystem::path controller = walk.cgroup_root / "memory";
    std::filesystem::path relative = walk.relative;
    while (true)
    {
        const std::filesystem::path directory = relative.empty() ? controller : controller / relative;
        if (const std::optional<std::uintmax_t> limit = ReadUintFile(directory / "memory.limit_in_bytes"))
        {
            if (!V1MemoryLimitIsUnlimited(*limit, physical_bytes))
            {
                const std::uintmax_t usage = ReadUintFile(directory / "memory.usage_in_bytes").value_or(0);
                MinHeadroom(tightest, *limit > usage ? *limit - usage : 0);
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

static auto CgroupMemoryHeadroom(const ProcCgroupRoots &roots, std::uintmax_t physical_bytes)
    -> std::optional<std::uintmax_t>
{
    const std::optional<CgroupPaths> paths = ReadCgroupPaths(roots.proc_root);
    if (!paths)
    {
        return std::nullopt;
    }
    std::optional<std::uintmax_t> tightest;
    if (paths->v2_path)
    {
        tightest = TightestV2MemoryHeadroom(roots.cgroup_root, *paths->v2_path);
    }
    if (paths->v1_memory_path)
    {
        if (const std::optional<std::uintmax_t> v1_headroom = TightestV1MemoryHeadroom(
                {.cgroup_root = roots.cgroup_root, .relative = *paths->v1_memory_path}, physical_bytes))
        {
            MinHeadroom(tightest, *v1_headroom);
        }
    }
    return tightest;
}

static auto TightestV1Quota(const CgroupV1Walk &walk) -> std::optional<size_t>
{
    // v1 stores the quota and the period in two files. Rejoin them into the
    // cpu.max shape.
    std::optional<size_t> tightest;
    const std::filesystem::path controller = walk.cgroup_root / "cpu";
    std::filesystem::path relative = walk.relative;
    while (true)
    {
        const std::filesystem::path directory = relative.empty() ? controller : controller / relative;
        std::ifstream quota_file(directory / "cpu.cfs_quota_us");
        std::ifstream period_file(directory / "cpu.cfs_period_us");
        std::string quota;
        std::string period;
        if (quota_file && period_file && std::getline(quota_file, quota) && std::getline(period_file, period))
        {
            const std::string quota_text = Trim(quota) == "-1" ? std::string{"max"} : std::string{Trim(quota)};
            const std::string joined = quota_text + " " + std::string{Trim(period)};
            if (const std::optional<size_t> parsed = ParseCpuMax(joined))
            {
                MinQuota(tightest, *parsed);
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

static auto CgroupCpuLimit(const ProcCgroupRoots &roots) -> std::optional<size_t>
{
    const std::optional<CgroupPaths> paths = ReadCgroupPaths(roots.proc_root);
    if (!paths)
    {
        return std::nullopt;
    }
    std::optional<size_t> tightest;
    if (paths->v2_path)
    {
        tightest = TightestQuota(roots.cgroup_root, *paths->v2_path, "cpu.max");
    }
    if (paths->v1_cpu_path)
    {
        if (const std::optional<size_t> v1_quota =
                TightestV1Quota({.cgroup_root = roots.cgroup_root, .relative = *paths->v1_cpu_path}))
        {
            MinQuota(tightest, *v1_quota);
        }
    }
    return tightest;
}

static auto CountPhysicalCores(const std::filesystem::path &cpu_dir, const std::optional<std::set<int>> &allowed)
    -> size_t
{
    std::set<std::pair<int, int>> cores;
    std::error_code error;
    for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(cpu_dir, error))
    {
        const std::string name = entry.path().filename().string();
        if (name.size() < 4 || !name.starts_with("cpu"))
        {
            continue;
        }
        if (!std::ranges::all_of(name.substr(3),
                                 [](unsigned char digit) -> bool { return digit >= '0' && digit <= '9'; }))
        {
            continue;
        }
        int cpu_index = 0;
        if (!ParseBoundedInt(name.substr(3), cpu_index))
        {
            continue;
        }
        if (allowed && !allowed->contains(cpu_index))
        {
            continue;
        }
        std::ifstream package_file(entry.path() / "topology/physical_package_id");
        std::ifstream core_file(entry.path() / "topology/core_id");
        int package_id = 0;
        int core_id = 0;
        if (!(package_file >> package_id) || !(core_file >> core_id))
        {
            continue;
        }
        cores.emplace(package_id, core_id);
    }
    return cores.size();
}

auto ReadMachine(const MachineFsRoots &roots) -> MachineResources
{
    MachineResources machine;
    machine.logical_threads = AtLeastOne(std::thread::hardware_concurrency());
    std::ifstream meminfo(roots.proc_root / "meminfo");
    machine.memory_limit_bytes = meminfo ? MemAvailableBytes(meminfo) : 0;
    if (machine.memory_limit_bytes == 0)
    {
        machine.memory_limit_bytes = PhysicalMemoryBytes();
    }
    const ProcCgroupRoots cgroup_roots{.proc_root = roots.proc_root, .cgroup_root = roots.cgroup_root};
    if (const std::optional<std::uintmax_t> headroom = CgroupMemoryHeadroom(cgroup_roots, PhysicalMemoryBytes()))
    {
        machine.memory_limit_bytes = std::min(machine.memory_limit_bytes, *headroom);
        // 0 means "unknown" in MakeSessionPolicy; keep a positive limit so the
        // existing max(..., 1) path still yields one session when usage exceeds
        // the cgroup.
        if (machine.memory_limit_bytes == 0)
        {
            machine.memory_limit_bytes = 1;
        }
    }
    const std::optional<std::set<int>> allowed = AllowedFromStatus(roots.proc_root / "self/status");
    size_t cores = CountPhysicalCores(roots.sys_root / "devices/system/cpu", allowed);
    if (cores == 0)
    {
        cores = allowed ? allowed->size() : machine.logical_threads;
    }
    if (const std::optional<size_t> quota = CgroupCpuLimit(cgroup_roots))
    {
        cores = std::min(cores, *quota);
    }
    machine.physical_cores = AtLeastOne(cores);
    return machine;
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
