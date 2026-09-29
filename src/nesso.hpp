// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_NESSO_HPP
#define NESSO_NESSO_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nesso
{

enum class ErrorKind : uint8_t
{
    EmbedderLoad,
    Parse,
    Embed,
    QueryEmbed,
    IndexBuild,
    Search,
    EmptyInput,
    NoMatches,
    CorpusRead,
    CorpusWrite,
    CorpusLoad,
    StoreInit,
    StoreOpen,
    StoreMissing,
    StoreSearch,
    InputOpen,
    QuerySize,
    QueryRead
};

/// Failure of a Nesso operation.
///
/// `code` is the underlying layer's error enum value. `path`, `skippedLines`, and `dimensions` are set when the
/// failure concerns a file, happens after parsing, or depends on a vector size.
struct Error
{
    ErrorKind kind{};
    int code = 0;
    std::filesystem::path path;
    size_t skippedLines = 0;
    uint64_t dimensions = 0;
};

inline constexpr size_t DEFAULT_TEXT_TOP_K = 5;
inline constexpr size_t DEFAULT_STORE_TOP_K = 10;
inline constexpr uint64_t DEFAULT_STORE_DIMENSIONS = 128;

struct GrepRequest
{
    std::string query;
    std::vector<std::filesystem::path> files;
    size_t topK = DEFAULT_TEXT_TOP_K;
};

struct IndexRequest
{
    std::vector<std::filesystem::path> files;
    std::filesystem::path output;
};

struct SearchRequest
{
    std::string query;
    std::filesystem::path index;
    size_t topK = DEFAULT_TEXT_TOP_K;
};

struct StoreInitRequest
{
    std::filesystem::path db;
    uint64_t dimensions = DEFAULT_STORE_DIMENSIONS;
};

struct StoreIngestRequest
{
    std::filesystem::path db;
    uint64_t dimensions = DEFAULT_STORE_DIMENSIONS;
    std::filesystem::path input;
};

struct StoreSearchRequest
{
    std::filesystem::path db;
    uint64_t dimensions = DEFAULT_STORE_DIMENSIONS;
    std::filesystem::path query;
    size_t topK = DEFAULT_STORE_TOP_K;
};

struct TextMatch
{
    std::string source;
    uint64_t line = 0;
    float score = 0.0F;
    std::string text;
};

struct TextResults
{
    std::vector<TextMatch> matches;
    size_t skippedLines = 0;
    bool multipleSources = false;
};

struct IndexSummary
{
    size_t chunks = 0;
    size_t skippedLines = 0;
};

struct StoreInitSummary
{
    uint64_t dimensions = 0;
};

struct StoreIngestSummary
{
    uint64_t before = 0;
    size_t added = 0;
    uint64_t total = 0;
    std::optional<int> appendFailureCode;
};

struct VectorMatch
{
    uint64_t index = 0;
    float score = 0.0F;
};

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
