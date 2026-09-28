// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_CORPUS_COMMANDS_HPP
#define NESSO_CORPUS_COMMANDS_HPP

#include <nesso/nesso.hpp>

namespace CLI
{
class App;
} // namespace CLI

namespace nesso::commands
{

int runCorpusIndex(const Nesso &nesso, const IndexRequest &request);

int runCorpusSearch(const Nesso &nesso, const SearchRequest &request);

void addCorpusIndexCommand(CLI::App &app);

void addCorpusSearchCommand(CLI::App &app);

} // namespace nesso::commands

#endif // NESSO_CORPUS_COMMANDS_HPP
