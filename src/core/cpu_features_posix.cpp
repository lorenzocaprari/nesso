// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/distance_kernels.hpp>

namespace nesso::core::math::detail
{

auto HostSupportsAvx2Fma() noexcept -> bool
{
#ifdef __x86_64__
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#else
    return false;
#endif
}

} // namespace nesso::core::math::detail
