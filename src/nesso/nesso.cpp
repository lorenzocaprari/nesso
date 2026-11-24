// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "core/core_types.hpp"
#include "core/corpus_index.hpp"
#include "embed/embed_types.hpp"
#include "embed/session_policy.hpp"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
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
#include <parser/line_header.hpp>
#include <parser/template_mask.hpp>

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

static std::string ModelLoadNote(const std::vector<std::filesystem::path> &tried)
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

namespace
{
struct FileStamp
{
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
    std::intmax_t size = 0;
    std::intmax_t modified_seconds = 0;
    std::intmax_t modified_nanoseconds = 0;
    std::intmax_t changed_seconds = 0;
    std::intmax_t changed_nanoseconds = 0;

    // A defaulted friend cannot be static. The type is already internal.
    // NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace)
    friend bool operator==(const FileStamp &, const FileStamp &) = default;
};
} // namespace

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
                     .modified_seconds = static_cast<std::intmax_t>(status.st_mtim.tv_sec),
                     .modified_nanoseconds = static_cast<std::intmax_t>(status.st_mtim.tv_nsec),
                     .changed_seconds = static_cast<std::intmax_t>(status.st_ctim.tv_sec),
                     .changed_nanoseconds = static_cast<std::intmax_t>(status.st_ctim.tv_nsec)};
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

    OnnxBoundEmbedder(std::unique_ptr<nesso::embed::OnnxEmbedder> inner, const std::filesystem::path &model_dir,
                      const ModelFileStamps &stamps)
        : inner_(std::move(inner)), model_path_(model_dir / "model.onnx"), vocab_path_(model_dir / "vocab.txt"),
          model_stamp_(stamps.model), vocab_stamp_(stamps.vocab)
    {
    }

    OnnxBoundEmbedder(const OnnxBoundEmbedder &) = delete;
    OnnxBoundEmbedder &operator=(const OnnxBoundEmbedder &) = delete;
    OnnxBoundEmbedder(OnnxBoundEmbedder &&) = delete;
    OnnxBoundEmbedder &operator=(OnnxBoundEmbedder &&) = delete;
    ~OnnxBoundEmbedder() override = default;

    [[nodiscard]] size_t sessions() const noexcept override { return inner_->Sessions(); }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        if (identity_)
        {
            return *identity_;
        }
        if (!model_stamp_ || !vocab_stamp_ || ReadFileStamp(model_path_) != model_stamp_ ||
            ReadFileStamp(vocab_path_) != vocab_stamp_)
        {
            return std::unexpected(nesso::embed::EmbedError::ModelLoadFailure);
        }
        const auto model_hash = nesso::core::Sha256File(model_path_);
        const auto vocab_hash = nesso::core::Sha256File(vocab_path_);
        if (!model_hash || !vocab_hash || ReadFileStamp(model_path_) != model_stamp_ ||
            ReadFileStamp(vocab_path_) != vocab_stamp_)
        {
            return std::unexpected(nesso::embed::EmbedError::ModelLoadFailure);
        }
        identity_ = nesso::embed::EmbedderIdentity{.model_id = std::string{nesso::embed::MINILM_MODEL_ID},
                                                   .model_sha256 = nesso::core::HexSha256(*model_hash),
                                                   .vocab_sha256 = nesso::core::HexSha256(*vocab_hash),
                                                   .quantized = inner_->Policy().quantized,
                                                   .dimensions = nesso::embed::MINILM_EMBEDDING_DIMENSIONS};
        return *identity_;
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError>
    embed(std::string_view text) const override
    {
        return inner_->Embed(text);
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
    {
        return inner_->EmbedBatch(texts, std::move(progress));
    }

  private:
    std::unique_ptr<nesso::embed::OnnxEmbedder> inner_;
    std::filesystem::path model_path_;
    std::filesystem::path vocab_path_;
    std::optional<FileStamp> model_stamp_;
    std::optional<FileStamp> vocab_stamp_;
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

std::expected<const nesso::embed::Embedder *, Error> Nesso::Embedder() const
{
    if (!embedder_)
    {
        std::vector<std::filesystem::path> tried;
        std::filesystem::path model_dir;
        if (config_.model_dir.has_value())
        {
            model_dir = *config_.model_dir;
            tried.push_back(model_dir);
        }
        else
        {
            tried = ModelDirCandidates();
            model_dir = ResolveDefaultModelDir();
        }
        auto model_stamp = ReadFileStamp(model_dir / "model.onnx");
        auto vocab_stamp = ReadFileStamp(model_dir / "vocab.txt");
        auto created = nesso::embed::OnnxEmbedder::Create(model_dir, config_.threads);
        if (!created)
        {
            Error error{.kind = ErrorKind::EmbedderLoad,
                        .cause = CauseOf(created.error()),
                        .path = model_dir,
                        .note = {},
                        .stale_sources = {},
                        .bundle_skips = {},
                        .policy_mismatch = false,
                        .corpus_version = {},
                        .model_mismatch = {}};
            error.note = ModelLoadNote(tried);
            return std::unexpected(std::move(error));
        }
        if (const std::string warning = nesso::embed::OverrideWarning((*created)->Policy(), config_.threads);
            !warning.empty())
        {
            std::println(stderr, "{}", warning);
        }
        if (ReadFileStamp(model_dir / "model.onnx") != model_stamp ||
            ReadFileStamp(model_dir / "vocab.txt") != vocab_stamp)
        {
            model_stamp.reset();
            vocab_stamp.reset();
        }
        embedder_ = std::make_unique<OnnxBoundEmbedder>(
            std::move(*created), model_dir,
            OnnxBoundEmbedder::ModelFileStamps{.model = model_stamp, .vocab = vocab_stamp});
    }
    return embedder_.get();
}

namespace detail
{

size_t StreamBatch() { return nesso::embed::BATCH_WINDOW; }

std::expected<std::vector<std::vector<float>>, Error>
EmbedDistinct(std::span<const std::string> texts, const nesso::embed::Embedder &embedder, ProgressReporter &progress,
              const EmbedProgressSpan &span, const LineCounts &counts)
{
    const size_t distinct = texts.size();
    std::vector<std::string> messages;
    messages.reserve(distinct);
    for (const std::string &text : texts)
    {
        messages.emplace_back(nesso::parser::SplitLogLine(text).message);
    }
    auto embeddings = embedder.EmbedBatch(
        messages, [&progress, span, distinct](size_t batch_completed, size_t)
        { progress.Update(span.lines_before + (span.lines_in_flush * batch_completed / distinct), span.total); });
    if (!embeddings)
    {
        return std::unexpected(MakeError(ErrorKind::Embed, CauseOf(embeddings.error()), {}, counts));
    }
    if (embeddings->size() != distinct)
    {
        return TextFailure(ErrorKind::Embed, nesso::core::EngineError::MismatchedDimensions, counts);
    }
    progress.Update(span.lines_before + span.lines_in_flush, span.total);
    return std::move(*embeddings);
}

// The planned runs and how each one reaches ONNX Runtime (ADR 0004, ADR 0016).
static std::string BatchPolicy()
{
    namespace embed = nesso::embed;
    return std::format("window:{},max:{},tokens:{},order:length-then-input,run:{}", embed::BATCH_WINDOW,
                       embed::BATCH_MAX_TEXTS, embed::BATCH_TOKEN_BUDGET, embed::BATCH_RUN);
}

std::expected<nesso::core::CorpusPolicy, Error> MakeCorpusPolicy(const nesso::embed::Embedder &embedder,
                                                                 const LineCounts &counts)
{
    const auto identity = embedder.identity();
    if (!identity)
    {
        Error error = MakeError(ErrorKind::EmbedderLoad, CauseOf(identity.error()), {}, counts);
        error.note = "Could not fingerprint the loaded model and vocabulary, or "
                     "they changed; reload them and retry.";
        return std::unexpected(std::move(error));
    }
    nesso::core::CorpusPolicy policy{
        {"bm25", std::string{nesso::core::BM25_POLICY}},
        {"dedup", "masked-v1"},
        {"dimensions", std::to_string(identity->dimensions)},
        {std::string{nesso::core::EMBED_PREFIX_KEY}, std::string{nesso::embed::MINILM_EMBED_PREFIX}},
        {std::string{nesso::core::EMBED_TEXT_KEY}, std::string{nesso::parser::HEADER_RULE_VERSION}},
        {"lexical.terms", std::string{nesso::core::LEXICAL_TERMS_VERSION}},
        {"mask", std::string{nesso::parser::MASK_RULE_VERSION}},
        {"model.id", identity->model_id},
        {"model.quantization", identity->quantized ? "int8" : "fp32"},
        {"model.sha256", identity->model_sha256},
        {"tokens.max", std::to_string(nesso::embed::DEFAULT_MAX_SEQUENCE_LENGTH)},
        {"truncate", std::format("{}-codepoints", MAX_LINE_LENGTH)},
        {"vocab.sha256", identity->vocab_sha256}};
    policy.emplace("batch", BatchPolicy());
    return policy;
}

std::string_view EffectiveModeName(EffectiveMode mode) noexcept
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

EffectiveMode ResolveEffectiveMode(SearchMode requested, nesso::core::QueryClass query_class) noexcept
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
        return query_class == nesso::core::QueryClass::Identifier ? EffectiveMode::Lexical : EffectiveMode::Hybrid;
    }
    std::unreachable();
}

std::vector<std::string_view> QueryTermViews(const std::vector<std::string> &terms)
{
    std::vector<std::string_view> views;
    views.reserve(terms.size());
    for (const std::string &term : terms)
    {
        views.push_back(term);
    }
    return views;
}

bool BetterHit(float left_score, uint64_t left_index, float right_score, uint64_t right_index) noexcept
{
    return nesso::core::RankBefore(left_score, left_index, right_score, right_index);
}

} // namespace detail

} // namespace nesso
