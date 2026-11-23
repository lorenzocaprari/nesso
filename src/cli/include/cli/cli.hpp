// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_CLI_HPP
#define NESSO_CLI_HPP

#include <nesso/nesso.hpp>

#include <optional>

namespace nesso::cli
{

/// A `grep` request and the CLI choice of provisional output.
struct GrepCommand
{
    GrepRequest request;
    /// `--provisional` (true), `--no-provisional` (false), or neither.
    std::optional<bool> provisional;
};

/// Runs one request through the facade and renders the outcome. Returns the
/// process exit code.
[[nodiscard]] int Execute(const Nesso &nesso, const GrepCommand &command);
[[nodiscard]] int Execute(const Nesso &nesso, const IndexRequest &request);
[[nodiscard]] int Execute(const Nesso &nesso, const SearchRequest &request);

} // namespace nesso::cli

#endif // NESSO_CLI_HPP
