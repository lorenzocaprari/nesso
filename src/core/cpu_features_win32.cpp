// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/distance_kernels.hpp>

#include <array>
#include <intrin.h>
#include <windows.h>

namespace nesso::core::math::detail
{

// __builtin_cpu_supports needs __cpu_model from compiler-rt, which the
// clang-cl link does not pull in, so ask Windows and CPUID instead.
auto HostSupportsAvx2Fma() noexcept -> bool
{
#if defined(_M_X64)
    // Also false when the OS does not save the YMM registers.
    if (IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) == 0)
    {
        return false;
    }
    std::array<int, 4> registers{};
    __cpuid(registers.data(), 1);
    constexpr int fma_bit = 1 << 12; // CPUID leaf 1, ECX
    return (registers[2] & fma_bit) != 0;
#else
    return false;
#endif
}

} // namespace nesso::core::math::detail
