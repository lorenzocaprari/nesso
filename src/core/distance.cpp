// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "core/core_types.hpp"
#include <concepts>
#include <core/distance.hpp>

#include <core/distance_kernels.hpp>
#include <platform/assert.hpp>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <span>
#include <string_view>

namespace nesso::core::math
{
namespace detail
{

template <SupportedScalar T> auto DotProductScalar(std::span<const T> a, std::span<const T> b) noexcept -> T
{
    NESSO_ASSERT(a.size() == b.size());
    T sum{};
    for (size_t i = 0; i < a.size(); ++i)
    {
        sum += a[i] * b[i];
    }
    return sum;
}

template <SupportedScalar T> auto L2SquaredDistanceScalar(std::span<const T> a, std::span<const T> b) noexcept -> T
{
    NESSO_ASSERT(a.size() == b.size());
    T sum{};
    for (size_t i = 0; i < a.size(); ++i)
    {
        const T delta = a[i] - b[i];
        sum += delta * delta;
    }
    return sum;
}

auto SelectDistanceKernel(std::string_view forced, bool avx2_fma) noexcept -> DistanceKernel
{
    if (forced == "scalar")
    {
        return DistanceKernel::Scalar;
    }
    // `avx2` stays on AVX2+FMA. It does not follow a wider automatic choice,
    // and it stays on scalar when this CPU cannot run those instructions.
    if (forced == "avx2")
    {
        if (avx2_fma)
        {
            return DistanceKernel::Avx2Fma;
        }
        return DistanceKernel::Scalar;
    }
    if (avx2_fma)
    {
        return DistanceKernel::Avx2Fma;
    }
    return DistanceKernel::Scalar;
}

[[nodiscard]] static auto HostSupportsAvx2Fma() noexcept -> bool
{
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__clang__) || defined(__GNUC__))
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#else
    return false;
#endif
}

auto SelectedKernel() noexcept -> DistanceKernel
{
    static const DistanceKernel KERNEL = [] noexcept -> DistanceKernel
    {
        // getenv is not thread-safe. The choice is fixed on first use; later
        // calls return the cached kernel and do not read the environment.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const char *env_value = std::getenv("NESSO_FORCE_KERNEL");
        const std::string_view forced = env_value == nullptr ? std::string_view{} : std::string_view{env_value};
        return SelectDistanceKernel(forced, HostSupportsAvx2Fma());
    }();
    return KERNEL;
}

auto DotProductKernel(DistanceKernel kernel, std::span<const float> a, std::span<const float> b) noexcept -> float
{
    NESSO_ASSERT(a.size() == b.size());
#ifdef __x86_64__
    if (kernel == DistanceKernel::Avx2Fma)
    {
        return DotProductAvx2Fma(a, b);
    }
#else
    static_cast<void>(kernel);
#endif
    return DotProductScalar<float>(a, b);
}

auto L2SquaredDistanceKernel(DistanceKernel kernel, std::span<const float> a, std::span<const float> b) noexcept
    -> float
{
    NESSO_ASSERT(a.size() == b.size());
#ifdef __x86_64__
    if (kernel == DistanceKernel::Avx2Fma)
    {
        return L2SquaredDistanceAvx2Fma(a, b);
    }
#else
    static_cast<void>(kernel);
#endif
    return L2SquaredDistanceScalar<float>(a, b);
}

template float DotProductScalar<float>(std::span<const float>, std::span<const float>) noexcept;
template double DotProductScalar<double>(std::span<const double>, std::span<const double>) noexcept;
template float L2SquaredDistanceScalar<float>(std::span<const float>, std::span<const float>) noexcept;
template double L2SquaredDistanceScalar<double>(std::span<const double>, std::span<const double>) noexcept;

} // namespace detail

template <SupportedScalar T>
// cppcheck-suppress unusedFunction
auto CosineSimilarity::Calculate(std::span<const T> a, std::span<const T> b) noexcept -> std::expected<T, EngineError>
{
    if (a.size() != b.size()) [[unlikely]]
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (a.empty()) [[unlikely]]
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }

    T dot_product{};
    T norm_a{};
    T norm_b{};

    for (size_t i = 0; i < a.size(); ++i)
    {
        dot_product += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    if (norm_a == T{} || norm_b == T{}) [[unlikely]]
    {
        return T{};
    }

    return dot_product / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

template std::expected<float, EngineError> CosineSimilarity::Calculate<float>(std::span<const float>,
                                                                              std::span<const float>) noexcept;
template std::expected<double, EngineError> CosineSimilarity::Calculate<double>(std::span<const double>,
                                                                                std::span<const double>) noexcept;

template <SupportedScalar T>
auto DistanceMetrics::DotProduct(std::span<const T> a, std::span<const T> b) noexcept -> std::expected<T, EngineError>
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
        return detail::DotProductKernel(detail::SelectedKernel(), a, b);
    }
    return detail::DotProductScalar(a, b);
}

template std::expected<float, EngineError> DistanceMetrics::DotProduct<float>(std::span<const float>,
                                                                              std::span<const float>) noexcept;
template std::expected<double, EngineError> DistanceMetrics::DotProduct<double>(std::span<const double>,
                                                                                std::span<const double>) noexcept;

template <SupportedScalar T>
// cppcheck-suppress unusedFunction
auto DistanceMetrics::L2SquaredDistance(std::span<const T> a, std::span<const T> b) noexcept
    -> std::expected<T, EngineError>
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
        return detail::L2SquaredDistanceKernel(detail::SelectedKernel(), a, b);
    }
    return detail::L2SquaredDistanceScalar(a, b);
}

template std::expected<float, EngineError> DistanceMetrics::L2SquaredDistance<float>(std::span<const float>,
                                                                                     std::span<const float>) noexcept;
template std::expected<double, EngineError>
    DistanceMetrics::L2SquaredDistance<double>(std::span<const double>, std::span<const double>) noexcept;

} // namespace nesso::core::math
