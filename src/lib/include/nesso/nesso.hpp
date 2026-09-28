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

struct Config
{
    std::optional<std::filesystem::path> modelDir;
};

class Nesso
{
  public:
    explicit Nesso(Config config);
    ~Nesso();
    Nesso(const Nesso &) = delete;
    Nesso &operator=(const Nesso &) = delete;
    Nesso(Nesso &&) noexcept;
    Nesso &operator=(Nesso &&) noexcept;

    [[nodiscard]] std::expected<TextResults, Error> grep(const GrepRequest &request) const;
    [[nodiscard]] std::expected<IndexSummary, Error> index(const IndexRequest &request) const;
    [[nodiscard]] std::expected<TextResults, Error> search(const SearchRequest &request) const;

    [[nodiscard]] std::expected<StoreInitSummary, Error> storeInit(const StoreInitRequest &request) const;
    [[nodiscard]] std::expected<StoreIngestSummary, Error> storeIngest(const StoreIngestRequest &request) const;
    [[nodiscard]] std::expected<std::vector<VectorMatch>, Error> storeSearch(const StoreSearchRequest &request) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nesso

#endif // NESSO_NESSO_HPP
