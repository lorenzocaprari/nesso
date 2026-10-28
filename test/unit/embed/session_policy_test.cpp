// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/session_policy.hpp>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

using nesso::embed::INT8_INTRA_OP_MAX;
using nesso::embed::INT8_SESSION_BYTES;
using nesso::embed::MachineResources;
using nesso::embed::SessionPolicy;

namespace
{

constexpr std::uintmax_t GIB = std::uintmax_t{1} << 30U;

class TempDir
{
  public:
    explicit TempDir(const std::string &name)
        : path_(std::filesystem::temp_directory_path() / (name + "_" + std::to_string(getpid())))
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    TempDir(TempDir &&) = delete;
    TempDir &operator=(TempDir &&) = delete;

    ~TempDir() { std::filesystem::remove_all(path_); }

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void writeText(const std::filesystem::path &path, const std::string &text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    REQUIRE(output);
    output << text;
    REQUIRE(output);
}

void writeCpu(const std::filesystem::path &sysRoot, const std::string &name, int package, int core)
{
    const auto topology = sysRoot / "devices/system/cpu" / name / "topology";
    writeText(topology / "physical_package_id", std::to_string(package) + "\n");
    writeText(topology / "core_id", std::to_string(core) + "\n");
}

std::uintmax_t parse(const std::string &text)
{
    std::istringstream input(text);
    return nesso::embed::memAvailableBytes(input);
}

} // namespace

TEST_CASE("MemAvailable is read in bytes", "[session-policy]")
{
    CHECK(parse("MemTotal:        8000000 kB\nMemFree:          100 kB\nMemAvailable:    3228812 kB\n") ==
          std::uintmax_t{3228812} * 1024);
    CHECK(parse("MemAvailable: 0 kB\n") == 0);
    CHECK(parse("MemTotal:        8000000 kB\nMemFree:          100 kB\n") == 0);
    CHECK(parse("MemAvailable:    lots kB\n") == 0);
    CHECK(parse("MemAvailable:    42 MB\n") == 0);
    CHECK(parse("MemAvailable:    42\n") == 0);
    CHECK(parse("MemAvailable:    99999999999999999999999 kB\n") == 0);
    CHECK(parse("") == 0);
}

TEST_CASE("the machine is read from proc and sys roots", "[session-policy]")
{
    const TempDir dir{"nesso_session_policy_machine"};
    const auto proc = dir.path() / "proc";
    const auto sys = dir.path() / "sys";
    writeText(proc / "meminfo", "MemTotal: 16000000 kB\nMemAvailable: 4194304 kB\n");
    writeCpu(sys, "cpu0", 0, 0);
    writeCpu(sys, "cpu1", 0, 0);
    writeCpu(sys, "cpu2", 0, 1);
    writeCpu(sys, "cpu3", 1, 0);
    writeText(sys / "devices/system/cpu/cpufreq/topology/core_id", "7\n");
    writeText(sys / "devices/system/cpu/cpu4x/topology/core_id", "7\n");
    std::filesystem::create_directories(sys / "devices/system/cpu/cpu5");

    const MachineResources machine = nesso::embed::readMachine(proc, sys);
    CHECK(machine.memoryLimitBytes == 4 * GIB);
    CHECK(machine.physicalCores == 3);
    CHECK(machine.logicalThreads >= 1);
}

TEST_CASE("without MemAvailable or a topology the machine falls back", "[session-policy]")
{
    const TempDir dir{"nesso_session_policy_fallback"};
    const auto proc = dir.path() / "proc";
    writeText(proc / "meminfo", "MemTotal: 16000000 kB\n");
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto pageSize = sysconf(_SC_PAGESIZE);
    REQUIRE(pages > 0);
    REQUIRE(pageSize > 0);
    const std::uintmax_t total = static_cast<std::uintmax_t>(pages) * static_cast<std::uintmax_t>(pageSize);

    const MachineResources noKey = nesso::embed::readMachine(proc, dir.path() / "no-sys");
    CHECK(noKey.memoryLimitBytes == total);
    CHECK(noKey.physicalCores == noKey.logicalThreads);

    const MachineResources noFile = nesso::embed::readMachine(dir.path() / "no-proc", dir.path() / "no-sys");
    CHECK(noFile.memoryLimitBytes == total);
}

TEST_CASE("int8 splits a cpu budget into sessions and intra-op threads", "[session-policy]")
{
    const MachineResources wide{.physicalCores = 16, .logicalThreads = 32, .memoryLimitBytes = 64 * GIB};
    const SessionPolicy sixteen = nesso::embed::sessionPolicy(true, 0, wide);
    CHECK(sixteen.intraOpThreads == static_cast<int>(INT8_INTRA_OP_MAX));
    CHECK(sixteen.sessions == 16 / INT8_INTRA_OP_MAX);
    CHECK(sixteen.quantized);

    const MachineResources eight{.physicalCores = 8, .logicalThreads = 16, .memoryLimitBytes = 64 * GIB};
    CHECK(nesso::embed::sessionPolicy(true, 0, eight).sessions == 2);
    CHECK(nesso::embed::sessionPolicy(true, 0, eight).intraOpThreads == 4);

    const MachineResources four{.physicalCores = 4, .logicalThreads = 4, .memoryLimitBytes = 64 * GIB};
    const MachineResources six{.physicalCores = 6, .logicalThreads = 12, .memoryLimitBytes = 64 * GIB};
    const auto at = [&](size_t budget) { return nesso::embed::sessionPolicy(true, budget, wide); };
#ifdef __aarch64__
    CHECK(nesso::embed::sessionPolicy(true, 0, four).sessions == 1);
    CHECK(nesso::embed::sessionPolicy(true, 0, four).intraOpThreads == 4);
    CHECK(nesso::embed::sessionPolicy(true, 0, four).shrinkArena);
    CHECK(nesso::embed::sessionPolicy(true, 0, six).sessions == 1);
    CHECK(nesso::embed::sessionPolicy(true, 0, six).intraOpThreads == 4);
    CHECK(at(1).sessions == 1);
    CHECK(at(1).intraOpThreads == 2);
    CHECK(at(1).shrinkArena);
    CHECK(at(2).sessions == 1);
    CHECK(at(2).intraOpThreads == 2);
    CHECK(at(4).sessions == 1);
    CHECK(at(4).intraOpThreads == 4);
    CHECK(at(5).sessions == 1);
    CHECK(at(5).intraOpThreads == 4);
    CHECK(at(6).sessions == 1);
    CHECK(at(6).intraOpThreads == 4);
#else
    CHECK(nesso::embed::sessionPolicy(true, 0, four).sessions == 4);
    CHECK(nesso::embed::sessionPolicy(true, 0, four).intraOpThreads == 1);
    CHECK_FALSE(nesso::embed::sessionPolicy(true, 0, four).shrinkArena);
    CHECK(nesso::embed::sessionPolicy(true, 0, six).sessions == 2);
    CHECK(nesso::embed::sessionPolicy(true, 0, six).intraOpThreads == 3);
    CHECK(nesso::embed::sessionPolicy(true, 0, six).shrinkArena);
    CHECK(at(1).sessions == 1);
    CHECK(at(1).intraOpThreads == 1);
    CHECK_FALSE(at(1).shrinkArena);
    CHECK(at(2).sessions == 2);
    CHECK(at(2).intraOpThreads == 1);
    CHECK(at(4).sessions == 4);
    CHECK(at(4).intraOpThreads == 1);
    CHECK(at(5).sessions == 2);
    CHECK(at(5).intraOpThreads == 2);
    CHECK(at(5).shrinkArena);
    CHECK(at(6).sessions == 2);
    CHECK(at(6).intraOpThreads == 3);
#endif
    CHECK(at(8).sessions == 2);
    CHECK(at(8).intraOpThreads == 4);
    CHECK(at(8).shrinkArena);
    CHECK(at(16).sessions == 4);
    CHECK(at(16).intraOpThreads == 4);
}

TEST_CASE("the default int8 pool reduces sessions while the estimate exceeds the memory cap", "[session-policy]")
{
    const auto policyWith = [](std::uintmax_t available, size_t cores)
    {
        const MachineResources machine{
            .physicalCores = cores, .logicalThreads = cores * 2, .memoryLimitBytes = available};
        return nesso::embed::sessionPolicy(true, 0, machine);
    };
    // Half of 4 session-estimates fits 2 sessions. 16 cores would otherwise open 4.
    const SessionPolicy two = policyWith(4 * INT8_SESSION_BYTES, 16);
    CHECK(two.memoryCap == 2);
    CHECK(two.sessions == 2);
    CHECK(two.intraOpThreads == 4);

    const SessionPolicy one = policyWith(INT8_SESSION_BYTES, 16);
    CHECK(one.memoryCap == 1);
    CHECK(one.sessions == 1);
    CHECK(one.intraOpThreads == 4);

    const SessionPolicy tiny = policyWith(100, 16);
    CHECK(tiny.memoryCap == 1);
    CHECK(tiny.sessions == 1);

    const SessionPolicy plenty = policyWith(64 * GIB, 16);
    CHECK(plenty.sessions == 4);
    CHECK(plenty.memoryCap == (32 * GIB) / INT8_SESSION_BYTES);
}

TEST_CASE("an unknown memory limit leaves the derived session count", "[session-policy]")
{
    const MachineResources machine{.physicalCores = 6, .logicalThreads = 12, .memoryLimitBytes = 0};
    const SessionPolicy policy = nesso::embed::sessionPolicy(true, 0, machine);
#ifdef __aarch64__
    CHECK(policy.sessions == 1);
    CHECK(policy.intraOpThreads == 4);
#else
    CHECK(policy.sessions == 2);
    CHECK(policy.intraOpThreads == 3);
#endif
    CHECK(policy.memoryCap == 0);
    CHECK(nesso::embed::overrideWarning(nesso::embed::sessionPolicy(true, 64, machine), 64).empty());

    const MachineResources noCores{.physicalCores = 0, .logicalThreads = 0, .memoryLimitBytes = 0};
    CHECK(nesso::embed::sessionPolicy(true, 0, noCores).sessions == 1);
    CHECK(nesso::embed::sessionPolicy(true, 0, noCores).intraOpThreads == nesso::embed::intraOpThreadFloor());
    CHECK(nesso::embed::sessionPolicy(false, 0, noCores).intraOpThreads == nesso::embed::intraOpThreadFloor());
}

TEST_CASE("an explicit cpu budget is not reduced and warns above the cap", "[session-policy]")
{
    const MachineResources machine{
        .physicalCores = 8, .logicalThreads = 16, .memoryLimitBytes = 4 * INT8_SESSION_BYTES};
    const SessionPolicy capped = nesso::embed::sessionPolicy(true, 0, machine);
    CHECK(capped.memoryCap == 2);
    CHECK(capped.sessions == 2);
    CHECK(nesso::embed::overrideWarning(capped, 0).empty());

    const SessionPolicy within = nesso::embed::sessionPolicy(true, 8, machine);
    CHECK(within.sessions == 2);
    CHECK(within.intraOpThreads == 4);
    CHECK(nesso::embed::overrideWarning(within, 8).empty());

    const SessionPolicy one = nesso::embed::sessionPolicy(true, 1, machine);
    CHECK(one.sessions == 1);
    CHECK(one.intraOpThreads == nesso::embed::intraOpThreadFloor());
    CHECK(nesso::embed::overrideWarning(one, 1).empty());

    const SessionPolicy above = nesso::embed::sessionPolicy(true, 16, machine);
    CHECK(above.sessions == 4);
    CHECK(above.intraOpThreads == 4);
    CHECK(above.memoryCap == 2);
    CHECK(nesso::embed::overrideWarning(above, 16) ==
          "Warning: 4 int8 sessions exceed the estimated memory limit of 2; continuing with 4.");
}

TEST_CASE("fp32 keeps one session at the logical thread count", "[session-policy]")
{
    const MachineResources machine{.physicalCores = 4, .logicalThreads = 8, .memoryLimitBytes = 1 * GIB};
    const SessionPolicy policy = nesso::embed::sessionPolicy(false, 0, machine);
    CHECK_FALSE(policy.quantized);
    CHECK(policy.sessions == 1);
    CHECK(policy.intraOpThreads == 8);
    CHECK(policy.memoryCap == 0);

    const SessionPolicy requested = nesso::embed::sessionPolicy(false, 64, machine);
    CHECK(requested.sessions == 1);
    CHECK(requested.intraOpThreads == 64);
    CHECK(nesso::embed::overrideWarning(requested, 64).empty());

    const SessionPolicy oneThread = nesso::embed::sessionPolicy(false, 1, machine);
    CHECK(oneThread.sessions == 1);
    CHECK(oneThread.intraOpThreads == nesso::embed::intraOpThreadFloor());
}

TEST_CASE("the policy text shows the session counts and the memory cap", "[session-policy]")
{
    const MachineResources capped{.physicalCores = 8, .logicalThreads = 16, .memoryLimitBytes = 4 * INT8_SESSION_BYTES};
    CHECK(nesso::embed::sessionPolicyText(0, capped) ==
          "int8: 2 sessions, 4 intra-op threads (estimated memory cap 2)\nfp32: 1 session, 16 intra-op threads");
#ifdef __aarch64__
    CHECK(nesso::embed::sessionPolicyText(2, capped) ==
          "int8: 1 session, 2 intra-op threads (estimated memory cap 2)\nfp32: 1 session, 2 intra-op threads");
    CHECK(nesso::embed::sessionPolicyText(1, capped) ==
          "int8: 1 session, 2 intra-op threads (estimated memory cap 2)\nfp32: 1 session, 2 intra-op threads");
#else
    CHECK(nesso::embed::sessionPolicyText(2, capped) ==
          "int8: 2 single-thread sessions (estimated memory cap 1)\nfp32: 1 session, 2 intra-op threads");
    CHECK(nesso::embed::sessionPolicyText(1, capped) ==
          "int8: 1 single-thread session (estimated memory cap 1)\nfp32: 1 session, 1 intra-op thread");
#endif

    const MachineResources unknown{.physicalCores = 1, .logicalThreads = 2, .memoryLimitBytes = 0};
#ifdef __aarch64__
    CHECK(nesso::embed::sessionPolicyText(0, unknown) ==
          "int8: 1 session, 2 intra-op threads\nfp32: 1 session, 2 intra-op threads");
#else
    CHECK(nesso::embed::sessionPolicyText(0, unknown) ==
          "int8: 1 single-thread session\nfp32: 1 session, 2 intra-op threads");
#endif
}

TEST_CASE("usable cores follow affinity and cgroup cpu.max", "[session-policy]")
{
    const TempDir dir{"nesso_session_policy_cpus"};
    const auto proc = dir.path() / "proc";
    const auto sys = dir.path() / "sys";
    const auto cgroup = dir.path() / "cgroup";
    writeText(proc / "meminfo", "MemAvailable: 1048576 kB\n");
    writeCpu(sys, "cpu0", 0, 0);
    writeCpu(sys, "cpu1", 0, 1);
    writeCpu(sys, "cpu2", 0, 2);
    writeCpu(sys, "cpu3", 0, 3);

    writeText(proc / "self/status", "Name:\tnesso\nCpus_allowed:\tf\nCpus_allowed_list:\t0,2\n");
    const MachineResources affinity = nesso::embed::readMachine(proc, sys, cgroup);
    CHECK(affinity.physicalCores == 2);

    writeText(proc / "self/status", "Cpus_allowed_list:\t0-3\n");
    writeText(proc / "self/cgroup", "0::/slice/leaf\n");
    writeText(cgroup / "slice/cpu.max", "200000 100000\n");
    const MachineResources quota = nesso::embed::readMachine(proc, sys, cgroup);
    CHECK(quota.physicalCores == 2);

    writeText(cgroup / "slice/leaf/cpu.max", "max 100000\n");
    writeText(cgroup / "cpu.max", "100000 100000\n");
    const MachineResources tightest = nesso::embed::readMachine(proc, sys, cgroup);
    CHECK(tightest.physicalCores == 1);

    writeText(cgroup / "slice/cpu.max", "50000 100000\n");
    writeText(cgroup / "cpu.max", "max 100000\n");
    const MachineResources fraction = nesso::embed::readMachine(proc, sys, cgroup);
    CHECK(fraction.physicalCores == 1);

    writeText(proc / "self/cgroup", "not-a-cgroup-line\n1:cpuacct:/ignored\n2:cpu,cpuacct:/limited\n");
    writeText(cgroup / "cpu/limited/cpu.cfs_quota_us", "300000\n");
    writeText(cgroup / "cpu/limited/cpu.cfs_period_us", "100000\n");
    writeText(cgroup / "cpu/cpu.cfs_quota_us", "-1\n");
    writeText(cgroup / "cpu/cpu.cfs_period_us", "100000\n");
    const MachineResources v1 = nesso::embed::readMachine(proc, sys, cgroup);
    CHECK(v1.physicalCores == 3);

    writeText(proc / "self/cgroup", "0::/slice/leaf\n2:cpu:/limited\n");
    writeText(cgroup / "slice/cpu.max", "400000 100000\n");
    const MachineResources both = nesso::embed::readMachine(proc, sys, cgroup);
    CHECK(both.physicalCores == 3);

    writeText(proc / "self/status", "Cpus_allowed_list:\tnope\n");
    writeText(proc / "self/cgroup", "0::/missing\n");
    const MachineResources ignored = nesso::embed::readMachine(proc, sys, cgroup);
    CHECK(ignored.physicalCores == 4);

    writeText(proc / "self/status", "Cpus_allowed_list:\t0-1\n");
    writeText(proc / "self/cgroup", "0::/stopped\n");
    writeText(cgroup / "stopped/cpu.max", "0 100000\n");
    const MachineResources zero = nesso::embed::readMachine(proc, sys, cgroup);
    CHECK(zero.physicalCores == 1);
}
