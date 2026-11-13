// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/nesso.hpp>

#include <nesso/error_map.hpp>
#include <nesso_detail.hpp>
#include <nesso/trace.hpp>

#include <core/bm25.hpp>
#include <core/dedup.hpp>
#include <core/distance.hpp>
#include <core/lexical_terms.hpp>
#include <core/query_class.hpp>
#include <core/score_fusion.hpp>
#include <core/top_k.hpp>
#include <embed/embedder.hpp>
#include <parser/archive_source.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nesso
{

using detail::betterHit;
using detail::EffectiveMode;
using detail::effectiveModeName;
using detail::embedDistinct;
using detail::expandRequestFiles;
using detail::LineCounts;
using detail::makeError;
using detail::MAX_LINE_LENGTH;
using detail::noKeptLines;
using detail::parseFailure;
using detail::ProgressReporter;
using detail::queryTermViews;
using detail::resolveEffectiveMode;
using detail::Scan;
using detail::scanFiles;
using detail::streamBatch;
using detail::textFailure;

// Files are parsed again so embeddings stay batched. Stdin is replayed from the count pass.
template <typename Consume>
static std::expected<void, nesso::parser::ParseError>
visitChunks(const std::filesystem::path &path, Scan &scan, bool &stdinUsed, std::string_view jsonField,
            const nesso::parser::ArchiveLimits &limits, const Consume &consume)
{
    if (path == "-")
    {
        if (!stdinUsed)
        {
            stdinUsed = true;
            for (nesso::parser::ParsedChunk &chunk : scan.stdinChunks)
            {
                consume(std::move(chunk));
            }
        }
        return {};
    }
    return nesso::parser::Chunker::forEach(path, MAX_LINE_LENGTH, nullptr, jsonField, consume, limits);
}

struct GrepWaiting
{
    uint64_t ordinal = 0;
    std::uint32_t distinctId = 0;
    std::uint32_t source = 0;
    uint64_t line = 0;
    bool arrayElement = false;
    std::string text;
};

struct GrepCandidate
{
    uint64_t index = 0;
    float score = 0.0F;
    std::uint32_t distinctId = 0;
    TextMatch match;
};

struct GrepHit
{
    uint64_t ordinal = 0;
    float score = 0.0F;
    float dense = 0.0F;
    float lexical = 0.0F;
    bool hasDense = false;
    bool hasLexical = false;
    bool substring = false;
    std::uint32_t distinctId = 0;
    std::uint32_t source = 0;
    uint64_t line = 0;
    bool arrayElement = false;
    std::string text;
};

struct GrepSourceCursor
{
    bool archive = false;
    std::uint32_t sourceId = 0;
    std::string source;
};

struct GrepEmbedState
{
    const GrepRequest *request = nullptr;
    Scan *scanned = nullptr;
    const std::vector<float> *queryEmbedding = nullptr;
    const nesso::embed::Embedder *model = nullptr;
    bool needLexical = false;
    nesso::core::DistinctIndex distinct;
    std::vector<float> scores;
    std::vector<GrepWaiting> waiting;
    std::vector<GrepWaiting> allLines;
    std::vector<char> groupSeen;
    std::vector<std::string> sources;
    std::vector<GrepCandidate> best;
    ProgressReporter *progress = nullptr;
    size_t linesSeen = 0;
    size_t linesReported = 0;
    uint64_t ordinal = 0;
    bool severalSources = false;
    std::string firstSource;
    size_t batch = 0;
};

struct GrepBm25
{
    std::vector<double> scores;
    EffectiveMode effective = EffectiveMode::Dense;
    bool lexicalFallback = false;
};

static bool betterGrepCandidate(const GrepCandidate &left, const GrepCandidate &right)
{
    return nesso::core::rankBefore(left.score, left.index, right.score, right.index);
}

static bool lexicalHitBefore(const GrepHit &left, const GrepHit &right)
{
    if (left.substring != right.substring)
    {
        return left.substring && !right.substring;
    }
    return betterHit(left.score, left.ordinal, right.score, right.ordinal);
}

static bool scoreHitBefore(const GrepHit &left, const GrepHit &right)
{
    return betterHit(left.score, left.ordinal, right.score, right.ordinal);
}

static void considerDenseGrep(GrepEmbedState &state, const GrepWaiting &item)
{
    if (state.needLexical)
    {
        return;
    }
    if (state.request->group)
    {
        if (state.groupSeen.size() <= item.distinctId)
        {
            state.groupSeen.resize(static_cast<std::size_t>(item.distinctId) + 1);
        }
        if (state.groupSeen[item.distinctId] != 0)
        {
            return;
        }
        state.groupSeen[item.distinctId] = 1;
    }
    TextMatch match{.source = state.sources[item.source],
                    .line = item.line,
                    .score = state.scores[item.distinctId],
                    .text = item.text,
                    .arrayElement = item.arrayElement,
                    .occurrences = 1,
                    .mode = "dense",
                    .dense = std::nullopt,
                    .lexical = std::nullopt};
    state.best.push_back(GrepCandidate{
        .index = item.ordinal, .score = match.score, .distinctId = item.distinctId, .match = std::move(match)});
    if (state.best.size() > state.request->topK)
    {
        std::partial_sort(state.best.begin(), state.best.begin() + static_cast<std::ptrdiff_t>(state.request->topK),
                          state.best.end(), betterGrepCandidate);
        state.best.resize(state.request->topK);
    }
}

static std::expected<void, Error> flushGrepEmbeddings(GrepEmbedState &state)
{
    const size_t linesInFlush = state.linesSeen - state.linesReported;
    const LineCounts &counts = state.scanned->counts;
    if (state.distinct.pending().empty())
    {
        state.linesReported = state.linesSeen;
        state.progress->update(state.linesReported, state.scanned->chunks);
        return {};
    }
    const auto embeddings = embedDistinct(
        state.distinct.pending(), *state.model, *state.progress,
        {.linesBefore = state.linesReported, .linesInFlush = linesInFlush, .total = state.scanned->chunks}, counts);
    if (!embeddings)
    {
        return std::unexpected(embeddings.error());
    }
    for (const std::vector<float> &embedding : *embeddings)
    {
        const auto score = nesso::core::math::DistanceMetrics::dotProduct<float>(*state.queryEmbedding, embedding);
        if (!score)
        {
            return textFailure(ErrorKind::Search, score.error(), counts);
        }
        state.scores.push_back(*score);
    }
    state.distinct.clearPending();
    state.linesReported = state.linesSeen;
    for (const GrepWaiting &item : state.waiting)
    {
        considerDenseGrep(state, item);
    }
    state.waiting.clear();
    return {};
}

static std::optional<Error> ingestGrepChunk(GrepEmbedState &state, GrepSourceCursor &cursor,
                                            nesso::parser::ParsedChunk chunk)
{
    if (cursor.archive && (cursor.source.empty() || chunk.entryStart || chunk.source != cursor.source))
    {
        state.sources.push_back(chunk.source);
        cursor.sourceId = static_cast<std::uint32_t>(state.sources.size() - 1);
        cursor.source = state.sources.back();
    }
    if (state.firstSource.empty())
    {
        state.firstSource = cursor.source;
    }
    else if (cursor.source != state.firstSource)
    {
        state.severalSources = true;
    }
    const std::uint32_t id = state.distinct.add(chunk.text);
    ++state.linesSeen;
    const GrepWaiting item{.ordinal = state.ordinal,
                           .distinctId = id,
                           .source = cursor.sourceId,
                           .line = chunk.lineNumber,
                           .arrayElement = chunk.arrayElement,
                           .text = std::move(chunk.text)};
    if (state.needLexical)
    {
        state.allLines.push_back(item);
    }
    if (id < state.scores.size())
    {
        considerDenseGrep(state, item);
    }
    else
    {
        state.waiting.push_back(item);
    }
    ++state.ordinal;
    if (state.distinct.pending().size() == state.batch)
    {
        if (const auto flushed = flushGrepEmbeddings(state); !flushed)
        {
            return flushed.error();
        }
    }
    return std::nullopt;
}

static std::expected<void, Error> embedGrepFiles(GrepEmbedState &state)
{
    const ScopedStage stage{"embed"};
    bool stdinUsed = false;
    for (const std::filesystem::path &path : state.request->files)
    {
        GrepSourceCursor cursor{.archive = nesso::parser::isArchivePath(path), .sourceId = 0, .source = {}};
        if (!cursor.archive)
        {
            state.sources.push_back(path.string());
            cursor.sourceId = static_cast<std::uint32_t>(state.sources.size() - 1);
            cursor.source = state.sources.back();
        }
        std::optional<Error> failed;
        const auto streamed =
            visitChunks(path, *state.scanned, stdinUsed, state.request->jsonField, state.request->archiveLimits,
                        [&](nesso::parser::ParsedChunk chunk)
                        {
                            if (failed)
                            {
                                return;
                            }
                            if (std::optional<Error> error = ingestGrepChunk(state, cursor, std::move(chunk)))
                            {
                                failed = std::move(error);
                            }
                        });
        if (!streamed)
        {
            return std::unexpected(
                parseFailure(streamed.error(), path, state.scanned->counts, state.request->archiveLimits));
        }
        if (failed)
        {
            return std::unexpected(*failed);
        }
    }
    if (const auto flushed = flushGrepEmbeddings(state); !flushed)
    {
        return std::unexpected(flushed.error());
    }
    return {};
}

static TextResults denseGrepResults(GrepEmbedState &state, TextResults results)
{
    std::ranges::sort(state.best, betterGrepCandidate);
    results.matches.reserve(state.best.size());
    for (GrepCandidate &candidate : state.best)
    {
        candidate.match.occurrences = state.distinct.occurrences(candidate.distinctId);
        candidate.match.mode = "dense";
        candidate.match.dense = candidate.score;
        results.matches.push_back(std::move(candidate.match));
    }
    return results;
}

static GrepBm25 buildGrepBm25Scores(const std::vector<GrepWaiting> &allLines,
                                    const nesso::core::QueryTokens &queryTokens, EffectiveMode effective)
{
    nesso::core::Bm25Index bm25;
    for (const GrepWaiting &item : allLines)
    {
        const std::vector<std::string> terms = nesso::core::lexicalTerms(item.text);
        std::unordered_map<std::uint32_t, std::uint32_t> termCounts;
        termCounts.reserve(terms.size());
        for (const std::string &term : terms)
        {
            ++termCounts[bm25.internTerm(term)];
        }
        std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
        entries.reserve(termCounts.size());
        for (const auto &[termId, frequency] : termCounts)
        {
            entries.emplace_back(termId, frequency);
        }
        bm25.addLinePostings(entries, static_cast<std::uint32_t>(terms.size()));
    }
    const std::vector<std::string> queryTerms = nesso::core::lexicalTerms(queryTokens.stripped);
    std::vector<double> bm25Scores = bm25.scores(queryTermViews(queryTerms));

    bool lexicalFallback = false;
    if (effective == EffectiveMode::Lexical)
    {
        const bool anyHit = std::ranges::any_of(bm25Scores, [](double score) { return score > 0.0; });
        if (!anyHit)
        {
            effective = EffectiveMode::Hybrid;
            lexicalFallback = true;
        }
    }
    return GrepBm25{.scores = std::move(bm25Scores), .effective = effective, .lexicalFallback = lexicalFallback};
}

static std::vector<GrepHit> lexicalGrepHits(const std::vector<GrepWaiting> &allLines,
                                            const std::vector<double> &bm25Scores,
                                            const nesso::core::QueryTokens &queryTokens)
{
    std::vector<GrepHit> hits;
    hits.reserve(allLines.size());
    for (size_t index = 0; index < allLines.size(); ++index)
    {
        if (!(bm25Scores[index] > 0.0))
        {
            continue;
        }
        const GrepWaiting &item = allLines[index];
        hits.push_back(
            {.ordinal = item.ordinal,
             .score = static_cast<float>(bm25Scores[index]),
             .lexical = static_cast<float>(bm25Scores[index]),
             .hasLexical = true,
             .substring = !queryTokens.stripped.empty() && item.text.find(queryTokens.stripped) != std::string::npos,
             .distinctId = item.distinctId,
             .source = item.source,
             .line = item.line,
             .arrayElement = item.arrayElement,
             .text = item.text});
    }
    std::ranges::sort(hits, lexicalHitBefore);
    return hits;
}

static std::vector<GrepHit> hybridGrepHits(const std::vector<GrepWaiting> &allLines, const std::vector<float> &scores,
                                           const std::vector<double> &bm25Scores)
{
    std::vector<double> denseValues;
    std::vector<double> lexicalValues;
    denseValues.reserve(allLines.size());
    lexicalValues.reserve(allLines.size());
    for (size_t index = 0; index < allLines.size(); ++index)
    {
        denseValues.push_back(static_cast<double>(scores[allLines[index].distinctId]));
        lexicalValues.push_back(bm25Scores[index]);
    }
    const std::vector<float> fused = nesso::core::fuseHybridScores(denseValues, lexicalValues);
    std::vector<GrepHit> hits;
    hits.reserve(allLines.size());
    for (size_t index = 0; index < allLines.size(); ++index)
    {
        const GrepWaiting &item = allLines[index];
        hits.push_back({.ordinal = item.ordinal,
                        .score = fused[index],
                        .dense = scores[item.distinctId],
                        .lexical = static_cast<float>(bm25Scores[index]),
                        .hasDense = true,
                        .hasLexical = true,
                        .distinctId = item.distinctId,
                        .source = item.source,
                        .line = item.line,
                        .arrayElement = item.arrayElement,
                        .text = item.text});
    }
    std::ranges::sort(hits, scoreHitBefore);
    return hits;
}

static std::vector<GrepHit> groupGrepHits(const std::vector<GrepHit> &hits, const std::vector<GrepWaiting> &allLines,
                                          const std::vector<float> &scores, EffectiveMode effective, size_t topK)
{
    std::vector<char> originSeen(scores.size(), 0);
    std::vector<GrepWaiting> origin(scores.size());
    for (const GrepWaiting &item : allLines)
    {
        if (originSeen[item.distinctId] != 0)
        {
            continue;
        }
        originSeen[item.distinctId] = 1;
        origin[item.distinctId] = item;
    }
    std::vector<char> taken(scores.size(), 0);
    std::vector<GrepHit> groups;
    groups.reserve(hits.size());
    for (const GrepHit &hit : hits)
    {
        if (taken[hit.distinctId] != 0)
        {
            continue;
        }
        taken[hit.distinctId] = 1;
        GrepHit row = hit;
        const GrepWaiting &first = origin[hit.distinctId];
        row.ordinal = first.ordinal;
        row.source = first.source;
        row.line = first.line;
        row.arrayElement = first.arrayElement;
        row.text = first.text;
        groups.push_back(std::move(row));
    }
    if (effective == EffectiveMode::Lexical)
    {
        std::ranges::sort(groups, lexicalHitBefore);
    }
    else
    {
        std::ranges::sort(groups, scoreHitBefore);
    }
    if (groups.size() > topK)
    {
        groups.resize(topK);
    }
    return groups;
}

static TextResults emptyGrepResults(const GrepEmbedState &state, bool group)
{
    return TextResults{.matches = {},
                       .skippedLines = state.scanned->counts.skipped,
                       .emptyLines = state.scanned->counts.emptyLines,
                       .malformedLines = state.scanned->counts.malformedLines,
                       .missingFieldLines = state.scanned->counts.missingFieldLines,
                       .truncatedLines = state.scanned->counts.truncated,
                       .binaryEntries = 0,
                       .jsonField = state.scanned->counts.jsonField,
                       .multipleSources = state.severalSources,
                       .staleSources = {},
                       .bundleSkips = state.scanned->bundleSkips,
                       .policyMismatch = false,
                       .lexicalFallback = false,
                       .grouped = group};
}

static TextResults materializeGrepHits(const GrepEmbedState &state, std::span<const GrepHit> hits, EffectiveMode mode,
                                       bool lexicalFallback)
{
    TextResults results = emptyGrepResults(state, state.request->group);
    results.lexicalFallback = lexicalFallback;
    results.matches.reserve(hits.size());
    const std::string modeLabel{effectiveModeName(mode)};
    for (const GrepHit &hit : hits)
    {
        TextMatch match{.source = state.sources[hit.source],
                        .line = hit.line,
                        .score = hit.score,
                        .text = hit.text,
                        .arrayElement = hit.arrayElement,
                        .occurrences = state.distinct.occurrences(hit.distinctId),
                        .mode = modeLabel,
                        .dense = std::nullopt,
                        .lexical = std::nullopt};
        if (hit.hasDense)
        {
            match.dense = hit.dense;
        }
        if (hit.hasLexical)
        {
            match.lexical = hit.lexical;
        }
        results.matches.push_back(std::move(match));
    }
    return results;
}

static TextResults rankLexicalGrep(GrepEmbedState &state, const nesso::core::QueryTokens &queryTokens,
                                   EffectiveMode effective)
{
    const GrepBm25 lexical = buildGrepBm25Scores(state.allLines, queryTokens, effective);
    std::vector<GrepHit> hits = lexical.effective == EffectiveMode::Lexical
                                    ? lexicalGrepHits(state.allLines, lexical.scores, queryTokens)
                                    : hybridGrepHits(state.allLines, state.scores, lexical.scores);
    if (state.request->group)
    {
        hits = groupGrepHits(hits, state.allLines, state.scores, lexical.effective, state.request->topK);
    }
    else if (hits.size() > state.request->topK)
    {
        hits.resize(state.request->topK);
    }
    return materializeGrepHits(state, hits, lexical.effective, lexical.lexicalFallback);
}

std::expected<TextResults, Error> Nesso::grep(const GrepRequest &input) const
{
    const TraceSession session;
    auto expanded = expandRequestFiles(input.files);
    if (!expanded)
    {
        return std::unexpected(expanded.error());
    }
    GrepRequest request = input;
    request.files = std::move(*expanded);
    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto scanned = scanFiles(request.files, request.jsonField, request.archiveLimits);
    if (!scanned)
    {
        return std::unexpected(scanned.error());
    }
    if (scanned->chunks == 0)
    {
        const ErrorCause cause = noKeptLines(scanned->counts) ? ErrorCause::EmptyFile : ErrorCause::NothingIndexable;
        const std::filesystem::path path = request.files.size() == 1 ? request.files.front() : std::filesystem::path{};
        return std::unexpected(makeError(ErrorKind::EmptyInput, cause, path, scanned->counts, scanned->bundleSkips));
    }

    const auto queryEmbedding = [&request, &model]
    {
        const ScopedStage stage{"query-embed"};
        return (*model)->embed(request.query);
    }();
    if (!queryEmbedding)
    {
        return textFailure(ErrorKind::QueryEmbed, queryEmbedding.error(), scanned->counts);
    }

    const nesso::core::QueryTokens queryTokens = nesso::core::tokenizeQuery(request.query);
    const EffectiveMode effective = resolveEffectiveMode(request.mode, nesso::core::classifyQuery(request.query));
    const bool needLexical = effective != EffectiveMode::Dense;
    ProgressReporter progress(request.progress);
    GrepEmbedState state;
    state.request = &request;
    state.scanned = &*scanned;
    state.queryEmbedding = &*queryEmbedding;
    state.model = *model;
    state.needLexical = needLexical;
    state.progress = &progress;
    state.batch = streamBatch();
    state.distinct.reserve(scanned->chunks);
    state.sources.reserve(request.files.size());
    if (needLexical)
    {
        state.allLines.reserve(scanned->chunks);
    }
    if (const auto embedded = embedGrepFiles(state); !embedded)
    {
        return std::unexpected(embedded.error());
    }
    progress.finish();

    if (!needLexical)
    {
        return denseGrepResults(state, emptyGrepResults(state, request.group));
    }
    return rankLexicalGrep(state, queryTokens, effective);
}

} // namespace nesso
