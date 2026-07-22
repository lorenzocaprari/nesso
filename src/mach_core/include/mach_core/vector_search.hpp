// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_VECTOR_SEARCH_HPP
#define MACH_CORE_VECTOR_SEARCH_HPP

#include "core_types.hpp"
#include "storage_engine.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace mach_core
{

template <SupportedScalar T> struct SearchResult
{
    uint64_t index;
    T score;
};

template <SupportedScalar T>
[[nodiscard]] std::expected<std::vector<SearchResult<T>>, EngineError>
searchTopKCosine(const StorageEngine<T> &engine, std::span<const T> query, size_t k);

} // namespace mach_core

#endif // MACH_CORE_VECTOR_SEARCH_HPP
