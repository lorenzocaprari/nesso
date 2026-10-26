#include <catch2/catch_all.hpp>
#include <embed/amx_guard.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>
#endif

TEST_CASE("the AMX guard refuses tile data for the rest of the process", "[amx-guard]")
{
    const bool first = nesso::embed::denyAmxTiles();
    const bool second = nesso::embed::denyAmxTiles();
    REQUIRE(first);
    REQUIRE(second);
#if defined(__linux__) && defined(__x86_64__)
    constexpr long kArchGetXcompPerm = 0x1022;
    constexpr long kArchReqXcompPerm = 0x1023;
    constexpr long kArchGetFs = 0x1003;
    constexpr unsigned long kXtiledataBit = 1UL << 18U;
    errno = 0;
    const bool refused = syscall(SYS_arch_prctl, kArchReqXcompPerm, 18L) == -1 && errno == EPERM;
    unsigned long mask = 0;
    const bool granted = syscall(SYS_arch_prctl, kArchGetXcompPerm, &mask) == 0 && (mask & kXtiledataBit) != 0;
    // Other arch_prctl calls still pass the filter.
    unsigned long fsBase = 0;
    const bool otherCallsPass = syscall(SYS_arch_prctl, kArchGetFs, &fsBase) == 0;
    CHECK(refused);
    CHECK_FALSE(granted);
    CHECK(otherCallsPass);
#endif
}
