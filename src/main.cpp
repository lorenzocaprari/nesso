// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE for details.

#include "corpus_commands.hpp"
#include "grep_command.hpp"
#include "storage_commands.hpp"

#include <CLI/CLI.hpp>

#include <exception>
#include <iostream>

#ifndef NESSO_VERSION
#define NESSO_VERSION "0.0.0"
#endif

int main(int argc, char *argv[]) noexcept
{
    try
    {
        std::ios_base::sync_with_stdio(false);

        CLI::App app{"Nesso - local semantic search for unstructured text"};
        app.set_version_flag("-V,--version", NESSO_VERSION);
        app.require_subcommand(1);

        nesso::commands::addStoreCommand(app);
        nesso::commands::addGrepCommand(app);
        nesso::commands::addCorpusIndexCommand(app);
        nesso::commands::addCorpusSearchCommand(app);

        CLI11_PARSE(app, argc, argv);
        return 0;
    }
    catch (const std::format_error &e)
    {
        std::cerr << "System Format Error: " << e.what() << '\n';
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unhandled Runtime Exception: " << e.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown critical failure occurred." << '\n';
        return 1;
    }
}
