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
#include <core/embed_order.hpp>
#include <core/lexical_terms.hpp>
#include <core/query_class.hpp>
#include <core/score_fusion.hpp>
#include <core/top_k.hpp>
#include <embed/embedder.hpp>
#include <parser/archive_source.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <bit>
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
using detail::NoKeptLines;
using detail::ProgressReporter;
using detail::QueryTermViews;
using detail::ResolveEffectiveMode;
using detail::Scan;
using detail::ScanFiles;
using detail::StreamBatch;
using detail::TextFailure;

struct GrepWaiting
{
    uint64_t ordinal = 0;
    std::uint32_t distinct_id = 0;
    std::uint32_t source = 0;
    uint64_t line = 0;
    bool array_element = false;
    // False when the line is its id's first occurrence and `text` is left empty:
    // `DistinctIndex::Text` holds the same bytes.
    bool own_text = true;
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
    std::uint32_t distinct_count = 0;
    std::vector<float> scores;
    // Hybrid and lexical keep every line. Dense keeps a line only while its id
    // has occurred at most `top_k` times (once with --group): a later line ties
    // an earlier one on score and loses on ordinal.
    std::vector<GrepWaiting> all_lines;
    std::vector<GrepWaiting> dense_lines;
    // Per window of `batch` distinct ids: its promise, its input lines, and,
    // with an observer, the kept lines (indexes into `Kept()`) whose id it holds.
    std::vector<nesso::core::WindowPromise> promise;
    std::vector<size_t> window_lines;
    std::vector<std::vector<size_t>> window_kept;
    std::string stripped_query;
    nesso::core::PromiseTerms query_terms;
    std::vector<char> group_seen;
    std::vector<std::string> sources;
    std::vector<GrepCandidate> best;
    // The request's observer, if any, and the (ordinal, score bits) list last
    // reported to it, so an unchanged top-k is not reported again.
    GrepObserver *observer = nullptr;
    std::vector<std::pair<uint64_t, std::uint32_t>> reported;
    ProgressReporter *progress = nullptr;
    // Input lines whose id is embedded.
    size_t lines_done = 0;
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

static const std::vector<GrepWaiting> &Kept(const GrepEmbedState &state)
{
    return state.need_lexical ? state.all_lines : state.dense_lines;
}

static std::string_view LineText(const GrepEmbedState &state, const GrepWaiting &item)
{
    return item.own_text ? std::string_view{item.text} : state.distinct.Text(item.distinct_id);
}

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

// Keeps the running dense top-k in `best`. Hybrid and lexical rank at the end
// and keep it only for an observer.
static void ConsiderDenseGrep(GrepEmbedState &state, const GrepWaiting &item)
{
    if (state.need_lexical && state.observer == nullptr)
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
                    .text = std::string{LineText(state, item)},
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

static TextResults EmptyGrepResults(const GrepEmbedState &state, bool group);

static void ReportProvisional(GrepEmbedState &state)
{
    if (state.observer == nullptr)
    {
        return;
    }
    std::vector<GrepCandidate> ranked = state.best;
    std::ranges::sort(ranked, BetterGrepCandidate);
    std::vector<std::pair<uint64_t, std::uint32_t>> key;
    key.reserve(ranked.size());
    for (const GrepCandidate &candidate : ranked)
    {
        key.emplace_back(candidate.index, std::bit_cast<std::uint32_t>(candidate.score));
    }
    if (key == state.reported)
    {
        return;
    }
    state.reported = std::move(key);
    TextResults results = EmptyGrepResults(state, state.request->group);
    results.matches.reserve(ranked.size());
    for (GrepCandidate &candidate : ranked)
    {
        candidate.match.occurrences = state.distinct.Occurrences(candidate.distinct_id);
        candidate.match.mode = "dense";
        candidate.match.dense = candidate.score;
        results.matches.push_back(std::move(candidate.match));
    }
    state.observer->OnProvisional(results, state.lines_done, state.scanned->chunks);
    state.progress->Redraw();
}

static void UpdatePromise(GrepEmbedState &state, nesso::core::WindowPromise &promise, std::string_view text)
{
    if (!promise.any_substr && !state.stripped_query.empty() && text.find(state.stripped_query) != std::string::npos)
    {
        promise.any_substr = true;
    }
    if (promise.max_hits < state.query_terms.Size())
    {
        const std::vector<std::string> terms = nesso::core::LexicalTerms(text);
        promise.max_hits = std::max(promise.max_hits, state.query_terms.Hits(terms));
    }
}

static void IngestGrepChunk(GrepEmbedState &state, GrepSourceCursor &cursor, nesso::parser::ParsedChunk chunk)
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
    if (state.distinct.Pending().size() == state.batch)
    {
        state.distinct.ClearPending();
    }
    state.distinct_count = std::max(state.distinct_count, entry_id + 1);
    const size_t window = entry_id / state.batch;
    if (state.promise.size() <= window)
    {
        state.promise.resize(window + 1);
        state.window_lines.resize(window + 1);
        if (state.observer != nullptr)
        {
            state.window_kept.resize(window + 1);
        }
    }
    ++state.window_lines[window];
    UpdatePromise(state, state.promise[window], chunk.text);

    const std::uint32_t occurrence = state.distinct.Occurrences(entry_id);
    const bool dense_candidate = state.request->group ? occurrence == 1 : occurrence <= state.request->top_k;
    GrepWaiting item{.ordinal = state.ordinal++,
                     .distinct_id = entry_id,
                     .source = cursor.source_id,
                     .line = chunk.line_number,
                     .array_element = chunk.array_element,
                     .own_text = true,
                     .text = {}};
    if (!state.need_lexical && !dense_candidate)
    {
        return;
    }
    if (state.need_lexical || occurrence > 1)
    {
        item.text = std::move(chunk.text);
    }
    else
    {
        item.own_text = false;
    }
    std::vector<GrepWaiting> &kept = state.need_lexical ? state.all_lines : state.dense_lines;
    if (state.observer != nullptr && dense_candidate)
    {
        state.window_kept[window].push_back(kept.size());
    }
    kept.push_back(std::move(item));
}

// Receives the read pass. Each path gets its source cursor, as the second parse
// used to.
class GrepIngest final : public detail::ScanSink
{
  public:
    explicit GrepIngest(GrepEmbedState &state) noexcept : state_(&state) {}

