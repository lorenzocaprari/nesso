// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_CLI_HPP
#define NESSO_CLI_HPP

#include <nesso/nesso.hpp>
#include <nesso/overview.hpp>

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

/// An `overview` request and the CLI choice of JSON Lines output.
struct OverviewCommand
{
    OverviewRequest request;
    bool json = false;
};

/// Runs one request through the facade and renders the outcome. Returns the
/// process exit code.
[[nodiscard]] auto Execute(const Nesso &nesso, const GrepCommand &command) -> int;
[[nodiscard]] auto Execute(const Nesso &nesso, const IndexRequest &request) -> int;
[[nodiscard]] auto Execute(const Nesso &nesso, const SearchRequest &request) -> int;
/// Does not use `nesso`, so no model is loaded.
[[nodiscard]] auto Execute(const Nesso &nesso, const OverviewCommand &command) -> int;

} // namespace nesso::cli

#endif // NESSO_CLI_HPP
