// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE for details.

#include <CLI/CLI.hpp>
#include <mach_core/core_types.hpp>
#include <mach_core/embedder.hpp>
#include <mach_core/log_index.hpp>

#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <string>

int main(int argc, char *argv[]) noexcept
{
    try
    {
        std::ios_base::sync_with_stdio(false);

        CLI::App app{"VecGrep - offline hybrid search for system and robotics logs"};
        app.require_subcommand(1);

        std::string dbPath = "index.vecgrep";
        std::string modelPath;
        std::string vocabPath;

        auto *indexCmd = app.add_subcommand("index", "Ingest a UTF-8 log file into the hybrid index");
        std::string logFile;
        indexCmd->add_option("-f,--log-file", logFile, "Path to the log file")->required()->check(CLI::ExistingFile);
        bool syncIngest = false;
        indexCmd->add_flag("--sync", syncIngest, "Use single-threaded ingest instead of the jthread pipeline");
        indexCmd->add_option("-p,--path", dbPath, "Path to the VecGrep index");
        indexCmd->add_option("-m,--model", modelPath, "INT8 ONNX MiniLM model (omit to use the hash stub embedder)");
        indexCmd->add_option("--vocab", vocabPath, "WordPiece vocab.txt for the ONNX model");

        auto *searchCmd = app.add_subcommand("search", "Hybrid search (semantic + BM25 fused with RRF)");
        std::string query;
        searchCmd->add_option("-q,--query", query, "Query string")->required();
        size_t topK = 10;
        searchCmd->add_option("-k,--top-k", topK, "Maximum number of hits to return")->default_val(10);
        searchCmd->add_option("-p,--path", dbPath, "Path to the VecGrep index");
        searchCmd->add_option("-m,--model", modelPath, "INT8 ONNX MiniLM model (omit to use the hash stub embedder)");
        searchCmd->add_option("--vocab", vocabPath, "WordPiece vocab.txt for the ONNX model");

        CLI11_PARSE(app, argc, argv);

        auto embedder = mach_core::makeEmbedder(modelPath, vocabPath);
        if (!embedder)
        {
            std::println(std::cerr, "Error: failed to load embedder. Code: {}",
                         static_cast<int>(embedder.error()));
            return 1;
        }

        mach_core::LogIndex index;
        auto opened = index.open(dbPath);
        if (!opened)
        {
            std::println(std::cerr, "Error: failed to open index '{}'. Code: {}", dbPath,
                         static_cast<int>(opened.error()));
            return 1;
        }

        if (indexCmd->parsed())
        {
            auto added = index.ingestFile(logFile, **embedder, !syncIngest);
            if (!added)
            {
                std::println(std::cerr, "Error: ingest failed. Code: {}", static_cast<int>(added.error()));
                return 1;
            }
            std::println("Indexed {} lines from '{}'. Total vectors: {}", *added, logFile, index.vectorCount());
            return 0;
        }

        auto hits = index.search(query, topK, **embedder);
        if (!hits)
        {
            std::println(std::cerr, "Error: search failed. Code: {}", static_cast<int>(hits.error()));
            return 1;
        }

        std::println("LINE\tRRF\tTEXT");
        for (const auto &hit : *hits)
        {
            std::println("{}\t{:.6f}\t{}", hit.lineNo, hit.rrfScore, hit.text);
        }
        return 0;
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
