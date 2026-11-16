// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "core/core_types.hpp"
#include <cstddef>
#include <cstdint>
#include <nesso/nesso.hpp>

#include <nesso/error_map.hpp>
#include <nesso/trace.hpp>
#include <nesso_detail.hpp>

#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/distance.hpp>
#include <core/lexical_terms.hpp>
#include <core/query_class.hpp>
#include <core/score_fusion.hpp>
#include <core/top_k.hpp>
#include <embed/embed_types.hpp>
#include <embed/embedder.hpp>
#include <embed/session_policy.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace nesso
{

using detail::BetterHit;
using detail::EffectiveMode;
using detail::EffectiveModeName;
using detail::MakeCorpusPolicy;
using detail::MakeError;
using detail::MAX_LINE_LENGTH;
using detail::QueryTermViews;
using detail::ResolveEffectiveMode;
using detail::SameSource;

static void AddSourceWarning(std::vector<StaleSource> &warnings, StaleSource warning)
{
    const auto existing =
        std::ranges::find_if(warnings, [&warning](const StaleSource &item) { return item.path == warning.path; });
    if (existing == warnings.end())
    {
        warnings.push_back(std::move(warning));
    }
    else if (warning.unreadable)
    {
        existing->unreadable = true;
    }
}

static std::vector<StaleSource> StaleSources(std::span<const nesso::core::SourceInfo> sources)
{
    std::vector<StaleSource> stale;
    for (const nesso::core::SourceInfo &source : sources)
    {
        if (source.stored_only)
        {
            continue;
        }
        if (!source.tracked)
        {
            AddSourceWarning(stale, {.path = source.path, .unreadable = true});
            continue;
        }

        std::error_code error;
        const std::uint64_t size = std::filesystem::file_size(source.path, error);
        if (error)
        {
            AddSourceWarning(stale, {.path = source.path, .missing = true});
            continue;
        }
        const auto modified = std::filesystem::last_write_time(source.path, error);
        if (error)
        {
            AddSourceWarning(stale, {.path = source.path, .missing = true});
            continue;
        }
        const auto modified_time =
            std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
        if (size != source.size || modified_time != source.modified_time)
        {
            AddSourceWarning(stale, {.path = source.path});
        }
    }
    return stale;
}

static std::expected<void, Error> RequireLexicalReady(nesso::core::LexicalStatus status,
                                                      std::string_view corpus_terms_version, SearchMode requested)
{
    if (requested == SearchMode::Dense)
    {
        return {};
    }
    if (status == nesso::core::LexicalStatus::Ok)
    {
        return {};
    }
    Error error;
    error.kind = ErrorKind::CorpusRead;
    if (status == nesso::core::LexicalStatus::Missing)
    {
        error.cause = ErrorCause::NoLexicalPostings;
    }
    else
    {
        error.cause = ErrorCause::LexicalTermsMismatch;
        error.note = std::string{corpus_terms_version.empty() ? "?" : corpus_terms_version};
    }
    return std::unexpected(std::move(error));
}

static std::vector<double> Bm25ScoresFromCorpus(const nesso::core::MappedCorpus &corpus,
                                                std::span<const std::string_view> query)
{
    const std::span<const nesso::core::LexicalLinePostings> rows = corpus.LexicalPostings();
    std::vector<nesso::core::Bm25LinePostings> lines;
    lines.reserve(rows.size());
    for (const nesso::core::LexicalLinePostings &row : rows)
    {
        lines.push_back(nesso::core::Bm25LinePostings{.length = row.length, .entries = row.entries});
    }
    return nesso::core::ScoresFromPostings(corpus.LexicalDictionary(), lines, query);
}

static size_t ScoringThreads(const Config &config)
{
    if (config.threads != 0)
    {
        return config.threads;
    }
    return nesso::embed::ReadMachine().physical_cores;
}

struct CorpusScore
{
    std::span<const float> query;
    std::span<const float> matrix;
    size_t dimensions = 0;
    std::span<float> scores;
    std::mutex mutex;
    std::optional<nesso::core::EngineError> dot_error;
    size_t failure_at = std::numeric_limits<size_t>::max();
    bool non_finite = false;
};

static void ScoreCorpusRows(void *raw, size_t begin, size_t end)
{
    auto *job = static_cast<CorpusScore *>(raw);
    for (size_t id = begin; id < end; ++id)
    {
        const auto row = job->matrix.subspan(id * job->dimensions, job->dimensions);
        const auto score = nesso::core::math::DistanceMetrics::DotProduct<float>(job->query, row);
        if (!score)
        {
            const std::scoped_lock lock(job->mutex);
            if (id < job->failure_at)
            {
                job->dot_error = score.error();
                job->non_finite = false;
                job->failure_at = id;
            }
            return;
        }
        if (!std::isfinite(*score))
        {
            const std::scoped_lock lock(job->mutex);
            if (id < job->failure_at)
            {
                job->dot_error.reset();
                job->non_finite = true;
                job->failure_at = id;
            }
            return;
        }
        job->scores[id] = *score;
    }
}

struct LineRank
{
    std::span<const nesso::core::CorpusLine> lines;
    const std::vector<float> *scores = nullptr;
};

static float LineScore(void *raw, size_t index)
{
    const auto *view = static_cast<const LineRank *>(raw);
    return (*view->scores)[view->lines[index].distinct_id];
}

static bool LineKeep(void *raw, size_t index)
{
    const auto *view = static_cast<const LineRank *>(raw);
    return view->lines[index].distinct_id != nesso::core::NO_DISTINCT;
}

struct FusedLineRank
{
    const std::vector<float> *scores = nullptr;
    const std::vector<char> *keep = nullptr;
};

static float FusedLineScore(void *raw, size_t index)
{
    const auto *view = static_cast<const FusedLineRank *>(raw);
    return (*view->scores)[index];
}

static bool FusedLineKeep(void *raw, size_t index)
{
    const auto *view = static_cast<const FusedLineRank *>(raw);
    return (*view->keep)[index] != 0;
}

struct RankedLine
{
    uint64_t ordinal = 0;
    float score = 0.0F;
    float dense = 0.0F;
    float lexical = 0.0F;
    bool has_dense = false;
    bool has_lexical = false;
};

static std::vector<RankedLine> RankDenseGroups(const nesso::core::MappedCorpus &corpus,
                                               const std::vector<float> &distinct_scores, size_t top_k)
{
    const std::size_t distinct = corpus.Texts().size();
    struct Group
    {
        uint64_t ordinal = 0;
        float score = 0.0F;
    };
    std::vector<Group> groups;
    std::vector<char> seen(distinct, 0);
    groups.reserve(distinct);
    for (std::size_t ordinal = 0; ordinal < corpus.Lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.Lines()[ordinal];
        if (line.distinct_id == nesso::core::NO_DISTINCT || seen[line.distinct_id] != 0)
        {
            continue;
        }
        seen[line.distinct_id] = 1;
        groups.push_back(Group{.ordinal = ordinal, .score = distinct_scores[line.distinct_id]});
    }
    std::ranges::sort(groups, [](const Group &left, const Group &right)
                      { return BetterHit(left.score, left.ordinal, right.score, right.ordinal); });
    if (groups.size() > top_k)
    {
        groups.resize(top_k);
    }
    std::vector<RankedLine> ranked;
    ranked.reserve(groups.size());
    for (const Group &group : groups)
    {
        ranked.push_back({.ordinal = group.ordinal, .score = group.score, .dense = group.score, .has_dense = true});
    }
    return ranked;
}

static std::vector<RankedLine> RankDenseLines(const nesso::core::MappedCorpus &corpus,
                                              const std::vector<float> &distinct_scores, size_t top_k, size_t threads,
                                              bool group)
{
    if (group)
    {
        return RankDenseGroups(corpus, distinct_scores, top_k);
    }
    LineRank view{.lines = corpus.Lines(), .scores = &distinct_scores};
    const std::vector<nesso::core::Hit> hits = nesso::core::RankTop({.count = corpus.Lines().size(),
                                                                     .k = top_k,
                                                                     .threads = threads,
                                                                     .score = LineScore,
                                                                     .keep = LineKeep,
                                                                     .context = &view});
    std::vector<RankedLine> ranked;
    ranked.reserve(hits.size());
    for (const nesso::core::Hit &hit : hits)
    {
        const nesso::core::CorpusLine &line = corpus.Lines()[static_cast<size_t>(hit.index)];
        ranked.push_back(
            {.ordinal = hit.index, .score = hit.score, .dense = hit.score, .has_dense = true, .has_lexical = false});
        (void)line;
    }
    return ranked;
}

struct HybridScoreInputs
{
    std::vector<uint64_t> ordinals;
    std::vector<float> fused;
};

static HybridScoreInputs CollectHybridScores(const nesso::core::MappedCorpus &corpus,
                                             const std::vector<float> &distinct_scores,
                                             const std::vector<double> &bm25_scores)
{
    std::vector<double> dense_values;
    std::vector<double> lexical_values;
    HybridScoreInputs inputs;
    dense_values.reserve(corpus.Lines().size());
    lexical_values.reserve(corpus.Lines().size());
    inputs.ordinals.reserve(corpus.Lines().size());
    for (size_t ordinal = 0; ordinal < corpus.Lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.Lines()[ordinal];
        if (line.distinct_id == nesso::core::NO_DISTINCT)
        {
            continue;
        }
        inputs.ordinals.push_back(ordinal);
        dense_values.push_back(static_cast<double>(distinct_scores[line.distinct_id]));
        lexical_values.push_back(bm25_scores[ordinal]);
    }
    inputs.fused = nesso::core::FuseHybridScores(dense_values, lexical_values);
    return inputs;
}

static std::vector<RankedLine> RankHybridGrouped(const nesso::core::MappedCorpus &corpus,
                                                 const std::vector<float> &distinct_scores,
                                                 const std::vector<double> &bm25_scores,
                                                 const HybridScoreInputs &inputs, size_t top_k)
{
    const std::size_t distinct = corpus.Texts().size();
    std::vector<uint64_t> first(distinct, 0);
    std::vector<char> have(distinct, 0);
    std::vector<float> best_score(distinct);
    std::vector<float> best_lexical(distinct);
    std::vector<uint64_t> best_ordinal(distinct);
    std::vector<std::uint32_t> ids;
    ids.reserve(distinct);
    for (std::size_t index = 0; index < inputs.ordinals.size(); ++index)
    {
        const uint64_t ordinal = inputs.ordinals[index];
        const auto distinct_id = corpus.Lines()[static_cast<std::size_t>(ordinal)].distinct_id;
        const float score = inputs.fused[index];
        const auto lexical = static_cast<float>(bm25_scores[static_cast<std::size_t>(ordinal)]);
        if (have[distinct_id] == 0)
        {
            have[distinct_id] = 1;
            first[distinct_id] = ordinal;
            ids.push_back(distinct_id);
            best_score[distinct_id] = score;
            best_lexical[distinct_id] = lexical;
            best_ordinal[distinct_id] = ordinal;
        }
        else if (BetterHit(score, ordinal, best_score[distinct_id], best_ordinal[distinct_id]))
        {
            best_score[distinct_id] = score;
            best_lexical[distinct_id] = lexical;
            best_ordinal[distinct_id] = ordinal;
        }
    }
    struct Group
    {
        uint64_t ordinal = 0;
        float score = 0.0F;
        float dense = 0.0F;
        float lexical = 0.0F;
    };
    std::vector<Group> groups;
    groups.reserve(ids.size());
    for (const std::uint32_t distinct_id : ids)
    {
        groups.push_back(Group{.ordinal = first[distinct_id],
                               .score = best_score[distinct_id],
                               .dense = distinct_scores[distinct_id],
                               .lexical = best_lexical[distinct_id]});
    }
    std::ranges::sort(groups, [](const Group &left, const Group &right)
                      { return BetterHit(left.score, left.ordinal, right.score, right.ordinal); });
    if (groups.size() > top_k)
    {
        groups.resize(top_k);
    }
    std::vector<RankedLine> ranked;
    ranked.reserve(groups.size());
    for (const Group &row : groups)
    {
        ranked.push_back({.ordinal = row.ordinal,
                          .score = row.score,
                          .dense = row.dense,
                          .lexical = row.lexical,
                          .has_dense = true,
                          .has_lexical = true});
    }
    return ranked;
}

static std::vector<RankedLine> RankHybridUngrouped(const nesso::core::MappedCorpus &corpus,
                                                   const std::vector<float> &distinct_scores,
                                                   const std::vector<double> &bm25_scores,
                                                   const HybridScoreInputs &inputs, size_t top_k, size_t threads)
{
    std::vector<float> line_scores(corpus.Lines().size(), 0.0F);
    std::vector<char> keep(corpus.Lines().size(), 0);
    for (size_t index = 0; index < inputs.ordinals.size(); ++index)
    {
        line_scores[static_cast<size_t>(inputs.ordinals[index])] = inputs.fused[index];
        keep[static_cast<size_t>(inputs.ordinals[index])] = 1;
    }
    FusedLineRank view{.scores = &line_scores, .keep = &keep};
    const std::vector<nesso::core::Hit> hits = nesso::core::RankTop({.count = corpus.Lines().size(),
                                                                     .k = top_k,
                                                                     .threads = threads,
                                                                     .score = FusedLineScore,
                                                                     .keep = FusedLineKeep,
                                                                     .context = &view});
    std::vector<RankedLine> ranked;
    ranked.reserve(hits.size());
    for (const nesso::core::Hit &hit : hits)
    {
        const auto ordinal = static_cast<size_t>(hit.index);
        const nesso::core::CorpusLine &line = corpus.Lines()[ordinal];
        ranked.push_back({.ordinal = hit.index,
                          .score = hit.score,
                          .dense = distinct_scores[line.distinct_id],
                          .lexical = static_cast<float>(bm25_scores[ordinal]),
                          .has_dense = true,
                          .has_lexical = true});
    }
    return ranked;
}

static std::vector<RankedLine> RankHybridLines(const nesso::core::MappedCorpus &corpus,
                                               const std::vector<float> &distinct_scores,
                                               const std::vector<double> &bm25_scores, size_t top_k, size_t threads,
                                               bool group)
{
    const HybridScoreInputs inputs = CollectHybridScores(corpus, distinct_scores, bm25_scores);
    if (group)
    {
        return RankHybridGrouped(corpus, distinct_scores, bm25_scores, inputs, top_k);
    }
    return RankHybridUngrouped(corpus, distinct_scores, bm25_scores, inputs, top_k, threads);
}

struct LexicalCandidate
{
    uint64_t ordinal = 0;
    float score = 0.0F;
    bool substring = false;
    std::string text;
};

static bool LexicalCandidateBefore(const LexicalCandidate &left, const LexicalCandidate &right)
{
    if (left.substring != right.substring)
    {
        return left.substring && !right.substring;
    }
    return BetterHit(left.score, left.ordinal, right.score, right.ordinal);
}

static void RematerializeLexicalSubstrings(const nesso::core::MappedCorpus &corpus, std::string_view needle,
                                           std::vector<LexicalCandidate> &hits)
{
    for (LexicalCandidate &hit : hits)
    {
        const nesso::core::CorpusLine &line = corpus.Lines()[static_cast<std::size_t>(hit.ordinal)];
        std::string own;
        if (line.stored_text)
        {
            own = std::string{*line.stored_text};
        }
        else
        {
            const nesso::core::SourceInfo &source = corpus.Sources()[line.source_index];
            auto reread = nesso::parser::Chunker::ReadRecord(source.path,
                                                             {.text = {},
                                                              .line_number = line.line_number,
                                                              .byte_offset = line.byte_offset,
                                                              .array_element = line.array_element,
                                                              .byte_length = line.byte_length},
                                                             source.json_field, MAX_LINE_LENGTH);
            if (reread)
            {
                own = std::move(*reread);
            }
            else
            {
                own = std::string{corpus.Texts()[line.distinct_id]};
            }
        }
        hit.substring = !needle.empty() && own.find(needle) != std::string::npos;
    }
    std::ranges::sort(hits, LexicalCandidateBefore);
}

static std::vector<LexicalCandidate> CollapseLexicalGroups(const nesso::core::MappedCorpus &corpus,
                                                           const std::vector<LexicalCandidate> &hits)
{
    const std::size_t distinct = corpus.Texts().size();
    std::vector<uint64_t> first(distinct, std::numeric_limits<uint64_t>::max());
    for (std::size_t ordinal = 0; ordinal < corpus.Lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.Lines()[ordinal];
        if (line.distinct_id == nesso::core::NO_DISTINCT ||
            first[line.distinct_id] != std::numeric_limits<uint64_t>::max())
        {
            continue;
        }
        first[line.distinct_id] = ordinal;
    }
    std::vector<char> taken(distinct, 0);
    std::vector<LexicalCandidate> groups;
    groups.reserve(hits.size());
    for (const LexicalCandidate &hit : hits)
    {
        const auto distinct_id = corpus.Lines()[static_cast<std::size_t>(hit.ordinal)].distinct_id;
        if (taken[distinct_id] != 0)
        {
            continue;
        }
        taken[distinct_id] = 1;
        LexicalCandidate row = hit;
        row.ordinal = first[distinct_id];
        groups.push_back(std::move(row));
    }
    std::ranges::sort(groups, LexicalCandidateBefore);
    return groups;
}

static std::vector<RankedLine> RankLexicalLines(const nesso::core::MappedCorpus &corpus,
                                                const std::vector<double> &bm25_scores, std::string_view needle,
                                                size_t top_k, bool group)
{
    std::vector<LexicalCandidate> hits;
    for (size_t ordinal = 0; ordinal < corpus.Lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.Lines()[ordinal];
        if (line.distinct_id == nesso::core::NO_DISTINCT || !(bm25_scores[ordinal] > 0.0))
        {
            continue;
        }
        const std::string_view text = line.stored_text.value_or(corpus.Texts()[line.distinct_id]);
        hits.push_back({.ordinal = ordinal,
                        .score = static_cast<float>(bm25_scores[ordinal]),
                        .substring = !needle.empty() && text.find(needle) != std::string_view::npos,
                        .text = std::string{text}});
    }
    std::ranges::sort(hits, LexicalCandidateBefore);
    if (group)
    {
        RematerializeLexicalSubstrings(corpus, needle, hits);
        hits = CollapseLexicalGroups(corpus, hits);
    }
    if (hits.size() > top_k)
    {
        hits.resize(top_k);
    }
    std::vector<RankedLine> ranked;
    ranked.reserve(hits.size());
    for (const LexicalCandidate &hit : hits)
    {
        ranked.push_back({.ordinal = hit.ordinal,
                          .score = hit.score,
                          .lexical = hit.score,
                          .has_dense = false,
                          .has_lexical = true});
    }
    return ranked;
}

static std::expected<nesso::core::MappedCorpus, Error> MapSearchCorpus(const std::filesystem::path &path)
{
    auto corpus = [&path]
    {
        const ScopedStage stage{"corpus-read"};
        return nesso::core::MapCorpusFile(path);
    }();
    if (!corpus)
    {
        const nesso::core::CorpusMapFailure &failure = corpus.error();
        Error error = MakeError(ErrorKind::CorpusRead, CauseOf(failure.code), path, {});
        if (failure.code == nesso::core::EngineError::UnsupportedVersion)
        {
            error.corpus_version = failure.version;
        }
        return std::unexpected(std::move(error));
    }
    if (corpus->corpus.Texts().empty())
    {
        return std::unexpected(MakeError(ErrorKind::EmptyInput, ErrorCause::EmptyCorpus, path, {}));
    }
    return std::move(corpus->corpus);
}

static std::expected<void, Error> RefuseModelMismatch(const nesso::core::CorpusPolicy &stored,
                                                      const nesso::core::CorpusPolicy &current,
                                                      const std::filesystem::path &path)
{
    constexpr std::array<std::string_view, 5> identity_keys{"model.id", "model.sha256", "vocab.sha256",
                                                            "model.quantization", "dimensions"};
    for (const std::string_view key : identity_keys)
    {
        if (stored.find(key)->second != current.find(key)->second)
        {
            return std::unexpected(MakeError(ErrorKind::CorpusRead, ErrorCause::ModelMismatch, path, {}));
        }
    }
    return {};
}

struct SearchLineMeta
{
    std::vector<std::uint32_t> occurrences;
    bool multiple_sources = false;
};

static SearchLineMeta TallySearchLineMeta(const nesso::core::MappedCorpus &corpus)
{
    SearchLineMeta meta{.occurrences = std::vector<std::uint32_t>(corpus.Texts().size(), 0)};
    std::optional<std::string_view> first_source;
    for (size_t ordinal = 0; ordinal < corpus.Lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.Lines()[ordinal];
        if (line.distinct_id == nesso::core::NO_DISTINCT)
        {
            continue;
        }
        ++meta.occurrences[line.distinct_id];
        const std::string &source = corpus.Sources()[line.source_index].path;
        if (!first_source)
        {
            first_source = source;
        }
        else if (*first_source != source)
        {
            meta.multiple_sources = true;
        }
    }
    return meta;
}

static bool SourcePathIsStale(std::span<const StaleSource> warnings, const std::string &path)
{
    return std::ranges::any_of(warnings, [&path](const StaleSource &warning) { return warning.path == path; });
}

static std::string ResolveSearchMatchText(const nesso::core::MappedCorpus &corpus, const nesso::core::CorpusLine &line,
                                          const nesso::core::SourceInfo &source, std::vector<StaleSource> &warnings)
{
    std::string text{line.stored_text.value_or(corpus.Texts()[line.distinct_id])};
    if (line.stored_text || SourcePathIsStale(warnings, source.path))
    {
        return text;
    }
    auto reread = nesso::parser::Chunker::ReadRecord(source.path,
                                                     {.text = {},
                                                      .line_number = line.line_number,
                                                      .byte_offset = line.byte_offset,
                                                      .array_element = line.array_element,
                                                      .byte_length = line.byte_length},
                                                     source.json_field, MAX_LINE_LENGTH);
    if (!reread)
    {
        AddSourceWarning(warnings, {.path = source.path, .unreadable = true});
        return text;
    }
    const auto after = nesso::core::SourceInfoFor(source.path, source.json_field);
    if (!after || !SameSource(source, *after))
    {
        AddSourceWarning(warnings, {.path = source.path, .missing = !after});
        return text;
    }
    return std::move(*reread);
}

static TextMatch MakeSearchMatch(const nesso::core::MappedCorpus &corpus, const RankedLine &hit,
                                 const std::vector<std::uint32_t> &occurrences, std::string_view mode_label,
                                 std::string text)
{
    const nesso::core::CorpusLine &line = corpus.Lines()[static_cast<size_t>(hit.ordinal)];
    const nesso::core::SourceInfo &source = corpus.Sources()[line.source_index];
    TextMatch match{.source = source.path,
                    .line = line.line_number,
                    .score = hit.score,
                    .text = std::move(text),
                    .array_element = line.array_element,
                    .occurrences = occurrences[line.distinct_id],
                    .mode = std::string{mode_label},
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
    return match;
}

struct SearchMaterializeFlags
{
    bool multiple_sources = false;
    bool policy_mismatch = false;
    bool lexical_fallback = false;
    bool group = false;
};

static TextResults MaterializeSearchMatches(const nesso::core::MappedCorpus &corpus,
                                            const std::vector<RankedLine> &ranked,
                                            const std::vector<std::uint32_t> &occurrences,
                                            const SearchMaterializeFlags &flags, std::string_view mode_label,
                                            std::vector<StaleSource> source_warnings)
{
    TextResults results{};
    results.grouped = flags.group;
    results.multiple_sources = flags.multiple_sources;
    results.policy_mismatch = flags.policy_mismatch;
    results.lexical_fallback = flags.lexical_fallback;
    results.matches.reserve(ranked.size());
    for (const RankedLine &hit : ranked)
    {
        const nesso::core::CorpusLine &line = corpus.Lines()[static_cast<size_t>(hit.ordinal)];
        const nesso::core::SourceInfo &source = corpus.Sources()[line.source_index];
        std::string text = ResolveSearchMatchText(corpus, line, source, source_warnings);
        results.matches.push_back(MakeSearchMatch(corpus, hit, occurrences, mode_label, std::move(text)));
    }
    for (size_t index = 0; index < ranked.size(); ++index)
    {
        const nesso::core::CorpusLine &line = corpus.Lines()[static_cast<size_t>(ranked[index].ordinal)];
        if (!line.stored_text && SourcePathIsStale(source_warnings, corpus.Sources()[line.source_index].path))
        {
            results.matches[index].text = corpus.Texts()[line.distinct_id];
        }
    }
    results.stale_sources = std::move(source_warnings);
    return results;
}

struct SearchDenseScores
{
    std::vector<float> scores;
    std::optional<nesso::core::EngineError> dot_error;
    bool non_finite = false;
};

static SearchDenseScores ScoreSearchDense(const nesso::core::MappedCorpus &corpus,
                                          std::span<const float> query_embedding, size_t threads)
{
    const ScopedStage distance{"distance"};
    SearchDenseScores result;
    result.scores.assign(corpus.Texts().size(), 0.0F);
    CorpusScore scored;
    scored.query = query_embedding;
    scored.matrix = corpus.Matrix();
    scored.dimensions = corpus.Dimensions();
    scored.scores = result.scores;
    nesso::core::detail::ParallelRows(threads, result.scores.size(), ScoreCorpusRows, &scored);
    result.dot_error = scored.dot_error;
    result.non_finite = scored.non_finite;
    return result;
}

struct SearchRankOptions
{
    EffectiveMode effective = EffectiveMode::Dense;
    size_t top_k = 0;
    size_t threads = 0;
    bool group = false;
};

static std::vector<RankedLine> SelectSearchRanked(const nesso::core::MappedCorpus &corpus,
                                                  const std::vector<float> &scores,
                                                  const std::vector<double> &bm25_scores,
                                                  const nesso::core::QueryTokens &query_tokens,
                                                  const SearchRankOptions &options)
{
    if (options.effective == EffectiveMode::Dense)
    {
        return RankDenseLines(corpus, scores, options.top_k, options.threads, options.group);
    }
    if (options.effective == EffectiveMode::Lexical)
    {
        return RankLexicalLines(corpus, bm25_scores, query_tokens.stripped, options.top_k, options.group);
    }
    return RankHybridLines(corpus, scores, bm25_scores, options.top_k, options.threads, options.group);
}

static std::expected<std::vector<float>, nesso::embed::EmbedError> EmbedSearchQuery(const nesso::embed::Embedder &model,
                                                                                    std::string_view query)
{
    const ScopedStage query_embed{"query-embed"};
    return model.embed(query);
}

static EffectiveMode MaybeLexicalFallback(EffectiveMode effective, std::span<const double> bm25_scores,
                                          bool &lexical_fallback)
{
    if (effective != EffectiveMode::Lexical)
    {
        return effective;
    }
    const bool any_hit = std::ranges::any_of(bm25_scores, [](double score) { return score > 0.0; });
    if (any_hit)
    {
        return effective;
    }
    lexical_fallback = true;
    return EffectiveMode::Hybrid;
}

std::expected<const nesso::embed::Embedder *, Error> Nesso::LoadSearchEmbedder() const
{
    const ScopedStage model_load{"model-load"};
    return Embedder();
}

std::expected<TextResults, Error> Nesso::Search(const SearchRequest &request) const
{
    const TraceSession session;
    auto corpus = MapSearchCorpus(request.index);
    if (!corpus)
    {
        return std::unexpected(std::move(corpus.error()));
    }

    const auto model = LoadSearchEmbedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }
    const auto current = MakeCorpusPolicy(**model, {});
    if (!current)
    {
        return std::unexpected(current.error());
    }
    if (const auto identity = RefuseModelMismatch(corpus->Policy(), *current, request.index); !identity)
    {
        return std::unexpected(identity.error());
    }
    constexpr std::array<std::string_view, 5> policy_keys{"batch", "truncate", "dedup", "mask", "tokens.max"};
    const bool policy_mismatch =
        std::ranges::any_of(policy_keys, [&](std::string_view key)
                            { return corpus->Policy().find(key)->second != current->find(key)->second; });
    auto source_warnings = StaleSources(corpus->Sources());
    const auto fail = [&](ErrorKind kind, ErrorCause cause)
    {
        Error error = MakeError(kind, cause, {}, {});
        error.policy_mismatch = policy_mismatch;
        error.stale_sources = source_warnings;
        return std::unexpected(std::move(error));
    };

    if (const auto lexical_ready =
            RequireLexicalReady(corpus->GetLexicalStatus(), corpus->CorpusLexicalTermsVersion(), request.mode);
        !lexical_ready)
    {
        Error error = lexical_ready.error();
        error.policy_mismatch = policy_mismatch;
        error.stale_sources = source_warnings;
        return std::unexpected(std::move(error));
    }

    const nesso::core::QueryTokens query_tokens = nesso::core::TokenizeQuery(request.query);
    EffectiveMode effective = ResolveEffectiveMode(request.mode, nesso::core::ClassifyQuery(request.query));
    bool lexical_fallback = false;

    const ScopedStage stage{"rank"};
    const auto query_embedding = EmbedSearchQuery(**model, request.query);
    if (!query_embedding)
    {
        return fail(ErrorKind::QueryEmbed, CauseOf(query_embedding.error()));
    }
    if (query_embedding->size() != corpus->Dimensions())
    {
        return fail(ErrorKind::Search, ErrorCause::MismatchedDimensions);
    }
    const size_t threads = ScoringThreads(config_);
    const SearchDenseScores dense = ScoreSearchDense(*corpus, *query_embedding, threads);
    if (dense.dot_error)
    {
        return fail(ErrorKind::Search, CauseOf(*dense.dot_error));
    }
    if (dense.non_finite)
    {
        return fail(ErrorKind::Search, ErrorCause::InvalidInput);
    }

    const SearchLineMeta line_meta = TallySearchLineMeta(*corpus);
    std::vector<double> bm25_scores;
    if (effective != EffectiveMode::Dense)
    {
        const std::vector<std::string> query_terms = nesso::core::LexicalTerms(query_tokens.stripped);
        bm25_scores = Bm25ScoresFromCorpus(*corpus, QueryTermViews(query_terms));
    }
    effective = MaybeLexicalFallback(effective, bm25_scores, lexical_fallback);

    const std::vector<RankedLine> ranked = SelectSearchRanked(
        *corpus, dense.scores, bm25_scores, query_tokens,
        {.effective = effective, .top_k = request.top_k, .threads = threads, .group = request.group});
    if (ranked.empty())
    {
        return fail(ErrorKind::NoMatches, ErrorCause::None);
    }

    return MaterializeSearchMatches(*corpus, ranked, line_meta.occurrences,
                                    {.multiple_sources = line_meta.multiple_sources,
                                     .policy_mismatch = policy_mismatch,
                                     .lexical_fallback = lexical_fallback,
                                     .group = request.group},
                                    EffectiveModeName(effective), std::move(source_warnings));
}

} // namespace nesso
