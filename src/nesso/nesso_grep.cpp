// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nesso/nesso.hpp>

#include <nesso/trace.hpp>
#include <nesso_detail.hpp>

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
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nesso
{

using detail::BetterHit;
using detail::EffectiveMode;
using detail::EffectiveModeName;
using detail::EmbedDistinct;
using detail::ExpandRequestFiles;
using detail::LineCounts;
using detail::MakeError;
using detail::MAX_LINE_LENGTH;
using detail::NoKeptLines;
using detail::ParseFailure;
using detail::ProgressReporter;
using detail::QueryTermViews;
using detail::ResolveEffectiveMode;
using detail::Scan;
using detail::ScanFiles;
using detail::StreamBatch;
using detail::TextFailure;

// Files are parsed again so embeddings stay batched. Stdin is replayed from the
// count pass.
template <typename Consume>
static std::expected<void, nesso::parser::ParseError>
VisitChunks(const std::filesystem::path &path, Scan &scan, bool &stdin_used, std::string_view json_field,
            const nesso::parser::ArchiveLimits &limits, const Consume &consume)
{
    if (path == "-")
    {
        if (!stdin_used)
        {
            stdin_used = true;
            for (nesso::parser::ParsedChunk &chunk : scan.stdin_chunks)
            {
                consume(std::move(chunk));
            }
        }
        return {};
    }
    return nesso::parser::Chunker::ForEach(path, MAX_LINE_LENGTH, nullptr, json_field, consume, limits);
}

struct GrepWaiting
{
    uint64_t ordinal = 0;
    std::uint32_t distinct_id = 0;
    std::uint32_t source = 0;
    uint64_t line = 0;
    bool array_element = false;
    std::string text;
};

struct GrepCandidate
{
    uint64_t index = 0;
    float score = 0.0F;
    std::uint32_t distinct_id = 0;
    TextMatch match;
};

struct GrepHit
{
    uint64_t ordinal = 0;
    float score = 0.0F;
    float dense = 0.0F;
    float lexical = 0.0F;
    bool has_dense = false;
    bool has_lexical = false;
    bool substring = false;
    std::uint32_t distinct_id = 0;
    std::uint32_t source = 0;
    uint64_t line = 0;
    bool array_element = false;
    std::string text;
};

struct GrepSourceCursor
{
    bool archive = false;
    std::uint32_t source_id = 0;
    std::string source;
};

struct GrepEmbedState
{
    const GrepRequest *request = nullptr;
    Scan *scanned = nullptr;
    const std::vector<float> *query_embedding = nullptr;
    const nesso::embed::Embedder *model = nullptr;
    bool need_lexical = false;
    nesso::core::DistinctIndex distinct;
    std::vector<float> scores;
    std::vector<GrepWaiting> waiting;
    std::vector<GrepWaiting> all_lines;
    std::vector<char> group_seen;
    std::vector<std::string> sources;
    std::vector<GrepCandidate> best;
    ProgressReporter *progress = nullptr;
    size_t lines_seen = 0;
    size_t lines_reported = 0;
    uint64_t ordinal = 0;
    bool several_sources = false;
    std::string first_source;
    size_t batch = 0;
};

struct GrepBm25
{
    std::vector<double> scores;
    std::vector<double> coverage;
    EffectiveMode effective = EffectiveMode::Dense;
    bool lexical_fallback = false;
};

static bool BetterGrepCandidate(const GrepCandidate &left, const GrepCandidate &right)
{
    return nesso::core::RankBefore(left.score, left.index, right.score, right.index);
}

static bool LexicalHitBefore(const GrepHit &left, const GrepHit &right)
{
    if (left.substring != right.substring)
    {
        return left.substring && !right.substring;
    }
    return BetterHit(left.score, left.ordinal, right.score, right.ordinal);
}

static bool ScoreHitBefore(const GrepHit &left, const GrepHit &right)
{
    return BetterHit(left.score, left.ordinal, right.score, right.ordinal);
}

static void ConsiderDenseGrep(GrepEmbedState &state, const GrepWaiting &item)
{
    if (state.need_lexical)
    {
        return;
    }
    if (state.request->group)
    {
        if (state.group_seen.size() <= item.distinct_id)
        {
            state.group_seen.resize(static_cast<std::size_t>(item.distinct_id) + 1);
        }
        if (state.group_seen[item.distinct_id] != 0)
        {
            return;
        }
        state.group_seen[item.distinct_id] = 1;
    }
    TextMatch match{.source = state.sources[item.source],
                    .line = item.line,
                    .score = state.scores[item.distinct_id],
                    .text = item.text,
                    .array_element = item.array_element,
                    .occurrences = 1,
                    .mode = "dense",
                    .dense = std::nullopt,
                    .lexical = std::nullopt};
    state.best.push_back(GrepCandidate{
        .index = item.ordinal, .score = match.score, .distinct_id = item.distinct_id, .match = std::move(match)});
    if (state.best.size() > state.request->top_k)
    {
        std::partial_sort(state.best.begin(), state.best.begin() + static_cast<std::ptrdiff_t>(state.request->top_k),
                          state.best.end(), BetterGrepCandidate);
        state.best.resize(state.request->top_k);
    }
}

static std::expected<void, Error> FlushGrepEmbeddings(GrepEmbedState &state)
{
    const size_t lines_in_flush = state.lines_seen - state.lines_reported;
    const LineCounts &counts = state.scanned->counts;
    if (state.distinct.Pending().empty())
    {
        state.lines_reported = state.lines_seen;
        state.progress->Update(state.lines_reported, state.scanned->chunks);
        return {};
    }
    const auto embeddings = EmbedDistinct(
        state.distinct.Pending(), *state.model, *state.progress,
        {.lines_before = state.lines_reported, .lines_in_flush = lines_in_flush, .total = state.scanned->chunks},
        counts);
    if (!embeddings)
    {
        return std::unexpected(embeddings.error());
    }
    for (const std::vector<float> &embedding : *embeddings)
    {
        const auto score = nesso::core::math::DistanceMetrics::DotProduct<float>(*state.query_embedding, embedding);
        if (!score)
        {
            return TextFailure(ErrorKind::Search, score.error(), counts);
        }
        state.scores.push_back(*score);
    }
    state.distinct.ClearPending();
    state.lines_reported = state.lines_seen;
    for (const GrepWaiting &item : state.waiting)
    {
        ConsiderDenseGrep(state, item);
    }
    state.waiting.clear();
    return {};
}

static std::optional<Error> IngestGrepChunk(GrepEmbedState &state, GrepSourceCursor &cursor,
                                            nesso::parser::ParsedChunk chunk)
{
    if (cursor.archive && (cursor.source.empty() || chunk.entry_start || chunk.source != cursor.source))
    {
        state.sources.push_back(chunk.source);
        cursor.source_id = static_cast<std::uint32_t>(state.sources.size() - 1);
        cursor.source = state.sources.back();
    }
    if (state.first_source.empty())
    {
        state.first_source = cursor.source;
    }
    else if (cursor.source != state.first_source)
    {
        state.several_sources = true;
    }
    const std::uint32_t entry_id = state.distinct.Add(chunk.text);
    ++state.lines_seen;
    const GrepWaiting item{.ordinal = state.ordinal,
                           .distinct_id = entry_id,
                           .source = cursor.source_id,
                           .line = chunk.line_number,
                           .array_element = chunk.array_element,
                           .text = std::move(chunk.text)};
    if (state.need_lexical)
    {
        state.all_lines.push_back(item);
    }
    if (entry_id < state.scores.size())
    {
        ConsiderDenseGrep(state, item);
    }
    else
    {
        state.waiting.push_back(item);
    }
    ++state.ordinal;
    if (state.distinct.Pending().size() == state.batch)
    {
        if (const auto flushed = FlushGrepEmbeddings(state); !flushed)
        {
            return flushed.error();
        }
    }
    return std::nullopt;
}

static std::expected<void, Error> EmbedGrepFiles(GrepEmbedState &state)
{
    const ScopedStage stage{"embed"};
    bool stdin_used = false;
    for (const std::filesystem::path &path : state.request->files)
    {
        GrepSourceCursor cursor{.archive = nesso::parser::IsArchivePath(path), .source_id = 0, .source = {}};
        if (!cursor.archive)
        {
            state.sources.push_back(path.string());
            cursor.source_id = static_cast<std::uint32_t>(state.sources.size() - 1);
            cursor.source = state.sources.back();
        }
        std::optional<Error> failed;
        const auto streamed =
            VisitChunks(path, *state.scanned, stdin_used, state.request->json_field, state.request->archive_limits,
                        [&](nesso::parser::ParsedChunk chunk)
                        {
                            if (failed)
                            {
                                return;
                            }
                            if (std::optional<Error> error = IngestGrepChunk(state, cursor, std::move(chunk)))
                            {
                                failed = std::move(error);
                            }
                        });
        if (!streamed)
        {
            return std::unexpected(
                ParseFailure(streamed.error(), path, state.scanned->counts, state.request->archive_limits));
        }
        if (failed)
        {
            return std::unexpected(*failed);
        }
    }
    if (const auto flushed = FlushGrepEmbeddings(state); !flushed)
    {
        return std::unexpected(flushed.error());
    }
    return {};
}

static TextResults DenseGrepResults(GrepEmbedState &state, TextResults results)
{
    std::ranges::sort(state.best, BetterGrepCandidate);
    results.matches.reserve(state.best.size());
    for (GrepCandidate &candidate : state.best)
    {
        candidate.match.occurrences = state.distinct.Occurrences(candidate.distinct_id);
        candidate.match.mode = "dense";
        candidate.match.dense = candidate.score;
        results.matches.push_back(std::move(candidate.match));
    }
    return results;
}

static GrepBm25 BuildGrepBm25Scores(const std::vector<GrepWaiting> &all_lines,
                                    const nesso::core::QueryTokens &query_tokens, EffectiveMode effective,
                                    nesso::core::QueryClass query_class)
{
    nesso::core::Bm25Index bm25;
    for (const GrepWaiting &item : all_lines)
    {
        const std::vector<std::string> terms = nesso::core::LexicalTerms(item.text);
        std::unordered_map<std::uint32_t, std::uint32_t> term_counts;
        term_counts.reserve(terms.size());
        for (const std::string &term : terms)
        {
            ++term_counts[bm25.InternTerm(term)];
        }
        std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
        entries.reserve(term_counts.size());
        for (const auto &[termId, frequency] : term_counts)
        {
            entries.emplace_back(termId, frequency);
        }
        bm25.AddLinePostings(entries, static_cast<std::uint32_t>(terms.size()));
    }
    const std::vector<std::string> query_terms = nesso::core::LexicalTerms(query_tokens.stripped);
    nesso::core::Bm25Result bm25_result = bm25.ScoresAndCoverage(QueryTermViews(query_terms));
    if (!nesso::core::HybridUsesCoverage(query_class))
    {
        std::ranges::fill(bm25_result.coverage, 1.0);
    }
    std::vector<double> bm25_scores = std::move(bm25_result.scores);

    bool lexical_fallback = false;
    if (effective == EffectiveMode::Lexical)
    {
        const bool any_hit = std::ranges::any_of(bm25_scores, [](double score) { return score > 0.0; });
        if (!any_hit)
        {
            effective = EffectiveMode::Hybrid;
            lexical_fallback = true;
        }
    }
    return GrepBm25{.scores = std::move(bm25_scores),
                    .coverage = std::move(bm25_result.coverage),
                    .effective = effective,
                    .lexical_fallback = lexical_fallback};
}

static std::vector<GrepHit> LexicalGrepHits(const std::vector<GrepWaiting> &all_lines,
                                            const std::vector<double> &bm25_scores,
                                            const nesso::core::QueryTokens &query_tokens)
{
    std::vector<GrepHit> hits;
    hits.reserve(all_lines.size());
    for (size_t index = 0; index < all_lines.size(); ++index)
    {
        if (!(bm25_scores[index] > 0.0))
        {
            continue;
        }
        const GrepWaiting &item = all_lines[index];
        hits.push_back(
            {.ordinal = item.ordinal,
             .score = static_cast<float>(bm25_scores[index]),
             .lexical = static_cast<float>(bm25_scores[index]),
             .has_lexical = true,
             .substring = !query_tokens.stripped.empty() && item.text.find(query_tokens.stripped) != std::string::npos,
             .distinct_id = item.distinct_id,
             .source = item.source,
             .line = item.line,
             .array_element = item.array_element,
             .text = item.text});
    }
    std::ranges::sort(hits, LexicalHitBefore);
    return hits;
}

static std::vector<GrepHit> HybridGrepHits(const std::vector<GrepWaiting> &all_lines, const std::vector<float> &scores,
                                           const GrepBm25 &lexical)
{
    const std::vector<double> &bm25_scores = lexical.scores;
    std::vector<double> dense_values;
    dense_values.reserve(all_lines.size());
    for (const GrepWaiting &item : all_lines)
    {
        dense_values.push_back(static_cast<double>(scores[item.distinct_id]));
    }
    const std::vector<float> fused = nesso::core::FuseHybridScores(dense_values, bm25_scores, lexical.coverage);
    std::vector<GrepHit> hits;
    hits.reserve(all_lines.size());
    for (size_t index = 0; index < all_lines.size(); ++index)
    {
        const GrepWaiting &item = all_lines[index];
        hits.push_back({.ordinal = item.ordinal,
                        .score = fused[index],
                        .dense = scores[item.distinct_id],
                        .lexical = static_cast<float>(bm25_scores[index]),
                        .has_dense = true,
                        .has_lexical = true,
                        .distinct_id = item.distinct_id,
                        .source = item.source,
                        .line = item.line,
                        .array_element = item.array_element,
                        .text = item.text});
    }
    std::ranges::sort(hits, ScoreHitBefore);
    return hits;
}

static std::vector<GrepHit> GroupGrepHits(const std::vector<GrepHit> &hits, const std::vector<GrepWaiting> &all_lines,
                                          const std::vector<float> &scores, EffectiveMode effective, size_t top_k)
{
    std::vector<char> origin_seen(scores.size(), 0);
    std::vector<GrepWaiting> origin(scores.size());
    for (const GrepWaiting &item : all_lines)
    {
        if (origin_seen[item.distinct_id] != 0)
        {
            continue;
        }
        origin_seen[item.distinct_id] = 1;
        origin[item.distinct_id] = item;
    }
    std::vector<char> taken(scores.size(), 0);
    std::vector<GrepHit> groups;
    groups.reserve(hits.size());
    for (const GrepHit &hit : hits)
    {
        if (taken[hit.distinct_id] != 0)
        {
            continue;
        }
        taken[hit.distinct_id] = 1;
        GrepHit row = hit;
        const GrepWaiting &first = origin[hit.distinct_id];
        row.ordinal = first.ordinal;
        row.source = first.source;
        row.line = first.line;
        row.array_element = first.array_element;
        row.text = first.text;
        groups.push_back(std::move(row));
    }
    if (effective == EffectiveMode::Lexical)
    {
        std::ranges::sort(groups, LexicalHitBefore);
    }
    else
    {
        std::ranges::sort(groups, ScoreHitBefore);
    }
    if (groups.size() > top_k)
    {
        groups.resize(top_k);
    }
    return groups;
}

static TextResults EmptyGrepResults(const GrepEmbedState &state, bool group)
{
    return TextResults{.matches = {},
                       .skipped_lines = state.scanned->counts.skipped,
                       .empty_lines = state.scanned->counts.empty_lines,
                       .malformed_lines = state.scanned->counts.malformed_lines,
                       .missing_field_lines = state.scanned->counts.missing_field_lines,
                       .truncated_lines = state.scanned->counts.truncated,
                       .binary_entries = 0,
                       .json_field = state.scanned->counts.json_field,
                       .multiple_sources = state.several_sources,
                       .stale_sources = {},
                       .bundle_skips = state.scanned->bundle_skips,
                       .policy_mismatch = false,
                       .lexical_fallback = false,
                       .grouped = group};
}

static TextResults MaterializeGrepHits(const GrepEmbedState &state, std::span<const GrepHit> hits, EffectiveMode mode,
                                       bool lexical_fallback)
{
    TextResults results = EmptyGrepResults(state, state.request->group);
    results.lexical_fallback = lexical_fallback;
    results.matches.reserve(hits.size());
    const std::string mode_label{EffectiveModeName(mode)};
    for (const GrepHit &hit : hits)
    {
        TextMatch match{.source = state.sources[hit.source],
                        .line = hit.line,
                        .score = hit.score,
                        .text = hit.text,
                        .array_element = hit.array_element,
                        .occurrences = state.distinct.Occurrences(hit.distinct_id),
                        .mode = mode_label,
                        .dense = std::nullopt,
                        .lexical = std::nullopt};
        if (hit.has_dense)
        {
            match.dense = hit.dense;
        }
        if (hit.has_lexical)
        {
            match.lexical = hit.lexical;
        }
        results.matches.push_back(std::move(match));
    }
    return results;
}

static TextResults RankLexicalGrep(GrepEmbedState &state, const nesso::core::QueryTokens &query_tokens,
                                   EffectiveMode effective)
{
    const GrepBm25 lexical =
        BuildGrepBm25Scores(state.all_lines, query_tokens, effective, nesso::core::ClassifyQuery(state.request->query));
    std::vector<GrepHit> hits = lexical.effective == EffectiveMode::Lexical
                                    ? LexicalGrepHits(state.all_lines, lexical.scores, query_tokens)
                                    : HybridGrepHits(state.all_lines, state.scores, lexical);
    if (state.request->group)
    {
        hits = GroupGrepHits(hits, state.all_lines, state.scores, lexical.effective, state.request->top_k);
    }
    else if (hits.size() > state.request->top_k)
    {
        hits.resize(state.request->top_k);
    }
    return MaterializeGrepHits(state, hits, lexical.effective, lexical.lexical_fallback);
}

std::expected<TextResults, Error> Nesso::Grep(const GrepRequest &input) const
{
    const TraceSession session;
    auto expanded = ExpandRequestFiles(input.files);
    if (!expanded)
    {
        return std::unexpected(expanded.error());
    }
    GrepRequest request = input;
    request.files = std::move(*expanded);
    const auto model = Embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto scanned = ScanFiles(request.files, request.json_field, request.archive_limits);
    if (!scanned)
    {
        return std::unexpected(scanned.error());
    }
    if (scanned->chunks == 0)
    {
        const ErrorCause cause = NoKeptLines(scanned->counts) ? ErrorCause::EmptyFile : ErrorCause::NothingIndexable;
        const std::filesystem::path path = request.files.size() == 1 ? request.files.front() : std::filesystem::path{};
        return std::unexpected(MakeError(ErrorKind::EmptyInput, cause, path, scanned->counts, scanned->bundle_skips));
    }

    const auto query_embedding = [&request, &model]
    {
        const ScopedStage stage{"query-embed"};
        return (*model)->embed(request.query);
    }();
    if (!query_embedding)
    {
        return TextFailure(ErrorKind::QueryEmbed, query_embedding.error(), scanned->counts);
    }

    const nesso::core::QueryTokens query_tokens = nesso::core::TokenizeQuery(request.query);
    const EffectiveMode effective = ResolveEffectiveMode(request.mode, nesso::core::ClassifyQuery(request.query));
    const bool need_lexical = effective != EffectiveMode::Dense;
    ProgressReporter progress(request.progress);
    GrepEmbedState state;
    state.request = &request;
    state.scanned = &*scanned;
    state.query_embedding = &*query_embedding;
    state.model = *model;
    state.need_lexical = need_lexical;
    state.progress = &progress;
    state.batch = StreamBatch();
    state.distinct.Reserve(scanned->chunks);
    state.sources.reserve(request.files.size());
    if (need_lexical)
    {
        state.all_lines.reserve(scanned->chunks);
    }
    if (const auto embedded = EmbedGrepFiles(state); !embedded)
    {
        return std::unexpected(embedded.error());
    }
    progress.Finish();

    if (!need_lexical)
    {
        return DenseGrepResults(state, EmptyGrepResults(state, request.group));
    }
    return RankLexicalGrep(state, query_tokens, effective);
}

} // namespace nesso
