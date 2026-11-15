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

    [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void WriteText(const std::filesystem::path &path, const std::string &text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    REQUIRE(output);
    output << text;
    REQUIRE(output);
}

void WriteCpu(const std::filesystem::path &sys_root, const std::string &name, int package, int core)
{
    const auto topology = sys_root / "devices/system/cpu" / name / "topology";
    WriteText(topology / "physical_package_id", std::to_string(package) + "\n");
    WriteText(topology / "core_id", std::to_string(core) + "\n");
}

std::uintmax_t Parse(const std::string &text)
{
    std::istringstream input(text);
    return nesso::embed::MemAvailableBytes(input);
}

} // namespace

TEST_CASE("MemAvailable is read in bytes", "[session-policy]")
{
    CHECK(Parse("MemTotal:        8000000 kB\nMemFree:          100 "
                "kB\nMemAvailable:    3228812 kB\n") == std::uintmax_t{3228812} * 1024);
    CHECK(Parse("MemAvailable: 0 kB\n") == 0);
    CHECK(Parse("MemTotal:        8000000 kB\nMemFree:          100 kB\n") == 0);
    CHECK(Parse("MemAvailable:    lots kB\n") == 0);
    CHECK(Parse("MemAvailable:    42 MB\n") == 0);
    CHECK(Parse("MemAvailable:    42\n") == 0);
    CHECK(Parse("MemAvailable:    99999999999999999999999 kB\n") == 0);
    CHECK(Parse("") == 0);
}

TEST_CASE("the machine is read from proc and sys roots", "[session-policy]")
{
    const TempDir dir{"nesso_session_policy_machine"};
    const auto proc = dir.Path() / "proc";
    const auto sys = dir.Path() / "sys";
    WriteText(proc / "meminfo", "MemTotal: 16000000 kB\nMemAvailable: 4194304 kB\n");
    WriteCpu(sys, "cpu0", 0, 0);
    WriteCpu(sys, "cpu1", 0, 0);
    WriteCpu(sys, "cpu2", 0, 1);
    WriteCpu(sys, "cpu3", 1, 0);
    WriteText(sys / "devices/system/cpu/cpufreq/topology/core_id", "7\n");
    WriteText(sys / "devices/system/cpu/cpu4x/topology/core_id", "7\n");
    std::filesystem::create_directories(sys / "devices/system/cpu/cpu5");

    const MachineResources machine = nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys});
    CHECK(machine.memory_limit_bytes == 4 * GIB);
    CHECK(machine.physical_cores == 3);
    CHECK(machine.logical_threads >= 1);
}

TEST_CASE("without MemAvailable or a topology the machine falls back", "[session-policy]")
{
    const TempDir dir{"nesso_session_policy_fallback"};
    const auto proc = dir.Path() / "proc";
    WriteText(proc / "meminfo", "MemTotal: 16000000 kB\n");
    const auto pages = sysconf(_SC_PHYS_PAGES);
    const auto page_size = sysconf(_SC_PAGESIZE);
    REQUIRE(pages > 0);
    REQUIRE(page_size > 0);
    const std::uintmax_t total = static_cast<std::uintmax_t>(pages) * static_cast<std::uintmax_t>(page_size);

    const MachineResources no_key = nesso::embed::ReadMachine({.proc_root = proc, .sys_root = dir.Path() / "no-sys"});
    CHECK(no_key.memory_limit_bytes == total);
    CHECK(no_key.physical_cores == no_key.logical_threads);

    const MachineResources no_file =
        nesso::embed::ReadMachine({.proc_root = dir.Path() / "no-proc", .sys_root = dir.Path() / "no-sys"});
    CHECK(no_file.memory_limit_bytes == total);
}

TEST_CASE("int8 splits a cpu budget into sessions and intra-op threads", "[session-policy]")
{
    const MachineResources wide{.physical_cores = 16, .logical_threads = 32, .memory_limit_bytes = 64 * GIB};
    const SessionPolicy sixteen = nesso::embed::MakeSessionPolicy(true, 0, wide);
    CHECK(sixteen.intra_op_threads == static_cast<int>(INT8_INTRA_OP_MAX));
    CHECK(sixteen.sessions == 16 / INT8_INTRA_OP_MAX);
    CHECK(sixteen.quantized);

    const MachineResources eight{.physical_cores = 8, .logical_threads = 16, .memory_limit_bytes = 64 * GIB};
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, eight).sessions == 2);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, eight).intra_op_threads == 4);

    const MachineResources four{.physical_cores = 4, .logical_threads = 4, .memory_limit_bytes = 64 * GIB};
    const MachineResources six{.physical_cores = 6, .logical_threads = 12, .memory_limit_bytes = 64 * GIB};
    const auto at = [&](size_t budget) { return nesso::embed::MakeSessionPolicy(true, budget, wide); };
