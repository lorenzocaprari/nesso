// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/core/distance.hpp"

#include "distance_kernels.hpp"

#include <cmath>

namespace core::math
{
namespace detail
{

template <SupportedScalar T> T dotProductScalar(std::span<const T> a, std::span<const T> b) noexcept
{
    T sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        sum += a[i] * b[i];
    }
    return sum;
}

template <SupportedScalar T> T l2SquaredDistanceScalar(std::span<const T> a, std::span<const T> b) noexcept
{
    T sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const T delta = a[i] - b[i];
        sum += delta * delta;
    }
    return sum;
}

DistanceKernel selectedKernel() noexcept
{
    static const DistanceKernel kernel = []
    {
#ifdef _MSC_VER
        // Phase 6 selects AVX2 with __cpuidex and _xgetbv under /arch:AVX2.
        return DistanceKernel::Scalar;
#elifdef __x86_64__
        if (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma"))
        {
            return DistanceKernel::Avx2Fma;
        }
        return DistanceKernel::Scalar;
#else
        return DistanceKernel::Scalar;
#endif
    }();
    return kernel;
}

float dotProductKernel(DistanceKernel kernel, std::span<const float> a, std::span<const float> b) noexcept
{
#ifdef __x86_64__
    if (kernel == DistanceKernel::Avx2Fma)
    {
        return dotProductAvx2Fma(a, b);
    }
#else
    (void)kernel;
#endif
    return dotProductScalar<float>(a, b);
}

float l2SquaredDistanceKernel(DistanceKernel kernel, std::span<const float> a, std::span<const float> b) noexcept
{
#ifdef __x86_64__
    if (kernel == DistanceKernel::Avx2Fma)
    {
        return l2SquaredDistanceAvx2Fma(a, b);
    }
#else
    (void)kernel;
#endif
    return l2SquaredDistanceScalar<float>(a, b);
}

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

    if constexpr (std::same_as<T, float>)
    {
        return detail::dotProductKernel(detail::selectedKernel(), a, b);
    }
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

    if constexpr (std::same_as<T, float>)
    {
        return detail::l2SquaredDistanceKernel(detail::selectedKernel(), a, b);
    }
    return detail::l2SquaredDistanceScalar(a, b);
}

template std::expected<float, EngineError> DistanceMetrics::l2SquaredDistance<float>(std::span<const float>,
                                                                                     std::span<const float>) noexcept;
template std::expected<double, EngineError>
    DistanceMetrics::l2SquaredDistance<double>(std::span<const double>, std::span<const double>) noexcept;

} // namespace core::math
