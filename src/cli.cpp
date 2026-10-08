// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "cli.hpp"

#include <expected>
#include <iostream>
#include <print>

namespace nesso::cli
{

static void renderSkippedLines(size_t skippedLines)
{
    if (skippedLines > 0)
    {
        std::println(std::cerr, "Skipped {} lines.", skippedLines);
    }
}

static int render(const TextResults &results)
{
    renderSkippedLines(results.skippedLines);
    for (const TextMatch &match : results.matches)
    {
        if (results.multipleSources)
        {
            std::println("{}:line {}: {:.4f}: {}", match.source, match.line, match.score, match.text);
        }
        else
        {
            std::println("line {}: {:.4f}: {}", match.line, match.score, match.text);
        }
    }
    return 0;
}

static int render(const IndexSummary &summary, const IndexRequest &request)
{
    renderSkippedLines(summary.skippedLines);
    std::println(std::cerr, "Indexed {} chunks into '{}'.", summary.chunks, request.output.string());
    return 0;
}

static int renderError(const Error &error, int status)
{
    if (error.kind == ErrorKind::Parse)
    {
        std::println(std::cerr, "{}", message(error));
        renderSkippedLines(error.skippedLines);
        return status;
    }
    renderSkippedLines(error.skippedLines);
    if (error.kind != ErrorKind::EmptyInput && error.kind != ErrorKind::NoMatches)
    {
        std::println(std::cerr, "{}", message(error));
    }
    return status;
}

static int grepStatus(const Error &error)
{
    if (error.kind == ErrorKind::EmptyInput || error.kind == ErrorKind::NoMatches)
    {
        return 1;
    }
    return 2;
}

template <typename Result, typename... Context>
static int report(const std::expected<Result, Error> &outcome, int failureStatus, const Context &...context)
{
    return outcome ? render(*outcome, context...) : renderError(outcome.error(), failureStatus);
}

int execute(const Nesso &nesso, const GrepRequest &request)
{
    const auto outcome = nesso.grep(request);
    return report(outcome, outcome ? 0 : grepStatus(outcome.error()));
}

int execute(const Nesso &nesso, const IndexRequest &request) { return report(nesso.index(request), 1, request); }

int execute(const Nesso &nesso, const SearchRequest &request) { return report(nesso.search(request), 1); }

} // namespace nesso::cli