    void BeginFile(size_t /*index*/, const std::filesystem::path &path) override
    {
        cursor_ = GrepSourceCursor{.archive = nesso::parser::IsArchivePath(path), .source_id = 0, .source = {}};
        if (!cursor_.archive)
        {
            state_->sources.push_back(path.string());
            cursor_.source_id = static_cast<std::uint32_t>(state_->sources.size() - 1);
            cursor_.source = state_->sources.back();
        }
    }

    void Chunk(nesso::parser::ParsedChunk chunk) override
    {
        if (!chunk.skipped)
        {
            IngestGrepChunk(*state_, cursor_, std::move(chunk));
        }
    }

  private:
    GrepEmbedState *state_;
    GrepSourceCursor cursor_;
};

static void CountSingletons(GrepEmbedState &state)
{
    for (std::uint32_t entry_id = 0; entry_id < state.distinct_count; ++entry_id)
    {
        if (state.distinct.Occurrences(entry_id) == 1)
        {
            ++state.promise[entry_id / state.batch].singletons;
        }
    }
}

// Embeds whole windows, most promising first. A window keeps its members and
// their order (ADR 0004), so each score is the one input order gives.
static std::expected<void, Error> EmbedGrepWindows(GrepEmbedState &state)
{
    const ScopedStage stage{"embed"};
    const LineCounts &counts = state.scanned->counts;
    CountSingletons(state);
    state.scores.assign(state.distinct_count, 0.0F);
    std::vector<std::string> texts;
    for (const size_t window : nesso::core::EmbedOrder(state.promise))
    {
        const size_t begin = window * state.batch;
        const size_t end = std::min(begin + state.batch, static_cast<size_t>(state.distinct_count));
        texts.clear();
        for (size_t entry_id = begin; entry_id < end; ++entry_id)
        {
            texts.emplace_back(state.distinct.Text(static_cast<std::uint32_t>(entry_id)));
        }
        const auto embeddings = EmbedDistinct(texts, *state.model, *state.progress,
                                              {.lines_before = state.lines_done,
                                               .lines_in_flush = state.window_lines[window],
                                               .total = state.scanned->chunks},
                                              counts);
        if (!embeddings)
        {
            return std::unexpected(embeddings.error());
        }
        for (size_t offset = 0; offset < embeddings->size(); ++offset)
        {
            const auto score =
                nesso::core::math::DistanceMetrics::DotProduct<float>(*state.query_embedding, (*embeddings)[offset]);
            if (!score)
            {
                return TextFailure(ErrorKind::Search, score.error(), counts);
            }
            state.scores[begin + offset] = *score;
        }
        state.lines_done += state.window_lines[window];
        if (state.observer != nullptr)
        {
            const std::vector<GrepWaiting> &kept = Kept(state);
            for (const size_t line : state.window_kept[window])
            {
                ConsiderDenseGrep(state, kept[line]);
            }
            ReportProvisional(state);
        }
    }
    if (state.observer == nullptr && !state.need_lexical)
    {
        for (const GrepWaiting &item : state.dense_lines)
        {
            ConsiderDenseGrep(state, item);
        }
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
                                          std::uint32_t distinct_count, EffectiveMode effective, size_t top_k)
{
    std::vector<char> origin_seen(distinct_count, 0);
    std::vector<GrepWaiting> origin(distinct_count);
    for (const GrepWaiting &item : all_lines)
    {
        if (origin_seen[item.distinct_id] != 0)
        {
            continue;
        }
        origin_seen[item.distinct_id] = 1;
        origin[item.distinct_id] = item;
    }
    std::vector<char> taken(distinct_count, 0);
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

static TextResults RankFromBm25(GrepEmbedState &state, const nesso::core::QueryTokens &query_tokens,
                                const GrepBm25 &lexical)
{
    std::vector<GrepHit> hits = lexical.effective == EffectiveMode::Lexical
                                    ? LexicalGrepHits(state.all_lines, lexical.scores, query_tokens)
                                    : HybridGrepHits(state.all_lines, state.scores, lexical);
    if (state.request->group)
    {
        hits = GroupGrepHits(hits, state.all_lines, state.distinct_count, lexical.effective, state.request->top_k);
    }
    else if (hits.size() > state.request->top_k)
    {
        hits.resize(state.request->top_k);
    }
    return MaterializeGrepHits(state, hits, lexical.effective, lexical.lexical_fallback);
}

static TextResults RankLexicalGrep(GrepEmbedState &state, const nesso::core::QueryTokens &query_tokens,
                                   EffectiveMode effective)
{
    const GrepBm25 lexical =
        BuildGrepBm25Scores(state.all_lines, query_tokens, effective, nesso::core::ClassifyQuery(state.request->query));
    return RankFromBm25(state, query_tokens, lexical);
}

// Calls `GrepObserver::OnScanDone` once, from `Fire` or on scope exit, then
// draws the progress line again.
class ScanDoneNotice
{
  public:
    ScanDoneNotice(GrepObserver *observer, const std::optional<ProgressReporter> &progress) noexcept
        : observer_(observer), progress_(&progress)
    {
    }
    ScanDoneNotice(const ScanDoneNotice &) = delete;
    ScanDoneNotice &operator=(const ScanDoneNotice &) = delete;
    ScanDoneNotice(ScanDoneNotice &&) = delete;
    ScanDoneNotice &operator=(ScanDoneNotice &&) = delete;
    ~ScanDoneNotice() { Fire(); }

    void Fire() noexcept
    {
        GrepObserver *const observer = std::exchange(observer_, nullptr);
        if (observer == nullptr)
        {
            return;
        }
        observer->OnScanDone();
        if (progress_->has_value())
        {
            (*progress_)->Redraw();
        }
    }

  private:
    GrepObserver *observer_;
    const std::optional<ProgressReporter> *progress_;
};

std::expected<TextResults, Error> Nesso::Grep(const GrepRequest &input) const
{
    const TraceSession session;
    // Declared before the notice, so the notice can still redraw it on an
    // early return.
    std::optional<ProgressReporter> progress;
    ScanDoneNotice scan_done{input.observer, progress};
    auto expanded = ExpandRequestFiles(input.files);
    if (!expanded)
    {
        return std::unexpected(expanded.error());
    }
    GrepRequest request = input;
    request.files = std::move(*expanded);
    const nesso::core::QueryTokens query_tokens = nesso::core::TokenizeQuery(request.query);
    const nesso::core::QueryClass query_class = nesso::core::ClassifyQuery(request.query);
    const EffectiveMode effective = ResolveEffectiveMode(request.mode, query_class);
    // Identifier auto is BM25 unless nothing matches. Dense scores are unused
    // on that path, so the model stays unloaded until a hybrid fallback needs it.
    const bool identifier_auto = request.mode == SearchMode::Auto && query_class == nesso::core::QueryClass::Identifier;
    const bool need_lexical = effective != EffectiveMode::Dense;
    GrepEmbedState state;
    state.request = &request;
    state.need_lexical = need_lexical;
    state.observer = request.observer;
    state.batch = StreamBatch();
    state.stripped_query = query_tokens.stripped;
    state.query_terms = nesso::core::PromiseTerms{nesso::core::LexicalTerms(query_tokens.stripped)};
    state.sources.reserve(request.files.size());
    const auto load_model = [&]() -> std::expected<void, Error>
    {
        const auto model = Embedder();
        if (!model)
        {
            return std::unexpected(model.error());
        }
        state.model = *model;
        return {};
    };
    if (!identifier_auto)
    {
        if (const auto loaded = load_model(); !loaded)
        {
            return std::unexpected(loaded.error());
        }
    }

    GrepIngest ingest{state};
    auto scanned = ScanFiles(request.files, request.json_field, request.archive_limits, ingest);
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
    state.scanned = &*scanned;
    if (identifier_auto)
    {
        const GrepBm25 lexical = BuildGrepBm25Scores(state.all_lines, query_tokens, effective, query_class);
        if (lexical.effective == EffectiveMode::Lexical)
        {
            scan_done.Fire();
            return RankFromBm25(state, query_tokens, lexical);
        }
        if (const auto loaded = load_model(); !loaded)
        {
            return std::unexpected(loaded.error());
        }
    }

    const auto query_embedding = [&state, &request]
    {
        const ScopedStage stage{"query-embed"};
        return state.model->embed(request.query);
    }();
    if (!query_embedding)
    {
        return TextFailure(ErrorKind::QueryEmbed, query_embedding.error(), scanned->counts);
    }

    progress.emplace(request.progress);
    state.query_embedding = &*query_embedding;
    state.progress = &*progress;
    if (const auto embedded = EmbedGrepWindows(state); !embedded)
    {
        return std::unexpected(embedded.error());
    }
    scan_done.Fire();
    progress->Finish();

    if (!need_lexical)
    {
        return DenseGrepResults(state, EmptyGrepResults(state, request.group));
    }
    return RankLexicalGrep(state, query_tokens, effective);
}

} // namespace nesso
