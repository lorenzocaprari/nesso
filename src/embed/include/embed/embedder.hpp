// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef EMBED_EMBEDDER_HPP
#define EMBED_EMBEDDER_HPP

#include <embed/embed_types.hpp>

#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nesso::embed
{

struct EmbedderIdentity
{
    std::string modelId;
    std::string modelSha256;
    std::string vocabSha256;
    bool quantized = false;
    size_t dimensions = 0;
};

/// Text embedder used by grep, index, and search.
class Embedder
{
  public:
    using ProgressCallback = std::function<void(size_t completed, size_t total)>;

    Embedder(const Embedder &) = delete;
    Embedder &operator=(const Embedder &) = delete;
    Embedder(Embedder &&) = delete;
    Embedder &operator=(Embedder &&) = delete;
    virtual ~Embedder() = default;

    [[nodiscard]] virtual size_t sessions() const noexcept = 0;

    /// Identity of the loaded model and vocabulary. Only index and search need to compute it.
    [[nodiscard]] virtual std::expected<EmbedderIdentity, EmbedError> identity() const = 0;

    [[nodiscard]] virtual std::expected<std::vector<float>, EmbedError> embed(std::string_view text) const = 0;

    [[nodiscard]] virtual std::expected<std::vector<std::vector<float>>, EmbedError>
    embedBatch(std::span<const std::string> texts, ProgressCallback progress) const = 0;

  protected:
    Embedder() = default;
};

} // namespace nesso::embed

#endif // EMBED_EMBEDDER_HPP
