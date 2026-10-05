// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include "nesso.hpp"

#include "model_paths.hpp"
#include "trace.hpp"

#include <core/corpus_index.hpp>
#include <core/embedding_store.hpp>
#include <core/storage_engine.hpp>
#include <core/vector_search.hpp>
#include <embed/onnx_embedder.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace nesso
{

static constexpr size_t MAX_LINE_LENGTH = 4096;

template <typename E> static int codeOf(E error) { return static_cast<int>(error); }

struct ParsedFiles
{
    std::vector<parser::ParsedChunk> chunks;
    std::vector<std::string> sources;
    size_t skippedLines = 0;
};

static std::expected<ParsedFiles, Error> parseFiles(std::span<const std::filesystem::path> files)
{
    ScopedStage stage{"parse"};
    parser::ParseStats stats{};
    ParsedFiles parsed;
    for (const std::filesystem::path &path : files)
    {
        auto fileChunks = parser::LogChunker::fromFile(path, MAX_LINE_LENGTH, &stats);
        if (!fileChunks)
        {
            return std::unexpected(Error{.kind = ErrorKind::Parse,
                                         .code = codeOf(fileChunks.error()),
                                         .path = path,
                                         .skippedLines = stats.skippedLines});
        }
        const std::string source = path.string();
        for (parser::ParsedChunk &chunk : *fileChunks)
        {
            parsed.chunks.push_back(std::move(chunk));
            parsed.sources.push_back(source);
        }
    }
    parsed.skippedLines = stats.skippedLines;
    return parsed;
}

static std::expected<std::vector<core::CorpusChunk>, Error> embedFiles(const ParsedFiles &parsed,
                                                                       const embed::OnnxEmbedder &embedder)
{
    ScopedStage stage{"embed"};
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
        return std::unexpected(Error{.kind = ErrorKind::Embed,
                                     .code = codeOf(embeddings.error()),
                                     .path = {},
                                     .skippedLines = parsed.skippedLines});
    }

    std::vector<core::CorpusChunk> chunks;
    chunks.reserve(embeddings->size());
    for (size_t index = 0; index < embeddings->size(); ++index)
    {
        chunks.push_back({.chunk = {.text = parsed.chunks[index].text,
                                    .lineNumber = parsed.chunks[index].lineNumber,
                                    .source = parsed.sources[index]},
                          .embedding = (*embeddings)[index]});
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

static std::unexpected<Error> textFailure(ErrorKind kind, int code, size_t skippedLines)
{
    return std::unexpected(Error{.kind = kind, .code = code, .path = {}, .skippedLines = skippedLines});
}

static std::expected<TextResults, Error> rankChunks(std::span<const core::CorpusChunk> chunks,
                                                    const embed::OnnxEmbedder &embedder, std::string_view query,
                                                    size_t topK, size_t skippedLines, ErrorKind insertFailure)
{
    ScopedStage stage{"rank"};
    core::EmbeddingStore<float> store;
    for (const core::CorpusChunk &chunk : chunks)
    {
        const auto inserted = store.insert(chunk.embedding, chunk.chunk);
        if (!inserted)
        {
            return textFailure(insertFailure, codeOf(inserted.error()), skippedLines);
        }
    }

    const auto queryEmbedding = embedder.embed(query);
    if (!queryEmbedding)
    {
        return textFailure(ErrorKind::QueryEmbed, codeOf(queryEmbedding.error()), skippedLines);
    }

    const auto ranked = store.searchTopK(*queryEmbedding, topK);
    if (!ranked)
    {
        return textFailure(ErrorKind::Search, codeOf(ranked.error()), skippedLines);
    }
    if (ranked->empty())
    {
        return textFailure(ErrorKind::NoMatches, 0, skippedLines);
    }

    TextResults results{.matches = {}, .skippedLines = skippedLines, .multipleSources = multipleSources(chunks)};
    results.matches.reserve(ranked->size());
    for (const core::EmbeddingSearchResult<float> &match : *ranked)
    {
        results.matches.push_back({.source = match.chunk.source,
                                   .line = match.chunk.lineNumber,
                                   .score = match.score,
                                   .text = match.chunk.text});
    }
    return results;
}

static std::expected<void, Error> openStore(core::StorageEngine<float> &engine, const std::filesystem::path &db,
                                            uint64_t dimensions, ErrorKind failure)
{
    const auto opened = engine.createOrOpen(db, dimensions);
    if (!opened)
    {
        return std::unexpected(Error{.kind = failure, .code = codeOf(opened.error()), .path = db});
    }
    return {};
}

class Nesso::Impl
{
  public:
    explicit Impl(Config config) : config_(std::move(config)) {}

    // The model is loaded on first use so store commands never pay for ONNX startup.
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
                    Error{.kind = ErrorKind::EmbedderLoad, .code = codeOf(created.error()), .path = modelDir});
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
    TraceSession session;
    const auto embedder = impl_->embedder();
    if (!embedder)
    {
        return std::unexpected(embedder.error());
    }

    const auto parsed = parseFiles(request.files);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }

    const auto chunks = embedFiles(*parsed, **embedder);
    if (!chunks)
    {
        return std::unexpected(chunks.error());
    }
    if (chunks->empty())
    {
        return textFailure(ErrorKind::EmptyInput, 0, parsed->skippedLines);
    }

    return rankChunks(*chunks, **embedder, request.query, request.topK, parsed->skippedLines, ErrorKind::IndexBuild);
}

