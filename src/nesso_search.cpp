// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso.hpp>

#include <error_map.hpp>
#include <nesso_detail.hpp>
#include <trace.hpp>

#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/distance.hpp>
#include <core/lexical_terms.hpp>
#include <core/query_class.hpp>
#include <core/score_fusion.hpp>
#include <core/top_k.hpp>
#include <embed/batch_plan.hpp>
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
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nesso
{

using detail::betterHit;
using detail::EffectiveMode;
using detail::effectiveModeName;
using detail::MakeCorpusPolicy;
using detail::makeError;
using detail::MAX_LINE_LENGTH;
using detail::queryTermViews;
using detail::resolveEffectiveMode;
using detail::sameSource;

static void addSourceWarning(std::vector<StaleSource> &warnings, StaleSource warning)
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

static std::vector<StaleSource> staleSources(std::span<const nesso::core::SourceInfo> sources)
{
    std::vector<StaleSource> stale;
    for (const nesso::core::SourceInfo &source : sources)
    {
        if (source.storedOnly)
        {
            continue;
        }
        if (!source.tracked)
        {
            addSourceWarning(stale, {.path = source.path, .unreadable = true});
            continue;
        }

        std::error_code error;
        const std::uint64_t size = std::filesystem::file_size(source.path, error);
        if (error)
        {
            addSourceWarning(stale, {.path = source.path, .missing = true});
            continue;
        }
        const auto modified = std::filesystem::last_write_time(source.path, error);
        if (error)
        {
            addSourceWarning(stale, {.path = source.path, .missing = true});
            continue;
        }
        const auto modifiedTime =
            std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
        if (size != source.size || modifiedTime != source.modifiedTime)
        {
            addSourceWarning(stale, {.path = source.path});
        }
    }
    return stale;
}

static std::expected<void, Error> requireLexicalReady(nesso::core::LexicalStatus status,
                                                      std::string_view corpusTermsVersion, SearchMode requested)
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
        error.note = std::string{corpusTermsVersion.empty() ? "?" : corpusTermsVersion};
    }
    return std::unexpected(std::move(error));
}

static std::vector<double> bm25ScoresFromCorpus(const nesso::core::MappedCorpus &corpus,
                                                std::span<const std::string_view> query)
{
    const std::span<const nesso::core::LexicalLinePostings> rows = corpus.lexicalPostings();
    std::vector<nesso::core::Bm25LinePostings> lines;
    lines.reserve(rows.size());
    for (const nesso::core::LexicalLinePostings &row : rows)
    {
        lines.push_back(nesso::core::Bm25LinePostings{.length = row.length, .entries = row.entries});
    }
    return nesso::core::scoresFromPostings(corpus.lexicalDictionary(), lines, query);
}

static size_t scoringThreads(const Config &config)
{
    if (config.threads != 0)
    {
        return config.threads;
    }
    return nesso::embed::readMachine().physicalCores;
}

struct CorpusScore
{
    std::span<const float> query;
    std::span<const float> matrix;
    size_t dimensions = 0;
    float *scores = nullptr;
    std::mutex mutex;
    std::optional<nesso::core::EngineError> dotError;
    size_t failureAt = std::numeric_limits<size_t>::max();
    bool nonFinite = false;
};

