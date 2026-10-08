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
#include <string_view>
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
    CorpusLoad
};

/// Layer failure carried by `Error`. `None` is the no-match outcome.
enum class ErrorCause : uint8_t
{
    None,
    FileOpen,
    UnsupportedFormat,
    VocabLoad,
    Tokenization,
    ModelLoad,
    Inference,
    InvalidInput,
    MismatchedDimensions,
    EmptyEmbedding,
    CorruptFile
};

/// Failure of a Nesso operation.
///
/// `cause` names the layer failure. `path` and `skippedLines` are set when the failure concerns a file or happens
/// after parsing.
struct Error
{
    ErrorKind kind{};
    ErrorCause cause = ErrorCause::None;
    std::filesystem::path path;
    size_t skippedLines = 0;
};

/// Phrase for `cause`, used after the operation sentence.
[[nodiscard]] std::string_view describe(ErrorCause cause);

/// One stderr line for `error`. Empty when the outcome is no match.
[[nodiscard]] std::string message(const Error &error);

inline constexpr size_t DEFAULT_TEXT_TOP_K = 5;

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

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nesso

#endif // NESSO_NESSO_HPP
