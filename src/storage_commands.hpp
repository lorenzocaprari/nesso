// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_STORAGE_COMMANDS_HPP
#define NESSO_STORAGE_COMMANDS_HPP

#include <nesso/nesso.hpp>

namespace CLI
{
class App;
} // namespace CLI

namespace nesso::commands
{

int runInit(const Nesso &nesso, const StoreInitRequest &request);

int runIndex(const Nesso &nesso, const StoreIngestRequest &request);

int runSearch(const Nesso &nesso, const StoreSearchRequest &request);

void addStoreCommand(CLI::App &app);

} // namespace nesso::commands

#endif // NESSO_STORAGE_COMMANDS_HPP
