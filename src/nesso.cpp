// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "nesso.hpp"

#include "error_map.hpp"
#include "model_paths.hpp"
#include "trace.hpp"

#include <core/corpus_index.hpp>
#include <core/top_k.hpp>
#include <embed/onnx_embedder.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <utility>

namespace nesso
{

static constexpr size_t MAX_LINE_LENGTH = 4096;

ErrorCause causeOf(parser::ParseError error)
{
    switch (error)
    {
    case parser::ParseError::FileOpenFailure:
        return ErrorCause::FileOpen;
    case parser::ParseError::UnsupportedFormat:
        return ErrorCause::UnsupportedFormat;
    }
    std::unreachable();
}

ErrorCause causeOf(embed::EmbedError error)
{
    switch (error)
    {
    case embed::EmbedError::VocabLoadFailure:
        return ErrorCause::VocabLoad;
    case embed::EmbedError::TokenizationFailure:
        return ErrorCause::Tokenization;
    case embed::EmbedError::ModelLoadFailure:
        return ErrorCause::ModelLoad;
    case embed::EmbedError::InferenceFailure:
        return ErrorCause::Inference;
    case embed::EmbedError::InvalidInput:
        return ErrorCause::InvalidInput;
    }
    std::unreachable();
}

ErrorCause causeOf(core::EngineError error)
{
    switch (error)
    {
    case core::EngineError::FileOpenFailure:
        return ErrorCause::FileOpen;
    case core::EngineError::MismatchedDimensions:
        return ErrorCause::MismatchedDimensions;
    case core::EngineError::DatabaseNotInitialized:
        return ErrorCause::EmptyEmbedding;
    case core::EngineError::CorruptDatabase:
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
        return std::format("Error: failed to load the embedder from '{}': {}", path, reason);
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
    case ErrorKind::NoMatches:
        return {};
    }
    std::unreachable();
}

struct ParsedFiles
{
    std::vector<parser::ParsedChunk> chunks;
    std::vector<std::string> sources;
    size_t skippedLines = 0;
    size_t truncatedLines = 0;
};

struct LineCounts
{
    size_t skipped = 0;
    size_t truncated = 0;
};

static LineCounts lineCounts(const ParsedFiles &parsed)
{
    return {.skipped = parsed.skippedLines, .truncated = parsed.truncatedLines};
}

static LineCounts lineCounts(const parser::ParseStats &stats)
{
    return {.skipped = stats.skippedLines, .truncated = stats.truncatedLines};
}

static Error makeError(ErrorKind kind, ErrorCause cause, std::filesystem::path path, LineCounts counts)
{
    return Error{.kind = kind,
                 .cause = cause,
                 .path = std::move(path),
                 .skippedLines = counts.skipped,
                 .truncatedLines = counts.truncated};
}

static std::expected<ParsedFiles, Error> parseFiles(std::span<const std::filesystem::path> files,
                                                    std::string_view jsonField)
{
    const ScopedStage stage{"parse"};
    parser::ParseStats stats{};
    ParsedFiles parsed;
    for (const std::filesystem::path &path : files)
    {
        auto fileChunks = parser::Chunker::fromFile(path, MAX_LINE_LENGTH, &stats, jsonField);
        if (!fileChunks)
        {
            return std::unexpected(makeError(ErrorKind::Parse, causeOf(fileChunks.error()), path, lineCounts(stats)));
        }
        const std::string source = path.string();
        for (parser::ParsedChunk &chunk : *fileChunks)
        {
            parsed.chunks.push_back(std::move(chunk));
            parsed.sources.push_back(source);
        }
    }
    parsed.skippedLines = stats.skippedLines;
    parsed.truncatedLines = stats.truncatedLines;
    return parsed;
}

template <typename E> static std::unexpected<Error> textFailure(ErrorKind kind, E cause, LineCounts counts)
{
    return std::unexpected(makeError(kind, causeOf(cause), {}, counts));
}

static std::unexpected<Error> noMatch(ErrorKind kind, LineCounts counts)
{
    return std::unexpected(makeError(kind, ErrorCause::None, {}, counts));
}

static std::expected<std::vector<core::CorpusChunk>, Error>
embedFiles(const ParsedFiles &parsed, const embed::OnnxEmbedder &embedder, std::vector<float> *matrix)
{
    const ScopedStage stage{"embed"};
    if (parsed.chunks.empty())
    {
        return std::vector<core::CorpusChunk>{};
    }

    std::vector<std::string> texts;
    texts.reserve(parsed.chunks.size());
    for (const parser::ParsedChunk &chunk : parsed.chunks)
    {
        texts.push_back(chunk.text);
    }

    const auto embeddings = embedder.embedBatch(texts);
    if (!embeddings)
    {
        return std::unexpected(makeError(ErrorKind::Embed, causeOf(embeddings.error()), {}, lineCounts(parsed)));
    }

    size_t dims = 0;
    if (matrix != nullptr)
    {
        dims = embeddings->front().size();
        if (dims == 0)
        {
            return textFailure(ErrorKind::Embed, core::EngineError::DatabaseNotInitialized, lineCounts(parsed));
        }
        matrix->reserve(embeddings->size() * dims);
    }

    std::vector<core::CorpusChunk> chunks;
    chunks.reserve(embeddings->size());
    for (size_t index = 0; index < embeddings->size(); ++index)
    {
        core::CorpusChunk chunk{.chunk = {.text = parsed.chunks[index].text,
                                          .lineNumber = parsed.chunks[index].lineNumber,
                                          .source = parsed.sources[index],
                                          .byteOffset = parsed.chunks[index].byteOffset},
                                .embedding = {}};
        if (matrix != nullptr)
        {
            const std::vector<float> &embedding = (*embeddings)[index];
            if (embedding.size() != dims)
            {
                return textFailure(ErrorKind::Embed, core::EngineError::MismatchedDimensions, lineCounts(parsed));
            }
            matrix->insert(matrix->end(), embedding.begin(), embedding.end());
        }
        else
        {
            chunk.embedding = (*embeddings)[index];
        }
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

static bool multipleSources(std::span<const core::CorpusChunk> chunks)
{
    if (chunks.empty())
    {
        return false;
    }
    const std::string &first = chunks.front().chunk.source;
    return std::ranges::any_of(chunks,
                               [&first](const core::CorpusChunk &chunk) { return chunk.chunk.source != first; });
}

static std::expected<std::vector<float>, Error> packMatrix(std::span<const core::CorpusChunk> chunks, LineCounts counts,
                                                           ErrorKind failure)
{
    const size_t dims = chunks.front().embedding.size();
    if (dims == 0)
    {
        return textFailure(failure, core::EngineError::DatabaseNotInitialized, counts);
    }

    std::vector<float> matrix;
    matrix.reserve(chunks.size() * dims);
    for (const core::CorpusChunk &chunk : chunks)
    {
        if (chunk.embedding.size() != dims)
        {
            return textFailure(failure, core::EngineError::MismatchedDimensions, counts);
        }
        matrix.insert(matrix.end(), chunk.embedding.begin(), chunk.embedding.end());
    }
    return matrix;
}

static std::expected<TextResults, Error> rankChunks(std::span<const core::CorpusChunk> chunks,
                                                    const embed::OnnxEmbedder &embedder, std::string_view query,
                                                    size_t topK, LineCounts counts, ErrorKind packFailure,
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

    const auto ranked = core::topK(*queryEmbedding, matrix, dims, topK);
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
                        .truncatedLines = counts.truncated,
                        .multipleSources = multipleSources(chunks)};
    results.matches.reserve(ranked->size());
    for (const core::Hit &hit : *ranked)
    {
        const core::LogChunk &chunk = chunks[static_cast<size_t>(hit.index)].chunk;
        results.matches.push_back(
            {.source = chunk.source, .line = chunk.lineNumber, .score = hit.score, .text = chunk.text});
    }
    return results;
}

class Nesso::Impl
{
  public:
    explicit Impl(Config config) : config_(std::move(config)) {}

    // The model is loaded on first use.
    std::expected<const embed::OnnxEmbedder *, Error> embedder()
    {
        if (!embedder_)
        {
            const std::filesystem::path modelDir =
                config_.modelDir.has_value() ? *config_.modelDir : resolveDefaultModelDir();
            auto created = embed::OnnxEmbedder::create(modelDir);
            if (!created)
            {
                return std::unexpected(
                    Error{.kind = ErrorKind::EmbedderLoad, .cause = causeOf(created.error()), .path = modelDir});
            }
            embedder_ = std::move(*created);
        }
        return embedder_.get();
    }

  private:
    Config config_;
    std::unique_ptr<embed::OnnxEmbedder> embedder_;
};

Nesso::Nesso(Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}

Nesso::~Nesso() = default;

Nesso::Nesso(Nesso &&) noexcept = default;

Nesso &Nesso::operator=(Nesso &&) noexcept = default;

std::expected<TextResults, Error> Nesso::grep(const GrepRequest &request) const
{
    const TraceSession session;
    const auto embedder = impl_->embedder();
    if (!embedder)
    {
        return std::unexpected(embedder.error());
    }

    const auto parsed = parseFiles(request.files, request.jsonField);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }

    std::vector<float> matrix;
    const auto chunks = embedFiles(*parsed, **embedder, &matrix);
    if (!chunks)
    {
        return std::unexpected(chunks.error());
    }
    if (chunks->empty())
    {
        return noMatch(ErrorKind::EmptyInput, lineCounts(*parsed));
    }

    return rankChunks(*chunks, **embedder, request.query, request.topK, lineCounts(*parsed), ErrorKind::IndexBuild,
                      matrix);
}

std::expected<IndexSummary, Error> Nesso::index(const IndexRequest &request) const
{
    const TraceSession session;
    const auto embedder = impl_->embedder();
    if (!embedder)
    {
        return std::unexpected(embedder.error());
    }

    const auto parsed = parseFiles(request.files, request.jsonField);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }

