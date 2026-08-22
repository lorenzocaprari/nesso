// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef MACH1_STORAGE_COMMANDS_HPP
#define MACH1_STORAGE_COMMANDS_HPP

#include <mach_core/storage_engine.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace mach1::commands
{

int runInit(mach_core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions);

int runIndex(mach_core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions,
             const std::string &inputFile);

int runSearch(mach_core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions,
              const std::string &queryFile, size_t topK);

} // namespace mach1::commands

#endif // MACH1_STORAGE_COMMANDS_HPP
