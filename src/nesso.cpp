// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso.hpp>

#include <error_map.hpp>
#include <model_paths.hpp>
#include <trace.hpp>

#include <core/corpus_index.hpp>
#include <core/dedup.hpp>
#include <core/sha256.hpp>
#include <core/top_k.hpp>
#include <embed/batch_plan.hpp>
#include <embed/embedder.hpp>
#include <embed/onnx_embedder.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <sys/stat.h>
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
        static_cast<void>(std::fflush(stderr));
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
    case nesso::core::EngineError::UnsupportedVersion:
        return ErrorCause::UnsupportedCorpusVersion;
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
    case ErrorCause::UnsupportedCorpusVersion:
        return "the corpus format is not supported by this nesso; re-run nesso index";
    case ErrorCause::ModelMismatch:
        return "the corpus was built with a different model; re-run nesso index";
    case ErrorCause::SourceChanged:
        return "the source changed while indexing; retry";
    case ErrorCause::EmptyFile:
        return "the file is empty";
    case ErrorCause::NothingIndexable:
        return "no indexable lines";
    case ErrorCause::EmptyCorpus:
        return "the corpus is empty";
    case ErrorCause::OutputIsInput:
        return "the output is one of the inputs";
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
        if (error.cause == ErrorCause::UnsupportedCorpusVersion && error.corpusVersion)
        {
            return std::format("Error: failed to read the corpus '{}': corpus format v{} is not supported; "
                               "re-run nesso index",
                               path, *error.corpusVersion);
        }
        return std::format("Error: failed to read the corpus '{}': {}", path, reason);
    case ErrorKind::CorpusWrite:
        return std::format("Error: failed to write the corpus '{}': {}", path, reason);
    case ErrorKind::CorpusLoad:
        return std::format("Error: failed to load the corpus: {}", reason);
    case ErrorKind::EmptyInput:
        if (!error.note.empty())
        {
            const std::string where = path.empty() ? std::string{} : std::format(" in '{}'", path);
            if (error.cause == ErrorCause::EmptyFile)
            {
                return std::format("no indexable lines{}: {}; {}", where, reason, error.note);
            }
            return std::format("no indexable lines{}; {}", where, error.note);
        }
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
                 .staleSources = {},
                 .policyMismatch = false,
                 .corpusVersion = {}};
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
    // Stdin cannot be rewound. The count pass keeps it for the embedding pass.
    std::vector<nesso::parser::ParsedChunk> stdinChunks;
    std::vector<nesso::core::SourceInfo> sources;
};

static bool sameSource(const nesso::core::SourceInfo &left, const nesso::core::SourceInfo &right)
{
    return left.tracked == right.tracked && left.size == right.size && left.modifiedTime == right.modifiedTime;
}

