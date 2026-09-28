// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "grep_command.hpp"

#include "text_search.hpp"

#include <core/embedding_store.hpp>
#include <embed/onnx_embedder.hpp>

#include <iostream>
#include <print>

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

    const auto chunks = parseAndEmbed(files, **embedder);
    if (!chunks)
    {
        return chunks.error();
    }
    if (chunks->empty())
    {
        return 1;
    }

    core::EmbeddingStore<float> store;
    for (const core::CorpusChunk &chunk : *chunks)
    {
        const auto insertResult = store.insert(chunk.embedding, chunk.chunk);
        if (!insertResult)
        {
            std::println(std::cerr, "Error: Failed to build index. Code: {}", static_cast<int>(insertResult.error()));
            return 1;
        }
    }

    return embedQueryAndPrint(store, **embedder, query, topK, multipleSources(*chunks));
}

} // namespace nesso::commands