static void scoreCorpusRows(void *raw, size_t begin, size_t end)
{
    auto *job = static_cast<CorpusScore *>(raw);
    for (size_t id = begin; id < end; ++id)
    {
        const auto row = job->matrix.subspan(id * job->dimensions, job->dimensions);
        const auto score = nesso::core::math::DistanceMetrics::dotProduct<float>(job->query, row);
        if (!score)
        {
            const std::scoped_lock lock(job->mutex);
            if (id < job->failureAt)
            {
                job->dotError = score.error();
                job->nonFinite = false;
                job->failureAt = id;
            }
            return;
        }
        if (!std::isfinite(*score))
        {
            const std::scoped_lock lock(job->mutex);
            if (id < job->failureAt)
            {
                job->dotError.reset();
                job->nonFinite = true;
                job->failureAt = id;
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

static float lineScore(void *raw, size_t index)
{
    const auto *view = static_cast<const LineRank *>(raw);
    return (*view->scores)[view->lines[index].distinctId];
}

static bool lineKeep(void *raw, size_t index)
{
    const auto *view = static_cast<const LineRank *>(raw);
    return view->lines[index].distinctId != nesso::core::NO_DISTINCT;
}

struct FusedLineRank
{
    const std::vector<float> *scores = nullptr;
    const std::vector<char> *keep = nullptr;
};

static float fusedLineScore(void *raw, size_t index)
{
    const auto *view = static_cast<const FusedLineRank *>(raw);
    return (*view->scores)[index];
}

static bool fusedLineKeep(void *raw, size_t index)
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
    bool hasDense = false;
    bool hasLexical = false;
};

static std::vector<RankedLine> rankDenseGroups(const nesso::core::MappedCorpus &corpus,
                                               const std::vector<float> &distinctScores, size_t topK)
{
    const std::size_t distinct = corpus.texts().size();
    struct Group
    {
        uint64_t ordinal = 0;
        float score = 0.0F;
    };
    std::vector<Group> groups;
    std::vector<char> seen(distinct, 0);
    groups.reserve(distinct);
    for (std::size_t ordinal = 0; ordinal < corpus.lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT || seen[line.distinctId] != 0)
        {
            continue;
        }
        seen[line.distinctId] = 1;
        groups.push_back(Group{.ordinal = ordinal, .score = distinctScores[line.distinctId]});
    }
    std::ranges::sort(groups, [](const Group &left, const Group &right)
                      { return betterHit(left.score, left.ordinal, right.score, right.ordinal); });
    if (groups.size() > topK)
    {
        groups.resize(topK);
    }
    std::vector<RankedLine> ranked;
    ranked.reserve(groups.size());
    for (const Group &group : groups)
    {
        ranked.push_back({.ordinal = group.ordinal, .score = group.score, .dense = group.score, .hasDense = true});
    }
    return ranked;
}

static std::vector<RankedLine> rankDenseLines(const nesso::core::MappedCorpus &corpus,
                                              const std::vector<float> &distinctScores, size_t topK, size_t threads,
                                              bool group)
{
    if (group)
    {
        return rankDenseGroups(corpus, distinctScores, topK);
    }
    LineRank view{.lines = corpus.lines(), .scores = &distinctScores};
    const std::vector<nesso::core::Hit> hits =
        nesso::core::rankTop(corpus.lines().size(), topK, threads, lineScore, lineKeep, &view);
    std::vector<RankedLine> ranked;
    ranked.reserve(hits.size());
    for (const nesso::core::Hit &hit : hits)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[static_cast<size_t>(hit.index)];
        ranked.push_back(
            {.ordinal = hit.index, .score = hit.score, .dense = hit.score, .hasDense = true, .hasLexical = false});
        (void)line;
    }
    return ranked;
}

static std::vector<RankedLine> rankHybridLines(const nesso::core::MappedCorpus &corpus,
                                               const std::vector<float> &distinctScores,
                                               const std::vector<double> &bm25Scores, size_t topK, size_t threads,
                                               bool group)
{
    std::vector<double> denseValues;
    std::vector<double> lexicalValues;
    std::vector<uint64_t> ordinals;
    denseValues.reserve(corpus.lines().size());
    lexicalValues.reserve(corpus.lines().size());
    ordinals.reserve(corpus.lines().size());
    for (size_t ordinal = 0; ordinal < corpus.lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT)
        {
            continue;
        }
        ordinals.push_back(ordinal);
        denseValues.push_back(static_cast<double>(distinctScores[line.distinctId]));
        lexicalValues.push_back(bm25Scores[ordinal]);
    }
    const std::vector<float> fused = nesso::core::fuseHybridScores(denseValues, lexicalValues);
    if (group)
    {
        const std::size_t distinct = corpus.texts().size();
        std::vector<uint64_t> first(distinct, 0);
        std::vector<char> have(distinct, 0);
        std::vector<float> bestScore(distinct);
        std::vector<float> bestLexical(distinct);
        std::vector<uint64_t> bestOrdinal(distinct);
        std::vector<std::uint32_t> ids;
        ids.reserve(distinct);
        for (std::size_t index = 0; index < ordinals.size(); ++index)
        {
            const uint64_t ordinal = ordinals[index];
            const auto id = corpus.lines()[static_cast<std::size_t>(ordinal)].distinctId;
            const float score = fused[index];
            const auto lexical = static_cast<float>(bm25Scores[static_cast<std::size_t>(ordinal)]);
            if (have[id] == 0)
            {
                have[id] = 1;
                first[id] = ordinal;
                ids.push_back(id);
                bestScore[id] = score;
                bestLexical[id] = lexical;
                bestOrdinal[id] = ordinal;
            }
            else if (betterHit(score, ordinal, bestScore[id], bestOrdinal[id]))
            {
                bestScore[id] = score;
                bestLexical[id] = lexical;
                bestOrdinal[id] = ordinal;
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
        for (const std::uint32_t id : ids)
        {
            groups.push_back(Group{
                .ordinal = first[id], .score = bestScore[id], .dense = distinctScores[id], .lexical = bestLexical[id]});
        }
        std::ranges::sort(groups, [](const Group &left, const Group &right)
                          { return betterHit(left.score, left.ordinal, right.score, right.ordinal); });
        if (groups.size() > topK)
        {
            groups.resize(topK);
        }
        std::vector<RankedLine> ranked;
        ranked.reserve(groups.size());
        for (const Group &row : groups)
        {
            ranked.push_back({.ordinal = row.ordinal,
                              .score = row.score,
                              .dense = row.dense,
                              .lexical = row.lexical,
                              .hasDense = true,
                              .hasLexical = true});
        }
        return ranked;
    }
    std::vector<float> lineScores(corpus.lines().size(), 0.0F);
    std::vector<char> keep(corpus.lines().size(), 0);
    for (size_t index = 0; index < ordinals.size(); ++index)
    {
        lineScores[static_cast<size_t>(ordinals[index])] = fused[index];
        keep[static_cast<size_t>(ordinals[index])] = 1;
    }
    FusedLineRank view{.scores = &lineScores, .keep = &keep};
    const std::vector<nesso::core::Hit> hits =
        nesso::core::rankTop(corpus.lines().size(), topK, threads, fusedLineScore, fusedLineKeep, &view);
    std::vector<RankedLine> ranked;
    ranked.reserve(hits.size());
    for (const nesso::core::Hit &hit : hits)
    {
        const auto ordinal = static_cast<size_t>(hit.index);
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        ranked.push_back({.ordinal = hit.index,
                          .score = hit.score,
                          .dense = distinctScores[line.distinctId],
                          .lexical = static_cast<float>(bm25Scores[ordinal]),
                          .hasDense = true,
                          .hasLexical = true});
    }
    return ranked;
}

struct LexicalCandidate
{
    uint64_t ordinal = 0;
    float score = 0.0F;
    bool substring = false;
    std::string text;
};

static bool lexicalCandidateBefore(const LexicalCandidate &left, const LexicalCandidate &right)
{
    if (left.substring != right.substring)
    {
        return left.substring && !right.substring;
    }
    return betterHit(left.score, left.ordinal, right.score, right.ordinal);
}

static void rematerializeLexicalSubstrings(const nesso::core::MappedCorpus &corpus, std::string_view needle,
                                           std::vector<LexicalCandidate> &hits)
{
    for (LexicalCandidate &hit : hits)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[static_cast<std::size_t>(hit.ordinal)];
        std::string own;
        if (line.storedText)
        {
            own = std::string{*line.storedText};
        }
        else
        {
            const nesso::core::SourceInfo &source = corpus.sources()[line.sourceIndex];
            auto reread =
                nesso::parser::Chunker::readRecord(source.path, line.byteOffset, line.byteLength, line.lineNumber,
                                                   line.arrayElement, source.jsonField, MAX_LINE_LENGTH);
            if (reread)
            {
                own = std::move(*reread);
            }
            else
            {
                own = std::string{corpus.texts()[line.distinctId]};
            }
        }
        hit.substring = !needle.empty() && own.find(needle) != std::string::npos;
    }
    std::ranges::sort(hits, lexicalCandidateBefore);
}

static std::vector<LexicalCandidate> collapseLexicalGroups(const nesso::core::MappedCorpus &corpus,
                                                           const std::vector<LexicalCandidate> &hits)
{
    const std::size_t distinct = corpus.texts().size();
    std::vector<uint64_t> first(distinct, std::numeric_limits<uint64_t>::max());
    for (std::size_t ordinal = 0; ordinal < corpus.lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT ||
            first[line.distinctId] != std::numeric_limits<uint64_t>::max())
        {
            continue;
        }
        first[line.distinctId] = ordinal;
    }
    std::vector<char> taken(distinct, 0);
    std::vector<LexicalCandidate> groups;
    groups.reserve(hits.size());
    for (const LexicalCandidate &hit : hits)
    {
        const auto id = corpus.lines()[static_cast<std::size_t>(hit.ordinal)].distinctId;
        if (taken[id] != 0)
        {
            continue;
        }
        taken[id] = 1;
        LexicalCandidate row = hit;
        row.ordinal = first[id];
        groups.push_back(std::move(row));
    }
    std::ranges::sort(groups, lexicalCandidateBefore);
    return groups;
}

