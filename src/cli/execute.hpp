// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_CLI_EXECUTE_HPP
#define NESSO_CLI_EXECUTE_HPP

#include <nesso/nesso.hpp>

namespace nesso::cli
{

/// Runs one request through the facade and renders the outcome. Returns the process exit code.
[[nodiscard]] int execute(const Nesso &nesso, const GrepRequest &request);
[[nodiscard]] int execute(const Nesso &nesso, const IndexRequest &request);
[[nodiscard]] int execute(const Nesso &nesso, const SearchRequest &request);
[[nodiscard]] int execute(const Nesso &nesso, const StoreInitRequest &request);
[[nodiscard]] int execute(const Nesso &nesso, const StoreIngestRequest &request);
[[nodiscard]] int execute(const Nesso &nesso, const StoreSearchRequest &request);

} // namespace nesso::cli

#endif // NESSO_CLI_EXECUTE_HPP
