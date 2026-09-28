// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_CLI_OUTPUT_HPP
#define NESSO_CLI_OUTPUT_HPP

#include <nesso/error.hpp>
#include <nesso/types.hpp>

#include <vector>

namespace nesso::cli
{

/// Matches go to stdout; progress, summaries, and diagnostics go to stderr. Each render returns the exit code.
void announce(const StoreInitRequest &request);
void announce(const StoreIngestRequest &request);

[[nodiscard]] int render(const TextResults &results);
[[nodiscard]] int render(const IndexSummary &summary, const IndexRequest &request);
[[nodiscard]] int render(const StoreInitSummary &summary);
[[nodiscard]] int render(const StoreIngestSummary &summary);
[[nodiscard]] int render(const std::vector<VectorMatch> &matches);

[[nodiscard]] int renderError(const Error &error);

} // namespace nesso::cli

#endif // NESSO_CLI_OUTPUT_HPP
