// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "grep_command.hpp"

#include <mach_core/embedding_store.hpp>
#include <mach_embed/onnx_embedder.hpp>
#include <mach_parser/log_chunker.hpp>

#include <iostream>
#include <print>
#include <string>
#include <vector>

namespace mach1::commands
{

int runGrep(const std::filesystem::path &logPath, const std::string &query, size_t topK,
            const std::filesystem::path &modelDir)
{
    const auto embedder = mach_embed::OnnxEmbedder::create(modelDir);
    if (!embedder)
    {
        std::println(std::cerr, "Error: Failed to load embedder. Code: {}", static_cast<int>(embedder.error()));
        return 1;
    }

    const auto chunks = mach_parser::LogChunker::fromFile(logPath);
    if (!chunks)
    {
        std::println(std::cerr, "Error: Failed to parse log file. Code: {}", static_cast<int>(chunks.error()));
        return 1;
    }

    std::vector<std::string> texts;
    texts.reserve(chunks->size());
    for (const mach_parser::ParsedChunk &chunk : *chunks)
    {
        texts.push_back(chunk.text);
    }

    const auto embeddings = (*embedder)->embedBatch(texts);
    if (!embeddings)
    {
        std::println(std::cerr, "Error: Failed to embed log chunks. Code: {}", static_cast<int>(embeddings.error()));
        return 1;
    }

    mach_core::EmbeddingStore<float> store;
    for (size_t i = 0; i < embeddings->size(); ++i)
    {
        const auto insertResult =
            store.insert((*embeddings)[i], {.text = (*chunks)[i].text, .lineNumber = (*chunks)[i].lineNumber});
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

    for (const auto &result : *results)
    {
        std::println("line {}: {:.4f}: {}", result.chunk.lineNumber, result.score, result.chunk.text);
    }
    return 0;
}

} // namespace mach1::commands
