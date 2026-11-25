// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "embed/session_policy.hpp"
#include "nesso/nesso.hpp"
#include "nesso/overview.hpp"
#include <nesso_cli/cli.hpp>
#include <nesso_cli/cli_parser.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <parser/archive_source.hpp>

#include <nesso_cli/thread_count.hpp>

#include <CLI/CLI.hpp>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef NESSO_VERSION
#define NESSO_VERSION "0.0.0"
#endif

namespace nesso::cli
{

static auto InputFile() -> CLI::Validator
{
    return CLI::Validator{[](std::string &value) -> std::string
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

static auto PositiveCount() -> CLI::Validator
{
    return CLI::Validator{[](std::string &value) -> std::string
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
                                              "Default: NESSO_MODEL_DIR, ./models, user data, packaged models";

static constexpr const char *TEXT_FILES_HELP = "Text files, directories, or tar/zip bundles. - reads stdin as lines";

static auto PositiveUint64() -> CLI::Validator
{
    return CLI::Validator{[](std::string &value) -> std::string
                          {
                              if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos ||
                                  value.find_first_not_of('0') == std::string::npos)
                              {
                                  return std::string{"must be a positive integer"};
                              }
                              std::uint64_t parsed = 0;
                              const char *const first = std::to_address(value.begin());
                              const char *const last = std::to_address(value.end());
                              const std::from_chars_result result = std::from_chars(first, last, parsed);
                              if (result.ec != std::errc{} || result.ptr != last)
                              {
                                  return std::string{"must be a positive integer"};
                              }
                              return std::string{};
                          },
                          "POSITIVE", "positive integer"};
}

static void AddArchiveLimits(CLI::App *command, nesso::parser::ArchiveLimits &limits)
{
    command->add_option("--archive-max-entry-bytes", limits.max_entry_bytes, "Maximum bytes of one archive entry")
        ->default_val(limits.max_entry_bytes)
        ->check(PositiveUint64());
    command->add_option("--archive-max-bytes", limits.max_total_bytes, "Maximum uncompressed bytes from one archive")
        ->default_val(limits.max_total_bytes)
        ->check(PositiveUint64());
    command->add_option("--archive-max-entries", limits.max_entries, "Maximum regular-file entries in one archive")
        ->default_val(limits.max_entries)
        ->check(PositiveUint64());
    command->add_option("--archive-max-ratio", limits.max_ratio, "Maximum uncompressed-to-archive size ratio")
        ->default_val(limits.max_ratio)
        ->check(PositiveUint64());
}

static constexpr const char *THREADS_HELP = "CPU budget N from 1 to 1024. Default and layout rules: docs/usage.md.";

static auto MakeThreadCountValidator() -> CLI::Validator
{
    return CLI::Validator{[](std::string &value) -> std::string
                          {
                              const auto parsed = ThreadCount::Parse(value);
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

static void PrintThreadsEnvironmentError(ThreadCountError error)
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
                                           "  nesso overview app.log --by new\n"
                                           "\n"
                                           "Exit codes: 0 matches printed or corpus written;\n"
                                           "1 nothing to search or index; 2 error.\n"
                                           "overview: 0 event types printed; 1 nothing to list; 2 error.";

namespace
{
struct TextOptions
{
    std::string query;
    std::vector<std::string> files;
    std::string output;
    std::string index;
    size_t top_k = DEFAULT_TEXT_TOP_K;
    std::string model_dir;
    std::string json_field = "message";
    bool quiet = false;
    bool json = false;
    bool group = false;
    bool provisional = false;
    const CLI::Option *provisional_opt = nullptr;
    size_t threads = 0;
    std::string mode = "auto";
    nesso::parser::ArchiveLimits grep_archive_limits{};
    nesso::parser::ArchiveLimits index_archive_limits{};
    size_t overview_top_k = DEFAULT_OVERVIEW_TOP_K;
    std::string order = "rare";
    nesso::parser::ArchiveLimits overview_archive_limits{};
};
} // namespace

static auto ParseSearchMode(std::string_view mode) -> SearchMode
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

static auto ToPaths(const std::vector<std::string> &args) -> std::vector<std::filesystem::path>
{
    return {args.begin(), args.end()};
}

static auto ConfigFrom(const CLI::Option &model_dir_opt, const TextOptions &text, size_t threads) -> Config
{
    Config config;
    if (!model_dir_opt.empty())
    {
        config.model_dir = text.model_dir;
    }
    config.threads = threads;
    return config;
}

static auto ThreadsFromEnvironment() -> std::expected<size_t, ThreadCountError>
{
    // getenv is not thread-safe. The CLI reads the environment once on the main
    // thread. NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *value = std::getenv("NESSO_THREADS");
    if (value == nullptr || std::string_view{value}.empty())
    {
        return 0;
    }
    const auto parsed = ThreadCount::Parse(value);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }
    return parsed->Get();
}

static void ConfigureAppVersion(CLI::App &app, bool &verbose)
{
    app.add_flag("--verbose", verbose, "With --version, print the embedding session policy");
    app.set_version_flag("-V,--version",
                         [&verbose] -> std::string
                         {
                             const auto from_env = ThreadsFromEnvironment();
                             if (!from_env)
                             {
                                 PrintThreadsEnvironmentError(from_env.error());
                                 throw CLI::RuntimeError(2);
                             }
                             const size_t threads = *from_env;
                             std::string version{NESSO_VERSION};
                             if (verbose)
                             {
                                 version.push_back('\n');
                                 version += nesso::embed::SessionPolicyText(threads, nesso::embed::ReadMachine());
                             }
                             return version;
                         });
}

namespace
{
struct TextSubcommand
{
    CLI::App *cmd = nullptr;
    const CLI::Option *threads_opt = nullptr;
    const CLI::Option *model_dir_opt = nullptr;
};
} // namespace

static auto AddGrepSubcommand(CLI::App &app, TextOptions &text) -> TextSubcommand
{
    auto *cmd = app.add_subcommand("grep", "Semantic search of text files. See docs/usage.md.");
    cmd->add_option("query", text.query, "Natural-language query")->required();
    cmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(InputFile());
    cmd->add_option("--json-field", text.json_field, "JSON object field to read")->default_val("message");
    cmd->add_option("-k,--top-k", text.top_k, "Maximum number of matches to return, or groups with --group")
        ->default_val(5)
        ->check(PositiveCount());
    cmd->add_flag("--quiet", text.quiet, "Suppress interactive progress output");
    cmd->add_flag("--json", text.json, "Print one JSON object per match");
    cmd->add_flag("--group", text.group, "Print one row per masked dedup group");
    text.provisional_opt = cmd->add_flag("--provisional,!--no-provisional", text.provisional,
                                         "Show the running dense top-k while embedding. Default on a terminal");
    cmd->add_option("--mode", text.mode, "Ranking mode: auto, hybrid, dense, or lexical")
        ->default_val("auto")
        ->check(CLI::IsMember({"auto", "hybrid", "dense", "lexical"}));
    AddArchiveLimits(cmd, text.grep_archive_limits);
    return TextSubcommand{
        .cmd = cmd,
        .threads_opt = cmd->add_option("--threads", text.threads, THREADS_HELP)->check(MakeThreadCountValidator()),
        .model_dir_opt = cmd->add_option("--model-dir", text.model_dir, MODEL_DIR_HELP)};
}

static auto AddIndexSubcommand(CLI::App &app, TextOptions &text) -> TextSubcommand
{
    auto *cmd = app.add_subcommand("index", "Embed text into a corpus file. See docs/usage.md.");
    cmd->add_option("-o,--output", text.output, "Corpus file to write")->required();
    cmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(InputFile());
    cmd->add_option("--json-field", text.json_field, "JSON object field to read")->default_val("message");
    cmd->add_flag("--quiet", text.quiet, "Suppress interactive progress output");
    AddArchiveLimits(cmd, text.index_archive_limits);
    return TextSubcommand{
        .cmd = cmd,
        .threads_opt = cmd->add_option("--threads", text.threads, THREADS_HELP)->check(MakeThreadCountValidator()),
        .model_dir_opt = cmd->add_option("--model-dir", text.model_dir, MODEL_DIR_HELP)};
}

static auto AddSearchSubcommand(CLI::App &app, TextOptions &text) -> TextSubcommand
{
    auto *cmd = app.add_subcommand("search", "Semantic search of a corpus file. Prints matches only.");
    cmd->add_option("query", text.query, "Natural-language query")->required();
    cmd->add_option("-i,--index", text.index, "Corpus file to search")->required()->check(CLI::ExistingFile);
    cmd->add_option("-k,--top-k", text.top_k, "Maximum number of matches to return, or groups with --group")
        ->default_val(5)
        ->check(PositiveCount());
    cmd->add_flag("--json", text.json, "Print one JSON object per match");
    cmd->add_flag("--group", text.group, "Print one row per masked dedup group");
    cmd->add_option("--mode", text.mode, "Ranking mode: auto, hybrid, dense, or lexical")
        ->default_val("auto")
        ->check(CLI::IsMember({"auto", "hybrid", "dense", "lexical"}));
    return TextSubcommand{
        .cmd = cmd,
        .threads_opt = cmd->add_option("--threads", text.threads, THREADS_HELP)->check(MakeThreadCountValidator()),
        .model_dir_opt = cmd->add_option("--model-dir", text.model_dir, MODEL_DIR_HELP)};
}

// No model is loaded, so there is no --model-dir or --threads.
static auto AddOverviewSubcommand(CLI::App &app, TextOptions &text) -> CLI::App *
{
    auto *cmd = app.add_subcommand("overview", "List rare or new event types of text files. No model is loaded.");
    cmd->add_option("files", text.files, TEXT_FILES_HELP)->required()->expected(1, -1)->check(InputFile());
    cmd->add_option("--json-field", text.json_field, "JSON object field to read")->default_val("message");
    cmd->add_option("-k,--top-k", text.overview_top_k, "Maximum number of event types to list")
        ->default_val(DEFAULT_OVERVIEW_TOP_K)
        ->check(PositiveCount());
    cmd->add_flag("--json", text.json, "Print one JSON object per event type");
    cmd->add_option("--by", text.order, "Order: rare (fewest lines first) or new (latest first appearance first)")
        ->default_val("rare")
        ->check(CLI::IsMember({"rare", "new"}));
    AddArchiveLimits(cmd, text.overview_archive_limits);
    return cmd;
}

static auto MakeOverviewCommand(const TextOptions &text) -> ParsedCommand
{
    return ParsedCommand{.config = Config{},
                         .request =
                             OverviewCommand{.request = OverviewRequest{.files = ToPaths(text.files),
                                                                        .top_k = text.overview_top_k,
                                                                        .json_field = text.json_field,
                                                                        .order = text.order == "new" ? EventOrder::New
                                                                                                     : EventOrder::Rare,
                                                                        .archive_limits = text.overview_archive_limits,
                                                                        .max_types = DEFAULT_MAX_EVENT_TYPES},
                                             .json = text.json}};
}

static auto ResolveThreads(const CLI::Option &threads_opt, size_t flagged) -> std::expected<size_t, int>
{
    if (!threads_opt.empty())
    {
        return flagged;
    }
    const auto from_env = ThreadsFromEnvironment();
    if (!from_env)
    {
        PrintThreadsEnvironmentError(from_env.error());
        return std::unexpected(2);
    }
    return *from_env;
}

static auto MakeParsedCommand(const CLI::App &grep_cmd, const CLI::App &index_cmd, const CLI::Option &model_dir_opt,
                              const TextOptions &text, size_t threads) -> ParsedCommand
{
    if (grep_cmd.parsed())
    {
        std::optional<bool> provisional;
        if (text.provisional_opt->count() > 0)
        {
            provisional = text.provisional;
        }
        return ParsedCommand{.config = ConfigFrom(model_dir_opt, text, threads),
                             .request = GrepCommand{.request = GrepRequest{.query = text.query,
                                                                           .files = ToPaths(text.files),
                                                                           .top_k = text.top_k,
                                                                           .json_field = text.json_field,
                                                                           .progress = !text.quiet,
                                                                           .json = text.json,
                                                                           .mode = ParseSearchMode(text.mode),
                                                                           .group = text.group,
                                                                           .archive_limits = text.grep_archive_limits,
                                                                           .observer = nullptr},
                                                    .provisional = provisional}};
    }
    if (index_cmd.parsed())
    {
        return ParsedCommand{.config = ConfigFrom(model_dir_opt, text, threads),
                             .request = IndexRequest{.files = ToPaths(text.files),
                                                     .output = text.output,
                                                     .json_field = text.json_field,
                                                     .progress = !text.quiet,
                                                     .archive_limits = text.index_archive_limits}};
    }
    return ParsedCommand{.config = ConfigFrom(model_dir_opt, text, threads),
                         .request = SearchRequest{.query = text.query,
                                                  .index = text.index,
                                                  .top_k = text.top_k,
                                                  .json = text.json,
                                                  .mode = ParseSearchMode(text.mode),
                                                  .group = text.group}};
}

auto ParseCommandLine(int argc, char **argv) -> std::expected<ParsedCommand, int>
{
    TextOptions text;
    CLI::App app{"Nesso - local semantic search for unstructured text"};
    bool verbose = false;
    const std::span<char *> args{argv, static_cast<std::size_t>(argc)};
    for (int index = 1; index < argc; ++index)
    {
        if (std::string_view{args[static_cast<std::size_t>(index)]} == "--verbose")
        {
            verbose = true;
        }
    }
    ConfigureAppVersion(app, verbose);
    app.get_formatter()->enable_footer_formatting(false);
    app.footer(HELP_FOOTER);
    app.require_subcommand(1);

    const TextSubcommand grep = AddGrepSubcommand(app, text);
    const TextSubcommand index = AddIndexSubcommand(app, text);
    const TextSubcommand search = AddSearchSubcommand(app, text);
    const CLI::App *overview = AddOverviewSubcommand(app, text);

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &error)
    {
        const int code = app.exit(error);
        return std::unexpected(code == 0 ? 0 : 2);
    }
    if (overview->parsed())
    {
        return MakeOverviewCommand(text);
    }

    const CLI::Option *threads_opt = grep.threads_opt;
    const CLI::Option *model_dir_opt = grep.model_dir_opt;
    if (index.cmd->parsed())
    {
        threads_opt = index.threads_opt;
        model_dir_opt = index.model_dir_opt;
    }
    else if (search.cmd->parsed())
    {
        threads_opt = search.threads_opt;
        model_dir_opt = search.model_dir_opt;
    }

    const auto threads = ResolveThreads(*threads_opt, text.threads);
    if (!threads)
    {
        return std::unexpected(threads.error());
    }
    return MakeParsedCommand(*grep.cmd, *index.cmd, *model_dir_opt, text, *threads);
}

} // namespace nesso::cli