#ifdef __aarch64__
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, four).sessions == 1);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, four).intraOpThreads == 4);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, four).shrinkArena);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, six).sessions == 1);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, six).intraOpThreads == 4);
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
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, four).sessions == 4);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, four).intra_op_threads == 1);
    CHECK_FALSE(nesso::embed::MakeSessionPolicy(true, 0, four).shrink_arena);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, six).sessions == 2);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, six).intra_op_threads == 3);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, six).shrink_arena);
    CHECK(at(1).sessions == 1);
    CHECK(at(1).intra_op_threads == 1);
    CHECK_FALSE(at(1).shrink_arena);
    CHECK(at(2).sessions == 2);
    CHECK(at(2).intra_op_threads == 1);
    CHECK(at(4).sessions == 4);
    CHECK(at(4).intra_op_threads == 1);
    CHECK(at(5).sessions == 2);
    CHECK(at(5).intra_op_threads == 2);
    CHECK(at(5).shrink_arena);
    CHECK(at(6).sessions == 2);
    CHECK(at(6).intra_op_threads == 3);
#endif
    CHECK(at(8).sessions == 2);
    CHECK(at(8).intra_op_threads == 4);
    CHECK(at(8).shrink_arena);
    CHECK(at(16).sessions == 4);
    CHECK(at(16).intra_op_threads == 4);
}

TEST_CASE("the default int8 pool reduces sessions while the estimate exceeds "
          "the memory cap",
          "[session-policy]")
{
    const auto policy_with = [](std::uintmax_t available, size_t cores)
    {
        const MachineResources machine{
            .physical_cores = cores, .logical_threads = cores * 2, .memory_limit_bytes = available};
        return nesso::embed::MakeSessionPolicy(true, 0, machine);
    };
    // Half of 4 session-estimates fits 2 sessions. 16 cores would otherwise
    // open 4.
    const SessionPolicy two = policy_with(4 * INT8_SESSION_BYTES, 16);
    CHECK(two.memory_cap == 2);
    CHECK(two.sessions == 2);
    CHECK(two.intra_op_threads == 4);

    const SessionPolicy one = policy_with(INT8_SESSION_BYTES, 16);
    CHECK(one.memory_cap == 1);
    CHECK(one.sessions == 1);
    CHECK(one.intra_op_threads == 4);

    const SessionPolicy tiny = policy_with(100, 16);
    CHECK(tiny.memory_cap == 1);
    CHECK(tiny.sessions == 1);

    const SessionPolicy plenty = policy_with(64 * GIB, 16);
    CHECK(plenty.sessions == 4);
    CHECK(plenty.memory_cap == (32 * GIB) / INT8_SESSION_BYTES);
}

TEST_CASE("an unknown memory limit leaves the derived session count", "[session-policy]")
{
    const MachineResources machine{.physical_cores = 6, .logical_threads = 12, .memory_limit_bytes = 0};
    const SessionPolicy policy = nesso::embed::MakeSessionPolicy(true, 0, machine);
#ifdef __aarch64__
    CHECK(policy.sessions == 1);
    CHECK(policy.intraOpThreads == 4);
#else
    CHECK(policy.sessions == 2);
    CHECK(policy.intra_op_threads == 3);
#endif
    CHECK(policy.memory_cap == 0);
    CHECK(nesso::embed::OverrideWarning(nesso::embed::MakeSessionPolicy(true, 64, machine), 64).empty());

    const MachineResources no_cores{.physical_cores = 0, .logical_threads = 0, .memory_limit_bytes = 0};
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, no_cores).sessions == 1);
    CHECK(nesso::embed::MakeSessionPolicy(true, 0, no_cores).intra_op_threads == nesso::embed::IntraOpThreadFloor());
    CHECK(nesso::embed::MakeSessionPolicy(false, 0, no_cores).intra_op_threads == nesso::embed::IntraOpThreadFloor());
}

