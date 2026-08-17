// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/vector_index.hpp"

namespace mach_core
{

VectorIndex::VectorIndex(const StorageEngine<float> &engine) noexcept
{
    if (!engine.isOpen() || engine.vectorData() == nullptr)
    {
        return;
    }
    m_view = FloatMdspan(engine.vectorData(), static_cast<std::size_t>(engine.getVectorCount()),
                         static_cast<std::size_t>(engine.getDimensions()));
}

} // namespace mach_core
