// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "corpus_commands.hpp"

#include "text_search.hpp"

#include <core/corpus_index.hpp>
#include <core/embedding_store.hpp>
#include <embed/onnx_embedder.hpp>

#include <iostream>
#include <print>

namespace nesso::commands
{

int runCorpusIndex(std::span<const std::filesystem::path> files, const std::filesystem::path &output,
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

    const auto written = core::writeCorpusFile(output, *chunks);
    if (!written)
    {
        std::println(std::cerr, "Error: Failed to write corpus. Code: {}", static_cast<int>(written.error()));
        return 1;
    }
    std::println(std::cerr, "Indexed {} chunks into '{}'.", chunks->size(), output.string());
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

    return embedQueryAndPrint(store, **embedder, query, topK, multipleSources(*chunks));
}

} // namespace nesso::commands
