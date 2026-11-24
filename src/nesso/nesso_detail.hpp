// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_NESSO_DETAIL_HPP
#define NESSO_NESSO_DETAIL_HPP

#include <nesso/nesso.hpp>

#include <nesso/error_map.hpp>

#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/query_class.hpp>
#include <embed/embedder.hpp>
#include <parser/archive_source.hpp>
#include <parser/log_chunker.hpp>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace nesso::detail
{

inline constexpr size_t MAX_LINE_LENGTH = 4096;

class ProgressReporter
{
  public:
    explicit ProgressReporter(bool requested)
        : enabled_(requested && ::isatty(STDERR_FILENO) != 0), started_(std::chrono::steady_clock::now())
    {
    }

    void Update(size_t completed, size_t total)
    {
        if (!enabled_)
        {
            return;
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started_;
        const double lines_per_second = elapsed.count() > 0.0 ? static_cast<double>(completed) / elapsed.count() : 0.0;
        last_ = std::format("Embedded {}/{} lines ({:.0f} lines/s)", completed, total, lines_per_second);
        Redraw();
    }

    /// Prints the last progress text again on the current terminal row.
    void Redraw() const noexcept
    {
        if (!enabled_ || last_.empty())
        {
            return;
        }
        static_cast<void>(std::fputc('\r', stderr));
        static_cast<void>(std::fputs(last_.c_str(), stderr));
        static_cast<void>(std::fflush(stderr));
    }

    void Finish() const
    {
        if (enabled_)
        {
            std::println(stderr);
        }
    }

  private:
    bool enabled_;
    std::chrono::steady_clock::time_point started_;
    std::string last_;
};

struct LineCounts
{
    size_t skipped = 0;
    size_t empty_lines = 0;
    size_t malformed_lines = 0;
    size_t missing_field_lines = 0;
    size_t truncated = 0;
    size_t binary_entries = 0;
    std::string json_field = "message";
};

inline auto NoKeptLines(const LineCounts &counts) -> bool { return counts.skipped == 0 && counts.binary_entries == 0; }

struct Scan
{
    size_t chunks = 0;
    LineCounts counts{};
    // Stdin cannot be rewound. The count pass keeps it for the embedding pass.
    // The sink overload of `ScanFiles` leaves it empty.
    std::vector<nesso::parser::ParsedChunk> stdin_chunks;
    std::vector<nesso::core::SourceInfo> sources;
    std::vector<BundleSkipSummary> bundle_skips;
};

enum class EffectiveMode : uint8_t
{
    Dense,
    Hybrid,
    Lexical
};

[[nodiscard]] auto MakeLineCounts(const nesso::parser::ParseStats &stats, std::string json_field) -> LineCounts;

/// Appends the archive member skips `path` added between `before` and `after`
/// to `scan.bundle_skips`. Plain files and bundles with no skip add nothing.
void RecordBundleSkips(Scan &scan, const std::filesystem::path &path, const nesso::parser::ParseStats &before,
                       const nesso::parser::ParseStats &after);

[[nodiscard]] auto MakeError(ErrorKind kind, ErrorCause cause, std::filesystem::path path, LineCounts counts,
                             std::vector<BundleSkipSummary> bundle_skips = {}) -> Error;

[[nodiscard]] auto ParseFailure(nesso::parser::ParseError code, std::filesystem::path path, const LineCounts &counts,
                                const nesso::parser::ArchiveLimits &limits, std::string_view encrypted_entry = {},
                                std::vector<BundleSkipSummary> bundle_skips = {}) -> Error;

[[nodiscard]] auto SameSource(const nesso::core::SourceInfo &left, const nesso::core::SourceInfo &right) -> bool;

[[nodiscard]] auto ScanFiles(std::span<const std::filesystem::path> files, std::string_view json_field,
                             const nesso::parser::ArchiveLimits &limits, bool for_index = false)
    -> std::expected<Scan, Error>;

/// Receives the read pass of the sink overload of `ScanFiles`, in input order.
class ScanSink
{
  public:
    ScanSink() = default;
    ScanSink(const ScanSink &) = delete;
    auto operator=(const ScanSink &) -> ScanSink & = delete;
    ScanSink(ScanSink &&) = delete;
    auto operator=(ScanSink &&) -> ScanSink & = delete;
    virtual ~ScanSink() = default;

    /// Called for each path in `files`, a repeated `-` included, before its
    /// chunks. `index` is the path's position in `files`.
    virtual void BeginFile(std::size_t index, const std::filesystem::path &path) = 0;

    /// Called for each kept chunk of the path last begun.
    virtual void Chunk(nesso::parser::ParsedChunk chunk) = 0;
};

/// Same statistics, skips, and errors as the overload above without
/// `for_index`, in one pass: each chunk goes to `sink`, and stdin is not kept.
[[nodiscard]] auto ScanFiles(std::span<const std::filesystem::path> files, std::string_view json_field,
                             const nesso::parser::ArchiveLimits &limits, ScanSink &sink) -> std::expected<Scan, Error>;

[[nodiscard]] auto ExpandRequestFiles(std::span<const std::filesystem::path> files)
    -> std::expected<std::vector<std::filesystem::path>, Error>;

[[nodiscard]] auto StreamBatch() -> size_t;

struct EmbedProgressSpan
{
    size_t lines_before = 0;
    size_t lines_in_flush = 0;
    size_t total = 0;
};

/// Embeds the message of each line in `texts`, without its header
/// (`parser::SplitLogLine`). `index` and `grep` both embed through here, so a
/// line gets the same vector from either command. Queries do not pass here.
[[nodiscard]] auto EmbedDistinct(std::span<const std::string> texts, const nesso::embed::Embedder &embedder,
                                 ProgressReporter &progress, const EmbedProgressSpan &span, const LineCounts &counts)
    -> std::expected<std::vector<std::vector<float>>, Error>;

[[nodiscard]] auto MakeCorpusPolicy(const nesso::embed::Embedder &embedder, const LineCounts &counts)
    -> std::expected<nesso::core::CorpusPolicy, Error>;

[[nodiscard]] auto EffectiveModeName(EffectiveMode mode) noexcept -> std::string_view;

[[nodiscard]] auto ResolveEffectiveMode(SearchMode requested, nesso::core::QueryClass query_class) noexcept
    -> EffectiveMode;

[[nodiscard]] auto QueryTermViews(const std::vector<std::string> &terms) -> std::vector<std::string_view>;

[[nodiscard]] auto BetterHit(float left_score, uint64_t left_index, float right_score, uint64_t right_index) noexcept
    -> bool;

template <typename E>
[[nodiscard]] auto TextFailure(ErrorKind kind, E cause, const LineCounts &counts) -> std::unexpected<Error>
{
    return std::unexpected(MakeError(kind, CauseOf(cause), {}, counts));
}

} // namespace nesso::detail

#endif // NESSO_NESSO_DETAIL_HPP
