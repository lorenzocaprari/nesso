// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_BM25_INDEX_HPP
#define MACH_CORE_BM25_INDEX_HPP

#include "vector_search.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mach_core
{

[[nodiscard]] std::vector<std::string> lexicalTokens(std::string_view text);

class Bm25Index
{
  public:
    void clear() noexcept;
    void addDocument(uint64_t docId, std::string_view text);
    [[nodiscard]] std::vector<SearchResult<float>> search(std::string_view query, size_t k) const;

  private:
    std::unordered_map<std::string, std::vector<uint32_t>> m_postings;
    std::unordered_map<uint64_t, uint32_t> m_docLength;
    uint64_t m_docCount{0};
    double m_avgDl{0.0};
};

} // namespace mach_core

#endif // MACH_CORE_BM25_INDEX_HPP