static std::vector<RankedLine> rankLexicalLines(const nesso::core::MappedCorpus &corpus,
                                                const std::vector<double> &bm25Scores, std::string_view needle,
                                                size_t topK, bool group)
{
    std::vector<LexicalCandidate> hits;
    for (size_t ordinal = 0; ordinal < corpus.lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT || !(bm25Scores[ordinal] > 0.0))
        {
            continue;
        }
        const std::string_view text = line.storedText.value_or(corpus.texts()[line.distinctId]);
        hits.push_back({.ordinal = ordinal,
                        .score = static_cast<float>(bm25Scores[ordinal]),
                        .substring = !needle.empty() && text.find(needle) != std::string_view::npos,
                        .text = std::string{text}});
    }
    std::ranges::sort(hits, lexicalCandidateBefore);
    if (group)
    {
        rematerializeLexicalSubstrings(corpus, needle, hits);
        hits = collapseLexicalGroups(corpus, hits);
    }
    if (hits.size() > topK)
    {
        hits.resize(topK);
    }
    std::vector<RankedLine> ranked;
    ranked.reserve(hits.size());
    for (const LexicalCandidate &hit : hits)
    {
        ranked.push_back(
            {.ordinal = hit.ordinal, .score = hit.score, .lexical = hit.score, .hasDense = false, .hasLexical = true});
    }
    return ranked;
}

