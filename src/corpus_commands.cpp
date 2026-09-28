// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "corpus_commands.hpp"

#include "command_output.hpp"

#include <nesso/model_paths.hpp>

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <vector>

namespace nesso::commands
{

int runCorpusIndex(const Nesso &nesso, const IndexRequest &request)
{
    const auto summary = nesso.index(request);
    if (!summary)
    {
        return reportError(summary.error());
    }
    reportSkippedLines(summary->skippedLines);
    std::println(std::cerr, "Indexed {} chunks into '{}'.", summary->chunks, request.output.string());
    return 0;
}

int runCorpusSearch(const Nesso &nesso, const SearchRequest &request)
{
    const auto results = nesso.search(request);
    if (!results)
    {
        return reportError(results.error());
    }
    printTextMatches(*results);
    return 0;
}

static Config configFrom(const CLI::Option *modelDirOpt, const std::string &modelDir)
{
    Config config;
    if (!modelDirOpt->empty())
    {
        config.modelDir = modelDir;
    }
    return config;
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
            const Nesso nesso{configFrom(modelDirOpt, opts->modelDir)};
            const int code =
                runCorpusIndex(nesso, {.files = {opts->files.begin(), opts->files.end()}, .output = opts->output});
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
        size_t topK = DEFAULT_TEXT_TOP_K;
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
            const Nesso nesso{configFrom(modelDirOpt, opts->modelDir)};
            const int code =
                runCorpusSearch(nesso, {.query = opts->query, .index = opts->indexPath, .topK = opts->topK});
            if (code != 0)
            {
                throw CLI::RuntimeError(code);
            }
        });
}

} // namespace nesso::commands