std::expected<IndexSummary, Error> Nesso::index(const IndexRequest &request) const
{
    TraceSession session;
    const auto embedder = impl_->embedder();
    if (!embedder)
    {
        return std::unexpected(embedder.error());
    }

    const auto parsed = parseFiles(request.files);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }

    const auto chunks = embedFiles(*parsed, **embedder);
    if (!chunks)
    {
        return std::unexpected(chunks.error());
    }

    const auto written = [&request, &chunks]
    {
        ScopedStage stage{"corpus-write"};
        return core::writeCorpusFile(request.output, *chunks);
    }();
    if (!written)
    {
        return std::unexpected(Error{.kind = ErrorKind::CorpusWrite,
                                     .code = codeOf(written.error()),
                                     .path = request.output,
                                     .skippedLines = parsed->skippedLines});
    }
    return IndexSummary{.chunks = chunks->size(), .skippedLines = parsed->skippedLines};
}

std::expected<TextResults, Error> Nesso::search(const SearchRequest &request) const
{
    TraceSession session;
    const auto chunks = [&request]
    {
        ScopedStage stage{"corpus-read"};
        return core::readCorpusFile(request.index);
    }();
    if (!chunks)
    {
        return std::unexpected(
            Error{.kind = ErrorKind::CorpusRead, .code = codeOf(chunks.error()), .path = request.index});
    }
    if (chunks->empty() || request.topK == 0)
    {
        return textFailure(ErrorKind::NoMatches, 0, 0);
    }

    const auto embedder = impl_->embedder();
    if (!embedder)
    {
        return std::unexpected(embedder.error());
    }

    return rankChunks(*chunks, **embedder, request.query, request.topK, 0, ErrorKind::CorpusLoad);
}

std::expected<StoreInitSummary, Error> Nesso::storeInit(const StoreInitRequest &request) const
{
    core::StorageEngine<float> engine;
    const auto opened = openStore(engine, request.db, request.dimensions, ErrorKind::StoreInit);
    if (!opened)
    {
        return std::unexpected(opened.error());
    }
    return StoreInitSummary{.dimensions = engine.getDimensions()};
}

std::expected<StoreIngestSummary, Error> Nesso::storeIngest(const StoreIngestRequest &request) const
{
    core::StorageEngine<float> engine;
    const auto opened = openStore(engine, request.db, request.dimensions, ErrorKind::StoreOpen);
    if (!opened)
    {
        return std::unexpected(opened.error());
    }

    StoreIngestSummary summary;
    summary.before = engine.getVectorCount();
    std::ifstream input(request.input, std::ios::binary);
    if (!input)
    {
        return std::unexpected(Error{.kind = ErrorKind::InputOpen, .path = request.input});
    }

    std::vector<float> buffer(engine.getDimensions());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    char *bufferData = reinterpret_cast<char *>(buffer.data());
    const auto recordSize = static_cast<std::streamsize>(buffer.size() * sizeof(float));
    while (input.read(bufferData, recordSize))
    {
        const auto appended = engine.appendVector(buffer);
        if (!appended)
        {
            summary.appendFailureCode = codeOf(appended.error());
            break;
        }
        ++summary.added;
    }
    summary.total = engine.getVectorCount();
    return summary;
}

std::expected<std::vector<VectorMatch>, Error> Nesso::storeSearch(const StoreSearchRequest &request) const
{
    if (!std::filesystem::exists(request.db))
    {
        return std::unexpected(Error{.kind = ErrorKind::StoreMissing, .path = request.db});
    }

    core::StorageEngine<float> engine;
    const auto opened = openStore(engine, request.db, request.dimensions, ErrorKind::StoreOpen);
    if (!opened)
    {
        return std::unexpected(opened.error());
    }

    const uint64_t dimensions = engine.getDimensions();
    const auto expectedQueryBytes = dimensions * sizeof(float);
    std::error_code sizeError;
    const auto queryBytes = std::filesystem::file_size(request.query, sizeError);
    if (sizeError || queryBytes != expectedQueryBytes)
    {
        return std::unexpected(Error{.kind = ErrorKind::QuerySize, .path = request.query, .dimensions = dimensions});
    }

    std::vector<float> query(dimensions);
    std::ifstream input(request.query, std::ios::binary);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    input.read(reinterpret_cast<char *>(query.data()), static_cast<std::streamsize>(expectedQueryBytes));
    if (!input)
    {
        return std::unexpected(Error{.kind = ErrorKind::QueryRead, .path = request.query});
    }

    const auto ranked = core::searchTopKCosine<float>(engine, query, request.topK);
    if (!ranked)
    {
        return std::unexpected(Error{.kind = ErrorKind::StoreSearch, .code = codeOf(ranked.error()), .path = {}});
    }

    std::vector<VectorMatch> matches;
    matches.reserve(ranked->size());
    for (const core::SearchResult<float> &match : *ranked)
    {
        matches.push_back({.index = match.index, .score = match.score});
    }
    return matches;
}

} // namespace nesso
