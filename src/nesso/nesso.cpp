// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/nesso.hpp>

#include <nesso/error_map.hpp>
#include <nesso/model_paths.hpp>
#include <nesso_detail.hpp>

#include <core/bm25.hpp>
#include <core/lexical_terms.hpp>
#include <core/query_class.hpp>
#include <core/sha256.hpp>
#include <core/top_k.hpp>
#include <embed/batch_plan.hpp>
#include <embed/embedder.hpp>
#include <embed/onnx_embedder.hpp>

#include <format>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace nesso
{

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

// NOLINTNEXTLINE(readability-identifier-naming)
static std::optional<FileStamp> ReadFileStamp(const std::filesystem::path &path)
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
    struct ModelFileStamps
    {
        std::optional<FileStamp> model;
        std::optional<FileStamp> vocab;
    };

    OnnxBoundEmbedder(std::unique_ptr<nesso::embed::OnnxEmbedder> inner, std::filesystem::path modelDir,
                      const ModelFileStamps &stamps)
        : inner_(std::move(inner)), modelPath_(modelDir / "model.onnx"), vocabPath_(modelDir / "vocab.txt"),
          modelStamp_(stamps.model), vocabStamp_(stamps.vocab)
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
        if (!modelStamp_ || !vocabStamp_ || ReadFileStamp(modelPath_) != modelStamp_ ||
            ReadFileStamp(vocabPath_) != vocabStamp_)
        {
            return std::unexpected(nesso::embed::EmbedError::ModelLoadFailure);
        }
        const auto modelHash = nesso::core::sha256File(modelPath_);
        const auto vocabHash = nesso::core::sha256File(vocabPath_);
        if (!modelHash || !vocabHash || ReadFileStamp(modelPath_) != modelStamp_ ||
            ReadFileStamp(vocabPath_) != vocabStamp_)
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
        auto modelStamp = ReadFileStamp(modelDir / "model.onnx");
        auto vocabStamp = ReadFileStamp(modelDir / "vocab.txt");
        auto created = nesso::embed::OnnxEmbedder::create(modelDir, config_.threads);
        if (!created)
        {
            Error error{.kind = ErrorKind::EmbedderLoad,
                        .cause = causeOf(created.error()),
                        .path = modelDir,
                        .note = {},
                        .staleSources = {},
                        .bundleSkips = {},
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
        if (ReadFileStamp(modelDir / "model.onnx") != modelStamp || ReadFileStamp(modelDir / "vocab.txt") != vocabStamp)
        {
            modelStamp.reset();
            vocabStamp.reset();
        }
        embedder_ = std::make_unique<OnnxBoundEmbedder>(
            std::move(*created), modelDir,
            OnnxBoundEmbedder::ModelFileStamps{.model = modelStamp, .vocab = vocabStamp});
    }
    return embedder_.get();
}

namespace detail
{

size_t streamBatch() { return nesso::embed::BATCH_WINDOW; }

std::expected<std::vector<std::vector<float>>, Error>
embedDistinct(std::span<const std::string> texts, const nesso::embed::Embedder &embedder, ProgressReporter &progress,
              const EmbedProgressSpan &span, const LineCounts &counts)
{
    const size_t distinct = texts.size();
    auto embeddings = embedder.embedBatch(
        texts, [&progress, span, distinct](size_t batchCompleted, size_t)
        { progress.update(span.linesBefore + (span.linesInFlush * batchCompleted / distinct), span.total); });
    if (!embeddings)
    {
        return std::unexpected(makeError(ErrorKind::Embed, causeOf(embeddings.error()), {}, counts));
    }
    if (embeddings->size() != distinct)
    {
        return textFailure(ErrorKind::Embed, nesso::core::EngineError::MismatchedDimensions, counts);
    }
    progress.update(span.linesBefore + span.linesInFlush, span.total);
    return std::move(*embeddings);
}

// NOLINTNEXTLINE(readability-identifier-naming)
std::expected<nesso::core::CorpusPolicy, Error> MakeCorpusPolicy(const nesso::embed::Embedder &embedder,
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
        {"bm25", std::string{nesso::core::BM25_POLICY}},
        {"dedup", "masked-v1"},
        {"dimensions", std::to_string(identity->dimensions)},
        {"lexical.terms", std::string{nesso::core::LEXICAL_TERMS_VERSION}},
        {"mask", "template-v1"},
        {"model.id", identity->modelId},
        {"model.quantization", identity->quantized ? "int8" : "fp32"},
        {"model.sha256", identity->modelSha256},
        {"tokens.max", std::to_string(nesso::embed::DEFAULT_MAX_SEQUENCE_LENGTH)},
        {"truncate", std::format("{}-codepoints", MAX_LINE_LENGTH)},
        {"vocab.sha256", identity->vocabSha256}};
}

std::string_view effectiveModeName(EffectiveMode mode) noexcept
{
    switch (mode)
    {
    case EffectiveMode::Dense:
        return "dense";
    case EffectiveMode::Hybrid:
        return "hybrid";
    case EffectiveMode::Lexical:
        return "lexical";
    }
    std::unreachable();
}

EffectiveMode resolveEffectiveMode(SearchMode requested, nesso::core::QueryClass queryClass) noexcept
{
    switch (requested)
    {
    case SearchMode::Dense:
        return EffectiveMode::Dense;
    case SearchMode::Hybrid:
        return EffectiveMode::Hybrid;
    case SearchMode::Lexical:
        return EffectiveMode::Lexical;
    case SearchMode::Auto:
        return queryClass == nesso::core::QueryClass::Identifier ? EffectiveMode::Lexical : EffectiveMode::Hybrid;
    }
    std::unreachable();
}

std::vector<std::string_view> queryTermViews(const std::vector<std::string> &terms)
{
    std::vector<std::string_view> views;
    views.reserve(terms.size());
    for (const std::string &term : terms)
    {
        views.push_back(term);
    }
    return views;
}

bool betterHit(float leftScore, uint64_t leftIndex, float rightScore, uint64_t rightIndex) noexcept
{
    return nesso::core::rankBefore(leftScore, leftIndex, rightScore, rightIndex);
}

} // namespace detail

} // namespace nesso