    const auto chunks = embedFiles(*parsed, **embedder, nullptr);
    if (!chunks)
    {
        return std::unexpected(chunks.error());
    }

    const auto written = [&request, &chunks]
    {
        const ScopedStage stage{"corpus-write"};
        return core::writeCorpusFile(request.output, *chunks, embed::MINILM_MODEL_ID);
    }();
    if (!written)
    {
        return std::unexpected(
            makeError(ErrorKind::CorpusWrite, causeOf(written.error()), request.output, lineCounts(*parsed)));
    }
    return IndexSummary{
        .chunks = chunks->size(), .skippedLines = parsed->skippedLines, .truncatedLines = parsed->truncatedLines};
}

std::expected<TextResults, Error> Nesso::search(const SearchRequest &request) const
{
    const TraceSession session;
    const auto chunks = [&request]
    {
        const ScopedStage stage{"corpus-read"};
        return core::mapCorpusFile(request.index);
    }();
    if (!chunks)
    {
        return std::unexpected(
            Error{.kind = ErrorKind::CorpusRead, .cause = causeOf(chunks.error()), .path = request.index});
    }
    if (chunks->chunks().empty() || request.topK == 0)
    {
        return noMatch(ErrorKind::NoMatches, {});
    }

    const auto embedder = impl_->embedder();
    if (!embedder)
    {
        return std::unexpected(embedder.error());
    }

    return rankChunks(chunks->chunks(), **embedder, request.query, request.topK, {}, ErrorKind::CorpusLoad,
                      chunks->matrix());
}

} // namespace nesso
