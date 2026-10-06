// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_CLI_PARSER_HPP
#define NESSO_CLI_PARSER_HPP

#include <nesso.hpp>

#include <expected>
#include <variant>

namespace nesso::cli
{

using Request = std::variant<GrepRequest, IndexRequest, SearchRequest>;

struct ParsedCommand
{
    Config config;
    Request request;
};

/// Parses argv into one request. The error is the process exit code when no command should run: help and version
/// yield 0, and a usage error yields CLI11's non-zero code after printing its diagnostic.
[[nodiscard]] std::expected<ParsedCommand, int> parseCommandLine(int argc, char **argv);

} // namespace nesso::cli

#endif // NESSO_CLI_PARSER_HPP
