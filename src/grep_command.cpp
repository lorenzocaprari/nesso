// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "grep_command.hpp"

#include <core/embedding_store.hpp>
#include <embed/onnx_embedder.hpp>
#include <parser/log_chunker.hpp>

#include <iostream>
#include <print>
#include <string>
#include <vector>

namespace nesso::commands
{

int runGrep(std::string_view query, std::span<const std::filesystem::path> files, size_t topK,
            const std::filesystem::path &modelDir)
{
    const auto embedder = embed::OnnxEmbedder::create(modelDir);
    if (!embedder)
    {
        std::println(std::cerr, "Error: Failed to load embedder. Code: {}", static_cast<int>(embedder.error()));
        return 1;
    }

    std::vector<parser::ParsedChunk> chunks;
    std::vector<std::string> sources;
    for (const std::filesystem::path &path : files)
    {
        const auto parsed = parser::LogChunker::fromFile(path);
        if (!parsed)
        {
            std::println(std::cerr, "Error: Failed to parse '{}'. Code: {}", path.string(),
                         static_cast<int>(parsed.error()));
            return 1;
        }
        const std::string source = path.string();
        for (const parser::ParsedChunk &chunk : *parsed)
        {
            chunks.push_back(chunk);
            sources.push_back(source);
        }
    }

    if (chunks.empty())
    {
        return 1;
    }

    std::vector<std::string> texts;
    texts.reserve(chunks.size());
    for (const parser::ParsedChunk &chunk : chunks)
    {
        texts.push_back(chunk.text);
    }

    const auto embeddings = (*embedder)->embedBatch(texts);
    if (!embeddings)
    {
        std::println(std::cerr, "Error: Failed to embed log chunks. Code: {}", static_cast<int>(embeddings.error()));
        return 1;
    }

    core::EmbeddingStore<float> store;
    for (size_t i = 0; i < embeddings->size(); ++i)
    {
        const auto insertResult = store.insert(
            (*embeddings)[i], {.text = chunks[i].text, .lineNumber = chunks[i].lineNumber, .source = sources[i]});
        if (!insertResult)
        {
            std::println(std::cerr, "Error: Failed to build index. Code: {}", static_cast<int>(insertResult.error()));
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

    const bool printSource = files.size() > 1;
    for (const auto &result : *results)
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
    return 0;
}

} // namespace nesso::commands
