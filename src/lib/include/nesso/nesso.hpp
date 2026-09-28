// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_NESSO_HPP
#define NESSO_NESSO_HPP

#include "error.hpp"
#include "types.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace nesso
{

/// Settings shared by every operation on one Nesso instance.
struct Config
{
    /// Directory holding model.onnx and vocab.txt. When empty, resolveDefaultModelDir() picks it.
    std::optional<std::filesystem::path> modelDir;
};

/// Single entry point of libnesso. Operations never print; they return a value or an Error.
///
/// The embedding model is loaded on the first text operation and reused afterwards. Store operations do not load it.
/// Not thread-safe: use one instance per thread.
class Nesso
{
  public:
    explicit Nesso(Config config);
    ~Nesso();
    Nesso(const Nesso &) = delete;
    Nesso &operator=(const Nesso &) = delete;
    Nesso(Nesso &&) noexcept;
    Nesso &operator=(Nesso &&) noexcept;

    /// Embeds the given files in memory and ranks their chunks against the query.
    /// Fails with EmptyInput when no file yields a chunk, and NoMatches when nothing ranks.
    [[nodiscard]] std::expected<TextResults, Error> grep(const GrepRequest &request) const;

    /// Embeds the given files and writes text plus embeddings to a corpus file. An empty corpus is a success.
    [[nodiscard]] std::expected<IndexSummary, Error> index(const IndexRequest &request) const;

    /// Ranks a corpus file written by index() against the query.
    /// Fails with NoMatches for an empty corpus or a top-k of zero, before loading the model.
    [[nodiscard]] std::expected<TextResults, Error> search(const SearchRequest &request) const;

    /// Creates the raw vector store, or opens it when the dimensions match.
    [[nodiscard]] std::expected<StoreInitSummary, Error> storeInit(const StoreInitRequest &request) const;

    /// Appends every whole float32 record from the input file. A failed append stops ingestion and is reported in
    /// the summary, not as an Error.
    [[nodiscard]] std::expected<StoreIngestSummary, Error> storeIngest(const StoreIngestRequest &request) const;

    /// Returns the top-k stored vectors by cosine similarity to the single float32 vector in the query file.
    [[nodiscard]] std::expected<std::vector<VectorMatch>, Error> storeSearch(const StoreSearchRequest &request) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nesso

#endif // NESSO_NESSO_HPP