TEST_CASE("an explicit cpu budget is not reduced and warns above the cap", "[session-policy]")
{
    const MachineResources machine{
        .physical_cores = 8, .logical_threads = 16, .memory_limit_bytes = 4 * INT8_SESSION_BYTES};
    const SessionPolicy capped = nesso::embed::MakeSessionPolicy(true, 0, machine);
    CHECK(capped.memory_cap == 2);
    CHECK(capped.sessions == 2);
    CHECK(nesso::embed::OverrideWarning(capped, 0).empty());

    const SessionPolicy within = nesso::embed::MakeSessionPolicy(true, 8, machine);
    CHECK(within.sessions == 2);
    CHECK(within.intra_op_threads == 4);
    CHECK(nesso::embed::OverrideWarning(within, 8).empty());

    const SessionPolicy one = nesso::embed::MakeSessionPolicy(true, 1, machine);
    CHECK(one.sessions == 1);
    CHECK(one.intra_op_threads == nesso::embed::IntraOpThreadFloor());
    CHECK(nesso::embed::OverrideWarning(one, 1).empty());

    const SessionPolicy above = nesso::embed::MakeSessionPolicy(true, 16, machine);
    CHECK(above.sessions == 4);
    CHECK(above.intra_op_threads == 4);
    CHECK(above.memory_cap == 2);
    CHECK(nesso::embed::OverrideWarning(above, 16) ==
          "Warning: 4 int8 sessions exceed the estimated memory limit of 2; "
          "continuing with 4.");
}

TEST_CASE("fp32 keeps one session at the logical thread count", "[session-policy]")
{
    const MachineResources machine{.physical_cores = 4, .logical_threads = 8, .memory_limit_bytes = 1 * GIB};
    const SessionPolicy policy = nesso::embed::MakeSessionPolicy(false, 0, machine);
    CHECK_FALSE(policy.quantized);
    CHECK(policy.sessions == 1);
    CHECK(policy.intra_op_threads == 8);
    CHECK(policy.memory_cap == 0);

    const SessionPolicy requested = nesso::embed::MakeSessionPolicy(false, 64, machine);
    CHECK(requested.sessions == 1);
    CHECK(requested.intra_op_threads == 64);
    CHECK(nesso::embed::OverrideWarning(requested, 64).empty());

    const SessionPolicy one_thread = nesso::embed::MakeSessionPolicy(false, 1, machine);
    CHECK(one_thread.sessions == 1);
    CHECK(one_thread.intra_op_threads == nesso::embed::IntraOpThreadFloor());
}

TEST_CASE("fp32 budget 1024 does not wrap intra-op threads", "[session-policy]")
{
    const MachineResources machine{.physical_cores = 4, .logical_threads = 8, .memory_limit_bytes = 1 * GIB};
    const SessionPolicy policy = nesso::embed::MakeSessionPolicy(false, 1024, machine);
    CHECK(policy.sessions == 1);
    CHECK(policy.intra_op_threads == 1024);
}

TEST_CASE("the policy text shows the session counts and the memory cap", "[session-policy]")
{
    const MachineResources capped{
        .physical_cores = 8, .logical_threads = 16, .memory_limit_bytes = 4 * INT8_SESSION_BYTES};
    CHECK(nesso::embed::SessionPolicyText(0, capped) ==
          "int8: 2 sessions, 4 intra-op threads (estimated memory cap 2)\nfp32: "
          "1 session, 16 intra-op threads");
#ifdef __aarch64__
    CHECK(nesso::embed::SessionPolicyText(2, capped) ==
          "int8: 1 session, 2 intra-op threads (estimated memory cap 2)\nfp32: 1 "
          "session, 2 intra-op threads");
    CHECK(nesso::embed::SessionPolicyText(1, capped) ==
          "int8: 1 session, 2 intra-op threads (estimated memory cap 2)\nfp32: 1 "
          "session, 2 intra-op threads");
#else
    CHECK(nesso::embed::SessionPolicyText(2, capped) ==
          "int8: 2 single-thread sessions (estimated memory cap 1)\nfp32: 1 "
          "session, 2 intra-op threads");
    CHECK(nesso::embed::SessionPolicyText(1, capped) ==
          "int8: 1 single-thread session (estimated memory cap 1)\nfp32: 1 "
          "session, 1 intra-op thread");
#endif

    const MachineResources unknown{.physical_cores = 1, .logical_threads = 2, .memory_limit_bytes = 0};
#ifdef __aarch64__
    CHECK(nesso::embed::SessionPolicyText(0, unknown) ==
          "int8: 1 session, 2 intra-op threads\nfp32: 1 session, 2 intra-op "
          "threads");
#else
    CHECK(nesso::embed::SessionPolicyText(0, unknown) ==
          "int8: 1 single-thread session\nfp32: 1 session, 2 intra-op threads");
#endif
}

