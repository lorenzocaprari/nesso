// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "nesso/nesso.hpp"
#include "nesso/overview.hpp"
#include <cli/check_stdout.hpp>
#include <cli/cli.hpp>
#include <cli/live_view.hpp>
#include <cli/render_json.hpp>
#include <cli/render_text.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace nesso::cli
{

static std::string CountPhrase(size_t count, std::string_view singular, std::string_view plural)
{
    return std::format("{} {}", count, count == 1 ? singular : plural);
}

static std::string JoinAnd(const std::vector<std::string> &parts)
{
    if (parts.size() == 1)
    {
        return parts.front();
    }
    if (parts.size() == 2)
    {
        return parts[0] + " and " + parts[1];
    }
    std::string text = parts[0];
    for (size_t index = 1; index + 1 < parts.size(); ++index)
    {
        text += ", ";
        text += parts[index];
    }
    text += ", and ";
    text += parts.back();
    return text;
}

struct LineReport
{
    size_t empty_lines = 0;
    size_t malformed_lines = 0;
    size_t missing_field_lines = 0;
    size_t truncated_lines = 0;
    std::string_view json_field = "message";
    std::vector<BundleSkipSummary> bundle_skips;
};

static void RenderLineCounts(const LineReport &lines)
{
    std::vector<std::string> parts;
    if (lines.empty_lines > 0)
    {
        parts.push_back(CountPhrase(lines.empty_lines, "empty line", "empty lines"));
    }
    if (lines.malformed_lines > 0)
    {
        parts.push_back(CountPhrase(lines.malformed_lines, "malformed line", "malformed lines"));
    }
    if (lines.missing_field_lines > 0)
    {
        const std::string_view noun = lines.missing_field_lines == 1 ? "line missing" : "lines missing";
        parts.push_back(std::format("{} {} the \"{}\" field", lines.missing_field_lines, noun, lines.json_field));
    }
    if (!parts.empty())
    {
        std::println(std::cerr, "Skipped {}.", JoinAnd(parts));
    }
    for (const BundleSkipSummary &summary : lines.bundle_skips)
    {
        const std::string text = FormatBundleSkipSummary(summary);
        if (!text.empty())
        {
            std::println(std::cerr, "{}", text);
        }
    }
    if (lines.truncated_lines > 0)
    {
        std::println(std::cerr, "Truncated {}.", CountPhrase(lines.truncated_lines, "line", "lines"));
    }
}

static void RenderStaleSources(const std::vector<StaleSource> &sources)
{
    for (const StaleSource &source : sources)
    {
        std::string_view status = "changed since indexing";
        if (source.unreadable)
        {
            status = "could not be read";
        }
        else if (source.missing)
        {
            status = "is missing";
        }
        std::println(std::cerr, "Warning: source '{}' {}.", source.path, status);
    }
}

static void RenderPolicyWarning(bool mismatch)
{
    if (mismatch)
    {
        std::println(std::cerr, "Warning: corpus indexing policy differs from this "
                                "nesso; search results may differ.");
    }
}

static LineReport MakeLineReport(const TextResults &results)
{
    return {.empty_lines = results.empty_lines,
            .malformed_lines = results.malformed_lines,
            .missing_field_lines = results.missing_field_lines,
            .truncated_lines = results.truncated_lines,
            .json_field = results.json_field,
            .bundle_skips = results.bundle_skips};
}

static LineReport MakeLineReport(const IndexSummary &summary)
{
    return {.empty_lines = summary.empty_lines,
            .malformed_lines = summary.malformed_lines,
            .missing_field_lines = summary.missing_field_lines,
            .truncated_lines = summary.truncated_lines,
            .json_field = summary.json_field,
            .bundle_skips = summary.bundle_skips};
}

static LineReport MakeLineReport(const OverviewResults &results)
{
    return {.empty_lines = results.empty_lines,
            .malformed_lines = results.malformed_lines,
            .missing_field_lines = results.missing_field_lines,
            .truncated_lines = results.truncated_lines,
            .json_field = results.json_field,
            .bundle_skips = results.bundle_skips};
}

static LineReport MakeLineReport(const Error &error)
{
    return {.empty_lines = error.empty_lines,
            .malformed_lines = error.malformed_lines,
            .missing_field_lines = error.missing_field_lines,
            .truncated_lines = error.truncated_lines,
            .json_field = error.json_field,
            .bundle_skips = error.bundle_skips};
}

static int RenderText(const TextResults &results)
{
    for (const TextMatch &match : results.matches)
    {
        std::println("{}", FormatTextRow(results, match));
    }
    return 0;
}

static int RenderJson(const TextResults &results)
{
    for (size_t index = 0; index < results.matches.size(); ++index)
    {
        std::println("{}", RenderMatchJson(results.matches[index], index + 1));
    }
    return 0;
}

static int Render(const TextResults &results, bool json)
{
    RenderLineCounts(MakeLineReport(results));
    RenderPolicyWarning(results.policy_mismatch);
    RenderStaleSources(results.stale_sources);
    if (results.lexical_fallback)
    {
        std::println(std::cerr, "Warning: no lexical match for the query; using hybrid");
    }
    bool write_threw = false;
    try
    {
        if (json)
        {
            RenderJson(results);
        }
        else
        {
            RenderText(results);
        }
    }
    catch (const std::system_error &)
    {
        write_threw = true;
    }
    if (write_threw || StdoutWriteFailed(stdout))
    {
        std::println(std::cerr, "Error: failed to write results");
        return 2;
    }
    return 0;
}

static int Render(const OverviewResults &results, bool json)
{
    RenderLineCounts(MakeLineReport(results));
    std::println(std::cerr, "Listed {} of {} in {}.", results.rows.size(),
                 CountPhrase(results.types, "event type", "event types"), CountPhrase(results.lines, "line", "lines"));
    bool write_threw = false;
    try
    {
        for (size_t index = 0; index < results.rows.size(); ++index)
        {
            const EventTypeRow &row = results.rows[index];
            std::println("{}", json ? RenderEventTypeJson(results, row, index + 1) : FormatEventTypeRow(row));
        }
    }
    catch (const std::system_error &)
    {
        write_threw = true;
    }
    if (write_threw || StdoutWriteFailed(stdout))
    {
        std::println(std::cerr, "Error: failed to write results");
        return 2;
    }
    return 0;
}

static int Render(const IndexSummary &summary, const IndexRequest &request)
{
    RenderLineCounts(MakeLineReport(summary));
    std::println(std::cerr, "Indexed {} chunks into '{}'.", summary.chunks, request.output.string());
    return 0;
}

static int RenderError(const Error &error, int status)
{
    const std::string text = Message(error);
    if (!text.empty())
    {
        std::println(std::cerr, "{}", text);
    }
    RenderLineCounts(MakeLineReport(error));
    RenderPolicyWarning(error.policy_mismatch);
    RenderStaleSources(error.stale_sources);
    return status;
}

static int FailureStatus(const Error &error)
{
    if (error.kind == ErrorKind::EmptyInput || error.kind == ErrorKind::NoMatches)
    {
        return 1;
    }
    return 2;
}

template <typename Result, typename... Context>
static int Report(const std::expected<Result, Error> &outcome, int failure_status, const Context &...context)
{
    return outcome ? Render(*outcome, context...) : RenderError(outcome.error(), failure_status);
}

// `--json --provisional`: each provisional top-k as JSON records on stdout,
// before the final records. A write error stays on the stream and fails the
// final render.
class ProvisionalJson final : public GrepObserver
{
  public:
    ProvisionalJson() = default;

    void OnProvisional(const TextResults &results, size_t /*lines_done*/, size_t /*lines_total*/) override
    {
        ++refresh_;
        for (size_t index = 0; index < results.matches.size(); ++index)
        {
            const std::string record = RenderProvisionalMatchJson(results.matches[index], index + 1, refresh_);
            static_cast<void>(std::fputs(record.c_str(), stdout));
            static_cast<void>(std::fputc('\n', stdout));
        }
        static_cast<void>(std::fflush(stdout));
    }

    void OnScanDone() noexcept override {}

  private:
    std::size_t refresh_ = 0;
};

int Execute(const Nesso &nesso, const GrepCommand &command)
{
    GrepRequest request = command.request;
    ProvisionalJson json;
    std::unique_ptr<LiveView> view;
    if (request.json && command.provisional.value_or(false))
    {
        request.observer = &json;
    }
    else if (LiveViewEnabled({.requested = command.provisional,
                              .quiet = !request.progress,
                              .stderr_is_terminal = ::isatty(STDERR_FILENO) != 0,
                              // The CLI reads the environment once on the main
                              // thread. NOLINTNEXTLINE(concurrency-mt-unsafe)
                              .term = std::getenv("TERM")}))
    {
        view = MakeStderrLiveView();
        request.observer = view.get();
    }
    const auto outcome = nesso.Grep(request);
    return outcome ? Render(*outcome, request.json) : RenderError(outcome.error(), FailureStatus(outcome.error()));
}

int Execute(const Nesso &nesso, const IndexRequest &request)
{
    const auto outcome = nesso.Index(request);
    return Report(outcome, outcome ? 0 : FailureStatus(outcome.error()), request);
}

int Execute(const Nesso &nesso, const SearchRequest &request)
{
    const auto outcome = nesso.Search(request);
    return outcome ? Render(*outcome, request.json) : RenderError(outcome.error(), FailureStatus(outcome.error()));
}

int Execute(const Nesso & /*nesso*/, const OverviewCommand &command)
{
    const auto outcome = Overview(command.request);
    return outcome ? Render(*outcome, command.json) : RenderError(outcome.error(), FailureStatus(outcome.error()));
}

} // namespace nesso::cli
