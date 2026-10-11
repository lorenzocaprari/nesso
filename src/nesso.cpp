// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <nesso.hpp>

#include <error_map.hpp>
#include <model_paths.hpp>
#include <trace.hpp>

#include <core/corpus_index.hpp>
#include <core/top_k.hpp>
#include <embed/onnx_embedder.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <unistd.h>
#include <utility>

namespace nesso
{

static constexpr size_t MAX_LINE_LENGTH = 4096;

class ProgressReporter
{
  public:
    explicit ProgressReporter(bool requested)
        : enabled_(requested && ::isatty(STDERR_FILENO) != 0), started_(std::chrono::steady_clock::now())
    {
    }

    void update(size_t completed, size_t total)
    {
        if (!enabled_)
        {
            return;
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started_;
        const double linesPerSecond = elapsed.count() > 0.0 ? static_cast<double>(completed) / elapsed.count() : 0.0;
        std::print(stderr, "\rEmbedded {}/{} lines ({:.0f} lines/s)", completed, total, linesPerSecond);
        (void)std::fflush(stderr);
    }

    void finish() const
    {
        if (enabled_)
        {
            std::println(stderr);
        }
    }

  private:
    bool enabled_;
    std::chrono::steady_clock::time_point started_;
};

ErrorCause causeOf(nesso::parser::ParseError error)
{
    switch (error)
    {
    case nesso::parser::ParseError::FileOpenFailure:
        return ErrorCause::FileOpen;
    case nesso::parser::ParseError::UnsupportedFormat:
        return ErrorCause::UnsupportedFormat;
    case nesso::parser::ParseError::BinaryFile:
        return ErrorCause::BinaryFile;
    }
    std::unreachable();
}

ErrorCause causeOf(nesso::embed::EmbedError error)
{
    switch (error)
    {
    case nesso::embed::EmbedError::VocabLoadFailure:
        return ErrorCause::VocabLoad;
    case nesso::embed::EmbedError::TokenizationFailure:
        return ErrorCause::Tokenization;
    case nesso::embed::EmbedError::ModelLoadFailure:
        return ErrorCause::ModelLoad;
    case nesso::embed::EmbedError::InferenceFailure:
        return ErrorCause::Inference;
    case nesso::embed::EmbedError::InvalidInput:
        return ErrorCause::InvalidInput;
    }
    std::unreachable();
}

ErrorCause causeOf(nesso::core::EngineError error)
{
    switch (error)
    {
    case nesso::core::EngineError::FileOpenFailure:
        return ErrorCause::FileOpen;
    case nesso::core::EngineError::MismatchedDimensions:
        return ErrorCause::MismatchedDimensions;
    case nesso::core::EngineError::DatabaseNotInitialized:
        return ErrorCause::EmptyEmbedding;
    case nesso::core::EngineError::CorruptDatabase:
        return ErrorCause::CorruptFile;
    }
    std::unreachable();
}

std::string_view describe(ErrorCause cause)
{
    switch (cause)
    {
    case ErrorCause::None:
        return {};
    case ErrorCause::FileOpen:
        return "could not open the file";
    case ErrorCause::UnsupportedFormat:
        return "unsupported format";
    case ErrorCause::BinaryFile:
        return "the file is binary";
    case ErrorCause::VocabLoad:
        return "could not load the vocabulary";
    case ErrorCause::Tokenization:
        return "could not tokenize the text";
    case ErrorCause::ModelLoad:
        return "could not load the model";
    case ErrorCause::Inference:
        return "inference failed";
    case ErrorCause::InvalidInput:
        return "the input is invalid";
    case ErrorCause::MismatchedDimensions:
        return "the embedding width does not match";
    case ErrorCause::EmptyEmbedding:
        return "the embedding is empty";
    case ErrorCause::CorruptFile:
        return "the file is corrupt";
    case ErrorCause::EmptyFile:
        return "the file is empty";
    case ErrorCause::NothingIndexable:
        return "no indexable lines";
    case ErrorCause::EmptyCorpus:
        return "the corpus is empty";
    }
    std::unreachable();
}

std::string message(const Error &error)
{
    const std::string_view reason = describe(error.cause);
    const std::string path = error.path.string();
    switch (error.kind)
    {
    case ErrorKind::EmbedderLoad:
    {
        std::string text = std::format("Error: failed to load the embedder from '{}': {}", path, reason);
        if (!error.note.empty())
        {
            text += ". ";
            text += error.note;
        }
        return text;
    }
    case ErrorKind::Parse:
        return std::format("Error: failed to parse '{}': {}", path, reason);
    case ErrorKind::Embed:
        return std::format("Error: failed to embed the input: {}", reason);
    case ErrorKind::QueryEmbed:
        return std::format("Error: failed to embed the query: {}", reason);
    case ErrorKind::IndexBuild:
        return std::format("Error: failed to build the index: {}", reason);
    case ErrorKind::Search:
        return std::format("Error: failed to search: {}", reason);
    case ErrorKind::CorpusRead:
        return std::format("Error: failed to read the corpus '{}': {}", path, reason);
    case ErrorKind::CorpusWrite:
        return std::format("Error: failed to write the corpus '{}': {}", path, reason);
    case ErrorKind::CorpusLoad:
        return std::format("Error: failed to load the corpus: {}", reason);
    case ErrorKind::EmptyInput:
        if (path.empty())
        {
            return std::format("nothing to search: {}", reason);
        }
        return std::format("nothing to search in '{}': {}", path, reason);
    case ErrorKind::NoMatches:
        return {};
    }
    std::unreachable();
}

struct LineCounts
{
    size_t skipped = 0;
    size_t emptyLines = 0;
    size_t malformedLines = 0;
    size_t missingFieldLines = 0;
    size_t truncated = 0;
    std::string jsonField = "message";
};

static LineCounts lineCounts(const nesso::parser::ParseStats &stats, std::string jsonField)
{
    return {.skipped = stats.skippedLines,
            .emptyLines = stats.emptyLines,
            .malformedLines = stats.malformedLines,
            .missingFieldLines = stats.missingFieldLines,
            .truncated = stats.truncatedLines,
            .jsonField = std::move(jsonField)};
}

static std::vector<StaleSource> staleSources(std::span<const nesso::core::SourceInfo> sources)
{
    std::vector<StaleSource> stale;
    for (const nesso::core::SourceInfo &source : sources)
    {
        if (!source.tracked || source.path == "-")
        {
            continue;
        }

        std::error_code error;
        const std::uint64_t size = std::filesystem::file_size(source.path, error);
        if (error)
        {
            stale.push_back({.path = source.path, .missing = true});
            continue;
        }
        const auto modified = std::filesystem::last_write_time(source.path, error);
        if (error)
        {
            stale.push_back({.path = source.path, .missing = true});
            continue;
        }
        const auto modifiedTime =
            std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
        if (size != source.size || modifiedTime != source.modifiedTime)
        {
            stale.push_back({.path = source.path, .missing = false});
        }
    }
    return stale;
}

static Error makeError(ErrorKind kind, ErrorCause cause, std::filesystem::path path, LineCounts counts)
{
    return Error{.kind = kind,
                 .cause = cause,
                 .path = std::move(path),
                 .skippedLines = counts.skipped,
                 .emptyLines = counts.emptyLines,
                 .malformedLines = counts.malformedLines,
                 .missingFieldLines = counts.missingFieldLines,
                 .truncatedLines = counts.truncated,
                 .jsonField = std::move(counts.jsonField),
                 .note = {},
                 .staleSources = {}};
}

static std::string modelLoadNote(const std::vector<std::filesystem::path> &tried)
{
    std::string text = "Tried";
    for (size_t index = 0; index < tried.size(); ++index)
    {
        if (index == 0)
        {
            text += ' ';
        }
        else if (index + 1 == tried.size())
        {
            text += " and ";
        }
        else
        {
            text += ", ";
        }
        text += std::format("'{}'", tried[index].string());
    }
    text += ". Run scripts/fetch-model or pass --model-dir.";
    return text;
}

struct Scan
{
    size_t chunks = 0;
    LineCounts counts{};
    // Stdin cannot be rewound. The count pass keeps its chunks so embed can stream them.
    std::vector<nesso::parser::ParsedChunk> stdinChunks;
};

static std::expected<Scan, Error> scanFiles(std::span<const std::filesystem::path> files, std::string_view jsonField)
{
    const ScopedStage stage{"parse"};
    nesso::parser::ParseStats stats{};
    Scan scan{.chunks = 0, .counts = {}, .stdinChunks = {}};
    bool capturedStdin = false;
    for (const std::filesystem::path &path : files)
    {
        if (path == "-")
        {
            if (capturedStdin)
            {
                continue;
            }
            capturedStdin = true;
            const auto streamed = nesso::parser::Chunker::forEach(path, MAX_LINE_LENGTH, &stats, jsonField,
                                                                  [&scan](nesso::parser::ParsedChunk chunk)
                                                                  {
                                                                      ++scan.chunks;
                                                                      scan.stdinChunks.push_back(std::move(chunk));
                                                                  });
            if (!streamed)
            {
                return std::unexpected(makeError(ErrorKind::Parse, causeOf(streamed.error()), path,
                                                 lineCounts(stats, std::string{jsonField})));
            }
            continue;
        }

        const auto streamed = nesso::parser::Chunker::forEach(path, MAX_LINE_LENGTH, &stats, jsonField,
                                                              [&scan](nesso::parser::ParsedChunk) { ++scan.chunks; });
        if (!streamed)
        {
            return std::unexpected(makeError(ErrorKind::Parse, causeOf(streamed.error()), path,
                                             lineCounts(stats, std::string{jsonField})));
        }
    }
    scan.counts = lineCounts(stats, std::string{jsonField});
    return scan;
}

// Files are parsed again so embeddings stay batched. Stdin is replayed from the count pass.
template <typename Consume>
static std::expected<void, nesso::parser::ParseError> visitChunks(const std::filesystem::path &path, Scan &scan,
                                                                  bool &stdinUsed, std::string_view jsonField,
                                                                  const Consume &consume)
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
    return nesso::parser::Chunker::forEach(path, MAX_LINE_LENGTH, nullptr, jsonField, consume);
}

template <typename E> static std::unexpected<Error> textFailure(ErrorKind kind, E cause, const LineCounts &counts)
{
    return std::unexpected(makeError(kind, causeOf(cause), {}, counts));
}

static std::unexpected<Error> noMatch(ErrorKind kind, const LineCounts &counts)
{
    return std::unexpected(makeError(kind, ErrorCause::None, {}, counts));
}

static bool multipleSources(std::span<const nesso::core::CorpusChunk> chunks)
{
    if (chunks.empty())
    {
        return false;
    }
    const std::string &first = chunks.front().chunk.source;
    return std::ranges::any_of(chunks,
                               [&first](const nesso::core::CorpusChunk &chunk) { return chunk.chunk.source != first; });
}

static std::expected<std::vector<float>, Error> packMatrix(std::span<const nesso::core::CorpusChunk> chunks,
                                                           const LineCounts &counts, ErrorKind failure)
{
    const size_t dims = chunks.front().embedding.size();
    if (dims == 0)
    {
        return textFailure(failure, nesso::core::EngineError::DatabaseNotInitialized, counts);
    }

    std::vector<float> matrix;
    matrix.reserve(chunks.size() * dims);
    for (const nesso::core::CorpusChunk &chunk : chunks)
    {
        if (chunk.embedding.size() != dims)
        {
            return textFailure(failure, nesso::core::EngineError::MismatchedDimensions, counts);
        }
        matrix.insert(matrix.end(), chunk.embedding.begin(), chunk.embedding.end());
    }
    return matrix;
}

static std::expected<TextResults, Error> rankChunks(std::span<const nesso::core::CorpusChunk> chunks,
                                                    const nesso::embed::OnnxEmbedder &embedder, std::string_view query,
                                                    size_t topK, const LineCounts &counts, ErrorKind packFailure,
                                                    std::span<const float> packed)
{
    const ScopedStage stage{"rank"};
    std::vector<float> owned;
    std::span<const float> matrix = packed;
    size_t dims = 0;
    if (matrix.empty())
    {
        auto built = packMatrix(chunks, counts, packFailure);
        if (!built)
        {
            return std::unexpected(built.error());
        }
        owned = std::move(*built);
        matrix = owned;
        dims = chunks.front().embedding.size();
    }
    else
    {
        dims = matrix.size() / chunks.size();
    }

    const auto queryEmbedding = embedder.embed(query);
    if (!queryEmbedding)
    {
        return textFailure(ErrorKind::QueryEmbed, queryEmbedding.error(), counts);
    }

    const auto ranked = nesso::core::topK(*queryEmbedding, matrix, dims, topK);
    if (!ranked)
    {
        return textFailure(ErrorKind::Search, ranked.error(), counts);
    }
    if (ranked->empty())
    {
        return noMatch(ErrorKind::NoMatches, counts);
    }

    TextResults results{.matches = {},
                        .skippedLines = counts.skipped,
                        .emptyLines = counts.emptyLines,
                        .malformedLines = counts.malformedLines,
                        .missingFieldLines = counts.missingFieldLines,
                        .truncatedLines = counts.truncated,
                        .jsonField = counts.jsonField,
                        .multipleSources = multipleSources(chunks),
                        .staleSources = {}};
    results.matches.reserve(ranked->size());
    for (const nesso::core::Hit &hit : *ranked)
    {
        const nesso::core::LogChunk &chunk = chunks[static_cast<size_t>(hit.index)].chunk;
        results.matches.push_back({.source = chunk.source,
                                   .line = chunk.lineNumber,
                                   .score = hit.score,
                                   .text = chunk.text,
                                   .arrayElement = chunk.arrayElement});
    }
    return results;
}

Nesso::Nesso(Config config) : config_(std::move(config)) {}

Nesso::~Nesso() = default;

Nesso::Nesso(Nesso &&) noexcept = default;

Nesso &Nesso::operator=(Nesso &&) noexcept = default;

std::expected<const nesso::embed::OnnxEmbedder *, Error> Nesso::embedder() const
{
    if (!embedder_)
    {
        std::vector<std::filesystem::path> tried;
        std::filesystem::path modelDir;
        if (config_.modelDir.has_value())
        {
            modelDir = *config_.modelDir;
            tried.push_back(modelDir);
        }
        else
        {
            tried = modelDirCandidates();
            modelDir = resolveDefaultModelDir();
        }
        auto created = nesso::embed::OnnxEmbedder::create(modelDir);
        if (!created)
        {
            Error error{.kind = ErrorKind::EmbedderLoad,
                        .cause = causeOf(created.error()),
                        .path = modelDir,
                        .note = {},
                        .staleSources = {}};
            error.note = modelLoadNote(tried);
            return std::unexpected(std::move(error));
        }
        embedder_ = std::move(*created);
    }
    return embedder_.get();
}

// Matches OnnxEmbedder's inference window so only one batch of vectors is live.
static constexpr size_t STREAM_BATCH = 32;

struct PendingChunk
{
    nesso::parser::ParsedChunk chunk;
    std::string source;
};

static std::expected<std::vector<std::vector<float>>, Error> embedPending(std::span<const PendingChunk> pending,
                                                                          const nesso::embed::OnnxEmbedder &embedder,
                                                                          ProgressReporter &progress, size_t &completed,
                                                                          size_t total, const LineCounts &counts)
{
    std::vector<std::string> texts;
    texts.reserve(pending.size());
    for (const PendingChunk &item : pending)
    {
        texts.push_back(item.chunk.text);
    }
    const auto embeddings = embedder.embedBatch(texts, [&progress, &completed, total](size_t batchCompleted, size_t)
                                                { progress.update(completed + batchCompleted, total); });
    if (!embeddings)
    {
        return std::unexpected(makeError(ErrorKind::Embed, causeOf(embeddings.error()), {}, counts));
    }
    completed += pending.size();
    progress.update(completed, total);
    return *embeddings;
}

std::expected<TextResults, Error> Nesso::grep(const GrepRequest &request) const
{
    const TraceSession session;
    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto scanned = scanFiles(request.files, request.jsonField);
    if (!scanned)
    {
        return std::unexpected(scanned.error());
    }
    if (scanned->chunks == 0)
    {
        const ErrorCause cause = scanned->counts.skipped == 0 ? ErrorCause::EmptyFile : ErrorCause::NothingIndexable;
        const std::filesystem::path path = request.files.size() == 1 ? request.files.front() : std::filesystem::path{};
        return std::unexpected(makeError(ErrorKind::EmptyInput, cause, path, scanned->counts));
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

    struct Candidate
    {
        uint64_t index = 0;
        float score = 0.0F;
        TextMatch match;
    };
    const auto better = [](const Candidate &left, const Candidate &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };

    std::vector<Candidate> best;
    std::vector<PendingChunk> pending;
    pending.reserve(STREAM_BATCH);
    ProgressReporter progress(request.progress);
    size_t completed = 0;
    uint64_t ordinal = 0;
    bool severalSources = false;
    std::string firstSource;
    const LineCounts &counts = scanned->counts;

    const auto flush = [&]() -> std::expected<void, Error>
    {
        if (pending.empty())
        {
            return {};
        }
        const auto embeddings = embedPending(pending, **model, progress, completed, scanned->chunks, counts);
        if (!embeddings)
        {
            return std::unexpected(embeddings.error());
        }
        if (embeddings->size() != pending.size())
        {
            return textFailure(ErrorKind::Embed, nesso::core::EngineError::MismatchedDimensions, counts);
        }
        for (size_t index = 0; index < pending.size(); ++index)
        {
            const std::vector<float> &embedding = (*embeddings)[index];
            const auto score = nesso::core::math::DistanceMetrics::dotProduct<float>(*queryEmbedding, embedding);
            if (!score)
            {
                return textFailure(ErrorKind::Search, score.error(), counts);
            }
            const PendingChunk &item = pending[index];
            best.push_back(Candidate{.index = ordinal,
                                     .score = *score,
                                     .match = {.source = item.source,
                                               .line = item.chunk.lineNumber,
                                               .score = *score,
                                               .text = item.chunk.text,
                                               .arrayElement = item.chunk.arrayElement}});
            ++ordinal;
        }
        if (best.size() > request.topK)
        {
            std::partial_sort(best.begin(), best.begin() + static_cast<std::ptrdiff_t>(request.topK), best.end(),
                              better);
            best.resize(request.topK);
        }
        pending.clear();
        return {};
    };

    {
        const ScopedStage stage{"embed"};
        std::optional<Error> failed;
        bool stdinUsed = false;
        for (const std::filesystem::path &path : request.files)
        {
            const std::string source = path.string();
            const auto streamed =
                visitChunks(path, *scanned, stdinUsed, request.jsonField,
                            [&](nesso::parser::ParsedChunk chunk)
                            {
                                if (failed)
                                {
                                    return;
                                }
                                if (firstSource.empty())
                                {
                                    firstSource = source;
                                }
                                else if (source != firstSource)
                                {
                                    severalSources = true;
                                }
                                pending.push_back(PendingChunk{.chunk = std::move(chunk), .source = source});
                                if (pending.size() == STREAM_BATCH)
                                {
                                    if (const auto flushed = flush(); !flushed)
                                    {
                                        failed = flushed.error();
                                    }
                                }
                            });
            if (!streamed)
            {
                return std::unexpected(makeError(ErrorKind::Parse, causeOf(streamed.error()), path, counts));
            }
            if (failed)
            {
                return std::unexpected(*failed);
            }
        }
        if (const auto flushed = flush(); !flushed)
        {
            return std::unexpected(flushed.error());
        }
    }
    progress.finish();
    std::ranges::sort(best, better);

    TextResults results{.matches = {},
                        .skippedLines = counts.skipped,
                        .emptyLines = counts.emptyLines,
                        .malformedLines = counts.malformedLines,
                        .missingFieldLines = counts.missingFieldLines,
                        .truncatedLines = counts.truncated,
                        .jsonField = counts.jsonField,
                        .multipleSources = severalSources,
                        .staleSources = {}};
    results.matches.reserve(best.size());
    for (const Candidate &candidate : best)
    {
        results.matches.push_back(candidate.match);
    }
    return results;
}

std::expected<IndexSummary, Error> Nesso::index(const IndexRequest &request) const
{
    const TraceSession session;
    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto scanned = scanFiles(request.files, request.jsonField);
    if (!scanned)
    {
        return std::unexpected(scanned.error());
    }

    auto writer = nesso::core::CorpusWriter::create(request.output, nesso::embed::MINILM_MODEL_ID);
    if (!writer)
    {
        return std::unexpected(
            makeError(ErrorKind::CorpusWrite, causeOf(writer.error()), request.output, scanned->counts));
    }

    std::vector<PendingChunk> pending;
    pending.reserve(STREAM_BATCH);
    ProgressReporter progress(request.progress);
    size_t completed = 0;
    const LineCounts &counts = scanned->counts;
    const auto flush = [&]() -> std::expected<void, Error>
    {
        if (pending.empty())
        {
            return {};
        }
        const auto embeddings = embedPending(pending, **model, progress, completed, scanned->chunks, counts);
        if (!embeddings)
        {
            return std::unexpected(embeddings.error());
        }
        if (embeddings->size() != pending.size())
        {
            return textFailure(ErrorKind::Embed, nesso::core::EngineError::MismatchedDimensions, counts);
        }
        std::vector<nesso::core::CorpusChunk> batch;
        batch.reserve(pending.size());
        for (size_t index = 0; index < pending.size(); ++index)
        {
            const PendingChunk &item = pending[index];
            batch.push_back(nesso::core::CorpusChunk{.chunk = {.text = item.chunk.text,
                                                               .lineNumber = item.chunk.lineNumber,
                                                               .source = item.source,
                                                               .byteOffset = item.chunk.byteOffset,
                                                               .arrayElement = item.chunk.arrayElement},
                                                     .embedding = (*embeddings)[index]});
        }
        if (const auto added = writer->add(batch); !added)
        {
            return std::unexpected(makeError(ErrorKind::CorpusWrite, causeOf(added.error()), request.output, counts));
        }
        pending.clear();
        return {};
    };

    {
        const ScopedStage stage{"embed"};
        std::optional<Error> failed;
        bool stdinUsed = false;
        for (const std::filesystem::path &path : request.files)
        {
            const std::string source = path.string();
            const auto streamed =
                visitChunks(path, *scanned, stdinUsed, request.jsonField,
                            [&](nesso::parser::ParsedChunk chunk)
                            {
                                if (failed)
                                {
                                    return;
                                }
                                pending.push_back(PendingChunk{.chunk = std::move(chunk), .source = source});
                                if (pending.size() == STREAM_BATCH)
                                {
                                    if (const auto flushed = flush(); !flushed)
                                    {
                                        failed = flushed.error();
                                    }
                                }
                            });
            if (!streamed)
            {
                return std::unexpected(makeError(ErrorKind::Parse, causeOf(streamed.error()), path, counts));
            }
            if (failed)
            {
                return std::unexpected(*failed);
            }
        }
        if (const auto flushed = flush(); !flushed)
        {
            return std::unexpected(flushed.error());
        }
    }
    progress.finish();

    const auto written = [&writer]
    {
        const ScopedStage stage{"corpus-write"};
        return writer->finish();
    }();
    if (!written)
    {
        return std::unexpected(makeError(ErrorKind::CorpusWrite, causeOf(written.error()), request.output, counts));
    }
    return IndexSummary{.chunks = scanned->chunks,
                        .skippedLines = counts.skipped,
                        .emptyLines = counts.emptyLines,
                        .malformedLines = counts.malformedLines,
                        .missingFieldLines = counts.missingFieldLines,
                        .truncatedLines = counts.truncated,
                        .jsonField = counts.jsonField};
}

std::expected<TextResults, Error> Nesso::search(const SearchRequest &request) const
{
    const TraceSession session;
    const auto chunks = [&request]
    {
        const ScopedStage stage{"corpus-read"};
        return nesso::core::mapCorpusFile(request.index);
    }();
    if (!chunks)
    {
        return std::unexpected(Error{.kind = ErrorKind::CorpusRead,
                                     .cause = causeOf(chunks.error()),
                                     .path = request.index,
                                     .note = {},
                                     .staleSources = {}});
    }
    if (chunks->chunks().empty())
    {
        return std::unexpected(Error{.kind = ErrorKind::EmptyInput,
                                     .cause = ErrorCause::EmptyCorpus,
                                     .path = request.index,
                                     .note = {},
                                     .staleSources = {}});
    }

    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto result =
        rankChunks(chunks->chunks(), **model, request.query, request.topK, {}, ErrorKind::CorpusLoad, chunks->matrix());
    const auto sourceWarnings = staleSources(chunks->sources());
    if (result)
    {
        result->staleSources = sourceWarnings;
    }
    else
    {
        result.error().staleSources = sourceWarnings;
    }
    return result;
}

} // namespace nesso
