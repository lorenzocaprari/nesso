// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "storage_commands.hpp"

#include <core/vector_search.hpp>

#include <CLI/CLI.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <vector>

namespace nesso::commands
{

int runInit(core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions)
{
    std::println(std::cerr, "Initializing database container at '{}'...", dbPath);

    const auto result = engine.createOrOpen(dbPath, dimensions);
    if (!result)
    {
        std::println(std::cerr, "Error: Failed to initialize. Code: {}", static_cast<int>(result.error()));
        return 1;
    }

    std::println(std::cerr, "Database container created successfully. Target Dimensions: {}", engine.getDimensions());
    return 0;
}

int runIndex(core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions,
             const std::string &inputFile)
{
    std::println(std::cerr, "Opening database container at '{}' for ingestion (dimensions: {})...", dbPath, dimensions);

    const auto result = engine.createOrOpen(dbPath, dimensions);
    if (!result)
    {
        std::println(std::cerr, "Error: Could not open database target file. Code: {}",
                     static_cast<int>(result.error()));
        return 1;
    }

    std::println(std::cerr, "Streaming ingestion target identified: '{}'", inputFile);
    std::println(std::cerr, "Current vector count before ingest: {}", engine.getVectorCount());

    std::ifstream infile(inputFile, std::ios::binary);
    if (!infile)
    {
        std::println(std::cerr, "Error: Failed to open input file stream.");
        return 1;
    }

    std::vector<float> buffer(engine.getDimensions());
    size_t ingestedCount = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    char *bufferData = reinterpret_cast<char *>(buffer.data());
    const auto readSize = static_cast<std::streamsize>(buffer.size() * sizeof(float));

    while (infile.read(bufferData, readSize))
    {
        const auto appendRes = engine.appendVector(buffer);
        if (!appendRes)
        {
            std::println(std::cerr, "Fatal error appending vector at index {}. Code: {}", ingestedCount,
                         static_cast<int>(appendRes.error()));
            break;
        }
        ingestedCount++;
    }

    std::println(std::cerr, "Ingestion complete. Added {} new vectors.", ingestedCount);
    std::println(std::cerr, "New total vector count on disk: {}", engine.getVectorCount());
    return 0;
}

int runSearch(core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions,
              const std::string &queryFile, size_t topK)
{
    if (!std::filesystem::exists(dbPath))
    {
        std::println(std::cerr, "Error: Database container '{}' does not exist.", dbPath);
        return 1;
    }

    const auto openResult = engine.createOrOpen(dbPath, dimensions);
    if (!openResult)
    {
        std::println(std::cerr, "Error: Could not open database target file. Code: {}",
                     static_cast<int>(openResult.error()));
        return 1;
    }

    const auto expectedQuerySize = engine.getDimensions() * sizeof(float);
    if (std::filesystem::file_size(queryFile) != expectedQuerySize)
    {
        std::println(std::cerr, "Error: Query file must contain exactly one {}-dimension float vector.",
                     engine.getDimensions());
        return 1;
    }

    std::vector<float> query(engine.getDimensions());
    std::ifstream infile(queryFile, std::ios::binary);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto *queryData = reinterpret_cast<char *>(query.data());
    infile.read(queryData, static_cast<std::streamsize>(expectedQuerySize));
    if (!infile)
    {
        std::println(std::cerr, "Error: Failed to read query vector.");
        return 1;
    }

    const auto results = core::searchTopKCosine<float>(engine, query, topK);
    if (!results)
    {
        std::println(std::cerr, "Error: Search failed. Code: {}", static_cast<int>(results.error()));
        return 1;
    }

    for (const auto &result : *results)
    {
        std::println("index: {}, score: {}", result.index, result.score);
    }
    return 0;
}

void addStoreCommand(CLI::App &app)
{
    struct Options
    {
        std::string dbPath = "vectors.nesso";
        uint64_t dimensions = 128;
        std::string inputFile;
        std::string queryFile;
        size_t searchTopK = 10;
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
            core::StorageEngine<float> engine;
            int code = 1;
            if (initCmd->parsed())
            {
                code = runInit(engine, opts->dbPath, opts->dimensions);
            }
            else if (indexCmd->parsed())
            {
                code = runIndex(engine, opts->dbPath, opts->dimensions, opts->inputFile);
            }
            else if (searchCmd->parsed())
            {
                code = runSearch(engine, opts->dbPath, opts->dimensions, opts->queryFile, opts->searchTopK);
            }
            if (code != 0)
            {
                throw CLI::RuntimeError(code);
            }
        });
}

} // namespace nesso::commands
