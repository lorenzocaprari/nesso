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
    std::string model_id;
    std::string model_sha256;
    std::string vocab_sha256;
    bool quantized = false;
    size_t dimensions = 0;
};

/// Text embedder used by grep, index, and search.
class Embedder
{
  public:
    using ProgressCallback = std::function<void(size_t completed, size_t total)>;

    Embedder(const Embedder &) = delete;
    auto operator=(const Embedder &) -> Embedder & = delete;
    Embedder(Embedder &&) = delete;
    auto operator=(Embedder &&) -> Embedder & = delete;
    virtual ~Embedder() = default;

    [[nodiscard]] virtual auto sessions() const noexcept -> size_t = 0;

    /// Identity of the loaded model and vocabulary. Only index and search need to
    /// compute it.
    [[nodiscard]] virtual auto identity() const -> std::expected<EmbedderIdentity, EmbedError> = 0;

    [[nodiscard]] virtual auto embed(std::string_view text) const -> std::expected<std::vector<float>, EmbedError> = 0;

    [[nodiscard]] virtual auto EmbedBatch(std::span<const std::string> texts, ProgressCallback progress) const
        -> std::expected<std::vector<std::vector<float>>, EmbedError> = 0;

  protected:
    Embedder() = default;
};

} // namespace nesso::embed

#endif // EMBED_EMBEDDER_HPP
