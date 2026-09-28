// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "text_search.hpp"

#include "command_output.hpp"

#include <embed/onnx_embedder.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <iostream>
#include <print>
#include <string>

namespace nesso::commands
{

bool multipleSources(std::span<const core::CorpusChunk> chunks)
{
    if (chunks.empty())
    {
        return false;
    }
    const std::string &first = chunks.front().chunk.source;
    return std::ranges::any_of(chunks,
                               [&first](const core::CorpusChunk &chunk) { return chunk.chunk.source != first; });
}

std::expected<std::vector<core::CorpusChunk>, int> parseAndEmbed(std::span<const std::filesystem::path> files,
                                                                 const embed::OnnxEmbedder &embedder)
{
    parser::ParseStats stats{};
    std::vector<parser::ParsedChunk> parsedChunks;
    std::vector<std::string> sources;
    for (const std::filesystem::path &path : files)
    {
        const auto parsed = parser::LogChunker::fromFile(path, 4096, &stats);
        if (!parsed)
        {
            std::println(std::cerr, "Error: Failed to parse '{}'. Code: {}", path.string(),
                         static_cast<int>(parsed.error()));
            reportSkippedLines(stats.skippedLines);
            return std::unexpected(1);
        }
        const std::string source = path.string();
        for (const parser::ParsedChunk &chunk : *parsed)
        {
            parsedChunks.push_back(chunk);
            sources.push_back(source);
        }
    }
    reportSkippedLines(stats.skippedLines);

    if (parsedChunks.empty())
    {
        return std::vector<core::CorpusChunk>{};
    }

    std::vector<std::string> texts;
    texts.reserve(parsedChunks.size());
    for (const parser::ParsedChunk &chunk : parsedChunks)
    {
        texts.push_back(chunk.text);
    }

    const auto embeddings = embedder.embedBatch(texts);
    if (!embeddings)
    {
        std::println(std::cerr, "Error: Failed to embed log chunks. Code: {}", static_cast<int>(embeddings.error()));
        return std::unexpected(1);
    }

    std::vector<core::CorpusChunk> chunks;
    chunks.reserve(embeddings->size());
    for (size_t index = 0; index < embeddings->size(); ++index)
    {
        chunks.push_back({.chunk = {.text = parsedChunks[index].text,
                                    .lineNumber = parsedChunks[index].lineNumber,
                                    .source = sources[index]},
                          .embedding = (*embeddings)[index]});
    }
    return chunks;
}

int embedQueryAndPrint(const core::EmbeddingStore<float> &store, const embed::OnnxEmbedder &embedder,
                       std::string_view query, size_t topK, bool printSource)
{
    const auto queryEmbedding = embedder.embed(query);
    if (!queryEmbedding)
    {
        std::println(std::cerr, "Error: Failed to embed query. Code: {}", static_cast<int>(queryEmbedding.error()));
        return 1;
    }

    const auto results = store.searchTopK(*queryEmbedding, topK);
    if (!results)
    {
        std::println(std::cerr, "Error: Semantic search failed. Code: {}", static_cast<int>(results.error()));
        return 1;
    }
    if (results->empty())
    {
        return 1;
    }
    printEmbeddingMatches(*results, printSource);
    return 0;
}

} // namespace nesso::commands
