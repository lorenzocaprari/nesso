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

static int renderError(const Error &error)
{
    if (error.kind == ErrorKind::Parse)
    {
        std::println(std::cerr, "Error: Failed to parse '{}'. Code: {}", error.path.string(), error.code);
        renderSkippedLines(error.skippedLines);
        return 1;
    }

    renderSkippedLines(error.skippedLines);
    switch (error.kind)
    {
    case ErrorKind::EmbedderLoad:
        std::println(std::cerr, "Error: Failed to load embedder. Code: {}", error.code);
        break;
    case ErrorKind::Embed:
        std::println(std::cerr, "Error: Failed to embed log chunks. Code: {}", error.code);
        break;
    case ErrorKind::QueryEmbed:
        std::println(std::cerr, "Error: Failed to embed query. Code: {}", error.code);
        break;
    case ErrorKind::IndexBuild:
        std::println(std::cerr, "Error: Failed to build index. Code: {}", error.code);
        break;
    case ErrorKind::Search:
        std::println(std::cerr, "Error: Semantic search failed. Code: {}", error.code);
        break;
    case ErrorKind::CorpusRead:
        std::println(std::cerr, "Error: Failed to read corpus. Code: {}", error.code);
        break;
    case ErrorKind::CorpusWrite:
        std::println(std::cerr, "Error: Failed to write corpus. Code: {}", error.code);
        break;
    case ErrorKind::CorpusLoad:
        std::println(std::cerr, "Error: Failed to load corpus. Code: {}", error.code);
        break;
    case ErrorKind::Parse:
    case ErrorKind::EmptyInput:
    case ErrorKind::NoMatches:
        break;
    }
    return 1;
}

template <typename Result, typename... Context>
static int report(const std::expected<Result, Error> &outcome, const Context &...context)
{
    return outcome ? render(*outcome, context...) : renderError(outcome.error());
}

int execute(const Nesso &nesso, const GrepRequest &request) { return report(nesso.grep(request)); }

int execute(const Nesso &nesso, const IndexRequest &request) { return report(nesso.index(request), request); }

int execute(const Nesso &nesso, const SearchRequest &request) { return report(nesso.search(request)); }

} // namespace nesso::cli
