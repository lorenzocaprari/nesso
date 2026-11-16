// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <platform/assert.hpp>

#include <cstdio>
#include <cstdlib>

extern "C"
{
    // Present only in a --coverage link. A weak reference is null otherwise, so the
    // trap can flush arc counts before abort without a dependency on libgcov.
    // NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp, readability-identifier-naming)
    [[gnu::weak]] void __gcov_dump() noexcept;
    // NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp, readability-identifier-naming)
}

namespace nesso::platform
{

[[noreturn]] void AssertFail(const char *condition, unsigned line, const char *file) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    const int written = std::fprintf(stderr, "nesso: assertion failed: %s at %s:%u\n", condition, file, line);
    if (written >= 0)
    {
        static_cast<void>(std::fflush(stderr));
    }
    // One statement so the arc counter for this line is stored before abort.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-comma-operator, readability-suspicious-call-argument)
    (__gcov_dump != nullptr ? __gcov_dump() : static_cast<void>(0)), std::abort();
}

} // namespace nesso::platform
