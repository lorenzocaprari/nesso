// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "grep_command.hpp"

#include "command_output.hpp"

#include <nesso/model_paths.hpp>

#include <CLI/CLI.hpp>

#include <memory>
#include <string>
#include <vector>

namespace nesso::commands
{

int runGrep(const Nesso &nesso, const GrepRequest &request)
{
    const auto results = nesso.grep(request);
    if (!results)
    {
        return reportError(results.error());
    }
    reportSkippedLines(results->skippedLines);
    printTextMatches(*results);
    return 0;
}

void addGrepCommand(CLI::App &app)
{
    struct Options
    {
        std::string query;
        std::vector<std::string> files;
        size_t topK = DEFAULT_TEXT_TOP_K;
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
            Config config;
            if (!modelDirOpt->empty())
            {
                config.modelDir = opts->modelDir;
            }
            const Nesso nesso{std::move(config)};
            const int code = runGrep(
                nesso, {.query = opts->query, .files = {opts->files.begin(), opts->files.end()}, .topK = opts->topK});
            if (code != 0)
            {
                throw CLI::RuntimeError(code);
            }
        });
}

} // namespace nesso::commands
