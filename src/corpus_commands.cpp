// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "corpus_commands.hpp"

#include "model_paths.hpp"
#include "text_search.hpp"

#include <core/corpus_index.hpp>
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

void addCorpusIndexCommand(CLI::App &app)
{
    struct Options
    {
        std::string output;
        std::vector<std::string> files;
        std::string modelDir;
    };
    auto opts = std::make_shared<Options>();

    auto *corpusIndexCmd = app.add_subcommand("index", "Embed .log, .json, and .jsonl files into a corpus file. "
                                                       "Skips empty lines and log lines longer than 4096 characters.");
    corpusIndexCmd->add_option("-o,--output", opts->output, "Corpus file to write")->required();
    corpusIndexCmd->add_option("files", opts->files, "One or more .log, .json, or .jsonl files")
        ->required()
        ->expected(1, -1)
        ->check(CLI::ExistingFile);
    auto *modelDirOpt = corpusIndexCmd->add_option("--model-dir", opts->modelDir, nesso::MODEL_DIR_HELP);

    // RuntimeError is how CLI11_PARSE returns a command status without an extra diagnostic.
    corpusIndexCmd->callback(
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
            const int code = runCorpusIndex(files, opts->output, modelDir);
            if (code != 0)
            {
                throw CLI::RuntimeError(code);
            }
        });
}

void addCorpusSearchCommand(CLI::App &app)
{
    struct Options
    {
        std::string query;
        std::string indexPath;
        size_t topK = 5;
        std::string modelDir;
    };
    auto opts = std::make_shared<Options>();

    auto *corpusSearchCmd = app.add_subcommand("search", "Semantic search of a corpus file. Prints matches only.");
    corpusSearchCmd->add_option("query", opts->query, "Natural-language query")->required();
    corpusSearchCmd->add_option("-i,--index", opts->indexPath, "Corpus file to search")
        ->required()
        ->check(CLI::ExistingFile);
    corpusSearchCmd->add_option("-k,--top-k", opts->topK, "Maximum number of matches to return (default: 5)")
        ->default_val(5);
    auto *modelDirOpt = corpusSearchCmd->add_option("--model-dir", opts->modelDir, nesso::MODEL_DIR_HELP);

    // RuntimeError is how CLI11_PARSE returns a command status without an extra diagnostic.
    corpusSearchCmd->callback(
        [opts, modelDirOpt]()
        {
            const std::filesystem::path modelDir =
                modelDirOpt->empty() ? nesso::resolveDefaultModelDir() : std::filesystem::path(opts->modelDir);
            const int code = runCorpusSearch(opts->query, opts->indexPath, opts->topK, modelDir);
            if (code != 0)
            {
                throw CLI::RuntimeError(code);
            }
        });
}

} // namespace nesso::commands
