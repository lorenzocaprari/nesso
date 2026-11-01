// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

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
    BinaryFile,
    VocabLoad,
    Tokenization,
    ModelLoad,
    Inference,
    InvalidInput,
    MismatchedDimensions,
    EmptyEmbedding,
    CorruptFile,
    UnsupportedCorpusVersion,
    ModelMismatch,
    SourceChanged,
    EmptyFile,
    NothingIndexable,
    EmptyCorpus,
    OutputIsInput
};

struct StaleSource
{
    std::string path;
    bool missing = false;
    bool unreadable = false;
};

/// Failure of a Nesso operation.
///
/// `cause` names the layer failure. `path`, the skip counters, and `truncatedLines` are set when the failure concerns
/// a file or happens after parsing. `note` is an extra sentence, used when the model directories were listed. On
/// `EmptyInput` it is the zero-chunk `index` check hint.
struct Error
{
    ErrorKind kind{};
    ErrorCause cause = ErrorCause::None;
    std::filesystem::path path;
    size_t skippedLines = 0;
    size_t emptyLines = 0;
    size_t malformedLines = 0;
    size_t missingFieldLines = 0;
    size_t truncatedLines = 0;
    std::string jsonField = "message";
    std::string note;
    std::vector<StaleSource> staleSources;
    bool policyMismatch = false;
    std::optional<std::uint32_t> corpusVersion;
};

/// Phrase for `cause`, used after the operation sentence.
[[nodiscard]] std::string_view describe(ErrorCause cause);

/// One stderr line for `error`. Empty when the outcome is no match. `EmptyInput` is a reason, without an "Error:"
/// prefix. A non-empty `note` on `EmptyInput` is the zero-chunk `index` line.
[[nodiscard]] std::string message(const Error &error);

inline constexpr size_t DEFAULT_TEXT_TOP_K = 5;

struct GrepRequest
{
    std::string query;
    std::vector<std::filesystem::path> files;
    size_t topK = DEFAULT_TEXT_TOP_K;
    std::string jsonField = "message";
    bool progress = false;
    bool json = false;
};

struct IndexRequest
{
    std::vector<std::filesystem::path> files;
    std::filesystem::path output;
    std::string jsonField = "message";
    bool progress = false;
};

struct SearchRequest
{
    std::string query;
    std::filesystem::path index;
    size_t topK = DEFAULT_TEXT_TOP_K;
    bool json = false;
};

struct TextMatch
{
    std::string source;
    uint64_t line = 0;
    float score = 0.0F;
    std::string text;
    bool arrayElement = false;
    uint32_t occurrences = 1;
};

struct TextResults
{
    std::vector<TextMatch> matches;
    size_t skippedLines = 0;
    size_t emptyLines = 0;
    size_t malformedLines = 0;
    size_t missingFieldLines = 0;
    size_t truncatedLines = 0;
    std::string jsonField = "message";
    bool multipleSources = false;
    std::vector<StaleSource> staleSources;
    bool policyMismatch = false;
};

struct IndexSummary
{
    size_t chunks = 0;
    size_t skippedLines = 0;
    size_t emptyLines = 0;
    size_t malformedLines = 0;
    size_t missingFieldLines = 0;
    size_t truncatedLines = 0;
    std::string jsonField = "message";
};

struct Config
{
    std::optional<std::filesystem::path> modelDir;
    // 0 selects the embedder default. A positive value is the CPU budget from
    // --threads or NESSO_THREADS. For int8, a budget of 4 or less is that many
    // single-thread sessions on x86. aarch64 uses at least two intra-op threads.
    size_t threads = 0;
};

namespace embed
{
class Embedder;
} // namespace embed

class Nesso
{
  public:
    explicit Nesso(Config config);
    /// Uses `embedder` and does not load a model from `config`.
    Nesso(Config config, std::unique_ptr<embed::Embedder> embedder);
    ~Nesso();
    Nesso(const Nesso &) = delete;
    Nesso &operator=(const Nesso &) = delete;
    Nesso(Nesso &&) noexcept;
    Nesso &operator=(Nesso &&) noexcept;

    [[nodiscard]] std::expected<TextResults, Error> grep(const GrepRequest &request) const;
    [[nodiscard]] std::expected<IndexSummary, Error> index(const IndexRequest &request) const;
    [[nodiscard]] std::expected<TextResults, Error> search(const SearchRequest &request) const;

  private:
    [[nodiscard]] std::expected<const embed::Embedder *, Error> embedder() const;

    Config config_;
    // Created on first use. const operations share that cache.
    mutable std::unique_ptr<embed::Embedder> embedder_;
};

} // namespace nesso

#endif // NESSO_NESSO_HPP
