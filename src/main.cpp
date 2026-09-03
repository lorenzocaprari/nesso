// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE for details.

#include "grep_command.hpp"
#include "storage_commands.hpp"

#include <CLI/CLI.hpp>
#include <core/storage_engine.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include <vector>

int main(int argc, char *argv[]) noexcept
{
    try
    {
        std::ios_base::sync_with_stdio(false);

        CLI::App app{"Nesso - local semantic search for unstructured text"};
        app.require_subcommand(1);

        std::string dbPath = "vectors.nesso";
        app.add_option("-p,--path", dbPath, "Path to the vector database storage file");

        uint64_t dimensions = 128;
        app.add_option("-d,--dims", dimensions, "Dimensionality of the vector space")->default_val(128);

        auto *initCmd = app.add_subcommand("init", "Initialize an empty database index container");

        std::string inputFile;
        auto *indexCmd = app.add_subcommand("index", "Ingest external raw vector binary data");
        indexCmd->add_option("-f,--file", inputFile, "Path to the raw floating-point binary file")
            ->required()
            ->check(CLI::ExistingFile);

        std::string queryFile;
        size_t searchTopK = 10;
        auto *searchCmd = app.add_subcommand("search", "Return the nearest vectors by cosine similarity");
        searchCmd->add_option("-q,--query-file", queryFile, "Path to one raw floating-point query vector")
            ->required()
            ->check(CLI::ExistingFile);
        searchCmd->add_option("-k,--top-k", searchTopK, "Maximum number of nearest vectors to return")->default_val(10);

        std::string queryText;
        std::vector<std::string> fileArgs;
        size_t grepTopK = 5;
        std::string modelDir = "models";
        auto *grepCmd = app.add_subcommand("grep", "Semantic search over .log, .json, and .jsonl files. "
                                                   "Skips empty lines and log lines longer than 4096 characters. "
                                                   "JSON objects must have a string 'message' field. "
                                                   "Directories are not searched.");
        grepCmd->add_option("query", queryText, "Natural-language query")->required();
        grepCmd->add_option("files", fileArgs, "One or more .log, .json, or .jsonl files")
            ->required()
            ->expected(1, -1)
            ->check(CLI::ExistingFile);
        grepCmd->add_option("-k,--top-k", grepTopK, "Maximum number of matches to return (default: 5)")->default_val(5);
        grepCmd->add_option("--model-dir", modelDir, "Directory containing model.onnx and vocab.txt")
            ->default_val("models");

        CLI11_PARSE(app, argc, argv);

        if (grepCmd->parsed())
        {
            std::vector<std::filesystem::path> files;
            files.reserve(fileArgs.size());
            for (const std::string &fileArg : fileArgs)
            {
                files.emplace_back(fileArg);
            }
            return nesso::commands::runGrep(queryText, files, grepTopK, modelDir);
        }

        core::StorageEngine<float> engine;

        if (initCmd->parsed())
        {
            return nesso::commands::runInit(engine, dbPath, dimensions);
        }
        if (indexCmd->parsed())
        {
            return nesso::commands::runIndex(engine, dbPath, dimensions, inputFile);
        }
        if (searchCmd->parsed())
        {
            return nesso::commands::runSearch(engine, dbPath, dimensions, queryFile, searchTopK);
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
