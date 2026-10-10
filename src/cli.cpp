// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "cli.hpp"

#include <expected>
#include <format>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace nesso::cli
{

static std::string countPhrase(size_t count, std::string_view singular, std::string_view plural)
{
    return std::format("{} {}", count, count == 1 ? singular : plural);
}

static std::string joinAnd(const std::vector<std::string> &parts)
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
    size_t emptyLines = 0;
    size_t malformedLines = 0;
    size_t missingFieldLines = 0;
    size_t truncatedLines = 0;
    std::string_view jsonField = "message";
};

static void renderLineCounts(const LineReport &lines)
{
    std::vector<std::string> parts;
    if (lines.emptyLines > 0)
    {
        parts.push_back(countPhrase(lines.emptyLines, "empty line", "empty lines"));
    }
    if (lines.malformedLines > 0)
    {
        parts.push_back(countPhrase(lines.malformedLines, "malformed line", "malformed lines"));
    }
    if (lines.missingFieldLines > 0)
    {
        const std::string_view noun = lines.missingFieldLines == 1 ? "line missing" : "lines missing";
        parts.push_back(std::format("{} {} the \"{}\" field", lines.missingFieldLines, noun, lines.jsonField));
    }
    if (!parts.empty())
    {
        std::println(std::cerr, "Skipped {}.", joinAnd(parts));
    }
    if (lines.truncatedLines > 0)
    {
        std::println(std::cerr, "Truncated {}.", countPhrase(lines.truncatedLines, "line", "lines"));
    }
}

static LineReport lineReport(const TextResults &results)
{
    return {.emptyLines = results.emptyLines,
            .malformedLines = results.malformedLines,
            .missingFieldLines = results.missingFieldLines,
            .truncatedLines = results.truncatedLines,
            .jsonField = results.jsonField};
}

static LineReport lineReport(const IndexSummary &summary)
{
    return {.emptyLines = summary.emptyLines,
            .malformedLines = summary.malformedLines,
            .missingFieldLines = summary.missingFieldLines,
            .truncatedLines = summary.truncatedLines,
            .jsonField = summary.jsonField};
}

static LineReport lineReport(const Error &error)
{
    return {.emptyLines = error.emptyLines,
            .malformedLines = error.malformedLines,
            .missingFieldLines = error.missingFieldLines,
            .truncatedLines = error.truncatedLines,
            .jsonField = error.jsonField};
}

static int render(const TextResults &results)
{
    renderLineCounts(lineReport(results));
    for (const TextMatch &match : results.matches)
    {
        if (results.multipleSources)
        {
            std::println("{}:line {}: {:.4f}: {}", match.source, match.line, match.score, match.text);
        }
        else
        {
            std::println("line {}: {:.4f}: {}", match.line, match.score, match.text);
        }
    }
    return 0;
}

static int render(const IndexSummary &summary, const IndexRequest &request)
{
    renderLineCounts(lineReport(summary));
    std::println(std::cerr, "Indexed {} chunks into '{}'.", summary.chunks, request.output.string());
    return 0;
}

static int renderError(const Error &error, int status)
{
    const std::string text = message(error);
    if (!text.empty())
    {
        std::println(std::cerr, "{}", text);
    }
    renderLineCounts(lineReport(error));
    return status;
}

static int failureStatus(const Error &error)
{
    if (error.kind == ErrorKind::EmptyInput || error.kind == ErrorKind::NoMatches)
    {
        return 1;
    }
    return 2;
}

template <typename Result, typename... Context>
static int report(const std::expected<Result, Error> &outcome, int failureStatus, const Context &...context)
{
    return outcome ? render(*outcome, context...) : renderError(outcome.error(), failureStatus);
}

int execute(const Nesso &nesso, const GrepRequest &request)
{
    const auto outcome = nesso.grep(request);
    return report(outcome, outcome ? 0 : failureStatus(outcome.error()));
}

int execute(const Nesso &nesso, const IndexRequest &request)
{
    const auto outcome = nesso.index(request);
    return report(outcome, outcome ? 0 : failureStatus(outcome.error()), request);
}

int execute(const Nesso &nesso, const SearchRequest &request)
{
    const auto outcome = nesso.search(request);
    return report(outcome, outcome ? 0 : failureStatus(outcome.error()));
}

} // namespace nesso::cli
