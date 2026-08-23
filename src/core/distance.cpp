// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/core/distance.hpp"

#include <cmath>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace core::math
{
namespace detail
{

template <SupportedScalar T> T dotProductScalar(std::span<const T> a, std::span<const T> b) noexcept
{
    T sum = 0.0;
#pragma GCC ivdep
    for (size_t i = 0; i < a.size(); ++i)
    {
        sum += a[i] * b[i];
    }
    return sum;
}

template <SupportedScalar T> T l2SquaredDistanceScalar(std::span<const T> a, std::span<const T> b) noexcept
{
    T sum = 0.0;
#pragma GCC ivdep
    for (size_t i = 0; i < a.size(); ++i)
    {
        const T delta = a[i] - b[i];
        sum += delta * delta;
    }
    return sum;
}

#ifdef __AVX2__
// NOLINTBEGIN(portability-simd-intrinsics)
static float horizontalSum(__m256 value) noexcept
{
    const __m128 low = _mm256_castps256_ps128(value);
    const __m128 high = _mm256_extractf128_ps(value, 1);
    __m128 sum = _mm_add_ps(low, high);
    sum = _mm_hadd_ps(sum, sum);
    sum = _mm_hadd_ps(sum, sum);
    return _mm_cvtss_f32(sum);
}

float dotProductAvx2(std::span<const float> a, std::span<const float> b) noexcept
{
    __m256 sum = _mm256_setzero_ps();
    size_t index = 0;
    for (; index + 8 <= a.size(); index += 8)
    {
        const __m256 left = _mm256_loadu_ps(a.data() + index);
        const __m256 right = _mm256_loadu_ps(b.data() + index);
        sum = _mm256_add_ps(sum, _mm256_mul_ps(left, right));
    }

    float result = horizontalSum(sum);
    for (; index < a.size(); ++index)
    {
        result += a[index] * b[index];
    }
    return result;
}

float l2SquaredDistanceAvx2(std::span<const float> a, std::span<const float> b) noexcept
{
    __m256 sum = _mm256_setzero_ps();
    size_t index = 0;
    for (; index + 8 <= a.size(); index += 8)
    {
        const __m256 left = _mm256_loadu_ps(a.data() + index);
        const __m256 right = _mm256_loadu_ps(b.data() + index);
        const __m256 delta = _mm256_sub_ps(left, right);
        sum = _mm256_add_ps(sum, _mm256_mul_ps(delta, delta));
    }

    float result = horizontalSum(sum);
    for (; index < a.size(); ++index)
    {
        const float delta = a[index] - b[index];
        result += delta * delta;
    }
    return result;
}
// NOLINTEND(portability-simd-intrinsics)
#endif

template float dotProductScalar<float>(std::span<const float>, std::span<const float>) noexcept;
template double dotProductScalar<double>(std::span<const double>, std::span<const double>) noexcept;
template float l2SquaredDistanceScalar<float>(std::span<const float>, std::span<const float>) noexcept;
template double l2SquaredDistanceScalar<double>(std::span<const double>, std::span<const double>) noexcept;

} // namespace detail

template <SupportedScalar T>
std::expected<T, EngineError> CosineSimilarity::calculate(std::span<const T> a, std::span<const T> b) noexcept
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

template std::expected<float, EngineError> CosineSimilarity::calculate<float>(std::span<const float>,
                                                                              std::span<const float>) noexcept;
template std::expected<double, EngineError> CosineSimilarity::calculate<double>(std::span<const double>,
                                                                                std::span<const double>) noexcept;

template <SupportedScalar T>
std::expected<T, EngineError> DistanceMetrics::dotProduct(std::span<const T> a, std::span<const T> b) noexcept
{
    if (a.size() != b.size()) [[unlikely]]
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (a.empty()) [[unlikely]]
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }

#ifdef __AVX2__
    if constexpr (std::same_as<T, float>)
    {
        return detail::dotProductAvx2(a, b);
    }
#endif
    return detail::dotProductScalar(a, b);
}

template std::expected<float, EngineError> DistanceMetrics::dotProduct<float>(std::span<const float>,
                                                                              std::span<const float>) noexcept;
template std::expected<double, EngineError> DistanceMetrics::dotProduct<double>(std::span<const double>,
                                                                                std::span<const double>) noexcept;

template <SupportedScalar T>
std::expected<T, EngineError> DistanceMetrics::l2SquaredDistance(std::span<const T> a, std::span<const T> b) noexcept
{
    if (a.size() != b.size()) [[unlikely]]
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (a.empty()) [[unlikely]]
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }

#ifdef __AVX2__
    if constexpr (std::same_as<T, float>)
    {
        return detail::l2SquaredDistanceAvx2(a, b);
    }
#endif
    return detail::l2SquaredDistanceScalar(a, b);
}

template std::expected<float, EngineError> DistanceMetrics::l2SquaredDistance<float>(std::span<const float>,
                                                                                     std::span<const float>) noexcept;
template std::expected<double, EngineError>
    DistanceMetrics::l2SquaredDistance<double>(std::span<const double>, std::span<const double>) noexcept;

} // namespace core::math
