// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_CLI_PARSER_HPP
#define NESSO_CLI_PARSER_HPP

#include <nesso/nesso.hpp>
#include <nesso_cli/cli.hpp>

#include <expected>
#include <variant>

namespace nesso::cli
{

using Request = std::variant<GrepCommand, IndexRequest, SearchRequest, OverviewCommand>;

struct ParsedCommand
{
    Config config;
    Request request;
};

/// Parses argv into one request. The error is the process exit code when no
/// command should run: help and version yield 0, and a usage error yields 2
/// after printing CLI11's diagnostic.
[[nodiscard]] auto ParseCommandLine(int argc, char **argv) -> std::expected<ParsedCommand, int>;

} // namespace nesso::cli

#endif // NESSO_CLI_PARSER_HPP
