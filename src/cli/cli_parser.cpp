// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cli/cli_parser.hpp>

#include <parser/archive_source.hpp>

#include <embed/onnx_embedder.hpp>

#include <cli/thread_count.hpp>

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

static constexpr const char *THREADS_HELP = "CPU budget N from 1 to 1024. Default and layout rules: docs/usage.md.";

// NOLINTNEXTLINE(readability-identifier-naming)
static CLI::Validator MakeThreadCountValidator()
{
    return CLI::Validator{[](std::string &value)
                          {
                              const auto parsed = ThreadCount::parse(value);
                              if (!parsed)
                              {
                                  if (parsed.error() == ThreadCountError::OutOfRange)
                                  {
                                      return std::string{"must be an integer from 1 to 1024"};
                                  }
                                  return std::string{"must be a positive integer"};
                              }
                              return std::string{};
                          },
                          "THREADS", "integer from 1 to 1024"};
}

static void printThreadsEnvironmentError(ThreadCountError error)
{
    if (error == ThreadCountError::OutOfRange)
    {
        std::cerr << "NESSO_THREADS must be an integer from 1 to 1024\n";
        return;
    }
    std::cerr << "NESSO_THREADS must be a positive integer\n";
}

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

static std::expected<size_t, ThreadCountError> threadsFromEnvironment()
{
    // getenv is not thread-safe. The CLI reads the environment once on the main thread.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *value = std::getenv("NESSO_THREADS");
    if (value == nullptr || value[0] == '\0')
    {
        return 0;
    }
    const auto parsed = ThreadCount::parse(value);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }
    return parsed->get();
}

static void configureAppVersion(CLI::App &app, bool &verbose)
{
    app.add_flag("--verbose", verbose, "With --version, print the embedding session policy");
    app.set_version_flag("-V,--version",
                         [&verbose]()
                         {
                             const auto fromEnv = threadsFromEnvironment();
                             if (!fromEnv)
                             {
                                 printThreadsEnvironmentError(fromEnv.error());
                                 throw CLI::RuntimeError(2);
                             }
                             const size_t threads = *fromEnv;
                             std::string version{NESSO_VERSION};
                             if (verbose)
                             {
                                 version.push_back('\n');
                                 version += nesso::embed::sessionPolicyText(threads, nesso::embed::readMachine());
                             }
                             return version;
                         });
}

static CLI::App *addGrepSubcommand(CLI::App &app, TextOptions &text, const CLI::Option *&threadsOpt,
                                   const CLI::Option *&modelDirOpt)
{
    auto *cmd = app.add_subcommand("grep", "Semantic search of text files. See docs/usage.md.");
    cmd->add_option("query", text.query, "Natural-language query")->required();
    cmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(inputFile());
    cmd->add_option("--json-field", text.jsonField, "JSON object field to read")->default_val("message");
    cmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return, or groups with --group")
        ->default_val(5)
        ->check(positiveCount());
    cmd->add_flag("--quiet", text.quiet, "Suppress interactive progress output");
    cmd->add_flag("--json", text.json, "Print one JSON object per match");
    cmd->add_flag("--group", text.group, "Print one row per masked dedup group");
    cmd->add_option("--mode", text.mode, "Ranking mode: auto, hybrid, dense, or lexical")
        ->default_val("auto")
        ->check(CLI::IsMember({"auto", "hybrid", "dense", "lexical"}));
    threadsOpt = cmd->add_option("--threads", text.threads, THREADS_HELP)->check(MakeThreadCountValidator());
    modelDirOpt = cmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);
    addArchiveLimits(cmd, text.grepArchiveLimits);
    return cmd;
}

static CLI::App *addIndexSubcommand(CLI::App &app, TextOptions &text, const CLI::Option *&threadsOpt,
                                    const CLI::Option *&modelDirOpt)
{
    auto *cmd = app.add_subcommand("index", "Embed text into a corpus file. See docs/usage.md.");
    cmd->add_option("-o,--output", text.output, "Corpus file to write")->required();
    cmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(inputFile());
    cmd->add_option("--json-field", text.jsonField, "JSON object field to read")->default_val("message");
    cmd->add_flag("--quiet", text.quiet, "Suppress interactive progress output");
    threadsOpt = cmd->add_option("--threads", text.threads, THREADS_HELP)->check(MakeThreadCountValidator());
    modelDirOpt = cmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);
    addArchiveLimits(cmd, text.indexArchiveLimits);
    return cmd;
}

