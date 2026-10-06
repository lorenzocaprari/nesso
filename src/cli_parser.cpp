// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "cli_parser.hpp"

#include <CLI/CLI.hpp>

#include <string>
#include <vector>

#ifndef NESSO_VERSION
#define NESSO_VERSION "0.0.0"
#endif

namespace nesso::cli
{

static constexpr const char *MODEL_DIR_HELP = "Directory containing model.onnx and vocab.txt. "
                                              "Default: NESSO_MODEL_DIR, ./models, XDG data, /usr/share/nesso";

static constexpr const char *TEXT_FILES_HELP = "One or more .log, .json, or .jsonl files";

struct TextOptions
{
    std::string query;
    std::vector<std::string> files;
    std::string output;
    std::string index;
    size_t topK = DEFAULT_TEXT_TOP_K;
    std::string modelDir;
};

static std::vector<std::filesystem::path> toPaths(const std::vector<std::string> &args)
{
    return {args.begin(), args.end()};
}

static Config configFrom(const CLI::Option &modelDirOpt, const TextOptions &text)
{
    Config config;
    if (!modelDirOpt.empty())
    {
        config.modelDir = text.modelDir;
    }
    return config;
}

std::expected<ParsedCommand, int> parseCommandLine(int argc, char **argv)
{
    TextOptions text;

    CLI::App app{"Nesso - local semantic search for unstructured text"};
    app.set_version_flag("-V,--version", NESSO_VERSION);
    app.require_subcommand(1);

    auto *grepCmd = app.add_subcommand("grep", "Semantic search over .log, .json, and .jsonl files. "
                                               "Skips empty lines and log lines longer than 4096 characters. "
                                               "JSON objects must have a string 'message' field. "
                                               "Directories are not searched.");
    grepCmd->add_option("query", text.query, "Natural-language query")->required();
    grepCmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(CLI::ExistingFile);
    grepCmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return (default: 5)")->default_val(5);
    const auto *grepModelDir = grepCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);

    auto *indexCmd = app.add_subcommand("index", "Embed .log, .json, and .jsonl files into a corpus file. "
                                                 "Skips empty lines and log lines longer than 4096 characters.");
    indexCmd->add_option("-o,--output", text.output, "Corpus file to write")->required();
    indexCmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(CLI::ExistingFile);
    const auto *indexModelDir = indexCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);

    auto *searchCmd = app.add_subcommand("search", "Semantic search of a corpus file. Prints matches only.");
    searchCmd->add_option("query", text.query, "Natural-language query")->required();
    searchCmd->add_option("-i,--index", text.index, "Corpus file to search")->required()->check(CLI::ExistingFile);
    searchCmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return (default: 5)")->default_val(5);
    const auto *searchModelDir = searchCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &error)
    {
        return std::unexpected(app.exit(error));
    }

    if (grepCmd->parsed())
    {
        return ParsedCommand{.config = configFrom(*grepModelDir, text),
                             .request =
                                 GrepRequest{.query = text.query, .files = toPaths(text.files), .topK = text.topK}};
    }
    if (indexCmd->parsed())
    {
        return ParsedCommand{.config = configFrom(*indexModelDir, text),
                             .request = IndexRequest{.files = toPaths(text.files), .output = text.output}};
    }
    return ParsedCommand{.config = configFrom(*searchModelDir, text),
                         .request = SearchRequest{.query = text.query, .index = text.index, .topK = text.topK}};
}

} // namespace nesso::cli
