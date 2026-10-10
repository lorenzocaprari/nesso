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

static CLI::Validator positiveCount()
{
    return CLI::Validator{[](std::string &value)
                          {
                              if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos ||
                                  value.find_first_not_of('0') == std::string::npos)
                              {
                                  return std::string{"must be a positive integer"};
                              }
                              return std::string{};
                          },
                          "POSITIVE", "positive integer"};
}

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
    std::string jsonField = "message";
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
    app.footer("Exit codes: 0 when matches are printed or index succeeds, 1 when nothing matches, 2 on error.");
    app.require_subcommand(1);

    auto *grepCmd = app.add_subcommand("grep", "Semantic search over .log, .json, and .jsonl files. "
                                               "Skips empty lines. Log lines and JSON string values longer than 4096 "
                                               "characters are truncated. JSON objects must have a string field "
                                               "(default: message). Directories are not searched.");
    grepCmd->add_option("query", text.query, "Natural-language query")->required();
    grepCmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(CLI::ExistingFile);
    grepCmd->add_option("--json-field", text.jsonField, "JSON object field to read (default: message)")
        ->default_val("message");
    grepCmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return (default: 5)")
        ->default_val(5)
        ->check(positiveCount());
    const auto *grepModelDir = grepCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);

    auto *indexCmd = app.add_subcommand("index", "Embed .log, .json, and .jsonl files into a corpus file. "
                                                 "Skips empty lines. Log lines and JSON string values longer than "
                                                 "4096 characters are truncated.");
    indexCmd->add_option("-o,--output", text.output, "Corpus file to write")->required();
    indexCmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(CLI::ExistingFile);
    indexCmd->add_option("--json-field", text.jsonField, "JSON object field to read (default: message)")
        ->default_val("message");
    const auto *indexModelDir = indexCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);

    auto *searchCmd = app.add_subcommand("search", "Semantic search of a corpus file. Prints matches only.");
    searchCmd->add_option("query", text.query, "Natural-language query")->required();
    searchCmd->add_option("-i,--index", text.index, "Corpus file to search")->required()->check(CLI::ExistingFile);
    searchCmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return (default: 5)")
        ->default_val(5)
        ->check(positiveCount());
    const auto *searchModelDir = searchCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &error)
    {
        const int code = app.exit(error);
        return std::unexpected(code == 0 ? 0 : 2);
    }

    if (grepCmd->parsed())
    {
        return ParsedCommand{
            .config = configFrom(*grepModelDir, text),
            .request = GrepRequest{
                .query = text.query, .files = toPaths(text.files), .topK = text.topK, .jsonField = text.jsonField}};
    }
    if (indexCmd->parsed())
    {
        return ParsedCommand{
            .config = configFrom(*indexModelDir, text),
            .request = IndexRequest{.files = toPaths(text.files), .output = text.output, .jsonField = text.jsonField}};
    }
    return ParsedCommand{.config = configFrom(*searchModelDir, text),
                         .request = SearchRequest{.query = text.query, .index = text.index, .topK = text.topK}};
}

} // namespace nesso::cli