static std::expected<Scan, Error> scanFiles(std::span<const std::filesystem::path> files, std::string_view jsonField,
                                            bool forIndex = false)
{
    const ScopedStage stage{"parse"};
    nesso::parser::ParseStats stats{};
    Scan scan{};
    bool capturedStdin = false;
    for (const std::filesystem::path &path : files)
    {
        if (forIndex)
        {
            auto source = nesso::core::sourceInfoFor(path, jsonField);
            if (!source)
            {
                return std::unexpected(makeError(ErrorKind::Parse, causeOf(source.error()), path,
                                                 lineCounts(stats, std::string{jsonField})));
            }
            scan.sources.push_back(std::move(*source));
        }
        if (path == "-")
        {
            if (capturedStdin)
            {
                continue;
            }
            capturedStdin = true;
            const auto keep = [&scan](nesso::parser::ParsedChunk chunk)
            {
                if (!chunk.skipped)
                {
                    ++scan.chunks;
                }
                scan.stdinChunks.push_back(std::move(chunk));
            };
            const auto streamed =
                forIndex ? nesso::parser::Chunker::forEachRecord(path, MAX_LINE_LENGTH, &stats, jsonField, keep)
                         : nesso::parser::Chunker::forEach(path, MAX_LINE_LENGTH, &stats, jsonField, keep);
            if (!streamed)
            {
                return std::unexpected(makeError(ErrorKind::Parse, causeOf(streamed.error()), path,
                                                 lineCounts(stats, std::string{jsonField})));
            }
            if (forIndex)
            {
                const auto after = nesso::core::sourceInfoFor(path, jsonField);
                if (!after || !sameSource(scan.sources.back(), *after))
                {
                    return std::unexpected(makeError(ErrorKind::Parse, ErrorCause::SourceChanged, path,
                                                     lineCounts(stats, std::string{jsonField})));
                }
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

// Like visitChunks, but also yields skipped records so v3 line ordinals stay complete.
template <typename Consume>
static std::expected<void, nesso::parser::ParseError> visitRecords(const std::filesystem::path &path, Scan &scan,
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
    return nesso::parser::Chunker::forEachRecord(path, MAX_LINE_LENGTH, nullptr, jsonField, consume);
}

template <typename E> static std::unexpected<Error> textFailure(ErrorKind kind, E cause, const LineCounts &counts)
{
    return std::unexpected(makeError(kind, causeOf(cause), {}, counts));
}

// `note` selects the index wording in `message`. `EmptyInput` stays exit 1.
static std::unexpected<Error> nothingToIndex(const IndexRequest &request, const Scan &scanned)
{
    const ErrorCause cause = scanned.counts.skipped == 0 ? ErrorCause::EmptyFile : ErrorCause::NothingIndexable;
    const std::filesystem::path path = request.files.size() == 1 ? request.files.front() : std::filesystem::path{};
    Error error = makeError(ErrorKind::EmptyInput, cause, path, scanned.counts);
    error.note = "check the input path or --json-field";
    return std::unexpected(std::move(error));
}

struct FileStamp
{
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
    std::intmax_t size = 0;
    std::intmax_t modifiedSeconds = 0;
    std::intmax_t modifiedNanoseconds = 0;
    std::intmax_t changedSeconds = 0;
    std::intmax_t changedNanoseconds = 0;

    friend bool operator==(const FileStamp &, const FileStamp &) = default;
};

static std::optional<FileStamp> fileStamp(const std::filesystem::path &path)
{
    struct stat status{};
    if (::stat(path.c_str(), &status) != 0)
    {
        return std::nullopt;
    }
    return FileStamp{.device = static_cast<std::uintmax_t>(status.st_dev),
                     .inode = static_cast<std::uintmax_t>(status.st_ino),
                     .size = static_cast<std::intmax_t>(status.st_size),
                     .modifiedSeconds = static_cast<std::intmax_t>(status.st_mtim.tv_sec),
                     .modifiedNanoseconds = static_cast<std::intmax_t>(status.st_mtim.tv_nsec),
                     .changedSeconds = static_cast<std::intmax_t>(status.st_ctim.tv_sec),
                     .changedNanoseconds = static_cast<std::intmax_t>(status.st_ctim.tv_nsec)};
}

namespace
{

class OnnxBoundEmbedder final : public nesso::embed::Embedder
{
  public:
    OnnxBoundEmbedder(std::unique_ptr<nesso::embed::OnnxEmbedder> inner, std::filesystem::path modelDir,
                      std::optional<FileStamp> modelStamp, std::optional<FileStamp> vocabStamp)
        : inner_(std::move(inner)), modelPath_(modelDir / "model.onnx"), vocabPath_(modelDir / "vocab.txt"),
          modelStamp_(modelStamp), vocabStamp_(vocabStamp)
    {
    }

    OnnxBoundEmbedder(const OnnxBoundEmbedder &) = delete;
    OnnxBoundEmbedder &operator=(const OnnxBoundEmbedder &) = delete;
    OnnxBoundEmbedder(OnnxBoundEmbedder &&) = delete;
    OnnxBoundEmbedder &operator=(OnnxBoundEmbedder &&) = delete;
    ~OnnxBoundEmbedder() override = default;

    [[nodiscard]] size_t sessions() const noexcept override { return inner_->sessions(); }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        if (identity_)
        {
            return *identity_;
        }
        if (!modelStamp_ || !vocabStamp_ || fileStamp(modelPath_) != modelStamp_ ||
            fileStamp(vocabPath_) != vocabStamp_)
        {
            return std::unexpected(nesso::embed::EmbedError::ModelLoadFailure);
        }
        const auto modelHash = nesso::core::sha256File(modelPath_);
        const auto vocabHash = nesso::core::sha256File(vocabPath_);
        if (!modelHash || !vocabHash || fileStamp(modelPath_) != modelStamp_ || fileStamp(vocabPath_) != vocabStamp_)
        {
            return std::unexpected(nesso::embed::EmbedError::ModelLoadFailure);
        }
        identity_ = nesso::embed::EmbedderIdentity{.modelId = std::string{nesso::embed::MINILM_MODEL_ID},
                                                   .modelSha256 = nesso::core::hexSha256(*modelHash),
                                                   .vocabSha256 = nesso::core::hexSha256(*vocabHash),
                                                   .quantized = inner_->policy().quantized,
                                                   .dimensions = nesso::embed::MINILM_EMBEDDING_DIMENSIONS};
        return *identity_;
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError>
    embed(std::string_view text) const override
    {
        return inner_->embed(text);
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    embedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
    {
        return inner_->embedBatch(texts, std::move(progress));
    }

  private:
    std::unique_ptr<nesso::embed::OnnxEmbedder> inner_;
    std::filesystem::path modelPath_;
    std::filesystem::path vocabPath_;
    std::optional<FileStamp> modelStamp_;
    std::optional<FileStamp> vocabStamp_;
    mutable std::optional<nesso::embed::EmbedderIdentity> identity_;
};

} // namespace

Nesso::Nesso(Config config) : Nesso(std::move(config), nullptr) {}

Nesso::Nesso(Config config, std::unique_ptr<embed::Embedder> embedder)
    : config_(std::move(config)), embedder_(std::move(embedder))
{
}

Nesso::~Nesso() = default;

Nesso::Nesso(Nesso &&) noexcept = default;

Nesso &Nesso::operator=(Nesso &&) noexcept = default;

std::expected<const nesso::embed::Embedder *, Error> Nesso::embedder() const
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
        auto modelStamp = fileStamp(modelDir / "model.onnx");
        auto vocabStamp = fileStamp(modelDir / "vocab.txt");
        auto created = nesso::embed::OnnxEmbedder::create(modelDir, config_.threads);
        if (!created)
        {
            Error error{.kind = ErrorKind::EmbedderLoad,
                        .cause = causeOf(created.error()),
                        .path = modelDir,
                        .note = {},
                        .staleSources = {},
                        .policyMismatch = false,
                        .corpusVersion = {}};
            error.note = modelLoadNote(tried);
            return std::unexpected(std::move(error));
        }
        if (const std::string warning = nesso::embed::overrideWarning((*created)->policy(), config_.threads);
            !warning.empty())
        {
            std::println(stderr, "{}", warning);
        }
        if (fileStamp(modelDir / "model.onnx") != modelStamp || fileStamp(modelDir / "vocab.txt") != vocabStamp)
        {
            modelStamp.reset();
            vocabStamp.reset();
        }
        embedder_ = std::make_unique<OnnxBoundEmbedder>(std::move(*created), modelDir, modelStamp, vocabStamp);
    }
    return embedder_.get();
}

// Fixed window. Session count must not change which texts share a padded run.
static size_t streamBatch() { return nesso::embed::BATCH_WINDOW; }

static std::expected<std::vector<std::vector<float>>, Error>
embedDistinct(std::span<const std::string> texts, const nesso::embed::Embedder &embedder, ProgressReporter &progress,
              size_t linesBefore, size_t linesInFlush, size_t total, const LineCounts &counts)
{
    const size_t distinct = texts.size();
    auto embeddings = embedder.embedBatch(
        texts, [&progress, linesBefore, linesInFlush, distinct, total](size_t batchCompleted, size_t)
        { progress.update(linesBefore + (linesInFlush * batchCompleted / distinct), total); });
    if (!embeddings)
    {
        return std::unexpected(makeError(ErrorKind::Embed, causeOf(embeddings.error()), {}, counts));
    }
    if (embeddings->size() != distinct)
    {
        return textFailure(ErrorKind::Embed, nesso::core::EngineError::MismatchedDimensions, counts);
    }
    progress.update(linesBefore + linesInFlush, total);
    return std::move(*embeddings);
}

static std::expected<nesso::core::CorpusPolicy, Error> corpusPolicy(const nesso::embed::Embedder &embedder,
                                                                    const LineCounts &counts)
{
    const auto identity = embedder.identity();
    if (!identity)
    {
        Error error = makeError(ErrorKind::EmbedderLoad, causeOf(identity.error()), {}, counts);
        error.note = "Could not fingerprint the loaded model and vocabulary, or they changed; reload them and retry.";
        return std::unexpected(std::move(error));
    }
    return nesso::core::CorpusPolicy{
        {"batch", std::format("window:{},max:{},tokens:{},order:length-then-input", nesso::embed::BATCH_WINDOW,
                              nesso::embed::BATCH_MAX_TEXTS, nesso::embed::BATCH_TOKEN_BUDGET)},
        {"dedup", "exact-v1"},
        {"dimensions", std::to_string(identity->dimensions)},
        {"mask", "none-v1"},
        {"model.id", identity->modelId},
        {"model.quantization", identity->quantized ? "int8" : "fp32"},
        {"model.sha256", identity->modelSha256},
        {"tokens.max", std::to_string(nesso::embed::DEFAULT_MAX_SEQUENCE_LENGTH)},
        {"truncate", std::format("{}-codepoints", MAX_LINE_LENGTH)},
        {"vocab.sha256", identity->vocabSha256}};
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
        std::uint32_t distinctId = 0;
        TextMatch match;
    };
    const auto better = [](const Candidate &left, const Candidate &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };

    std::vector<Candidate> best;
    nesso::core::DistinctIndex distinct;
    distinct.reserve(scanned->chunks);
    std::vector<float> scores;
    struct Waiting
    {
        uint64_t ordinal = 0;
        std::uint32_t distinctId = 0;
        std::uint32_t source = 0;
        uint64_t line = 0;
        bool arrayElement = false;
    };
    std::vector<Waiting> waiting;
    std::vector<std::string> sources;
    sources.reserve(request.files.size());
    const size_t batch = streamBatch();
    ProgressReporter progress(request.progress);
    size_t linesSeen = 0;
    size_t linesReported = 0;
    uint64_t ordinal = 0;
    bool severalSources = false;
    std::string firstSource;
    const LineCounts &counts = scanned->counts;

    const auto consider = [&](const Waiting &item)
    {
        TextMatch match{.source = sources[item.source],
                        .line = item.line,
                        .score = scores[item.distinctId],
                        .text = std::string(distinct.text(item.distinctId)),
                        .arrayElement = item.arrayElement};
        best.push_back(Candidate{
            .index = item.ordinal, .score = match.score, .distinctId = item.distinctId, .match = std::move(match)});
        if (best.size() > request.topK)
        {
            std::partial_sort(best.begin(), best.begin() + static_cast<std::ptrdiff_t>(request.topK), best.end(),
                              better);
            best.resize(request.topK);
        }
    };

    const auto flush = [&]() -> std::expected<void, Error>
    {
        const size_t linesInFlush = linesSeen - linesReported;
        if (distinct.pending().empty())
        {
            linesReported = linesSeen;
            progress.update(linesReported, scanned->chunks);
            return {};
        }
        const auto embeddings =
            embedDistinct(distinct.pending(), **model, progress, linesReported, linesInFlush, scanned->chunks, counts);
        if (!embeddings)
        {
            return std::unexpected(embeddings.error());
        }
        for (const std::vector<float> &embedding : *embeddings)
        {
            const auto score = nesso::core::math::DistanceMetrics::dotProduct<float>(*queryEmbedding, embedding);
            if (!score)
            {
                return textFailure(ErrorKind::Search, score.error(), counts);
            }
            scores.push_back(*score);
        }
        distinct.clearPending();
        linesReported = linesSeen;
        for (const Waiting &item : waiting)
        {
            consider(item);
        }
        waiting.clear();
        return {};
    };

    {
        const ScopedStage stage{"embed"};
        std::optional<Error> failed;
        bool stdinUsed = false;
        for (const std::filesystem::path &path : request.files)
        {
            sources.push_back(path.string());
            const auto sourceId = static_cast<std::uint32_t>(sources.size() - 1);
            const std::string &source = sources.back();
            const auto streamed = visitChunks(path, *scanned, stdinUsed, request.jsonField,
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
                                                  const std::uint32_t id = distinct.add(chunk.text);
                                                  ++linesSeen;
                                                  const Waiting item{.ordinal = ordinal,
                                                                     .distinctId = id,
                                                                     .source = sourceId,
                                                                     .line = chunk.lineNumber,
                                                                     .arrayElement = chunk.arrayElement};
                                                  if (id < scores.size())
                                                  {
                                                      consider(item);
                                                  }
                                                  else
                                                  {
                                                      waiting.push_back(item);
                                                  }
                                                  ++ordinal;
                                                  if (distinct.pending().size() == batch)
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
    for (Candidate &candidate : best)
    {
        candidate.match.occurrences = distinct.occurrences(candidate.distinctId);
        results.matches.push_back(std::move(candidate.match));
    }
    return results;
}

std::expected<IndexSummary, Error> Nesso::index(const IndexRequest &request) const
{
    const TraceSession session;
    for (const std::filesystem::path &path : request.files)
    {
        std::error_code error;
        if (path != "-" && std::filesystem::equivalent(path, request.output, error))
        {
            return std::unexpected(makeError(ErrorKind::CorpusWrite, ErrorCause::OutputIsInput, request.output, {}));
        }
    }
    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto scanned = scanFiles(request.files, request.jsonField, true);
    if (!scanned)
    {
        return std::unexpected(scanned.error());
    }

    auto policy = corpusPolicy(**model, scanned->counts);
    if (!policy)
    {
        return std::unexpected(policy.error());
    }
    // create() rejects an unusable output path; only finish() replaces an existing corpus.
    auto writer = nesso::core::CorpusWriter::create(request.output, std::move(*policy));
    if (!writer)
    {
        return std::unexpected(
            makeError(ErrorKind::CorpusWrite, causeOf(writer.error()), request.output, scanned->counts));
    }
    if (scanned->chunks == 0)
    {
        return nothingToIndex(request, *scanned);
    }

    const size_t batch = streamBatch();
    nesso::core::DistinctIndex distinct;
    distinct.reserve(scanned->chunks);
    std::uint32_t reserved = 0;
    std::uint32_t vectorsWritten = 0;
    ProgressReporter progress(request.progress);
    size_t linesSeen = 0;
    size_t linesReported = 0;
    const LineCounts &counts = scanned->counts;
    const auto embedOpenWindow = [&]() -> std::expected<void, Error>
    {
        if (distinct.pending().empty())
        {
            return {};
        }
        const size_t linesInFlush = linesSeen - linesReported;
        auto embeddings =
            embedDistinct(distinct.pending(), **model, progress, linesReported, linesInFlush, scanned->chunks, counts);
        if (!embeddings)
        {
            return std::unexpected(embeddings.error());
        }
        for (const std::vector<float> &embedding : *embeddings)
        {
            const auto added = writer->addVector(vectorsWritten, embedding);
            if (!added)
            {
                return std::unexpected(
                    makeError(ErrorKind::CorpusWrite, causeOf(added.error()), request.output, counts));
            }
            ++vectorsWritten;
        }
        distinct.clearPending();
        linesReported = linesSeen;
        progress.update(linesReported, scanned->chunks);
        return {};
    };

    {
        const ScopedStage stage{"embed"};
        std::optional<Error> failed;
        bool stdinUsed = false;
        for (size_t sourceIndex = 0; sourceIndex < request.files.size(); ++sourceIndex)
        {
            const std::filesystem::path &path = request.files[sourceIndex];
            const nesso::core::SourceInfo &source = scanned->sources[sourceIndex];
            if (path != "-")
            {
                const auto before = nesso::core::sourceInfoFor(path, request.jsonField);
                if (!before || !sameSource(source, *before))
                {
                    return std::unexpected(makeError(ErrorKind::Parse, ErrorCause::SourceChanged, path, counts));
                }
            }
            const auto sourceId = writer->addSource(source);
            if (!sourceId)
            {
                return std::unexpected(
                    makeError(ErrorKind::CorpusWrite, causeOf(sourceId.error()), request.output, counts));
            }
            const auto streamed = visitRecords(
                path, *scanned, stdinUsed, request.jsonField,
                [&](nesso::parser::ParsedChunk chunk)
                {
                    if (failed)
                    {
                        return;
                    }
                    std::uint32_t id = nesso::core::NO_DISTINCT;
                    if (!chunk.skipped)
                    {
                        id = distinct.add(chunk.text);
                        if (id == reserved)
                        {
                            const auto added = writer->reserveDistinct(distinct.text(id));
                            if (!added || *added != id)
                            {
                                failed = makeError(ErrorKind::CorpusWrite,
                                                   added ? ErrorCause::CorruptFile : causeOf(added.error()),
                                                   request.output, counts);
                                return;
                            }
                            ++reserved;
                        }
                        ++linesSeen;
                    }
                    nesso::core::CorpusLine line{.lineNumber = chunk.lineNumber,
                                                 .byteOffset = chunk.byteOffset,
                                                 .byteLength = chunk.byteLength,
                                                 .sourceIndex = *sourceId,
                                                 .distinctId = id,
                                                 .arrayElement = chunk.arrayElement};
                    if (source.storedOnly)
                    {
                        line.storedText = chunk.text;
                    }
                    if (const auto added = writer->addLines(std::span{&line, 1}); !added)
                    {
                        failed = makeError(ErrorKind::CorpusWrite, causeOf(added.error()), request.output, counts);
                        return;
                    }
                    if (distinct.pending().size() == batch)
                    {
                        if (const auto flushed = embedOpenWindow(); !flushed)
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
            if (path != "-")
            {
                const auto after = nesso::core::sourceInfoFor(path, request.jsonField);
                if (!after || !sameSource(source, *after))
                {
                    return std::unexpected(makeError(ErrorKind::Parse, ErrorCause::SourceChanged, path, counts));
                }
            }
        }
        if (const auto flushed = embedOpenWindow(); !flushed)
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
    std::uint32_t version = 0;
    const auto corpus = [&request, &version]
    {
        const ScopedStage stage{"corpus-read"};
        return nesso::core::mapCorpusFile(request.index, &version);
    }();
    if (!corpus)
    {
        Error error = makeError(ErrorKind::CorpusRead, causeOf(corpus.error()), request.index, {});
        if (corpus.error() == nesso::core::EngineError::UnsupportedVersion)
        {
            error.corpusVersion = version;
        }
        return std::unexpected(std::move(error));
    }
    if (corpus->texts().empty())
    {
        return std::unexpected(makeError(ErrorKind::EmptyInput, ErrorCause::EmptyCorpus, request.index, {}));
    }

    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }
    const auto current = corpusPolicy(**model, {});
    if (!current)
    {
        return std::unexpected(current.error());
    }
    const auto &stored = corpus->policy();
    constexpr std::array<std::string_view, 5> identityKeys{"model.id", "model.sha256", "vocab.sha256",
                                                           "model.quantization", "dimensions"};
    for (const std::string_view key : identityKeys)
    {
        if (stored.find(key)->second != current->find(key)->second)
        {
            return std::unexpected(makeError(ErrorKind::CorpusRead, ErrorCause::ModelMismatch, request.index, {}));
        }
    }
    constexpr std::array<std::string_view, 5> policyKeys{"batch", "truncate", "dedup", "mask", "tokens.max"};
    const bool policyMismatch = std::ranges::any_of(policyKeys, [&](std::string_view key)
                                                    { return stored.find(key)->second != current->find(key)->second; });
    auto sourceWarnings = staleSources(corpus->sources());
    const auto fail = [&](ErrorKind kind, ErrorCause cause)
    {
        Error error = makeError(kind, cause, {}, {});
        error.policyMismatch = policyMismatch;
        error.staleSources = sourceWarnings;
        return std::unexpected(std::move(error));
    };
    const auto sourceIsStale = [&](const std::string &path)
    {
        return std::ranges::any_of(sourceWarnings,
                                   [&path](const StaleSource &warning) { return warning.path == path; });
    };

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
    std::vector<float> scores;
    scores.reserve(corpus->texts().size());
    for (size_t id = 0; id < corpus->texts().size(); ++id)
    {
        const auto row = corpus->matrix().subspan(id * dimensions, dimensions);
        const auto score = nesso::core::math::DistanceMetrics::dotProduct<float>(*queryEmbedding, row);
        if (!score)
        {
            return fail(ErrorKind::Search, causeOf(score.error()));
        }
        if (!std::isfinite(*score))
        {
            return fail(ErrorKind::Search, ErrorCause::InvalidInput);
        }
        scores.push_back(*score);
    }

    const auto better = [](const nesso::core::Hit &left, const nesso::core::Hit &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };
    std::vector<nesso::core::Hit> ranked;
    ranked.reserve(std::min(request.topK, corpus->lines().size()));
    std::vector<std::uint32_t> occurrences(corpus->texts().size(), 0);
    std::optional<std::string_view> firstSource;
    bool multipleSources = false;
    for (size_t ordinal = 0; ordinal < corpus->lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus->lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT)
        {
            continue;
        }
        ++occurrences[line.distinctId];
        const std::string &source = corpus->sources()[line.sourceIndex].path;
        if (!firstSource)
        {
            firstSource = source;
        }
        else if (*firstSource != source)
        {
            multipleSources = true;
        }
        if (request.topK == 0)
        {
            continue;
        }
        const nesso::core::Hit hit{.index = ordinal, .score = scores[line.distinctId]};
        if (ranked.size() < request.topK)
        {
            ranked.push_back(hit);
            std::ranges::push_heap(ranked, better);
        }
        else if (better(hit, ranked.front()))
        {
            std::ranges::pop_heap(ranked, better);
            ranked.back() = hit;
            std::ranges::push_heap(ranked, better);
        }
    }
    if (ranked.empty())
    {
        return fail(ErrorKind::NoMatches, ErrorCause::None);
    }
    std::ranges::sort(ranked, better);

    TextResults results{};
    results.multipleSources = multipleSources;
    results.policyMismatch = policyMismatch;
    results.matches.reserve(ranked.size());
    for (const nesso::core::Hit &hit : ranked)
    {
        const nesso::core::CorpusLine &line = corpus->lines()[static_cast<size_t>(hit.index)];
        const nesso::core::SourceInfo &source = corpus->sources()[line.sourceIndex];
        std::string text{line.storedText.value_or(corpus->texts()[line.distinctId])};
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
        results.matches.push_back({.source = source.path,
                                   .line = line.lineNumber,
                                   .score = hit.score,
                                   .text = std::move(text),
                                   .arrayElement = line.arrayElement,
                                   .occurrences = occurrences[line.distinctId]});
    }
    for (size_t index = 0; index < ranked.size(); ++index)
    {
        const nesso::core::CorpusLine &line = corpus->lines()[static_cast<size_t>(ranked[index].index)];
        if (!line.storedText && sourceIsStale(corpus->sources()[line.sourceIndex].path))
        {
            results.matches[index].text = corpus->texts()[line.distinctId];
        }
    }
    results.staleSources = std::move(sourceWarnings);
    return results;
}

} // namespace nesso
