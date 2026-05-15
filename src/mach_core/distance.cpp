// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "include/mach_core/distance.hpp"
#include <cmath>
#include <numeric>

namespace mach_core::math
{

template <SupportedScalar T>
std::expected<T, EngineError> CosineSimilarity::calculate(std::span<const T> a, std::span<const T> b) noexcept
{
    // 1. Safety Gates
    if (a.size() != b.size()) [[unlikely]]
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (a.empty()) [[unlikely]]
    {
        return std::unexpected(EngineError::DatabaseNotInitialized); // Or a specific EmptyVector
                                                                     // error
    }

    T dotProduct = 0.0;
    T normA = 0.0;
    T normB = 0.0;

// 2. The Hot Loop
// The GCC ivdep pragma tells the compiler it is safe to vectorize
// this loop using SIMD (AVX2/AVX-512) because the memory arrays do not overlap.
#pragma GCC ivdep
    for (size_t i = 0; i < a.size(); ++i)
    {
        dotProduct += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }

    // 3. Mathematical Safety
    if (normA == 0.0 || normB == 0.0) [[unlikely]]
    {
        return 0.0; // Prevent division by zero if a zero-vector is passed
    }

    // 4. Final Calculation
    return dotProduct / (std::sqrt(normA) * std::sqrt(normB));
}

// ============================================================================
// EXPLICIT TEMPLATE INSTANTIATIONS
// ============================================================================
// This forces the compiler to generate the binary code for these specific
// types right now, so they can be linked to main.cpp without needing the
// implementation in the header file.
template std::expected<float, EngineError> CosineSimilarity::calculate<float>(std::span<const float>,
                                                                              std::span<const float>) noexcept;
template std::expected<double, EngineError> CosineSimilarity::calculate<double>(std::span<const double>,
                                                                                std::span<const double>) noexcept;

} // namespace mach_core::math
