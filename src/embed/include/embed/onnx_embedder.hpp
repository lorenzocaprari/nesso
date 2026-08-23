// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef EMBED_ONNX_EMBEDDER_HPP
#define EMBED_ONNX_EMBEDDER_HPP

#include "embed_types.hpp"
#include "wordpiece_tokenizer.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace embed
{

class OnnxEmbedder
{
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<OnnxEmbedder>, EmbedError>
    create(const std::filesystem::path &modelDir);

    OnnxEmbedder(WordPieceTokenizer tokenizer, const std::filesystem::path &modelPath);

    [[nodiscard]] std::expected<std::vector<float>, EmbedError> embed(std::string_view text) const;

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, EmbedError>
    embedBatch(std::span<const std::string> texts) const;

    OnnxEmbedder(const OnnxEmbedder &) = delete;
    OnnxEmbedder &operator=(const OnnxEmbedder &) = delete;
    OnnxEmbedder(OnnxEmbedder &&) = delete;
    OnnxEmbedder &operator=(OnnxEmbedder &&) = delete;
    ~OnnxEmbedder();

  private:
    struct SessionImpl;
    std::unique_ptr<SessionImpl> session_;
    WordPieceTokenizer tokenizer_;
};

} // namespace embed

#endif // EMBED_ONNX_EMBEDDER_HPP
