// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "grep_command.hpp"

#include "model_paths.hpp"
#include "text_search.hpp"

#include <core/embedding_store.hpp>
#include <embed/onnx_embedder.hpp>

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
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

void addGrepCommand(CLI::App &app)
{
    struct Options
    {
        std::string query;
        std::vector<std::string> files;
        size_t topK = 5;
        std::string modelDir;
    };
    auto opts = std::make_shared<Options>();

    auto *grepCmd = app.add_subcommand("grep", "Semantic search over .log, .json, and .jsonl files. "
                                               "Skips empty lines and log lines longer than 4096 characters. "
                                               "JSON objects must have a string 'message' field. "
                                               "Directories are not searched.");
    grepCmd->add_option("query", opts->query, "Natural-language query")->required();
    grepCmd->add_option("files", opts->files, "One or more .log, .json, or .jsonl files")
        ->required()
        ->expected(1, -1)
        ->check(CLI::ExistingFile);
    grepCmd->add_option("-k,--top-k", opts->topK, "Maximum number of matches to return (default: 5)")->default_val(5);
    auto *modelDirOpt = grepCmd->add_option("--model-dir", opts->modelDir, nesso::MODEL_DIR_HELP);

    // RuntimeError is how CLI11_PARSE returns a command status without an extra diagnostic.
    grepCmd->callback(
        [opts, modelDirOpt]()
        {
            std::vector<std::filesystem::path> files;
            files.reserve(opts->files.size());
            for (const std::string &fileArg : opts->files)
            {
                files.emplace_back(fileArg);
            }
            const std::filesystem::path modelDir =
                modelDirOpt->empty() ? nesso::resolveDefaultModelDir() : std::filesystem::path(opts->modelDir);
            const int code = runGrep(opts->query, files, opts->topK, modelDir);
            if (code != 0)
            {
                throw CLI::RuntimeError(code);
            }
        });
}

} // namespace nesso::commands
