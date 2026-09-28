// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_GREP_COMMAND_HPP
#define NESSO_GREP_COMMAND_HPP

#include <nesso/nesso.hpp>

namespace CLI
{
class App;
} // namespace CLI

namespace nesso::commands
{

int runGrep(const Nesso &nesso, const GrepRequest &request);

void addGrepCommand(CLI::App &app);

} // namespace nesso::commands

#endif // NESSO_GREP_COMMAND_HPP