TEST_CASE("usable cores follow affinity and cgroup cpu.max", "[session-policy]")
{
    const TempDir dir{"nesso_session_policy_cpus"};
    const auto proc = dir.Path() / "proc";
    const auto sys = dir.Path() / "sys";
    const auto cgroup = dir.Path() / "cgroup";
    WriteText(proc / "meminfo", "MemAvailable: 1048576 kB\n");
    WriteCpu(sys, "cpu0", 0, 0);
    WriteCpu(sys, "cpu1", 0, 1);
    WriteCpu(sys, "cpu2", 0, 2);
    WriteCpu(sys, "cpu3", 0, 3);

    WriteText(proc / "self/status", "Name:\tnesso\nCpus_allowed:\tf\nCpus_allowed_list:\t0,2\n");
    const MachineResources affinity =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(affinity.physical_cores == 2);

    WriteText(proc / "self/status", "Cpus_allowed_list:\t0-3\n");
    WriteText(proc / "self/cgroup", "0::/slice/leaf\n");
    WriteText(cgroup / "slice/cpu.max", "200000 100000\n");
    const MachineResources quota =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(quota.physical_cores == 2);

    WriteText(cgroup / "slice/leaf/cpu.max", "max 100000\n");
    WriteText(cgroup / "cpu.max", "100000 100000\n");
    const MachineResources tightest =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(tightest.physical_cores == 1);

    WriteText(cgroup / "slice/cpu.max", "50000 100000\n");
    WriteText(cgroup / "cpu.max", "max 100000\n");
    const MachineResources fraction =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(fraction.physical_cores == 1);

    WriteText(proc / "self/cgroup", "not-a-cgroup-line\n1:cpuacct:/ignored\n2:cpu,cpuacct:/limited\n");
    WriteText(cgroup / "cpu/limited/cpu.cfs_quota_us", "300000\n");
    WriteText(cgroup / "cpu/limited/cpu.cfs_period_us", "100000\n");
    WriteText(cgroup / "cpu/cpu.cfs_quota_us", "-1\n");
    WriteText(cgroup / "cpu/cpu.cfs_period_us", "100000\n");
    const MachineResources v1 = nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(v1.physical_cores == 3);

    WriteText(proc / "self/cgroup", "0::/slice/leaf\n2:cpu:/limited\n");
    WriteText(cgroup / "slice/cpu.max", "400000 100000\n");
    const MachineResources both =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(both.physical_cores == 3);

    WriteText(proc / "self/status", "Cpus_allowed_list:\tnope\n");
    WriteText(proc / "self/cgroup", "0::/missing\n");
    const MachineResources ignored =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(ignored.physical_cores == 4);

    WriteText(proc / "self/status", "Cpus_allowed_list:\t0-1\n");
    WriteText(proc / "self/cgroup", "0::/stopped\n");
    WriteText(cgroup / "stopped/cpu.max", "0 100000\n");
    const MachineResources zero =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(zero.physical_cores == 1);

    WriteText(proc / "self/status", "Cpus_allowed_list:\t0-3\n");
    WriteText(proc / "self/cgroup", "0::/bad\n");
    WriteText(cgroup / "bad/cpu.max", "nope\n");
    const MachineResources malformed =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(malformed.physical_cores == 4);

    WriteText(cgroup / "bad/cpu.max", "100000\n");
    const MachineResources missing_period =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(missing_period.physical_cores == 4);

    std::filesystem::remove(proc / "self/cgroup");
    const MachineResources no_cgroup =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(no_cgroup.physical_cores == 4);

    WriteText(proc / "self/cgroup", "2:cpu:/broken\n");
    WriteText(cgroup / "cpu/broken/cpu.cfs_quota_us", "not-a-number\n");
    WriteText(cgroup / "cpu/broken/cpu.cfs_period_us", "100000\n");
    const MachineResources bad_quota =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(bad_quota.physical_cores == 4);

    WriteText(cgroup / "cpu/broken/cpu.cfs_quota_us", "200000\n");
    WriteText(cgroup / "cpu/broken/cpu.cfs_period_us", "0\n");
    const MachineResources zero_period =
        nesso::embed::ReadMachine({.proc_root = proc, .sys_root = sys, .cgroup_root = cgroup});
    CHECK(zero_period.physical_cores == 4);
}
