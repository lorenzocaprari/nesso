// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_RRF_HPP
#define MACH_CORE_RRF_HPP

#include "vector_search.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace mach_core
{

inline constexpr int kDefaultRrfK = 60;

[[nodiscard]] std::vector<SearchResult<float>> fuseRrf(std::span<const SearchResult<float>> semantic,
                                                       std::span<const SearchResult<float>> lexical, size_t k,
                                                       int rrfK = kDefaultRrfK);

} // namespace mach_core

#endif // MACH_CORE_RRF_HPP
