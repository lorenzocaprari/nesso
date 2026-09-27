// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_COMMAND_OUTPUT_HPP
#define NESSO_COMMAND_OUTPUT_HPP

#include <core/embedding_store.hpp>

#include <iostream>
#include <print>
#include <span>

namespace nesso::commands
{

inline void reportSkippedLines(size_t skippedLines)
{
    if (skippedLines > 0)
    {
        std::println(std::cerr, "Skipped {} lines.", skippedLines);
    }
}

inline void printEmbeddingMatches(std::span<const core::EmbeddingSearchResult<float>> results, bool printSource)
{
    for (const core::EmbeddingSearchResult<float> &result : results)
    {
        if (printSource)
        {
            std::println("{}:line {}: {:.4f}: {}", result.chunk.source, result.chunk.lineNumber, result.score,
                         result.chunk.text);
        }
        else
        {
            std::println("line {}: {:.4f}: {}", result.chunk.lineNumber, result.score, result.chunk.text);
        }
    }
}

} // namespace nesso::commands

#endif // NESSO_COMMAND_OUTPUT_HPP
