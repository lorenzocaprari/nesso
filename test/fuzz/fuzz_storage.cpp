// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE for details.

#include <core/storage_engine.hpp>
#include <core/vector_search.hpp>

#include <array>
#include <span>

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        return 1;
    }

    constexpr uint64_t dims = 2;
    core::StorageEngine<float> engine;
    if (!engine.createOrOpen(argv[1], dims))
    {
        return 0;
    }

    const std::array<float, 2> query{1.0F, 0.0F};
    (void)core::searchTopKCosine<float>(engine, std::span<const float>(query), 1);
    return 0;
}