static std::expected<nesso::core::MappedCorpus, Error> mapSearchCorpus(const std::filesystem::path &path)
{
    std::uint32_t version = 0;
    auto corpus = [&path, &version]
    {
        const ScopedStage stage{"corpus-read"};
        return nesso::core::mapCorpusFile(path, &version);
    }();
    if (!corpus)
    {
        Error error = makeError(ErrorKind::CorpusRead, causeOf(corpus.error()), path, {});
        if (corpus.error() == nesso::core::EngineError::UnsupportedVersion)
        {
            error.corpusVersion = version;
        }
        return std::unexpected(std::move(error));
    }
    if (corpus->texts().empty())
    {
        return std::unexpected(makeError(ErrorKind::EmptyInput, ErrorCause::EmptyCorpus, path, {}));
    }
    return std::move(*corpus);
}

static std::expected<void, Error> refuseModelMismatch(const nesso::core::CorpusPolicy &stored,
                                                      const nesso::core::CorpusPolicy &current,
                                                      const std::filesystem::path &path)
{
    constexpr std::array<std::string_view, 5> identityKeys{"model.id", "model.sha256", "vocab.sha256",
                                                           "model.quantization", "dimensions"};
    for (const std::string_view key : identityKeys)
    {
        if (stored.find(key)->second != current.find(key)->second)
        {
            return std::unexpected(makeError(ErrorKind::CorpusRead, ErrorCause::ModelMismatch, path, {}));
        }
    }
    return {};
}

