// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "cli.hpp"

#include <expected>
#include <iostream>
#include <print>
#include <vector>

namespace nesso::cli
{

static void renderSkippedLines(size_t skippedLines)
{
    if (skippedLines > 0)
    {
        std::println(std::cerr, "Skipped {} lines.", skippedLines);
    }
}

static void announce(const StoreInitRequest &request)
{
    std::println(std::cerr, "Initializing database container at '{}'...", request.db.string());
}

static void announce(const StoreIngestRequest &request)
{
    std::println(std::cerr, "Opening database container at '{}' for ingestion (dimensions: {})...", request.db.string(),
                 request.dimensions);
    std::println(std::cerr, "Streaming ingestion target identified: '{}'", request.input.string());
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

static int render(const StoreInitSummary &summary)
{
    std::println(std::cerr, "Database container created successfully. Target Dimensions: {}", summary.dimensions);
    return 0;
}

static int render(const StoreIngestSummary &summary)
{
    std::println(std::cerr, "Current vector count before ingest: {}", summary.before);
    if (summary.appendFailureCode)
    {
        std::println(std::cerr, "Fatal error appending vector at index {}. Code: {}", summary.added,
                     *summary.appendFailureCode);
    }
    std::println(std::cerr, "Ingestion complete. Added {} new vectors.", summary.added);
    std::println(std::cerr, "New total vector count on disk: {}", summary.total);
    return 0;
}

static int render(const std::vector<VectorMatch> &matches)
{
    for (const VectorMatch &match : matches)
    {
        std::println("index: {}, score: {}", match.index, match.score);
    }
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
    case ErrorKind::StoreInit:
        std::println(std::cerr, "Error: Failed to initialize. Code: {}", error.code);
        break;
    case ErrorKind::StoreOpen:
        std::println(std::cerr, "Error: Could not open database target file. Code: {}", error.code);
        break;
    case ErrorKind::StoreMissing:
        std::println(std::cerr, "Error: Database container '{}' does not exist.", error.path.string());
        break;
    case ErrorKind::StoreSearch:
        std::println(std::cerr, "Error: Search failed. Code: {}", error.code);
        break;
    case ErrorKind::InputOpen:
        std::println(std::cerr, "Error: Failed to open input file stream.");
        break;
    case ErrorKind::QuerySize:
        std::println(std::cerr, "Error: Query file must contain exactly one {}-dimension float vector.",
                     error.dimensions);
        break;
    case ErrorKind::QueryRead:
        std::println(std::cerr, "Error: Failed to read query vector.");
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

int execute(const Nesso &nesso, const StoreInitRequest &request)
{
    announce(request);
    return report(nesso.storeInit(request));
}

int execute(const Nesso &nesso, const StoreIngestRequest &request)
{
    announce(request);
    return report(nesso.storeIngest(request));
}

int execute(const Nesso &nesso, const StoreSearchRequest &request) { return report(nesso.storeSearch(request)); }

} // namespace nesso::cli
