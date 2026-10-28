// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cli_parser.hpp>

#include <embed/onnx_embedder.hpp>

#include <CLI/CLI.hpp>

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef NESSO_VERSION
#define NESSO_VERSION "0.0.0"
#endif

namespace nesso::cli
{

static CLI::Validator inputFile()
{
    return CLI::Validator{[](std::string &value)
                          {
                              if (value == "-")
                              {
                                  return std::string{};
                              }
                              std::error_code error;
                              if (!std::filesystem::is_regular_file(value, error))
                              {
                                  return std::string{"file does not exist"};
                              }
                              return std::string{};
                          },
                          "FILE", "existing file, or - for stdin"};
}

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

static constexpr const char *TEXT_FILES_HELP = "Text files to read. - reads stdin as lines";

static constexpr const char *THREADS_HELP =
    "CPU budget N. On x86, int8 uses one intra-op thread and N sessions when N is 4 or less. A larger "
    "budget uses I = min(4, N/2) intra-op threads and max(1, N/I) sessions, and shrinks the arena. "
    "On aarch64 I is at least 2, including when N is 1. The default N is the usable physical core count "
    "after affinity and cgroup cpu.max. For fp32, N is the intra-op thread count of the one session and the "
    "default is the logical CPU count. NESSO_THREADS supplies N when --threads is omitted.";

struct TextOptions
{
    std::string query;
    std::vector<std::string> files;
    std::string output;
    std::string index;
    size_t topK = DEFAULT_TEXT_TOP_K;
    std::string modelDir;
    std::string jsonField = "message";
    bool quiet = false;
    size_t threads = 0;
};

static std::vector<std::filesystem::path> toPaths(const std::vector<std::string> &args)
{
    return {args.begin(), args.end()};
}

static Config configFrom(const CLI::Option &modelDirOpt, const TextOptions &text, size_t threads)
{
    Config config;
    if (!modelDirOpt.empty())
    {
        config.modelDir = text.modelDir;
    }
    config.threads = threads;
    return config;
}

static std::expected<size_t, int> threadsFromEnvironment()
{
    // getenv is not thread-safe. The CLI reads the environment once on the main thread.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *value = std::getenv("NESSO_THREADS");
    if (value == nullptr || value[0] == '\0')
    {
        return 0;
    }
    const std::string text{value};
    if (text.find_first_not_of("0123456789") != std::string::npos || text.find_first_not_of('0') == std::string::npos)
    {
        return std::unexpected(2);
    }
    size_t parsed = 0;
    const char *const first = text.data();
    const char *const last = first + text.size();
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last)
    {
        return std::unexpected(2);
    }
    return parsed;
}

std::expected<ParsedCommand, int> parseCommandLine(int argc, char **argv)
{
    TextOptions text;

    CLI::App app{"Nesso - local semantic search for unstructured text"};
    bool verbose = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument == "--verbose")
        {
            verbose = true;
        }
    }
    app.add_flag("--verbose", verbose, "With --version, print the embedding session policy");
    app.set_version_flag("-V,--version",
                         [verbose]()
                         {
                             const size_t threads = threadsFromEnvironment().value_or(0);
                             std::string version{NESSO_VERSION};
                             if (verbose)
                             {
                                 version.push_back('\n');
                                 version += nesso::embed::sessionPolicyText(threads, nesso::embed::readMachine());
                             }
                             return version;
                         });
    app.footer("Exit codes: 0 when matches are printed or index succeeds, 1 when nothing matches, 2 on error.");
    app.require_subcommand(1);

    auto *grepCmd = app.add_subcommand("grep", "Semantic search over text files. .json and .jsonl are JSON; every "
                                               "other name, including no extension and rotations, is read as lines. "
                                               "- reads stdin. Skips empty lines. Lines and JSON string values longer "
                                               "than 4096 characters are truncated. JSON objects must have a string "
                                               "field (default: message). Directories are not searched.");
    grepCmd->add_option("query", text.query, "Natural-language query")->required();
    grepCmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(inputFile());
    grepCmd->add_option("--json-field", text.jsonField, "JSON object field to read (default: message)")
        ->default_val("message");
    grepCmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return (default: 5)")
        ->default_val(5)
        ->check(positiveCount());
    grepCmd->add_flag("--quiet", text.quiet, "Suppress interactive progress output");
    const auto *grepThreads = grepCmd->add_option("--threads", text.threads, THREADS_HELP)->check(positiveCount());
    const auto *grepModelDir = grepCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);

    auto *indexCmd = app.add_subcommand("index", "Embed text files into a corpus file. .json and .jsonl are JSON; "
                                                 "every other name is read as lines. - reads stdin. Skips empty lines. "
                                                 "Lines and JSON string values longer than 4096 characters are "
                                                 "truncated.");
    indexCmd->add_option("-o,--output", text.output, "Corpus file to write")->required();
    indexCmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(inputFile());
    indexCmd->add_option("--json-field", text.jsonField, "JSON object field to read (default: message)")
        ->default_val("message");
    indexCmd->add_flag("--quiet", text.quiet, "Suppress interactive progress output");
    const auto *indexThreads = indexCmd->add_option("--threads", text.threads, THREADS_HELP)->check(positiveCount());
    const auto *indexModelDir = indexCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);

    auto *searchCmd = app.add_subcommand("search", "Semantic search of a corpus file. Prints matches only.");
    searchCmd->add_option("query", text.query, "Natural-language query")->required();
    searchCmd->add_option("-i,--index", text.index, "Corpus file to search")->required()->check(CLI::ExistingFile);
    searchCmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return (default: 5)")
        ->default_val(5)
        ->check(positiveCount());
    const auto *searchThreads = searchCmd->add_option("--threads", text.threads, THREADS_HELP)->check(positiveCount());
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

    const CLI::Option *threadsOpt = grepThreads;
    const CLI::Option *modelDirOpt = grepModelDir;
    if (indexCmd->parsed())
    {
        threadsOpt = indexThreads;
        modelDirOpt = indexModelDir;
    }
    else if (searchCmd->parsed())
    {
        threadsOpt = searchThreads;
        modelDirOpt = searchModelDir;
    }

    size_t threads = text.threads;
    if (threadsOpt->empty())
    {
        const auto fromEnv = threadsFromEnvironment();
        if (!fromEnv)
        {
            std::cerr << "NESSO_THREADS must be a positive integer\n";
            return std::unexpected(fromEnv.error());
        }
        threads = *fromEnv;
    }

    if (grepCmd->parsed())
    {
        return ParsedCommand{.config = configFrom(*modelDirOpt, text, threads),
                             .request = GrepRequest{.query = text.query,
                                                    .files = toPaths(text.files),
                                                    .topK = text.topK,
                                                    .jsonField = text.jsonField,
                                                    .progress = !text.quiet}};
    }
    if (indexCmd->parsed())
    {
        return ParsedCommand{.config = configFrom(*modelDirOpt, text, threads),
                             .request = IndexRequest{.files = toPaths(text.files),
                                                     .output = text.output,
                                                     .jsonField = text.jsonField,
                                                     .progress = !text.quiet}};
    }
    return ParsedCommand{.config = configFrom(*modelDirOpt, text, threads),
                         .request = SearchRequest{.query = text.query, .index = text.index, .topK = text.topK}};
}

} // namespace nesso::cli
