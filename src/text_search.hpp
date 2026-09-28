// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_TEXT_SEARCH_HPP
#define NESSO_TEXT_SEARCH_HPP

#include <core/corpus_index.hpp>
#include <core/embedding_store.hpp>

#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace embed
{
class OnnxEmbedder;
} // namespace embed

namespace nesso::commands
{

[[nodiscard]] bool multipleSources(std::span<const core::CorpusChunk> chunks);

[[nodiscard]] std::expected<std::vector<core::CorpusChunk>, int>
parseAndEmbed(std::span<const std::filesystem::path> files, const embed::OnnxEmbedder &embedder);

[[nodiscard]] int embedQueryAndPrint(const core::EmbeddingStore<float> &store, const embed::OnnxEmbedder &embedder,
                                     std::string_view query, size_t topK, bool printSource);

} // namespace nesso::commands

#endif // NESSO_TEXT_SEARCH_HPP