struct SearchLineMeta
{
    std::vector<std::uint32_t> occurrences;
    bool multipleSources = false;
};

static SearchLineMeta tallySearchLineMeta(const nesso::core::MappedCorpus &corpus)
{
    SearchLineMeta meta{.occurrences = std::vector<std::uint32_t>(corpus.texts().size(), 0)};
    std::optional<std::string_view> firstSource;
    for (size_t ordinal = 0; ordinal < corpus.lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT)
        {
            continue;
        }
        ++meta.occurrences[line.distinctId];
        const std::string &source = corpus.sources()[line.sourceIndex].path;
        if (!firstSource)
        {
            firstSource = source;
        }
        else if (*firstSource != source)
        {
            meta.multipleSources = true;
        }
    }
    return meta;
}

static TextResults materializeSearchMatches(const nesso::core::MappedCorpus &corpus,
                                            const std::vector<RankedLine> &ranked,
                                            const std::vector<std::uint32_t> &occurrences, bool multipleSources,
                                            bool policyMismatch, bool lexicalFallback, bool group,
                                            std::string_view modeLabel, std::vector<StaleSource> sourceWarnings)
{
    const auto sourceIsStale = [&](const std::string &path)
    {
        return std::ranges::any_of(sourceWarnings,
                                   [&path](const StaleSource &warning) { return warning.path == path; });
    };

    TextResults results{};
    results.grouped = group;
    results.multipleSources = multipleSources;
    results.policyMismatch = policyMismatch;
    results.lexicalFallback = lexicalFallback;
    results.matches.reserve(ranked.size());
    for (const RankedLine &hit : ranked)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[static_cast<size_t>(hit.ordinal)];
        const nesso::core::SourceInfo &source = corpus.sources()[line.sourceIndex];
        std::string text{line.storedText.value_or(corpus.texts()[line.distinctId])};
        if (!line.storedText && !sourceIsStale(source.path))
        {
            auto reread =
                nesso::parser::Chunker::readRecord(source.path, line.byteOffset, line.byteLength, line.lineNumber,
                                                   line.arrayElement, source.jsonField, MAX_LINE_LENGTH);
            if (!reread)
            {
                addSourceWarning(sourceWarnings, {.path = source.path, .unreadable = true});
            }
            else
            {
                const auto after = nesso::core::sourceInfoFor(source.path, source.jsonField);
                if (!after || !sameSource(source, *after))
                {
                    addSourceWarning(sourceWarnings, {.path = source.path, .missing = !after});
                }
                else
                {
                    text = std::move(*reread);
                }
            }
        }
        TextMatch match{.source = source.path,
                        .line = line.lineNumber,
                        .score = hit.score,
                        .text = std::move(text),
                        .arrayElement = line.arrayElement,
                        .occurrences = occurrences[line.distinctId],
                        .mode = std::string{modeLabel},
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
    for (size_t index = 0; index < ranked.size(); ++index)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[static_cast<size_t>(ranked[index].ordinal)];
        if (!line.storedText && sourceIsStale(corpus.sources()[line.sourceIndex].path))
        {
            results.matches[index].text = corpus.texts()[line.distinctId];
        }
    }
    results.staleSources = std::move(sourceWarnings);
    return results;
}

