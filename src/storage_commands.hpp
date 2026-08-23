// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_STORAGE_COMMANDS_HPP
#define NESSO_STORAGE_COMMANDS_HPP

#include <core/storage_engine.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace nesso::commands
{

int runInit(core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions);

int runIndex(core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions,
             const std::string &inputFile);

int runSearch(core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions,
              const std::string &queryFile, size_t topK);

} // namespace nesso::commands

#endif // NESSO_STORAGE_COMMANDS_HPP
