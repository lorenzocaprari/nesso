// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "distance_kernels.hpp"

#include <immintrin.h>

namespace core::math::detail
{
namespace
{

// Keep the ISA on this function so LTO cannot inline AVX2 into a generic caller.
#ifdef __GNUC__
#define NESSO_TARGET_AVX2_FMA __attribute__((target("avx2,fma")))
#else
#define NESSO_TARGET_AVX2_FMA
#endif

// NOLINTBEGIN(portability-simd-intrinsics)
NESSO_TARGET_AVX2_FMA float horizontalSum(__m256 value) noexcept
{
    const __m128 low = _mm256_castps256_ps128(value);
    const __m128 high = _mm256_extractf128_ps(value, 1);
    __m128 sum = _mm_add_ps(low, high);
    sum = _mm_hadd_ps(sum, sum);
    sum = _mm_hadd_ps(sum, sum);
    return _mm_cvtss_f32(sum);
}

} // namespace

NESSO_TARGET_AVX2_FMA float dotProductAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept
{
    __m256 sum0 = _mm256_setzero_ps();
    __m256 sum1 = _mm256_setzero_ps();
    __m256 sum2 = _mm256_setzero_ps();
    __m256 sum3 = _mm256_setzero_ps();
    size_t index = 0;
    for (; index + 32 <= a.size(); index += 32)
    {
        sum0 = _mm256_fmadd_ps(_mm256_loadu_ps(a.data() + index), _mm256_loadu_ps(b.data() + index), sum0);
        sum1 = _mm256_fmadd_ps(_mm256_loadu_ps(a.data() + index + 8), _mm256_loadu_ps(b.data() + index + 8), sum1);
        sum2 = _mm256_fmadd_ps(_mm256_loadu_ps(a.data() + index + 16), _mm256_loadu_ps(b.data() + index + 16), sum2);
        sum3 = _mm256_fmadd_ps(_mm256_loadu_ps(a.data() + index + 24), _mm256_loadu_ps(b.data() + index + 24), sum3);
    }
    for (; index + 8 <= a.size(); index += 8)
    {
        sum0 = _mm256_fmadd_ps(_mm256_loadu_ps(a.data() + index), _mm256_loadu_ps(b.data() + index), sum0);
    }

    sum0 = _mm256_add_ps(sum0, sum1);
    sum2 = _mm256_add_ps(sum2, sum3);
    float result = horizontalSum(_mm256_add_ps(sum0, sum2));
    for (; index < a.size(); ++index)
    {
        result += a[index] * b[index];
    }
    return result;
}

NESSO_TARGET_AVX2_FMA float l2SquaredDistanceAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept
{
    __m256 sum0 = _mm256_setzero_ps();
    __m256 sum1 = _mm256_setzero_ps();
    __m256 sum2 = _mm256_setzero_ps();
    __m256 sum3 = _mm256_setzero_ps();
    size_t index = 0;
    for (; index + 32 <= a.size(); index += 32)
    {
        const __m256 delta0 = _mm256_sub_ps(_mm256_loadu_ps(a.data() + index), _mm256_loadu_ps(b.data() + index));
        const __m256 delta1 =
            _mm256_sub_ps(_mm256_loadu_ps(a.data() + index + 8), _mm256_loadu_ps(b.data() + index + 8));
        const __m256 delta2 =
            _mm256_sub_ps(_mm256_loadu_ps(a.data() + index + 16), _mm256_loadu_ps(b.data() + index + 16));
        const __m256 delta3 =
            _mm256_sub_ps(_mm256_loadu_ps(a.data() + index + 24), _mm256_loadu_ps(b.data() + index + 24));
        sum0 = _mm256_fmadd_ps(delta0, delta0, sum0);
        sum1 = _mm256_fmadd_ps(delta1, delta1, sum1);
        sum2 = _mm256_fmadd_ps(delta2, delta2, sum2);
        sum3 = _mm256_fmadd_ps(delta3, delta3, sum3);
    }
    for (; index + 8 <= a.size(); index += 8)
    {
        const __m256 delta = _mm256_sub_ps(_mm256_loadu_ps(a.data() + index), _mm256_loadu_ps(b.data() + index));
        sum0 = _mm256_fmadd_ps(delta, delta, sum0);
    }

    sum0 = _mm256_add_ps(sum0, sum1);
    sum2 = _mm256_add_ps(sum2, sum3);
    float result = horizontalSum(_mm256_add_ps(sum0, sum2));
    for (; index < a.size(); ++index)
    {
        const float delta = a[index] - b[index];
        result += delta * delta;
    }
    return result;
}
// NOLINTEND(portability-simd-intrinsics)

#undef NESSO_TARGET_AVX2_FMA

} // namespace core::math::detail
