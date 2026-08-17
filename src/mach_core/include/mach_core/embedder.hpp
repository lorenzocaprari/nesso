// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#ifndef MACH_CORE_EMBEDDER_HPP
#define MACH_CORE_EMBEDDER_HPP

#include "core_types.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace mach_core
{

class IEmbedder
{
  public:
    virtual ~IEmbedder() = default;

    [[nodiscard]] virtual std::expected<std::vector<float>, EngineError> embed(std::string_view text) const = 0;
    [[nodiscard]] virtual uint64_t dimensions() const noexcept { return kEmbeddingDims; }
};

class HashEmbedder final : public IEmbedder
{
  public:
    [[nodiscard]] std::expected<std::vector<float>, EngineError> embed(std::string_view text) const override;
};

[[nodiscard]] std::expected<std::unique_ptr<IEmbedder>, EngineError>
makeEmbedder(const std::filesystem::path &modelPath, const std::filesystem::path &vocabPath);

} // namespace mach_core

#endif // MACH_CORE_EMBEDDER_HPP
