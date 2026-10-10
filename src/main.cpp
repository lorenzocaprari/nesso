// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE for details.

#include "cli.hpp"
#include "cli_parser.hpp"

#include <nesso.hpp>

#include <exception>
#include <format>
#include <iostream>
#include <variant>

int main(int argc, char *argv[]) noexcept
{
    try
    {
        std::ios_base::sync_with_stdio(false);

        const auto parsed = nesso::cli::parseCommandLine(argc, argv);
        if (!parsed)
        {
            return parsed.error();
        }

        const nesso::Nesso nesso{parsed->config};
        return std::visit([&nesso](const auto &request) { return nesso::cli::execute(nesso, request); },
                          parsed->request);
    }
    catch (const std::format_error &e)
    {
        std::cerr << "System Format Error: " << e.what() << '\n';
        return 2;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unhandled Runtime Exception: " << e.what() << '\n';
        return 2;
    }
    catch (...)
    {
        std::cerr << "Unknown critical failure occurred." << '\n';
        return 2;
    }
}
