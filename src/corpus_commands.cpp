// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "corpus_commands.hpp"

#include "command_output.hpp"

#include <core/corpus_index.hpp>
#include <core/embedding_store.hpp>
#include <embed/onnx_embedder.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <iostream>
#include <print>
#include <string>
#include <vector>

namespace nesso::commands
{

static bool multipleSources(std::span<const core::CorpusChunk> chunks)
{
    if (chunks.empty())
    {
        return false;
    }
    const std::string &first = chunks.front().chunk.source;
    return std::ranges::any_of(chunks,
                               [&first](const core::CorpusChunk &chunk) { return chunk.chunk.source != first; });
}

int runCorpusIndex(std::span<const std::filesystem::path> files, const std::filesystem::path &output,
                   const std::filesystem::path &modelDir)
{
    const auto embedder = embed::OnnxEmbedder::create(modelDir);
    if (!embedder)
    {
        std::println(std::cerr, "Error: Failed to load embedder. Code: {}", static_cast<int>(embedder.error()));
        return 1;
    }

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
            return 1;
        }
        const std::string source = path.string();
        for (const parser::ParsedChunk &chunk : *parsed)
        {
            parsedChunks.push_back(chunk);
            sources.push_back(source);
        }
    }
    reportSkippedLines(stats.skippedLines);

    std::vector<core::CorpusChunk> chunks;
    if (!parsedChunks.empty())
    {
        std::vector<std::string> texts;
        texts.reserve(parsedChunks.size());
        for (const parser::ParsedChunk &chunk : parsedChunks)
        {
            texts.push_back(chunk.text);
        }
        const auto embeddings = (*embedder)->embedBatch(texts);
        if (!embeddings)
        {
            std::println(std::cerr, "Error: Failed to embed log chunks. Code: {}",
                         static_cast<int>(embeddings.error()));
            return 1;
        }
        chunks.reserve(embeddings->size());
        for (size_t index = 0; index < embeddings->size(); ++index)
        {
            chunks.push_back({.chunk = {.text = parsedChunks[index].text,
                                        .lineNumber = parsedChunks[index].lineNumber,
                                        .source = sources[index]},
                              .embedding = (*embeddings)[index]});
        }
    }

    const auto written = core::writeCorpusFile(output, chunks);
    if (!written)
    {
        std::println(std::cerr, "Error: Failed to write corpus. Code: {}", static_cast<int>(written.error()));
        return 1;
    }
    std::println(std::cerr, "Indexed {} chunks into '{}'.", chunks.size(), output.string());
    return 0;
}

int runCorpusSearch(std::string_view query, const std::filesystem::path &indexPath, size_t topK,
                    const std::filesystem::path &modelDir)
{
    const auto chunks = core::readCorpusFile(indexPath);
    if (!chunks)
    {
        std::println(std::cerr, "Error: Failed to read corpus. Code: {}", static_cast<int>(chunks.error()));
        return 1;
    }
    if (chunks->empty() || topK == 0)
    {
        return 1;
    }

    const auto embedder = embed::OnnxEmbedder::create(modelDir);
    if (!embedder)
    {
        std::println(std::cerr, "Error: Failed to load embedder. Code: {}", static_cast<int>(embedder.error()));
        return 1;
    }

    core::EmbeddingStore<float> store;
    for (const core::CorpusChunk &chunk : *chunks)
    {
        const auto inserted = store.insert(chunk.embedding, chunk.chunk);
        if (!inserted)
        {
            std::println(std::cerr, "Error: Failed to load corpus. Code: {}", static_cast<int>(inserted.error()));
            return 1;
        }
    }

    const auto queryEmbedding = (*embedder)->embed(query);
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
    printEmbeddingMatches(*results, multipleSources(*chunks));
    return 0;
}

} // namespace nesso::commands