std::expected<TextResults, Error> Nesso::search(const SearchRequest &request) const
{
    const TraceSession session;
    auto corpus = mapSearchCorpus(request.index);
    if (!corpus)
    {
        return std::unexpected(std::move(corpus.error()));
    }

    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }
    const auto current = MakeCorpusPolicy(**model, {});
    if (!current)
    {
        return std::unexpected(current.error());
    }
    if (const auto identity = refuseModelMismatch(corpus->policy(), *current, request.index); !identity)
    {
        return std::unexpected(identity.error());
    }
    constexpr std::array<std::string_view, 5> policyKeys{"batch", "truncate", "dedup", "mask", "tokens.max"};
    const bool policyMismatch =
        std::ranges::any_of(policyKeys, [&](std::string_view key)
                            { return corpus->policy().find(key)->second != current->find(key)->second; });
    auto sourceWarnings = staleSources(corpus->sources());
    const auto fail = [&](ErrorKind kind, ErrorCause cause)
    {
        Error error = makeError(kind, cause, {}, {});
        error.policyMismatch = policyMismatch;
        error.staleSources = sourceWarnings;
        return std::unexpected(std::move(error));
    };

    if (const auto lexicalReady =
            requireLexicalReady(corpus->GetLexicalStatus(), corpus->corpusLexicalTermsVersion(), request.mode);
        !lexicalReady)
    {
        Error error = lexicalReady.error();
        error.policyMismatch = policyMismatch;
        error.staleSources = sourceWarnings;
        return std::unexpected(std::move(error));
    }

    const nesso::core::QueryTokens queryTokens = nesso::core::tokenizeQuery(request.query);
    EffectiveMode effective = resolveEffectiveMode(request.mode, nesso::core::classifyQuery(request.query));
    bool lexicalFallback = false;

    const ScopedStage stage{"rank"};
    const auto queryEmbedding = (*model)->embed(request.query);
    if (!queryEmbedding)
    {
        return fail(ErrorKind::QueryEmbed, causeOf(queryEmbedding.error()));
    }
    const size_t dimensions = corpus->dimensions();
    if (queryEmbedding->size() != dimensions)
    {
        return fail(ErrorKind::Search, ErrorCause::MismatchedDimensions);
    }
    const size_t threads = scoringThreads(config_);
    std::vector<float> scores(corpus->texts().size());
    CorpusScore scored;
    scored.query = *queryEmbedding;
    scored.matrix = corpus->matrix();
    scored.dimensions = dimensions;
    scored.scores = scores.data();
    nesso::core::detail::parallelRows(threads, scores.size(), scoreCorpusRows, &scored);
    if (scored.dotError)
    {
        return fail(ErrorKind::Search, causeOf(*scored.dotError));
    }
    if (scored.nonFinite)
    {
        return fail(ErrorKind::Search, ErrorCause::InvalidInput);
    }

    const SearchLineMeta lineMeta = tallySearchLineMeta(*corpus);

    std::vector<double> bm25Scores;
    if (effective != EffectiveMode::Dense)
    {
        const std::vector<std::string> queryTerms = nesso::core::lexicalTerms(queryTokens.stripped);
        bm25Scores = bm25ScoresFromCorpus(*corpus, queryTermViews(queryTerms));
    }

    if (effective == EffectiveMode::Lexical)
    {
        const bool anyHit = std::ranges::any_of(bm25Scores, [](double score) { return score > 0.0; });
        if (!anyHit)
        {
            effective = EffectiveMode::Hybrid;
            lexicalFallback = true;
        }
    }

    std::vector<RankedLine> ranked;
    if (effective == EffectiveMode::Dense)
    {
        ranked = rankDenseLines(*corpus, scores, request.topK, threads, request.group);
    }
    else if (effective == EffectiveMode::Lexical)
    {
        ranked = rankLexicalLines(*corpus, bm25Scores, queryTokens.stripped, request.topK, request.group);
    }
    else
    {
        ranked = rankHybridLines(*corpus, scores, bm25Scores, request.topK, threads, request.group);
    }
    if (ranked.empty())
    {
        return fail(ErrorKind::NoMatches, ErrorCause::None);
    }

    return materializeSearchMatches(*corpus, ranked, lineMeta.occurrences, lineMeta.multipleSources, policyMismatch,
                                    lexicalFallback, request.group, effectiveModeName(effective),
                                    std::move(sourceWarnings));
}

} // namespace nesso
