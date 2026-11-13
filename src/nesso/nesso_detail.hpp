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
#include <cstdio>
#include <expected>
#include <filesystem>
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

    void update(size_t completed, size_t total)
    {
        if (!enabled_)
        {
            return;
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started_;
        const double linesPerSecond = elapsed.count() > 0.0 ? static_cast<double>(completed) / elapsed.count() : 0.0;
        std::print(stderr, "\rEmbedded {}/{} lines ({:.0f} lines/s)", completed, total, linesPerSecond);
        static_cast<void>(std::fflush(stderr));
    }

    void finish() const
    {
        if (enabled_)
        {
            std::println(stderr);
        }
    }

  private:
    bool enabled_;
    std::chrono::steady_clock::time_point started_;
};

struct LineCounts
{
    size_t skipped = 0;
    size_t emptyLines = 0;
    size_t malformedLines = 0;
    size_t missingFieldLines = 0;
    size_t truncated = 0;
    size_t binaryEntries = 0;
    std::string jsonField = "message";
};

inline bool noKeptLines(const LineCounts &counts) { return counts.skipped == 0 && counts.binaryEntries == 0; }

struct Scan
{
    size_t chunks = 0;
    LineCounts counts{};
    // Stdin cannot be rewound. The count pass keeps it for the embedding pass.
    std::vector<nesso::parser::ParsedChunk> stdinChunks;
    std::vector<nesso::core::SourceInfo> sources;
    std::vector<BundleSkipSummary> bundleSkips;
};

enum class EffectiveMode : uint8_t
{
    Dense,
    Hybrid,
    Lexical
};

[[nodiscard]] Error makeError(ErrorKind kind, ErrorCause cause, std::filesystem::path path, LineCounts counts,
                              std::vector<BundleSkipSummary> bundleSkips = {});

[[nodiscard]] Error parseFailure(nesso::parser::ParseError code, std::filesystem::path path, const LineCounts &counts,
                                 const nesso::parser::ArchiveLimits &limits, std::string_view encryptedEntry = {},
                                 std::vector<BundleSkipSummary> bundleSkips = {});

[[nodiscard]] bool sameSource(const nesso::core::SourceInfo &left, const nesso::core::SourceInfo &right);

[[nodiscard]] std::expected<Scan, Error> scanFiles(std::span<const std::filesystem::path> files,
                                                   std::string_view jsonField,
                                                   const nesso::parser::ArchiveLimits &limits, bool forIndex = false);

[[nodiscard]] std::expected<std::vector<std::filesystem::path>, Error>
expandRequestFiles(std::span<const std::filesystem::path> files);

[[nodiscard]] size_t streamBatch();

struct EmbedProgressSpan
{
    size_t linesBefore = 0;
    size_t linesInFlush = 0;
    size_t total = 0;
};

[[nodiscard]] std::expected<std::vector<std::vector<float>>, Error>
embedDistinct(std::span<const std::string> texts, const nesso::embed::Embedder &embedder, ProgressReporter &progress,
              const EmbedProgressSpan &span, const LineCounts &counts);

// NOLINTNEXTLINE(readability-identifier-naming)
[[nodiscard]] std::expected<nesso::core::CorpusPolicy, Error> MakeCorpusPolicy(const nesso::embed::Embedder &embedder,
                                                                               const LineCounts &counts);

[[nodiscard]] std::string_view effectiveModeName(EffectiveMode mode) noexcept;

[[nodiscard]] EffectiveMode resolveEffectiveMode(SearchMode requested, nesso::core::QueryClass queryClass) noexcept;

[[nodiscard]] std::vector<std::string_view> queryTermViews(const std::vector<std::string> &terms);

[[nodiscard]] bool betterHit(float leftScore, uint64_t leftIndex, float rightScore, uint64_t rightIndex) noexcept;

template <typename E>
[[nodiscard]] std::unexpected<Error> textFailure(ErrorKind kind, E cause, const LineCounts &counts)
{
    return std::unexpected(makeError(kind, causeOf(cause), {}, counts));
}

} // namespace nesso::detail

#endif // NESSO_NESSO_DETAIL_HPP
