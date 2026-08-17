// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
//
// Standalone AFL++ fuzz harness for StorageEngine + searchTopKCosine.
// Intentionally avoids main.cpp / LogIndex / CLI11 so the fuzz target
// remains stable across CLI refactors.
#include <mach_core/storage_engine.hpp>
#include <mach_core/vector_search.hpp>

#include <array>
#include <span>

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        return 1;
    }

    mach_core::StorageEngine<float> engine;
    // Fuzz the DB-open path: AFL mutates the file at argv[1].
    // dims=2 keeps the query cheap; corrupt headers / dims are the attack surface.
    engine.open(argv[1], 2);
    if (engine.isOpen())
    {
        std::array<float, 2> query{1.0F, 0.0F};
        mach_core::searchTopKCosine(engine, std::span<const float>(query), 1);
    }
    return 0;
}
