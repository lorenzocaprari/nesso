// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/session_policy.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

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

TEST_CASE("the default int8 pool is capped by estimated session memory", "[session-policy]")
{
    const auto sessionsWith = [](std::uintmax_t available)
    {
        const MachineResources machine{.physicalCores = 16, .logicalThreads = 32, .memoryLimitBytes = available};
        return nesso::embed::sessionPolicy(true, 0, machine);
    };
    CHECK(sessionsWith(8 * GIB).sessions == 8);
    CHECK(sessionsWith(4 * GIB).sessions == 4);
    CHECK(sessionsWith(2 * GIB).sessions == 2);
    CHECK(sessionsWith(1 * GIB).sessions == 1);
    CHECK(sessionsWith(1 * GIB).memoryCap == 1);
    CHECK(sessionsWith(100).sessions == 1);
    CHECK(sessionsWith(100).memoryCap == 1);
    CHECK(sessionsWith(64 * GIB).sessions == 16);
    CHECK(sessionsWith(64 * GIB).memoryCap == 68);
    CHECK(sessionsWith(2 * INT8_SESSION_BYTES * 3).sessions == 3);
    CHECK(sessionsWith(8 * GIB).intraOpThreads == 1);
    CHECK(sessionsWith(8 * GIB).quantized);
}

TEST_CASE("an unknown memory limit leaves the pool at the core count", "[session-policy]")
{
    const MachineResources machine{.physicalCores = 6, .logicalThreads = 12, .memoryLimitBytes = 0};
    const SessionPolicy policy = nesso::embed::sessionPolicy(true, 0, machine);
    CHECK(policy.sessions == 6);
    CHECK(policy.memoryCap == 0);
    CHECK(nesso::embed::overrideWarning(nesso::embed::sessionPolicy(true, 64, machine), 64).empty());

    const MachineResources noCores{.physicalCores = 0, .logicalThreads = 0, .memoryLimitBytes = 0};
    CHECK(nesso::embed::sessionPolicy(true, 0, noCores).sessions == 1);
    CHECK(nesso::embed::sessionPolicy(false, 0, noCores).intraOpThreads == 1);
}

TEST_CASE("an explicit thread count overrides the cap and warns above it", "[session-policy]")
{
    const MachineResources machine{.physicalCores = 8, .logicalThreads = 16, .memoryLimitBytes = 2 * GIB};
    const SessionPolicy capped = nesso::embed::sessionPolicy(true, 0, machine);
    CHECK(capped.sessions == 2);
    CHECK(nesso::embed::overrideWarning(capped, 0).empty());

    const SessionPolicy within = nesso::embed::sessionPolicy(true, 2, machine);
    CHECK(within.sessions == 2);
    CHECK(nesso::embed::overrideWarning(within, 2).empty());

    const SessionPolicy above = nesso::embed::sessionPolicy(true, 6, machine);
    CHECK(above.sessions == 6);
    CHECK(above.intraOpThreads == 1);
    CHECK(above.memoryCap == 2);
    CHECK(nesso::embed::overrideWarning(above, 6) ==
          "Warning: 6 int8 sessions exceed the estimated memory limit of 2; continuing with 6.");
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
}

TEST_CASE("the policy text shows the session counts and the memory cap", "[session-policy]")
{
    const MachineResources capped{.physicalCores = 8, .logicalThreads = 16, .memoryLimitBytes = 3 * GIB};
    CHECK(nesso::embed::sessionPolicyText(0, capped) ==
          "int8: 3 single-thread sessions (estimated memory cap 3)\nfp32: 1 session, 16 intra-op threads");
    CHECK(nesso::embed::sessionPolicyText(2, capped) ==
          "int8: 2 single-thread sessions (estimated memory cap 3)\nfp32: 1 session, 2 intra-op threads");

    const MachineResources unknown{.physicalCores = 1, .logicalThreads = 2, .memoryLimitBytes = 0};
    CHECK(nesso::embed::sessionPolicyText(0, unknown) ==
          "int8: 1 single-thread session\nfp32: 1 session, 2 intra-op threads");
}
