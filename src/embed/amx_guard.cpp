// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/amx_guard.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace nesso::embed
{

#if defined(__linux__) && defined(__x86_64__)

// ONNX Runtime 1.18 selects its AMX int8 GEMM whenever the kernel grants
// ARCH_REQ_XCOMP_PERM for tile data. Some KVM guests grant it but do not save
// the tiles on a context switch, so a preempted GEMM returns wrong numbers.
// That made int8 embeddings change from run to run under CPU load. Integer
// GEMM is exact, so the VNNI and AVX2 kernels return the same bytes AMX does
// on a sound host. Refusing the request is the only switch ORT 1.18 reads.
static constexpr std::uint64_t GET_TILE_PERMISSION = 0x1022;
static constexpr std::uint32_t REQUEST_TILE_PERMISSION = 0x1023;
static constexpr std::uint64_t TILE_DATA_FEATURE = 18;

static bool tilePermissionGranted()
{
    // mask stays 0 when the kernel predates the call.
    std::uint64_t mask = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    static_cast<void>(syscall(SYS_arch_prctl, GET_TILE_PERMISSION, &mask));
    return (mask & (std::uint64_t{1} << TILE_DATA_FEATURE)) != 0;
}

static bool installRefusal()
{
    // Return EPERM for arch_prctl(ARCH_REQ_XCOMP_PERM, ...) and allow every
    // other call. TSYNC applies the filter to threads that already exist.
    std::array<sock_filter, 8> program{{
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_arch_prctl, 0, 2),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, REQUEST_TILE_PERMISSION, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
    }};
    sock_fprog fprog{.len = static_cast<std::uint16_t>(program.size()), .filter = program.data()};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
    {
        return false;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    return syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &fprog) == 0;
}

bool denyAmxTiles()
{
    static const bool denied = !tilePermissionGranted() && installRefusal();
    return denied;
}

#else

bool denyAmxTiles() { return true; }

#endif

} // namespace nesso::embed
