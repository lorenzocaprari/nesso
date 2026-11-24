// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0
// Licensed under the Apache License, Version 2.0. See LICENSE for details.

#include <cli/cli.hpp>
#include <cli/cli_parser.hpp>

#include <nesso/nesso.hpp>

#include <exception>
#include <format>
#include <iostream>
#include <variant>

auto main(int argc, char *argv[]) noexcept -> int
{
    try
    {
        std::ios_base::sync_with_stdio(false);

        const auto parsed = nesso::cli::ParseCommandLine(argc, argv);
        if (!parsed)
        {
            return parsed.error();
        }

        const nesso::Nesso nesso{parsed->config};
        return std::visit([&nesso](const auto &request) -> auto { return nesso::cli::Execute(nesso, request); },
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
