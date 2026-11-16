// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/distance_kernels.hpp>
#include <platform/assert.hpp>

#include <cstddef>
#include <immintrin.h>
#include <span>

namespace nesso::core::math::detail
{
namespace
{

// This translation unit is compiled for the baseline ISA. The attribute makes
// the AVX2 and FMA intrinsics legal, and it stops LTO from inlining these
// bodies into a generic caller. HorizontalSum uses the intrinsics. FloatAddr
// is always_inline into the kernels and carries the same target.
#ifdef __GNUC__
#define NESSO_TARGET_AVX2_FMA __attribute__((target("avx2,fma")))
#else
#define NESSO_TARGET_AVX2_FMA
#endif

// NOLINTBEGIN(portability-simd-intrinsics)
NESSO_TARGET_AVX2_FMA float HorizontalSum(__m256 value) noexcept
{
    const __m128 low = _mm256_castps256_ps128(value);
    const __m128 high = _mm256_extractf128_ps(value, 1);
    __m128 sum = _mm_add_ps(low, high);
    sum = _mm_hadd_ps(sum, sum);
    sum = _mm_hadd_ps(sum, sum);
    return _mm_cvtss_f32(sum);
}

[[nodiscard, gnu::always_inline]] NESSO_TARGET_AVX2_FMA inline const float *FloatAddr(std::span<const float> values,
                                                                                      size_t index) noexcept
{
    return values.subspan(index).data();
}

} // namespace

NESSO_TARGET_AVX2_FMA float DotProductAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept
{
    NESSO_ASSERT(a.size() == b.size());
    __m256 sum0 = _mm256_setzero_ps();
    __m256 sum1 = _mm256_setzero_ps();
    __m256 sum2 = _mm256_setzero_ps();
    __m256 sum3 = _mm256_setzero_ps();
    size_t index = 0;
    for (; index + 32 <= a.size(); index += 32)
    {
        sum0 = _mm256_fmadd_ps(_mm256_loadu_ps(FloatAddr(a, index)), _mm256_loadu_ps(FloatAddr(b, index)), sum0);
        sum1 =
            _mm256_fmadd_ps(_mm256_loadu_ps(FloatAddr(a, index + 8)), _mm256_loadu_ps(FloatAddr(b, index + 8)), sum1);
        sum2 =
            _mm256_fmadd_ps(_mm256_loadu_ps(FloatAddr(a, index + 16)), _mm256_loadu_ps(FloatAddr(b, index + 16)), sum2);
        sum3 =
            _mm256_fmadd_ps(_mm256_loadu_ps(FloatAddr(a, index + 24)), _mm256_loadu_ps(FloatAddr(b, index + 24)), sum3);
    }
    for (; index + 8 <= a.size(); index += 8)
    {
        sum0 = _mm256_fmadd_ps(_mm256_loadu_ps(FloatAddr(a, index)), _mm256_loadu_ps(FloatAddr(b, index)), sum0);
    }

    sum0 = _mm256_add_ps(sum0, sum1);
    sum2 = _mm256_add_ps(sum2, sum3);
    float result = HorizontalSum(_mm256_add_ps(sum0, sum2));
    for (; index < a.size(); ++index)
    {
        result += a[index] * b[index];
    }
    return result;
}

NESSO_TARGET_AVX2_FMA float L2SquaredDistanceAvx2Fma(std::span<const float> a, std::span<const float> b) noexcept
{
    NESSO_ASSERT(a.size() == b.size());
    __m256 sum0 = _mm256_setzero_ps();
    __m256 sum1 = _mm256_setzero_ps();
    __m256 sum2 = _mm256_setzero_ps();
    __m256 sum3 = _mm256_setzero_ps();
    size_t index = 0;
    for (; index + 32 <= a.size(); index += 32)
    {
        const __m256 delta0 = _mm256_sub_ps(_mm256_loadu_ps(FloatAddr(a, index)), _mm256_loadu_ps(FloatAddr(b, index)));
        const __m256 delta1 =
            _mm256_sub_ps(_mm256_loadu_ps(FloatAddr(a, index + 8)), _mm256_loadu_ps(FloatAddr(b, index + 8)));
        const __m256 delta2 =
            _mm256_sub_ps(_mm256_loadu_ps(FloatAddr(a, index + 16)), _mm256_loadu_ps(FloatAddr(b, index + 16)));
        const __m256 delta3 =
            _mm256_sub_ps(_mm256_loadu_ps(FloatAddr(a, index + 24)), _mm256_loadu_ps(FloatAddr(b, index + 24)));
        sum0 = _mm256_fmadd_ps(delta0, delta0, sum0);
        sum1 = _mm256_fmadd_ps(delta1, delta1, sum1);
        sum2 = _mm256_fmadd_ps(delta2, delta2, sum2);
        sum3 = _mm256_fmadd_ps(delta3, delta3, sum3);
    }
    for (; index + 8 <= a.size(); index += 8)
    {
        const __m256 delta = _mm256_sub_ps(_mm256_loadu_ps(FloatAddr(a, index)), _mm256_loadu_ps(FloatAddr(b, index)));
        sum0 = _mm256_fmadd_ps(delta, delta, sum0);
    }

    sum0 = _mm256_add_ps(sum0, sum1);
    sum2 = _mm256_add_ps(sum2, sum3);
    float result = HorizontalSum(_mm256_add_ps(sum0, sum2));
    for (; index < a.size(); ++index)
    {
        const float delta = a[index] - b[index];
        result += delta * delta;
    }
    return result;
}
// NOLINTEND(portability-simd-intrinsics)

#undef NESSO_TARGET_AVX2_FMA

} // namespace nesso::core::math::detail
