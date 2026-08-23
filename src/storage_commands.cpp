// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "storage_commands.hpp"

#include <core/vector_search.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <vector>

namespace nesso::commands
{

int runInit(core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions)
{
    std::println("Initializing database container at '{}'...", dbPath);

    const auto result = engine.createOrOpen(dbPath, dimensions);
    if (!result)
    {
        std::println(std::cerr, "Error: Failed to initialize. Code: {}", static_cast<int>(result.error()));
        return 1;
    }

    std::println("Database container created successfully. Target Dimensions: {}", engine.getDimensions());
    return 0;
}

int runIndex(core::StorageEngine<float> &engine, const std::string &dbPath, uint64_t dimensions,
             const std::string &inputFile)
{
    std::println("Opening database container at '{}' for ingestion (dimensions: {})...", dbPath, dimensions);

    const auto result = engine.createOrOpen(dbPath, dimensions);
    if (!result)
    {
        std::println(std::cerr, "Error: Could not open database target file. Code: {}",
                     static_cast<int>(result.error()));
        return 1;
    }

    std::println("Streaming ingestion target identified: '{}'", inputFile);
    std::println("Current vector count before ingest: {}", engine.getVectorCount());

    std::ifstream infile(inputFile, std::ios::binary);
    if (!infile)
    {
        std::println(std::cerr, "Failed to open input file stream.");
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

    std::println("Ingestion complete. Added {} new vectors.", ingestedCount);
    std::println("New total vector count on disk: {}", engine.getVectorCount());
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

} // namespace nesso::commands
