// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_COMMAND_OUTPUT_HPP
#define NESSO_COMMAND_OUTPUT_HPP

#include <nesso/error.hpp>
#include <nesso/types.hpp>

#include <iostream>
#include <print>

namespace nesso::commands
{

inline void reportSkippedLines(size_t skippedLines)
{
    if (skippedLines > 0)
    {
        std::println(std::cerr, "Skipped {} lines.", skippedLines);
    }
}

inline void printTextMatches(const TextResults &results)
{
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
}

inline int reportError(const Error &error)
{
    if (error.kind == ErrorKind::Parse)
    {
        std::println(std::cerr, "Error: Failed to parse '{}'. Code: {}", error.path.string(), error.code);
        reportSkippedLines(error.skippedLines);
        return 1;
    }

    reportSkippedLines(error.skippedLines);
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

} // namespace nesso::commands

#endif // NESSO_COMMAND_OUTPUT_HPP
