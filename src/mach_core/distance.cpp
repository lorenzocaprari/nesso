// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/distance.hpp"

#include <cmath>
#include <numeric>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace mach_core::math
{

template <SupportedScalar T>
std::expected<T, EngineError> CosineSimilarity::calculateScalar(std::span<const T> a, std::span<const T> b) noexcept
{
    if (a.size() != b.size()) [[unlikely]]
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (a.empty()) [[unlikely]]
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }

    T dotProduct = 0.0;
    T normA = 0.0;
    T normB = 0.0;

#pragma GCC ivdep
    for (size_t i = 0; i < a.size(); ++i)
    {
        dotProduct += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }

    if (normA == 0.0 || normB == 0.0) [[unlikely]]
    {
        return 0.0;
    }

    return dotProduct / (std::sqrt(normA) * std::sqrt(normB));
}

#if defined(__x86_64__) || defined(_M_X64)
__attribute__((target("avx2"))) static float hsum256(__m256 v) noexcept
{
    const __m128 low = _mm256_castps256_ps128(v);
    const __m128 high = _mm256_extractf128_ps(v, 1);
    __m128 sum = _mm_add_ps(low, high);
    sum = _mm_add_ps(sum, _mm_movehl_ps(sum, sum));
    sum = _mm_add_ss(sum, _mm_shuffle_ps(sum, sum, 1));
    return _mm_cvtss_f32(sum);
}

__attribute__((target("avx2"))) static float cosineAvx2(std::span<const float> a, std::span<const float> b) noexcept
{
    const size_t n = a.size();
    const float *pa = a.data();
    const float *pb = b.data();

    __m256 accDot = _mm256_setzero_ps();
    __m256 accA = _mm256_setzero_ps();
    __m256 accB = _mm256_setzero_ps();

    size_t i = 0;
    for (; i + 8 <= n; i += 8)
    {
        const __m256 va = _mm256_loadu_ps(pa + i);
        const __m256 vb = _mm256_loadu_ps(pb + i);
        accDot = _mm256_add_ps(accDot, _mm256_mul_ps(va, vb));
        accA = _mm256_add_ps(accA, _mm256_mul_ps(va, va));
        accB = _mm256_add_ps(accB, _mm256_mul_ps(vb, vb));
    }

    float dotProduct = hsum256(accDot);
    float normA = hsum256(accA);
    float normB = hsum256(accB);
    for (; i < n; ++i)
    {
        dotProduct += pa[i] * pb[i];
        normA += pa[i] * pa[i];
        normB += pb[i] * pb[i];
    }

    if (normA == 0.0f || normB == 0.0f)
    {
        return 0.0f;
    }
    return dotProduct / (std::sqrt(normA) * std::sqrt(normB));
}
#endif

template <SupportedScalar T>
std::expected<T, EngineError> CosineSimilarity::calculate(std::span<const T> a, std::span<const T> b) noexcept
{
    if constexpr (std::same_as<T, float>)
    {
#if defined(__x86_64__) || defined(_M_X64)
        if (a.size() == b.size() && !a.empty() && __builtin_cpu_supports("avx2"))
        {
            return cosineAvx2(a, b);
        }
#endif
    }
    return calculateScalar(a, b);
}

template std::expected<float, EngineError> CosineSimilarity::calculate<float>(std::span<const float>,
                                                                              std::span<const float>) noexcept;
template std::expected<double, EngineError> CosineSimilarity::calculate<double>(std::span<const double>,
                                                                                std::span<const double>) noexcept;
template std::expected<float, EngineError> CosineSimilarity::calculateScalar<float>(std::span<const float>,
                                                                                    std::span<const float>) noexcept;
template std::expected<double, EngineError>
CosineSimilarity::calculateScalar<double>(std::span<const double>, std::span<const double>) noexcept;

} // namespace mach_core::math
