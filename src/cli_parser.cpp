// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cli_parser.hpp>

#include <parser/archive_source.hpp>

#include <embed/onnx_embedder.hpp>

#include <CLI/CLI.hpp>

#include <charconv>
#include <cstdint>
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
                              if (std::filesystem::is_regular_file(value, error) ||
                                  std::filesystem::is_directory(value, error))
                              {
                                  return std::string{};
                              }
                              return std::string{"file does not exist"};
                          },
                          "FILE", "existing file or directory, or - for stdin"};
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

static constexpr const char *TEXT_FILES_HELP = "Text files, directories, or tar/zip bundles. - reads stdin as lines";

static CLI::Validator positiveUint64()
{
    return CLI::Validator{[](std::string &value)
                          {
                              if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos ||
                                  value.find_first_not_of('0') == std::string::npos)
                              {
                                  return std::string{"must be a positive integer"};
                              }
                              std::uint64_t parsed = 0;
                              const char *const first = value.data();
                              const char *const last = first + value.size();
                              const std::from_chars_result result = std::from_chars(first, last, parsed);
                              if (result.ec != std::errc{} || result.ptr != last)
                              {
                                  return std::string{"must be a positive integer"};
                              }
                              return std::string{};
                          },
                          "POSITIVE", "positive integer"};
}

static void addArchiveLimits(CLI::App *command, nesso::parser::ArchiveLimits &limits)
{
    command->add_option("--archive-max-entry-bytes", limits.maxEntryBytes, "Maximum bytes of one archive entry")
        ->default_val(limits.maxEntryBytes)
        ->check(positiveUint64());
    command->add_option("--archive-max-bytes", limits.maxTotalBytes, "Maximum uncompressed bytes from one archive")
        ->default_val(limits.maxTotalBytes)
        ->check(positiveUint64());
    command->add_option("--archive-max-entries", limits.maxEntries, "Maximum regular-file entries in one archive")
        ->default_val(limits.maxEntries)
        ->check(positiveUint64());
    command->add_option("--archive-max-ratio", limits.maxRatio, "Maximum uncompressed-to-archive size ratio")
        ->default_val(limits.maxRatio)
        ->check(positiveUint64());
}

static constexpr const char *THREADS_HELP = "CPU budget N. The full rules are in docs/usage.md.";

static constexpr const char *HELP_FOOTER = "Examples:\n"
                                           "  nesso grep \"database connection error\" app.log\n"
                                           "  nesso grep \"payment timeout\" app.log events.jsonl -k 5\n"
                                           "  nesso index -o corpus.nesso app.log\n"
                                           "  nesso search \"payment timeout\" -i corpus.nesso\n"
                                           "\n"
                                           "Exit codes: 0 matches printed or corpus written;\n"
                                           "1 nothing to search or index; 2 error.";

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
    bool json = false;
    bool group = false;
    size_t threads = 0;
    std::string mode = "auto";
    nesso::parser::ArchiveLimits grepArchiveLimits{};
    nesso::parser::ArchiveLimits indexArchiveLimits{};
};

static SearchMode parseSearchMode(std::string_view mode)
{
    if (mode == "hybrid")
    {
        return SearchMode::Hybrid;
    }
    if (mode == "dense")
    {
        return SearchMode::Dense;
    }
    if (mode == "lexical")
    {
        return SearchMode::Lexical;
    }
    return SearchMode::Auto;
}

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
    app.get_formatter()->enable_footer_formatting(false);
    app.footer(HELP_FOOTER);
    app.require_subcommand(1);

    auto *grepCmd = app.add_subcommand("grep", "Semantic search of text files. See docs/usage.md.");
    grepCmd->add_option("query", text.query, "Natural-language query")->required();
    grepCmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(inputFile());
    grepCmd->add_option("--json-field", text.jsonField, "JSON object field to read")->default_val("message");
    grepCmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return, or groups with --group")
        ->default_val(5)
        ->check(positiveCount());
    grepCmd->add_flag("--quiet", text.quiet, "Suppress interactive progress output");
    grepCmd->add_flag("--json", text.json, "Print one JSON object per match");
    grepCmd->add_flag("--group", text.group, "Print one row per masked dedup group");
    grepCmd->add_option("--mode", text.mode, "Ranking mode: auto, hybrid, dense, or lexical")
        ->default_val("auto")
        ->check(CLI::IsMember({"auto", "hybrid", "dense", "lexical"}));
    const auto *grepThreads = grepCmd->add_option("--threads", text.threads, THREADS_HELP)->check(positiveCount());
    const auto *grepModelDir = grepCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);
    addArchiveLimits(grepCmd, text.grepArchiveLimits);

    auto *indexCmd = app.add_subcommand("index", "Embed text into a corpus file. See docs/usage.md.");
    indexCmd->add_option("-o,--output", text.output, "Corpus file to write")->required();
    indexCmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(inputFile());
    indexCmd->add_option("--json-field", text.jsonField, "JSON object field to read")->default_val("message");
    indexCmd->add_flag("--quiet", text.quiet, "Suppress interactive progress output");
    const auto *indexThreads = indexCmd->add_option("--threads", text.threads, THREADS_HELP)->check(positiveCount());
    const auto *indexModelDir = indexCmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);
    addArchiveLimits(indexCmd, text.indexArchiveLimits);

    auto *searchCmd = app.add_subcommand("search", "Semantic search of a corpus file. Prints matches only.");
    searchCmd->add_option("query", text.query, "Natural-language query")->required();
    searchCmd->add_option("-i,--index", text.index, "Corpus file to search")->required()->check(CLI::ExistingFile);
    searchCmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return, or groups with --group")
        ->default_val(5)
        ->check(positiveCount());
    searchCmd->add_flag("--json", text.json, "Print one JSON object per match");
    searchCmd->add_flag("--group", text.group, "Print one row per masked dedup group");
    searchCmd->add_option("--mode", text.mode, "Ranking mode: auto, hybrid, dense, or lexical")
        ->default_val("auto")
        ->check(CLI::IsMember({"auto", "hybrid", "dense", "lexical"}));
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
                                                    .progress = !text.quiet,
                                                    .json = text.json,
                                                    .mode = parseSearchMode(text.mode),
                                                    .group = text.group,
                                                    .archiveLimits = text.grepArchiveLimits}};
    }
    if (indexCmd->parsed())
    {
        return ParsedCommand{.config = configFrom(*modelDirOpt, text, threads),
                             .request = IndexRequest{.files = toPaths(text.files),
                                                     .output = text.output,
                                                     .jsonField = text.jsonField,
                                                     .progress = !text.quiet,
                                                     .archiveLimits = text.indexArchiveLimits}};
    }
    return ParsedCommand{.config = configFrom(*modelDirOpt, text, threads),
                         .request = SearchRequest{.query = text.query,
                                                  .index = text.index,
                                                  .topK = text.topK,
                                                  .json = text.json,
                                                  .mode = parseSearchMode(text.mode),
                                                  .group = text.group}};
}

} // namespace nesso::cli
