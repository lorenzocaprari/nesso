// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE for details.

#include <CLI/CLI.hpp>
#include <mach_core/storage_engine.hpp>

#include <exception>
#include <format>
#include <iostream>
#include <print>

int main(int argc, char *argv[]) noexcept
{
    try
    {
        std::ios_base::sync_with_stdio(false);

        CLI::App app{"Mach1 - High Performance Vector Database & Semantic Indexer"};
        app.require_subcommand(1); // Enforce that a verb (subcommand) must be provided

        // Shared configuration flags across subcommands
        std::string dbPath = "vectors.mach1";
        app.add_option("-p,--path", dbPath, "Path to the vector database storage file");

        // --------------------------------------------------------------------
        // Subcommand: INIT
        // --------------------------------------------------------------------
        uint64_t dimensions = 128;
        auto *initCmd = app.add_subcommand("init", "Initialize an empty database index container");
        initCmd->add_option("-d,--dims", dimensions, "Dimensionality of the vector space")->default_val(128);

        // --------------------------------------------------------------------
        // Subcommand: INDEX
        // --------------------------------------------------------------------
        std::string inputFile;
        auto *indexCmd = app.add_subcommand("index", "Ingest external raw vector binary data");
        indexCmd->add_option("-f,--file", inputFile,
                             "Path to the raw floating-point binary file")
            ->required()
            ->check(CLI::ExistingFile); // Validates file existence out-of-the-box

        // Parse command line arguments
        CLI11_PARSE(app, argc, argv);

        // Instantiate our RAII storage engine
        mach_core::StorageEngine<float> engine;

        // --------------------------------------------------------------------
        // Command Routing & Action Layer
        // --------------------------------------------------------------------
        if (initCmd->parsed())
        {
            std::println("Initializing database container at '{}'...", dbPath);

            auto result = engine.createOrOpen(dbPath, dimensions);
            if (!result)
            {
                std::println(std::cerr, "Error: Failed to initialize. Code: {}", static_cast<int>(result.error()));
                return 1;
            }

            std::println("Database container created successfully. Target "
                         "Dimensions: {}",
                         engine.getDimensions());
        }
        else if (indexCmd->parsed())
        {
            std::println("Opening database container at '{}' for ingestion...", dbPath);

            // Match the 256 dimensions we initialized the database with!
            auto result = engine.createOrOpen(dbPath, 256);
            if (!result)
            {
                // Now we cast the EngineError enum to an integer so we can
                // actually see WHY it failed
                std::println(std::cerr, "Error: Could not open database target file. Code: {}",
                             static_cast<int>(result.error()));
                return 1;
            }

            std::println("Streaming ingestion target identified: '{}'", inputFile);
            std::println("Current vector count before ingest: {}", engine.getVectorCount());

            // Binary Streaming Loop
            std::ifstream infile(inputFile, std::ios::binary);
            if (!infile)
            {
                std::println(std::cerr, "Failed to open input file stream.");
                return 1;
            }

            // Create a single allocation buffer sized exactly to our database schema
            std::vector<float> buffer(engine.getDimensions());
            size_t ingestedCount = 0;
            // We are intentionally treating the float buffer as raw bytes for streaming
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            char *bufferData = reinterpret_cast<char *>(buffer.data());
            const auto readSize = static_cast<std::streamsize>(buffer.size() * sizeof(float));

            // Read exactly 'dimensions * sizeof(float)' bytes per loop
            while (infile.read(bufferData, readSize))
            {
                auto appendRes = engine.appendVector(buffer);
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
        }
    }
    catch (const std::format_error &e)
    {
        std::cerr << "System Format Error: " << e.what() << '\n';
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unhandled Runtime Exception: " << e.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown critical failure occurred." << '\n';
        return 1;
    }
}
