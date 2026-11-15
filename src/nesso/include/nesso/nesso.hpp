// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_NESSO_HPP
#define NESSO_NESSO_HPP

#include <parser/archive_source.hpp>

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
    CorruptGzip,
    CorruptArchive,
    EncryptedArchive,
    ArchiveLimit,
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
    OutputIsInput,
    NoLexicalPostings,
    LexicalTermsMismatch
};

/// Ranking mode requested on `grep` / `search` (`--mode`).
enum class SearchMode : uint8_t
{
    Auto,
    Hybrid,
    Dense,
    Lexical
};

struct StaleSource
{
    std::string path;
    bool missing = false;
    bool unreadable = false;
};

/// Per-bundle archive members skipped without ranking (directories, links,
/// binary).
struct BundleSkipSummary
{
    std::string path;
    size_t directories = 0;
    size_t symlinks = 0;
    size_t hardlinks = 0;
    size_t other = 0;
    size_t binary = 0;
};

[[nodiscard]] inline size_t BundleSkipTotal(const BundleSkipSummary &summary)
{
    return summary.directories + summary.symlinks + summary.hardlinks + summary.other + summary.binary;
}

/// One stderr line for a bundle skip summary. Empty when
/// `BundleSkipTotal(summary)` is 0.
[[nodiscard]] std::string FormatBundleSkipSummary(const BundleSkipSummary &summary);

/// Failure of a Nesso operation.
///
/// `cause` names the layer failure. `path`, the skip counters, and
/// `truncatedLines` are set when the failure concerns a file or happens after
/// parsing. `note` is an extra sentence, used when the model directories were
/// listed. On `EmptyInput` it is the zero-chunk `index` check hint.
struct Error
{
    ErrorKind kind{};
    ErrorCause cause = ErrorCause::None;
    std::filesystem::path path;
    size_t skipped_lines = 0;
    size_t empty_lines = 0;
    size_t malformed_lines = 0;
    size_t missing_field_lines = 0;
    size_t truncated_lines = 0;
    size_t binary_entries = 0;
    std::string json_field = "message";
    std::string note;
    std::vector<StaleSource> stale_sources;
    std::vector<BundleSkipSummary> bundle_skips;
    bool policy_mismatch = false;
    std::optional<std::uint32_t> corpus_version;
};

/// Phrase for `cause`, used after the operation sentence.
[[nodiscard]] std::string_view Describe(ErrorCause cause);

/// One stderr line for `error`. Empty when the outcome is no match.
/// `EmptyInput` is a reason, without an "Error:" prefix. A non-empty `note` on
/// `EmptyInput` is the zero-chunk `index` line.
[[nodiscard]] std::string Message(const Error &error);

inline constexpr size_t DEFAULT_TEXT_TOP_K = 5;

struct GrepRequest
{
    std::string query;
    std::vector<std::filesystem::path> files;
    size_t top_k = DEFAULT_TEXT_TOP_K;
    std::string json_field = "message";
    bool progress = false;
    bool json = false;
    SearchMode mode = SearchMode::Auto;
    /// One row per masked dedup key. `-k` then counts groups.
    bool group = false;
    /// Caps for tar and zip bundles. Plain files ignore them.
    nesso::parser::ArchiveLimits archive_limits{};
};

struct IndexRequest
{
    std::vector<std::filesystem::path> files;
    std::filesystem::path output;
    std::string json_field = "message";
    bool progress = false;
    /// Caps for tar and zip bundles. Plain files ignore them.
    nesso::parser::ArchiveLimits archive_limits{};
};

struct SearchRequest
{
    std::string query;
    std::filesystem::path index;
    size_t top_k = DEFAULT_TEXT_TOP_K;
    bool json = false;
    SearchMode mode = SearchMode::Auto;
    /// One row per masked dedup key. `-k` then counts groups.
    bool group = false;
};

struct TextMatch
{
    std::string source;
    uint64_t line = 0;
    float score = 0.0F;
    std::string text;
    bool array_element = false;
    uint32_t occurrences = 1;
    /// Effective mode after routing: `dense`, `hybrid`, or `lexical`.
    std::string mode = "dense";
    std::optional<float> dense;
    std::optional<float> lexical;
};

struct TextResults
{
    std::vector<TextMatch> matches;
    size_t skipped_lines = 0;
    size_t empty_lines = 0;
    size_t malformed_lines = 0;
    size_t missing_field_lines = 0;
    size_t truncated_lines = 0;
    size_t binary_entries = 0;
    std::string json_field = "message";
    bool multiple_sources = false;
    std::vector<StaleSource> stale_sources;
    std::vector<BundleSkipSummary> bundle_skips;
    bool policy_mismatch = false;
    /// When true, CLI prints the lexical→hybrid fallback warning.
    bool lexical_fallback = false;
    /// When true, each match is one masked dedup key and text output prints
    /// `occurrences`.
    bool grouped = false;
};

struct IndexSummary
{
    size_t chunks = 0;
    size_t skipped_lines = 0;
    size_t empty_lines = 0;
    size_t malformed_lines = 0;
    size_t missing_field_lines = 0;
    size_t truncated_lines = 0;
    size_t binary_entries = 0;
    std::string json_field = "message";
    std::vector<BundleSkipSummary> bundle_skips;
};

struct Config
{
    std::optional<std::filesystem::path> model_dir;
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

    [[nodiscard]] std::expected<TextResults, Error> Grep(const GrepRequest &input) const;
    [[nodiscard]] std::expected<IndexSummary, Error> Index(const IndexRequest &input) const;
    [[nodiscard]] std::expected<TextResults, Error> Search(const SearchRequest &request) const;

  private:
    [[nodiscard]] std::expected<const embed::Embedder *, Error> Embedder() const;

    Config config_;
    // Created on first use. const operations share that cache.
    mutable std::unique_ptr<embed::Embedder> embedder_;
};

} // namespace nesso

#endif // NESSO_NESSO_HPP
