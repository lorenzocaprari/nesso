// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_VECTOR_INDEX_HPP
#define MACH_CORE_VECTOR_INDEX_HPP

#include "storage_engine.hpp"

#include <cstddef>

namespace mach_core
{

/// 2D view over a packed float arena. libstdc++ 15 on this toolchain has no
/// <mdspan>; the subscript matches std::mdspan's `m[i, j]` operator.
class FloatMdspan
{
  public:
    FloatMdspan() noexcept = default;
    FloatMdspan(const float *data, std::size_t rows, std::size_t cols) noexcept
        : m_data(data), m_rows(rows), m_cols(cols)
    {
    }

    [[nodiscard]] const float &operator[](std::size_t row, std::size_t col) const noexcept
    {
        return m_data[row * m_cols + col];
    }
    [[nodiscard]] std::size_t extent(std::size_t dim) const noexcept { return dim == 0 ? m_rows : m_cols; }
    [[nodiscard]] const float *data() const noexcept { return m_data; }

  private:
    const float *m_data{nullptr};
    std::size_t m_rows{0};
    std::size_t m_cols{0};
};

class VectorIndex
{
  public:
    explicit VectorIndex(const StorageEngine<float> &engine) noexcept;

    [[nodiscard]] FloatMdspan asMdspan() const noexcept { return m_view; }
    [[nodiscard]] std::size_t rows() const noexcept { return m_view.extent(0); }
    [[nodiscard]] std::size_t cols() const noexcept { return m_view.extent(1); }

  private:
    FloatMdspan m_view;
};

} // namespace mach_core

#endif // MACH_CORE_VECTOR_INDEX_HPP
