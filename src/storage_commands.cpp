// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "storage_commands.hpp"

#include "command_output.hpp"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <print>
#include <string>

namespace nesso::commands
{

int runInit(const Nesso &nesso, const StoreInitRequest &request)
{
    std::println(std::cerr, "Initializing database container at '{}'...", request.db.string());

    const auto summary = nesso.storeInit(request);
    if (!summary)
    {
        return reportError(summary.error());
    }
    std::println(std::cerr, "Database container created successfully. Target Dimensions: {}", summary->dimensions);
    return 0;
}

int runIndex(const Nesso &nesso, const StoreIngestRequest &request)
{
    std::println(std::cerr, "Opening database container at '{}' for ingestion (dimensions: {})...", request.db.string(),
                 request.dimensions);
    std::println(std::cerr, "Streaming ingestion target identified: '{}'", request.input.string());

    const auto summary = nesso.storeIngest(request);
    if (!summary)
    {
        return reportError(summary.error());
    }
    std::println(std::cerr, "Current vector count before ingest: {}", summary->before);
    if (summary->appendFailureCode)
    {
        std::println(std::cerr, "Fatal error appending vector at index {}. Code: {}", summary->added,
                     *summary->appendFailureCode);
    }
    std::println(std::cerr, "Ingestion complete. Added {} new vectors.", summary->added);
    std::println(std::cerr, "New total vector count on disk: {}", summary->total);
    return 0;
}

int runSearch(const Nesso &nesso, const StoreSearchRequest &request)
{
    const auto matches = nesso.storeSearch(request);
    if (!matches)
    {
        return reportError(matches.error());
    }
    for (const VectorMatch &match : *matches)
    {
        std::println("index: {}, score: {}", match.index, match.score);
    }
    return 0;
}

void addStoreCommand(CLI::App &app)
{
    struct Options
    {
        std::string dbPath = "vectors.nesso";
        uint64_t dimensions = DEFAULT_STORE_DIMENSIONS;
        std::string inputFile;
        std::string queryFile;
        size_t searchTopK = DEFAULT_STORE_TOP_K;
    };
    auto opts = std::make_shared<Options>();

    auto *storeCmd = app.add_subcommand("store", "Raw float32 vector store (mmap, cosine top-k)");
    storeCmd->require_subcommand(1);
    storeCmd->add_option("-p,--path", opts->dbPath, "Path to the vector database storage file");
    storeCmd->add_option("-d,--dims", opts->dimensions, "Dimensionality of the vector space")->default_val(128);

    auto *initCmd = storeCmd->add_subcommand("init", "Initialize an empty database index container");
    initCmd->fallthrough();

    auto *indexCmd = storeCmd->add_subcommand("index", "Ingest external raw vector binary data");
    indexCmd->fallthrough();
    indexCmd->add_option("-f,--file", opts->inputFile, "Path to the raw floating-point binary file")
        ->required()
        ->check(CLI::ExistingFile);

    auto *searchCmd = storeCmd->add_subcommand("search", "Return the nearest vectors by cosine similarity");
    searchCmd->fallthrough();
    searchCmd->add_option("-q,--query-file", opts->queryFile, "Path to one raw floating-point query vector")
        ->required()
        ->check(CLI::ExistingFile);
    searchCmd
        ->add_option("-k,--top-k", opts->searchTopK,
                     "Maximum number of nearest vectors to return (default: 10, differs from grep/search)")
        ->default_val(10);

    // RuntimeError is how CLI11_PARSE returns a command status without an extra diagnostic.
    storeCmd->callback(
        [opts, initCmd, indexCmd, searchCmd]()
        {
            const Nesso nesso{Config{}};
            int code = 1;
            if (initCmd->parsed())
            {
                code = runInit(nesso, {.db = opts->dbPath, .dimensions = opts->dimensions});
            }
            else if (indexCmd->parsed())
            {
                code = runIndex(nesso, {.db = opts->dbPath, .dimensions = opts->dimensions, .input = opts->inputFile});
            }
            else if (searchCmd->parsed())
            {
                code = runSearch(nesso, {.db = opts->dbPath,
                                         .dimensions = opts->dimensions,
                                         .query = opts->queryFile,
                                         .topK = opts->searchTopK});
            }
            if (code != 0)
            {
                throw CLI::RuntimeError(code);
            }
        });
}

} // namespace nesso::commands