static CLI::App *addSearchSubcommand(CLI::App &app, TextOptions &text, const CLI::Option *&threadsOpt,
                                     const CLI::Option *&modelDirOpt)
{
    auto *cmd = app.add_subcommand("search", "Semantic search of a corpus file. Prints matches only.");
    cmd->add_option("query", text.query, "Natural-language query")->required();
    cmd->add_option("-i,--index", text.index, "Corpus file to search")->required()->check(CLI::ExistingFile);
    cmd->add_option("-k,--top-k", text.topK, "Maximum number of matches to return, or groups with --group")
        ->default_val(5)
        ->check(positiveCount());
    cmd->add_flag("--json", text.json, "Print one JSON object per match");
    cmd->add_flag("--group", text.group, "Print one row per masked dedup group");
    cmd->add_option("--mode", text.mode, "Ranking mode: auto, hybrid, dense, or lexical")
        ->default_val("auto")
        ->check(CLI::IsMember({"auto", "hybrid", "dense", "lexical"}));
    threadsOpt = cmd->add_option("--threads", text.threads, THREADS_HELP)->check(MakeThreadCountValidator());
    modelDirOpt = cmd->add_option("--model-dir", text.modelDir, MODEL_DIR_HELP);
    return cmd;
}

static std::expected<size_t, int> resolveThreads(const CLI::Option &threadsOpt, size_t flagged)
{
    if (!threadsOpt.empty())
    {
        return flagged;
    }
    const auto fromEnv = threadsFromEnvironment();
    if (!fromEnv)
    {
        printThreadsEnvironmentError(fromEnv.error());
        return std::unexpected(2);
    }
    return *fromEnv;
}

static ParsedCommand makeParsedCommand(const CLI::App &grepCmd, const CLI::App &indexCmd,
                                       const CLI::Option &modelDirOpt, const TextOptions &text, size_t threads)
{
    if (grepCmd.parsed())
    {
        return ParsedCommand{.config = configFrom(modelDirOpt, text, threads),
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
    if (indexCmd.parsed())
    {
        return ParsedCommand{.config = configFrom(modelDirOpt, text, threads),
                             .request = IndexRequest{.files = toPaths(text.files),
                                                     .output = text.output,
                                                     .jsonField = text.jsonField,
                                                     .progress = !text.quiet,
                                                     .archiveLimits = text.indexArchiveLimits}};
    }
    return ParsedCommand{.config = configFrom(modelDirOpt, text, threads),
                         .request = SearchRequest{.query = text.query,
                                                  .index = text.index,
                                                  .topK = text.topK,
                                                  .json = text.json,
                                                  .mode = parseSearchMode(text.mode),
                                                  .group = text.group}};
}

std::expected<ParsedCommand, int> parseCommandLine(int argc, char **argv)
{
    TextOptions text;
    CLI::App app{"Nesso - local semantic search for unstructured text"};
    bool verbose = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::string_view{argv[index]} == "--verbose")
        {
            verbose = true;
        }
    }
    configureAppVersion(app, verbose);
    app.get_formatter()->enable_footer_formatting(false);
    app.footer(HELP_FOOTER);
    app.require_subcommand(1);

    const CLI::Option *grepThreads = nullptr;
    const CLI::Option *grepModelDir = nullptr;
    const CLI::Option *indexThreads = nullptr;
    const CLI::Option *indexModelDir = nullptr;
    const CLI::Option *searchThreads = nullptr;
    const CLI::Option *searchModelDir = nullptr;
    const CLI::App *grepCmd = addGrepSubcommand(app, text, grepThreads, grepModelDir);
    const CLI::App *indexCmd = addIndexSubcommand(app, text, indexThreads, indexModelDir);
    const CLI::App *searchCmd = addSearchSubcommand(app, text, searchThreads, searchModelDir);

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

    const auto threads = resolveThreads(*threadsOpt, text.threads);
    if (!threads)
    {
        return std::unexpected(threads.error());
    }
    return makeParsedCommand(*grepCmd, *indexCmd, *modelDirOpt, text, *threads);
}

} // namespace nesso::cli
