// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_ONNX_EMBEDDER_HPP
#define EMBED_ONNX_EMBEDDER_HPP

#include <embed/embed_types.hpp>
#include <embed/session_policy.hpp>
#include <embed/wordpiece_tokenizer.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace nesso::embed
{

class OnnxEmbedder
{
  public:
    using ProgressCallback = std::function<void(size_t completed, size_t total)>;

    [[nodiscard]] static std::expected<std::unique_ptr<OnnxEmbedder>, EmbedError>
    Create(const std::filesystem::path &model_dir, size_t threads = 0);

    /// One session at this intra-op width, raised to IntraOpThreadFloor().
    /// The product path is create(). This pins a width the policy would not.
    [[nodiscard]] static std::expected<std::unique_ptr<OnnxEmbedder>, EmbedError>
    CreateWithIntraOp(const std::filesystem::path &model_dir, int intra_op_threads);

    OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &model_path, size_t threads);

    [[nodiscard]] const SessionPolicy &Policy() const noexcept;

    /// int8 session count, or 1 for an fp32 model.
    [[nodiscard]] size_t Sessions() const noexcept;

    [[nodiscard]] std::expected<std::vector<float>, EmbedError> Embed(std::string_view text) const;

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback progress = {}) const;

    OnnxEmbedder(const OnnxEmbedder &) = delete;
    OnnxEmbedder &operator=(const OnnxEmbedder &) = delete;
    OnnxEmbedder(OnnxEmbedder &&) = delete;
    OnnxEmbedder &operator=(OnnxEmbedder &&) = delete;
    ~OnnxEmbedder();

    /// The first embedder environment, including the AMX guard.
    static void PrepareEmbedderEnv();

  private:
    OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &model_path, const SessionPolicy &policy);
    class SessionImpl;
    std::unique_ptr<SessionImpl> session_;
    WordPieceTokenizer tokenizer_;
    SessionPolicy policy_{};
};

} // namespace nesso::embed

#endif // EMBED_ONNX_EMBEDDER_HPP
