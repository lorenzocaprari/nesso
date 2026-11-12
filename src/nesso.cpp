// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso.hpp>

#include <error_map.hpp>
#include <model_paths.hpp>
#include <trace.hpp>

#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/dedup.hpp>
#include <core/distance.hpp>
#include <core/lexical_terms.hpp>
#include <core/query_class.hpp>
#include <core/score_fusion.hpp>
#include <core/sha256.hpp>
#include <core/top_k.hpp>
#include <embed/batch_plan.hpp>
#include <embed/embedder.hpp>
#include <embed/onnx_embedder.hpp>
#include <parser/archive_source.hpp>
#include <parser/input_walk.hpp>
#include <parser/log_chunker.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nesso
{

static constexpr size_t MAX_LINE_LENGTH = 4096;

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

ErrorCause causeOf(nesso::parser::ParseError error)
{
    switch (error)
    {
    case nesso::parser::ParseError::FileOpenFailure:
        return ErrorCause::FileOpen;
    case nesso::parser::ParseError::UnsupportedFormat:
        return ErrorCause::UnsupportedFormat;
    case nesso::parser::ParseError::BinaryFile:
        return ErrorCause::BinaryFile;
    case nesso::parser::ParseError::CorruptGzip:
        return ErrorCause::CorruptGzip;
    case nesso::parser::ParseError::CorruptArchive:
        return ErrorCause::CorruptArchive;
    case nesso::parser::ParseError::EncryptedArchive:
        return ErrorCause::EncryptedArchive;
    case nesso::parser::ParseError::ArchiveEntryLimit:
    case nesso::parser::ParseError::ArchiveTotalLimit:
    case nesso::parser::ParseError::ArchiveCountLimit:
    case nesso::parser::ParseError::ArchiveRatioLimit:
        return ErrorCause::ArchiveLimit;
    }
    std::unreachable();
}

ErrorCause causeOf(nesso::embed::EmbedError error)
{
    switch (error)
    {
    case nesso::embed::EmbedError::VocabLoadFailure:
        return ErrorCause::VocabLoad;
    case nesso::embed::EmbedError::TokenizationFailure:
        return ErrorCause::Tokenization;
    case nesso::embed::EmbedError::ModelLoadFailure:
        return ErrorCause::ModelLoad;
    case nesso::embed::EmbedError::InferenceFailure:
        return ErrorCause::Inference;
    case nesso::embed::EmbedError::InvalidInput:
        return ErrorCause::InvalidInput;
    }
    std::unreachable();
}

ErrorCause causeOf(nesso::core::EngineError error)
{
    switch (error)
    {
    case nesso::core::EngineError::FileOpenFailure:
        return ErrorCause::FileOpen;
    case nesso::core::EngineError::MismatchedDimensions:
        return ErrorCause::MismatchedDimensions;
    case nesso::core::EngineError::DatabaseNotInitialized:
        return ErrorCause::EmptyEmbedding;
    case nesso::core::EngineError::CorruptDatabase:
        return ErrorCause::CorruptFile;
    case nesso::core::EngineError::UnsupportedVersion:
        return ErrorCause::UnsupportedCorpusVersion;
    }
    std::unreachable();
}

std::string_view describe(ErrorCause cause)
{
    switch (cause)
    {
    case ErrorCause::None:
        return {};
    case ErrorCause::FileOpen:
        return "could not open the file";
    case ErrorCause::UnsupportedFormat:
        return "unsupported format";
    case ErrorCause::BinaryFile:
        return "the file is binary";
    case ErrorCause::CorruptGzip:
        return "the gzip stream is truncated or corrupt";
    case ErrorCause::CorruptArchive:
        return "the archive is truncated or corrupt";
    case ErrorCause::EncryptedArchive:
        return "the archive is encrypted; decrypt it first";
    case ErrorCause::ArchiveLimit:
        return "an archive limit was exceeded";
    case ErrorCause::VocabLoad:
        return "could not load the vocabulary";
    case ErrorCause::Tokenization:
        return "could not tokenize the text";
    case ErrorCause::ModelLoad:
        return "could not load the model";
    case ErrorCause::Inference:
        return "inference failed";
    case ErrorCause::InvalidInput:
        return "the input is invalid";
    case ErrorCause::MismatchedDimensions:
        return "the embedding width does not match";
    case ErrorCause::EmptyEmbedding:
        return "the embedding is empty";
    case ErrorCause::CorruptFile:
        return "the file is corrupt";
    case ErrorCause::UnsupportedCorpusVersion:
        return "the corpus format is not supported by this nesso; re-run nesso index";
    case ErrorCause::ModelMismatch:
        return "the corpus was built with a different model; re-run nesso index";
    case ErrorCause::SourceChanged:
        return "the source changed while indexing; retry";
    case ErrorCause::EmptyFile:
        return "the file is empty";
    case ErrorCause::NothingIndexable:
        return "no indexable lines";
    case ErrorCause::EmptyCorpus:
        return "the corpus is empty";
    case ErrorCause::OutputIsInput:
        return "the output is one of the inputs";
    case ErrorCause::NoLexicalPostings:
        return "corpus has no lexical postings; re-run `nesso index`";
    case ErrorCause::LexicalTermsMismatch:
        return "corpus lexical terms version mismatch; re-run `nesso index`";
    }
    std::unreachable();
}

static std::optional<std::string> messageLexicalSpecial(const Error &error)
{
    if (error.cause == ErrorCause::NoLexicalPostings)
    {
        return "Error: corpus has no lexical postings; re-run `nesso index`";
    }
    if (error.cause == ErrorCause::LexicalTermsMismatch)
    {
        return std::format("Error: corpus lexical terms are version {}, this build uses version {}; "
                           "re-run `nesso index`",
                           error.note.empty() ? "?" : error.note, nesso::core::LEXICAL_TERMS_VERSION);
    }
    return std::nullopt;
}

static std::string messageCorpusRead(const Error &error, std::string_view reason, const std::string &path)
{
    if (const auto special = messageLexicalSpecial(error))
    {
        return *special;
    }
    if (error.cause == ErrorCause::UnsupportedCorpusVersion && error.corpusVersion)
    {
        return std::format("Error: failed to read the corpus '{}': corpus format v{} is not supported; "
                           "re-run nesso index",
                           path, *error.corpusVersion);
    }
    return std::format("Error: failed to read the corpus '{}': {}", path, reason);
}

static std::string messageEmptyInput(const Error &error, std::string_view reason, const std::string &path)
{
    if (!error.note.empty())
    {
        const std::string where = path.empty() ? std::string{} : std::format(" in '{}'", path);
        if (error.cause == ErrorCause::EmptyFile)
        {
            return std::format("no indexable lines{}: {}; {}", where, reason, error.note);
        }
        return std::format("no indexable lines{}; {}", where, error.note);
    }
    if (path.empty())
    {
        return std::format("nothing to search: {}", reason);
    }
    return std::format("nothing to search in '{}': {}", path, reason);
}

std::string message(const Error &error)
{
    const std::string_view reason = describe(error.cause);
    const std::string path = error.path.string();
    switch (error.kind)
    {
    case ErrorKind::EmbedderLoad:
    {
        std::string text = std::format("Error: failed to load the embedder from '{}': {}", path, reason);
        if (!error.note.empty())
        {
            text += ". ";
            text += error.note;
        }
        return text;
    }
    case ErrorKind::Parse:
        if (!error.note.empty())
        {
            return std::format("Error: failed to parse '{}': {}", path, error.note);
        }
        return std::format("Error: failed to parse '{}': {}", path, reason);
    case ErrorKind::Embed:
        return std::format("Error: failed to embed the input: {}", reason);
    case ErrorKind::QueryEmbed:
        return std::format("Error: failed to embed the query: {}", reason);
    case ErrorKind::IndexBuild:
        return std::format("Error: failed to build the index: {}", reason);
    case ErrorKind::Search:
        if (const auto special = messageLexicalSpecial(error))
        {
            return *special;
        }
        return std::format("Error: failed to search: {}", reason);
    case ErrorKind::CorpusRead:
        return messageCorpusRead(error, reason, path);
    case ErrorKind::CorpusWrite:
        return std::format("Error: failed to write the corpus '{}': {}", path, reason);
    case ErrorKind::CorpusLoad:
        return std::format("Error: failed to load the corpus: {}", reason);
    case ErrorKind::EmptyInput:
        return messageEmptyInput(error, reason, path);
    case ErrorKind::NoMatches:
        return {};
    }
    std::unreachable();
}

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

static bool noKeptLines(const LineCounts &counts) { return counts.skipped == 0 && counts.binaryEntries == 0; }

static LineCounts lineCounts(const nesso::parser::ParseStats &stats, std::string jsonField)
{
    return {.skipped = stats.skippedLines,
            .emptyLines = stats.emptyLines,
            .malformedLines = stats.malformedLines,
            .missingFieldLines = stats.missingFieldLines,
            .truncated = stats.truncatedLines,
            .binaryEntries = stats.binaryEntries,
            .jsonField = std::move(jsonField)};
}

static std::string countPhrase(size_t count, std::string_view singular, std::string_view plural)
{
    return std::format("{} {}", count, count == 1 ? singular : plural);
}

static std::string joinAnd(const std::vector<std::string> &parts)
{
    if (parts.size() == 1)
    {
        return parts.front();
    }
    if (parts.size() == 2)
    {
        return parts[0] + " and " + parts[1];
    }
    std::string text = parts[0];
    for (size_t index = 1; index + 1 < parts.size(); ++index)
    {
        text += ", ";
        text += parts[index];
    }
    text += ", and ";
    text += parts.back();
    return text;
}

std::string formatBundleSkipSummary(const BundleSkipSummary &summary)
{
    const size_t total = bundleSkipTotal(summary);
    if (total == 0)
    {
        return {};
    }
    std::vector<std::string> parts;
    if (summary.directories > 0)
    {
        parts.push_back(countPhrase(summary.directories, "directory", "directories"));
    }
    if (summary.symlinks > 0)
    {
        parts.push_back(countPhrase(summary.symlinks, "symlink", "symlinks"));
    }
    if (summary.hardlinks > 0)
    {
        parts.push_back(countPhrase(summary.hardlinks, "hard link", "hard links"));
    }
    if (summary.other > 0)
    {
        parts.push_back(countPhrase(summary.other, "non-regular entry", "non-regular entries"));
    }
    if (summary.binary > 0)
    {
        parts.push_back(countPhrase(summary.binary, "binary file", "binary files"));
    }
    return std::format("Skipped {} in '{}' ({}).", countPhrase(total, "entry", "entries"), summary.path,
                       joinAnd(parts));
}

static void addSourceWarning(std::vector<StaleSource> &warnings, StaleSource warning)
{
    const auto existing =
        std::ranges::find_if(warnings, [&warning](const StaleSource &item) { return item.path == warning.path; });
    if (existing == warnings.end())
    {
        warnings.push_back(std::move(warning));
    }
    else if (warning.unreadable)
    {
        existing->unreadable = true;
    }
}

static std::vector<StaleSource> staleSources(std::span<const nesso::core::SourceInfo> sources)
{
    std::vector<StaleSource> stale;
    for (const nesso::core::SourceInfo &source : sources)
    {
        if (source.storedOnly)
        {
            continue;
        }
        if (!source.tracked)
        {
            addSourceWarning(stale, {.path = source.path, .unreadable = true});
            continue;
        }

        std::error_code error;
        const std::uint64_t size = std::filesystem::file_size(source.path, error);
        if (error)
        {
            addSourceWarning(stale, {.path = source.path, .missing = true});
            continue;
        }
        const auto modified = std::filesystem::last_write_time(source.path, error);
        if (error)
        {
            addSourceWarning(stale, {.path = source.path, .missing = true});
            continue;
        }
        const auto modifiedTime =
            std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count();
        if (size != source.size || modifiedTime != source.modifiedTime)
        {
            addSourceWarning(stale, {.path = source.path});
        }
    }
    return stale;
}

static Error makeError(ErrorKind kind, ErrorCause cause, std::filesystem::path path, LineCounts counts,
                       std::vector<BundleSkipSummary> bundleSkips = {})
{
    return Error{.kind = kind,
                 .cause = cause,
                 .path = std::move(path),
                 .skippedLines = counts.skipped,
                 .emptyLines = counts.emptyLines,
                 .malformedLines = counts.malformedLines,
                 .missingFieldLines = counts.missingFieldLines,
                 .truncatedLines = counts.truncated,
                 .binaryEntries = counts.binaryEntries,
                 .jsonField = std::move(counts.jsonField),
                 .note = {},
                 .staleSources = {},
                 .bundleSkips = std::move(bundleSkips),
                 .policyMismatch = false,
                 .corpusVersion = {}};
}

static std::string archiveLimitNote(nesso::parser::ParseError code, const nesso::parser::ArchiveLimits &limits)
{
    switch (code)
    {
    case nesso::parser::ParseError::ArchiveEntryLimit:
        return std::format("an archive entry is larger than {} bytes; raise the limit with --archive-max-entry-bytes",
                           limits.maxEntryBytes);
    case nesso::parser::ParseError::ArchiveTotalLimit:
        return std::format("the archive expands past {} uncompressed bytes; raise the limit with --archive-max-bytes",
                           limits.maxTotalBytes);
    case nesso::parser::ParseError::ArchiveCountLimit:
        return std::format("the archive entry count exceeds {}; raise the limit with --archive-max-entries",
                           limits.maxEntries);
    case nesso::parser::ParseError::ArchiveRatioLimit:
        return std::format("the archive compression ratio exceeds {}; raise the limit with --archive-max-ratio",
                           limits.maxRatio);
    case nesso::parser::ParseError::FileOpenFailure:
    case nesso::parser::ParseError::UnsupportedFormat:
    case nesso::parser::ParseError::BinaryFile:
    case nesso::parser::ParseError::CorruptGzip:
    case nesso::parser::ParseError::CorruptArchive:
    case nesso::parser::ParseError::EncryptedArchive:
        return {};
    }
    std::unreachable();
}

static std::string encryptedArchiveNote(std::string_view entry)
{
    if (entry.empty())
    {
        return "the archive is an encrypted OpenPGP file; decrypt it first (for example with gpg --decrypt)";
    }
    return std::format("entry '{}' is encrypted; decrypt it first", entry);
}

static Error parseFailure(nesso::parser::ParseError code, std::filesystem::path path, const LineCounts &counts,
                          const nesso::parser::ArchiveLimits &limits, std::string_view encryptedEntry = {},
                          std::vector<BundleSkipSummary> bundleSkips = {})
{
    Error error = makeError(ErrorKind::Parse, causeOf(code), std::move(path), counts, std::move(bundleSkips));
    if (code == nesso::parser::ParseError::EncryptedArchive)
    {
        error.note = encryptedArchiveNote(encryptedEntry);
    }
    else
    {
        error.note = archiveLimitNote(code, limits);
    }
    return error;
}

static std::string modelLoadNote(const std::vector<std::filesystem::path> &tried)
{
    std::string text = "Tried";
    for (size_t index = 0; index < tried.size(); ++index)
    {
        if (index == 0)
        {
            text += ' ';
        }
        else if (index + 1 == tried.size())
        {
            text += " and ";
        }
        else
        {
            text += ", ";
        }
        text += std::format("'{}'", tried[index].string());
    }
    text += ". Run scripts/fetch-model or pass --model-dir.";
    return text;
}

struct Scan
{
    size_t chunks = 0;
    LineCounts counts{};
    // Stdin cannot be rewound. The count pass keeps it for the embedding pass.
    std::vector<nesso::parser::ParsedChunk> stdinChunks;
    std::vector<nesso::core::SourceInfo> sources;
    std::vector<BundleSkipSummary> bundleSkips;
};

static void recordBundleSkips(Scan &scan, const std::filesystem::path &path, const nesso::parser::ParseStats &before,
                              const nesso::parser::ParseStats &after)
{
    if (!nesso::parser::isArchivePath(path))
    {
        return;
    }
    BundleSkipSummary summary{.path = path.string(),
                              .directories = after.archiveDirectories - before.archiveDirectories,
                              .symlinks = after.archiveSymlinks - before.archiveSymlinks,
                              .hardlinks = after.archiveHardlinks - before.archiveHardlinks,
                              .other = after.archiveOtherSkipped - before.archiveOtherSkipped,
                              .binary = after.binaryEntries - before.binaryEntries};
    if (bundleSkipTotal(summary) > 0)
    {
        scan.bundleSkips.push_back(std::move(summary));
    }
}

static bool sameSource(const nesso::core::SourceInfo &left, const nesso::core::SourceInfo &right)
{
    return left.tracked == right.tracked && left.size == right.size && left.modifiedTime == right.modifiedTime;
}

static std::expected<void, Error> scanStdin(Scan &scan, nesso::parser::ParseStats &stats, std::string_view jsonField,
                                            const nesso::parser::ArchiveLimits &limits, bool forIndex)
{
    const std::filesystem::path path{"-"};
    const auto keep = [&scan](nesso::parser::ParsedChunk chunk)
    {
        if (!chunk.skipped)
        {
            ++scan.chunks;
        }
        scan.stdinChunks.push_back(std::move(chunk));
    };
    const auto streamed =
        forIndex ? nesso::parser::Chunker::forEachRecord(path, MAX_LINE_LENGTH, &stats, jsonField, keep, limits)
                 : nesso::parser::Chunker::forEach(path, MAX_LINE_LENGTH, &stats, jsonField, keep, limits);
    if (!streamed)
    {
        return std::unexpected(parseFailure(streamed.error(), path, lineCounts(stats, std::string{jsonField}), limits,
                                            stats.encryptedEntry, scan.bundleSkips));
    }
    if (forIndex)
    {
        const auto after = nesso::core::sourceInfoFor(path, jsonField);
        if (!after || !sameSource(scan.sources.back(), *after))
        {
            return std::unexpected(makeError(ErrorKind::Parse, ErrorCause::SourceChanged, path,
                                             lineCounts(stats, std::string{jsonField}), scan.bundleSkips));
        }
    }
    return {};
}

static std::expected<Scan, Error> scanFiles(std::span<const std::filesystem::path> files, std::string_view jsonField,
                                            const nesso::parser::ArchiveLimits &limits, bool forIndex = false)
{
    const ScopedStage stage{"parse"};
    nesso::parser::ParseStats stats{};
    Scan scan{};
    bool capturedStdin = false;
    for (const std::filesystem::path &path : files)
    {
        if (forIndex)
        {
            auto source = nesso::core::sourceInfoFor(path, jsonField);
            if (!source)
            {
                return std::unexpected(makeError(ErrorKind::Parse, causeOf(source.error()), path,
                                                 lineCounts(stats, std::string{jsonField})));
            }
            scan.sources.push_back(std::move(*source));
        }
        if (path == "-")
        {
            if (capturedStdin)
            {
                continue;
            }
            capturedStdin = true;
            if (const auto scanned = scanStdin(scan, stats, jsonField, limits, forIndex); !scanned)
            {
                return std::unexpected(scanned.error());
            }
            continue;
        }

        const nesso::parser::ParseStats before = stats;
        const auto streamed = nesso::parser::Chunker::forEach(
            path, MAX_LINE_LENGTH, &stats, jsonField, [&scan](nesso::parser::ParsedChunk) { ++scan.chunks; }, limits);
        if (!streamed)
        {
            recordBundleSkips(scan, path, before, stats);
            return std::unexpected(parseFailure(streamed.error(), path, lineCounts(stats, std::string{jsonField}),
                                                limits, stats.encryptedEntry, scan.bundleSkips));
        }
        recordBundleSkips(scan, path, before, stats);
    }
    scan.counts = lineCounts(stats, std::string{jsonField});
    return scan;
}

// Files are parsed again so embeddings stay batched. Stdin is replayed from the count pass.
template <typename Consume>
static std::expected<void, nesso::parser::ParseError>
visitChunks(const std::filesystem::path &path, Scan &scan, bool &stdinUsed, std::string_view jsonField,
            const nesso::parser::ArchiveLimits &limits, const Consume &consume)
{
    if (path == "-")
    {
        if (!stdinUsed)
        {
            stdinUsed = true;
            for (nesso::parser::ParsedChunk &chunk : scan.stdinChunks)
            {
                consume(std::move(chunk));
            }
        }
        return {};
    }
    return nesso::parser::Chunker::forEach(path, MAX_LINE_LENGTH, nullptr, jsonField, consume, limits);
}

// Like visitChunks, but also yields skipped records so v3 line ordinals stay complete.
template <typename Consume>
static std::expected<void, nesso::parser::ParseError>
visitRecords(const std::filesystem::path &path, Scan &scan, bool &stdinUsed, std::string_view jsonField,
             const nesso::parser::ArchiveLimits &limits, const Consume &consume)
{
    if (path == "-")
    {
        if (!stdinUsed)
        {
            stdinUsed = true;
            for (nesso::parser::ParsedChunk &chunk : scan.stdinChunks)
            {
                consume(std::move(chunk));
            }
        }
        return {};
    }
    return nesso::parser::Chunker::forEachRecord(path, MAX_LINE_LENGTH, nullptr, jsonField, consume, limits);
}

template <typename E> static std::unexpected<Error> textFailure(ErrorKind kind, E cause, const LineCounts &counts)
{
    return std::unexpected(makeError(kind, causeOf(cause), {}, counts));
}

// `note` selects the index wording in `message`. `EmptyInput` stays exit 1.
static std::unexpected<Error> nothingToIndex(const IndexRequest &request, const Scan &scanned)
{
    const ErrorCause cause = noKeptLines(scanned.counts) ? ErrorCause::EmptyFile : ErrorCause::NothingIndexable;
    const std::filesystem::path path = request.files.size() == 1 ? request.files.front() : std::filesystem::path{};
    Error error = makeError(ErrorKind::EmptyInput, cause, path, scanned.counts, scanned.bundleSkips);
    error.note = "check the input path or --json-field";
    return std::unexpected(std::move(error));
}

struct FileStamp
{
    std::uintmax_t device = 0;
    std::uintmax_t inode = 0;
    std::intmax_t size = 0;
    std::intmax_t modifiedSeconds = 0;
    std::intmax_t modifiedNanoseconds = 0;
    std::intmax_t changedSeconds = 0;
    std::intmax_t changedNanoseconds = 0;

    friend bool operator==(const FileStamp &, const FileStamp &) = default;
};

static std::optional<FileStamp> fileStamp(const std::filesystem::path &path)
{
    struct stat status{};
    if (::stat(path.c_str(), &status) != 0)
    {
        return std::nullopt;
    }
    return FileStamp{.device = static_cast<std::uintmax_t>(status.st_dev),
                     .inode = static_cast<std::uintmax_t>(status.st_ino),
                     .size = static_cast<std::intmax_t>(status.st_size),
                     .modifiedSeconds = static_cast<std::intmax_t>(status.st_mtim.tv_sec),
                     .modifiedNanoseconds = static_cast<std::intmax_t>(status.st_mtim.tv_nsec),
                     .changedSeconds = static_cast<std::intmax_t>(status.st_ctim.tv_sec),
                     .changedNanoseconds = static_cast<std::intmax_t>(status.st_ctim.tv_nsec)};
}

namespace
{

class OnnxBoundEmbedder final : public nesso::embed::Embedder
{
  public:
    OnnxBoundEmbedder(std::unique_ptr<nesso::embed::OnnxEmbedder> inner, std::filesystem::path modelDir,
                      std::optional<FileStamp> modelStamp, std::optional<FileStamp> vocabStamp)
        : inner_(std::move(inner)), modelPath_(modelDir / "model.onnx"), vocabPath_(modelDir / "vocab.txt"),
          modelStamp_(modelStamp), vocabStamp_(vocabStamp)
    {
    }

    OnnxBoundEmbedder(const OnnxBoundEmbedder &) = delete;
    OnnxBoundEmbedder &operator=(const OnnxBoundEmbedder &) = delete;
    OnnxBoundEmbedder(OnnxBoundEmbedder &&) = delete;
    OnnxBoundEmbedder &operator=(OnnxBoundEmbedder &&) = delete;
    ~OnnxBoundEmbedder() override = default;

    [[nodiscard]] size_t sessions() const noexcept override { return inner_->sessions(); }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        if (identity_)
        {
            return *identity_;
        }
        if (!modelStamp_ || !vocabStamp_ || fileStamp(modelPath_) != modelStamp_ ||
            fileStamp(vocabPath_) != vocabStamp_)
        {
            return std::unexpected(nesso::embed::EmbedError::ModelLoadFailure);
        }
        const auto modelHash = nesso::core::sha256File(modelPath_);
        const auto vocabHash = nesso::core::sha256File(vocabPath_);
        if (!modelHash || !vocabHash || fileStamp(modelPath_) != modelStamp_ || fileStamp(vocabPath_) != vocabStamp_)
        {
            return std::unexpected(nesso::embed::EmbedError::ModelLoadFailure);
        }
        identity_ = nesso::embed::EmbedderIdentity{.modelId = std::string{nesso::embed::MINILM_MODEL_ID},
                                                   .modelSha256 = nesso::core::hexSha256(*modelHash),
                                                   .vocabSha256 = nesso::core::hexSha256(*vocabHash),
                                                   .quantized = inner_->policy().quantized,
                                                   .dimensions = nesso::embed::MINILM_EMBEDDING_DIMENSIONS};
        return *identity_;
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError>
    embed(std::string_view text) const override
    {
        return inner_->embed(text);
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    embedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
    {
        return inner_->embedBatch(texts, std::move(progress));
    }

  private:
    std::unique_ptr<nesso::embed::OnnxEmbedder> inner_;
    std::filesystem::path modelPath_;
    std::filesystem::path vocabPath_;
    std::optional<FileStamp> modelStamp_;
    std::optional<FileStamp> vocabStamp_;
    mutable std::optional<nesso::embed::EmbedderIdentity> identity_;
};

} // namespace

Nesso::Nesso(Config config) : Nesso(std::move(config), nullptr) {}

Nesso::Nesso(Config config, std::unique_ptr<embed::Embedder> embedder)
    : config_(std::move(config)), embedder_(std::move(embedder))
{
}

Nesso::~Nesso() = default;

Nesso::Nesso(Nesso &&) noexcept = default;

Nesso &Nesso::operator=(Nesso &&) noexcept = default;

std::expected<const nesso::embed::Embedder *, Error> Nesso::embedder() const
{
    if (!embedder_)
    {
        std::vector<std::filesystem::path> tried;
        std::filesystem::path modelDir;
        if (config_.modelDir.has_value())
        {
            modelDir = *config_.modelDir;
            tried.push_back(modelDir);
        }
        else
        {
            tried = modelDirCandidates();
            modelDir = resolveDefaultModelDir();
        }
        auto modelStamp = fileStamp(modelDir / "model.onnx");
        auto vocabStamp = fileStamp(modelDir / "vocab.txt");
        auto created = nesso::embed::OnnxEmbedder::create(modelDir, config_.threads);
        if (!created)
        {
            Error error{.kind = ErrorKind::EmbedderLoad,
                        .cause = causeOf(created.error()),
                        .path = modelDir,
                        .note = {},
                        .staleSources = {},
                        .bundleSkips = {},
                        .policyMismatch = false,
                        .corpusVersion = {}};
            error.note = modelLoadNote(tried);
            return std::unexpected(std::move(error));
        }
        if (const std::string warning = nesso::embed::overrideWarning((*created)->policy(), config_.threads);
            !warning.empty())
        {
            std::println(stderr, "{}", warning);
        }
        if (fileStamp(modelDir / "model.onnx") != modelStamp || fileStamp(modelDir / "vocab.txt") != vocabStamp)
        {
            modelStamp.reset();
            vocabStamp.reset();
        }
        embedder_ = std::make_unique<OnnxBoundEmbedder>(std::move(*created), modelDir, modelStamp, vocabStamp);
    }
    return embedder_.get();
}

// Fixed window. Session count must not change which texts share a padded run.
static size_t streamBatch() { return nesso::embed::BATCH_WINDOW; }

static std::expected<std::vector<std::vector<float>>, Error>
embedDistinct(std::span<const std::string> texts, const nesso::embed::Embedder &embedder, ProgressReporter &progress,
              size_t linesBefore, size_t linesInFlush, size_t total, const LineCounts &counts)
{
    const size_t distinct = texts.size();
    auto embeddings = embedder.embedBatch(
        texts, [&progress, linesBefore, linesInFlush, distinct, total](size_t batchCompleted, size_t)
        { progress.update(linesBefore + (linesInFlush * batchCompleted / distinct), total); });
    if (!embeddings)
    {
        return std::unexpected(makeError(ErrorKind::Embed, causeOf(embeddings.error()), {}, counts));
    }
    if (embeddings->size() != distinct)
    {
        return textFailure(ErrorKind::Embed, nesso::core::EngineError::MismatchedDimensions, counts);
    }
    progress.update(linesBefore + linesInFlush, total);
    return std::move(*embeddings);
}

static std::expected<nesso::core::CorpusPolicy, Error> corpusPolicy(const nesso::embed::Embedder &embedder,
                                                                    const LineCounts &counts)
{
    const auto identity = embedder.identity();
    if (!identity)
    {
        Error error = makeError(ErrorKind::EmbedderLoad, causeOf(identity.error()), {}, counts);
        error.note = "Could not fingerprint the loaded model and vocabulary, or they changed; reload them and retry.";
        return std::unexpected(std::move(error));
    }
    return nesso::core::CorpusPolicy{
        {"batch", std::format("window:{},max:{},tokens:{},order:length-then-input", nesso::embed::BATCH_WINDOW,
                              nesso::embed::BATCH_MAX_TEXTS, nesso::embed::BATCH_TOKEN_BUDGET)},
        {"bm25", std::string{nesso::core::BM25_POLICY}},
        {"dedup", "masked-v1"},
        {"dimensions", std::to_string(identity->dimensions)},
        {"lexical.terms", std::string{nesso::core::LEXICAL_TERMS_VERSION}},
        {"mask", "template-v1"},
        {"model.id", identity->modelId},
        {"model.quantization", identity->quantized ? "int8" : "fp32"},
        {"model.sha256", identity->modelSha256},
        {"tokens.max", std::to_string(nesso::embed::DEFAULT_MAX_SEQUENCE_LENGTH)},
        {"truncate", std::format("{}-codepoints", MAX_LINE_LENGTH)},
        {"vocab.sha256", identity->vocabSha256}};
}

enum class EffectiveMode : uint8_t
{
    Dense,
    Hybrid,
    Lexical
};

static std::string_view effectiveModeName(EffectiveMode mode) noexcept
{
    switch (mode)
    {
    case EffectiveMode::Dense:
        return "dense";
    case EffectiveMode::Hybrid:
        return "hybrid";
    case EffectiveMode::Lexical:
        return "lexical";
    }
    std::unreachable();
}

static EffectiveMode resolveEffectiveMode(SearchMode requested, nesso::core::QueryClass queryClass) noexcept
{
    switch (requested)
    {
    case SearchMode::Dense:
        return EffectiveMode::Dense;
    case SearchMode::Hybrid:
        return EffectiveMode::Hybrid;
    case SearchMode::Lexical:
        return EffectiveMode::Lexical;
    case SearchMode::Auto:
        return queryClass == nesso::core::QueryClass::Identifier ? EffectiveMode::Lexical : EffectiveMode::Hybrid;
    }
    std::unreachable();
}

static std::expected<void, Error> requireLexicalReady(nesso::core::LexicalStatus status,
                                                      std::string_view corpusTermsVersion, SearchMode requested)
{
    if (requested == SearchMode::Dense)
    {
        return {};
    }
    if (status == nesso::core::LexicalStatus::Ok)
    {
        return {};
    }
    Error error;
    error.kind = ErrorKind::CorpusRead;
    if (status == nesso::core::LexicalStatus::Missing)
    {
        error.cause = ErrorCause::NoLexicalPostings;
    }
    else
    {
        error.cause = ErrorCause::LexicalTermsMismatch;
        error.note = std::string{corpusTermsVersion.empty() ? "?" : corpusTermsVersion};
    }
    return std::unexpected(std::move(error));
}

static std::vector<std::string_view> queryTermViews(const std::vector<std::string> &terms)
{
    std::vector<std::string_view> views;
    views.reserve(terms.size());
    for (const std::string &term : terms)
    {
        views.push_back(term);
    }
    return views;
}

static std::vector<double> bm25ScoresFromCorpus(const nesso::core::MappedCorpus &corpus,
                                                std::span<const std::string_view> query)
{
    const std::span<const nesso::core::LexicalLinePostings> rows = corpus.lexicalPostings();
    std::vector<nesso::core::Bm25LinePostings> lines;
    lines.reserve(rows.size());
    for (const nesso::core::LexicalLinePostings &row : rows)
    {
        lines.push_back(nesso::core::Bm25LinePostings{.length = row.length, .entries = row.entries});
    }
    return nesso::core::scoresFromPostings(corpus.lexicalDictionary(), lines, query);
}

static bool betterHit(float leftScore, uint64_t leftIndex, float rightScore, uint64_t rightIndex) noexcept
{
    return nesso::core::rankBefore(leftScore, leftIndex, rightScore, rightIndex);
}

static std::expected<std::vector<std::filesystem::path>, Error>
expandRequestFiles(std::span<const std::filesystem::path> files)
{
    auto expanded = nesso::parser::expandInputs(files);
    if (!expanded)
    {
        return std::unexpected(makeError(ErrorKind::Parse, ErrorCause::FileOpen, expanded.error().path, {}));
    }
    return std::move(*expanded);
}

struct GrepWaiting
{
    uint64_t ordinal = 0;
    std::uint32_t distinctId = 0;
    std::uint32_t source = 0;
    uint64_t line = 0;
    bool arrayElement = false;
    std::string text;
};

struct GrepCandidate
{
    uint64_t index = 0;
    float score = 0.0F;
    std::uint32_t distinctId = 0;
    TextMatch match;
};

struct GrepHit
{
    uint64_t ordinal = 0;
    float score = 0.0F;
    float dense = 0.0F;
    float lexical = 0.0F;
    bool hasDense = false;
    bool hasLexical = false;
    bool substring = false;
    std::uint32_t distinctId = 0;
    std::uint32_t source = 0;
    uint64_t line = 0;
    bool arrayElement = false;
    std::string text;
};

struct GrepSourceCursor
{
    bool archive = false;
    std::uint32_t sourceId = 0;
    std::string source;
};

struct GrepEmbedState
{
    const GrepRequest *request = nullptr;
    Scan *scanned = nullptr;
    const std::vector<float> *queryEmbedding = nullptr;
    const nesso::embed::Embedder *model = nullptr;
    bool needLexical = false;
    nesso::core::DistinctIndex distinct;
    std::vector<float> scores;
    std::vector<GrepWaiting> waiting;
    std::vector<GrepWaiting> allLines;
    std::vector<char> groupSeen;
    std::vector<std::string> sources;
    std::vector<GrepCandidate> best;
    ProgressReporter *progress = nullptr;
    size_t linesSeen = 0;
    size_t linesReported = 0;
    uint64_t ordinal = 0;
    bool severalSources = false;
    std::string firstSource;
    size_t batch = 0;
};

struct GrepBm25
{
    std::vector<double> scores;
    EffectiveMode effective = EffectiveMode::Dense;
    bool lexicalFallback = false;
};

static bool betterGrepCandidate(const GrepCandidate &left, const GrepCandidate &right)
{
    return nesso::core::rankBefore(left.score, left.index, right.score, right.index);
}

static bool lexicalHitBefore(const GrepHit &left, const GrepHit &right)
{
    if (left.substring != right.substring)
    {
        return left.substring && !right.substring;
    }
    return betterHit(left.score, left.ordinal, right.score, right.ordinal);
}

static bool scoreHitBefore(const GrepHit &left, const GrepHit &right)
{
    return betterHit(left.score, left.ordinal, right.score, right.ordinal);
}

static void considerDenseGrep(GrepEmbedState &state, const GrepWaiting &item)
{
    if (state.needLexical)
    {
        return;
    }
    if (state.request->group)
    {
        if (state.groupSeen.size() <= item.distinctId)
        {
            state.groupSeen.resize(static_cast<std::size_t>(item.distinctId) + 1);
        }
        if (state.groupSeen[item.distinctId] != 0)
        {
            return;
        }
        state.groupSeen[item.distinctId] = 1;
    }
    TextMatch match{.source = state.sources[item.source],
                    .line = item.line,
                    .score = state.scores[item.distinctId],
                    .text = item.text,
                    .arrayElement = item.arrayElement,
                    .occurrences = 1,
                    .mode = "dense",
                    .dense = std::nullopt,
                    .lexical = std::nullopt};
    state.best.push_back(GrepCandidate{
        .index = item.ordinal, .score = match.score, .distinctId = item.distinctId, .match = std::move(match)});
    if (state.best.size() > state.request->topK)
    {
        std::partial_sort(state.best.begin(), state.best.begin() + static_cast<std::ptrdiff_t>(state.request->topK),
                          state.best.end(), betterGrepCandidate);
        state.best.resize(state.request->topK);
    }
}

static std::expected<void, Error> flushGrepEmbeddings(GrepEmbedState &state)
{
    const size_t linesInFlush = state.linesSeen - state.linesReported;
    const LineCounts &counts = state.scanned->counts;
    if (state.distinct.pending().empty())
    {
        state.linesReported = state.linesSeen;
        state.progress->update(state.linesReported, state.scanned->chunks);
        return {};
    }
    const auto embeddings = embedDistinct(state.distinct.pending(), *state.model, *state.progress, state.linesReported,
                                          linesInFlush, state.scanned->chunks, counts);
    if (!embeddings)
    {
        return std::unexpected(embeddings.error());
    }
    for (const std::vector<float> &embedding : *embeddings)
    {
        const auto score = nesso::core::math::DistanceMetrics::dotProduct<float>(*state.queryEmbedding, embedding);
        if (!score)
        {
            return textFailure(ErrorKind::Search, score.error(), counts);
        }
        state.scores.push_back(*score);
    }
    state.distinct.clearPending();
    state.linesReported = state.linesSeen;
    for (const GrepWaiting &item : state.waiting)
    {
        considerDenseGrep(state, item);
    }
    state.waiting.clear();
    return {};
}

static std::optional<Error> ingestGrepChunk(GrepEmbedState &state, GrepSourceCursor &cursor,
                                            nesso::parser::ParsedChunk chunk)
{
    if (cursor.archive && (cursor.source.empty() || chunk.entryStart || chunk.source != cursor.source))
    {
        state.sources.push_back(chunk.source);
        cursor.sourceId = static_cast<std::uint32_t>(state.sources.size() - 1);
        cursor.source = state.sources.back();
    }
    if (state.firstSource.empty())
    {
        state.firstSource = cursor.source;
    }
    else if (cursor.source != state.firstSource)
    {
        state.severalSources = true;
    }
    const std::uint32_t id = state.distinct.add(chunk.text);
    ++state.linesSeen;
    const GrepWaiting item{.ordinal = state.ordinal,
                           .distinctId = id,
                           .source = cursor.sourceId,
                           .line = chunk.lineNumber,
                           .arrayElement = chunk.arrayElement,
                           .text = std::move(chunk.text)};
    if (state.needLexical)
    {
        state.allLines.push_back(item);
    }
    if (id < state.scores.size())
    {
        considerDenseGrep(state, item);
    }
    else
    {
        state.waiting.push_back(item);
    }
    ++state.ordinal;
    if (state.distinct.pending().size() == state.batch)
    {
        if (const auto flushed = flushGrepEmbeddings(state); !flushed)
        {
            return flushed.error();
        }
    }
    return std::nullopt;
}

static std::expected<void, Error> embedGrepFiles(GrepEmbedState &state)
{
    const ScopedStage stage{"embed"};
    bool stdinUsed = false;
    for (const std::filesystem::path &path : state.request->files)
    {
        GrepSourceCursor cursor{.archive = nesso::parser::isArchivePath(path), .sourceId = 0, .source = {}};
        if (!cursor.archive)
        {
            state.sources.push_back(path.string());
            cursor.sourceId = static_cast<std::uint32_t>(state.sources.size() - 1);
            cursor.source = state.sources.back();
        }
        std::optional<Error> failed;
        const auto streamed =
            visitChunks(path, *state.scanned, stdinUsed, state.request->jsonField, state.request->archiveLimits,
                        [&](nesso::parser::ParsedChunk chunk)
                        {
                            if (failed)
                            {
                                return;
                            }
                            if (std::optional<Error> error = ingestGrepChunk(state, cursor, std::move(chunk)))
                            {
                                failed = std::move(error);
                            }
                        });
        if (!streamed)
        {
            return std::unexpected(
                parseFailure(streamed.error(), path, state.scanned->counts, state.request->archiveLimits));
        }
        if (failed)
        {
            return std::unexpected(*failed);
        }
    }
    if (const auto flushed = flushGrepEmbeddings(state); !flushed)
    {
        return std::unexpected(flushed.error());
    }
    return {};
}

static TextResults denseGrepResults(GrepEmbedState &state, TextResults results)
{
    std::ranges::sort(state.best, betterGrepCandidate);
    results.matches.reserve(state.best.size());
    for (GrepCandidate &candidate : state.best)
    {
        candidate.match.occurrences = state.distinct.occurrences(candidate.distinctId);
        candidate.match.mode = "dense";
        candidate.match.dense = candidate.score;
        results.matches.push_back(std::move(candidate.match));
    }
    return results;
}

static GrepBm25 buildGrepBm25Scores(const std::vector<GrepWaiting> &allLines,
                                    const nesso::core::QueryTokens &queryTokens, EffectiveMode effective)
{
    nesso::core::Bm25Index bm25;
    for (const GrepWaiting &item : allLines)
    {
        const std::vector<std::string> terms = nesso::core::lexicalTerms(item.text);
        std::unordered_map<std::uint32_t, std::uint32_t> termCounts;
        termCounts.reserve(terms.size());
        for (const std::string &term : terms)
        {
            ++termCounts[bm25.internTerm(term)];
        }
        std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
        entries.reserve(termCounts.size());
        for (const auto &[termId, frequency] : termCounts)
        {
            entries.emplace_back(termId, frequency);
        }
        bm25.addLinePostings(entries, static_cast<std::uint32_t>(terms.size()));
    }
    const std::vector<std::string> queryTerms = nesso::core::lexicalTerms(queryTokens.stripped);
    std::vector<double> bm25Scores = bm25.scores(queryTermViews(queryTerms));

    bool lexicalFallback = false;
    if (effective == EffectiveMode::Lexical)
    {
        const bool anyHit = std::ranges::any_of(bm25Scores, [](double score) { return score > 0.0; });
        if (!anyHit)
        {
            effective = EffectiveMode::Hybrid;
            lexicalFallback = true;
        }
    }
    return GrepBm25{.scores = std::move(bm25Scores), .effective = effective, .lexicalFallback = lexicalFallback};
}

static std::vector<GrepHit> lexicalGrepHits(const std::vector<GrepWaiting> &allLines,
                                            const std::vector<double> &bm25Scores,
                                            const nesso::core::QueryTokens &queryTokens)
{
    std::vector<GrepHit> hits;
    hits.reserve(allLines.size());
    for (size_t index = 0; index < allLines.size(); ++index)
    {
        if (!(bm25Scores[index] > 0.0))
        {
            continue;
        }
        const GrepWaiting &item = allLines[index];
        hits.push_back(
            {.ordinal = item.ordinal,
             .score = static_cast<float>(bm25Scores[index]),
             .lexical = static_cast<float>(bm25Scores[index]),
             .hasLexical = true,
             .substring = !queryTokens.stripped.empty() && item.text.find(queryTokens.stripped) != std::string::npos,
             .distinctId = item.distinctId,
             .source = item.source,
             .line = item.line,
             .arrayElement = item.arrayElement,
             .text = item.text});
    }
    std::ranges::sort(hits, lexicalHitBefore);
    return hits;
}

static std::vector<GrepHit> hybridGrepHits(const std::vector<GrepWaiting> &allLines, const std::vector<float> &scores,
                                           const std::vector<double> &bm25Scores)
{
    std::vector<double> denseValues;
    std::vector<double> lexicalValues;
    denseValues.reserve(allLines.size());
    lexicalValues.reserve(allLines.size());
    for (size_t index = 0; index < allLines.size(); ++index)
    {
        denseValues.push_back(static_cast<double>(scores[allLines[index].distinctId]));
        lexicalValues.push_back(bm25Scores[index]);
    }
    const std::vector<float> fused = nesso::core::fuseHybridScores(denseValues, lexicalValues);
    std::vector<GrepHit> hits;
    hits.reserve(allLines.size());
    for (size_t index = 0; index < allLines.size(); ++index)
    {
        const GrepWaiting &item = allLines[index];
        hits.push_back({.ordinal = item.ordinal,
                        .score = fused[index],
                        .dense = scores[item.distinctId],
                        .lexical = static_cast<float>(bm25Scores[index]),
                        .hasDense = true,
                        .hasLexical = true,
                        .distinctId = item.distinctId,
                        .source = item.source,
                        .line = item.line,
                        .arrayElement = item.arrayElement,
                        .text = item.text});
    }
    std::ranges::sort(hits, scoreHitBefore);
    return hits;
}

static std::vector<GrepHit> groupGrepHits(const std::vector<GrepHit> &hits, const std::vector<GrepWaiting> &allLines,
                                          const std::vector<float> &scores, EffectiveMode effective, size_t topK)
{
    std::vector<char> originSeen(scores.size(), 0);
    std::vector<GrepWaiting> origin(scores.size());
    for (const GrepWaiting &item : allLines)
    {
        if (originSeen[item.distinctId] != 0)
        {
            continue;
        }
        originSeen[item.distinctId] = 1;
        origin[item.distinctId] = item;
    }
    std::vector<char> taken(scores.size(), 0);
    std::vector<GrepHit> groups;
    groups.reserve(hits.size());
    for (const GrepHit &hit : hits)
    {
        if (taken[hit.distinctId] != 0)
        {
            continue;
        }
        taken[hit.distinctId] = 1;
        GrepHit row = hit;
        const GrepWaiting &first = origin[hit.distinctId];
        row.ordinal = first.ordinal;
        row.source = first.source;
        row.line = first.line;
        row.arrayElement = first.arrayElement;
        row.text = first.text;
        groups.push_back(std::move(row));
    }
    if (effective == EffectiveMode::Lexical)
    {
        std::ranges::sort(groups, lexicalHitBefore);
    }
    else
    {
        std::ranges::sort(groups, scoreHitBefore);
    }
    if (groups.size() > topK)
    {
        groups.resize(topK);
    }
    return groups;
}

std::expected<TextResults, Error> Nesso::grep(const GrepRequest &input) const
{
    const TraceSession session;
    auto expanded = expandRequestFiles(input.files);
    if (!expanded)
    {
        return std::unexpected(expanded.error());
    }
    GrepRequest request = input;
    request.files = std::move(*expanded);
    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto scanned = scanFiles(request.files, request.jsonField, request.archiveLimits);
    if (!scanned)
    {
        return std::unexpected(scanned.error());
    }
    if (scanned->chunks == 0)
    {
        const ErrorCause cause = noKeptLines(scanned->counts) ? ErrorCause::EmptyFile : ErrorCause::NothingIndexable;
        const std::filesystem::path path = request.files.size() == 1 ? request.files.front() : std::filesystem::path{};
        return std::unexpected(makeError(ErrorKind::EmptyInput, cause, path, scanned->counts, scanned->bundleSkips));
    }

    const auto queryEmbedding = [&request, &model]
    {
        const ScopedStage stage{"query-embed"};
        return (*model)->embed(request.query);
    }();
    if (!queryEmbedding)
    {
        return textFailure(ErrorKind::QueryEmbed, queryEmbedding.error(), scanned->counts);
    }

    const nesso::core::QueryTokens queryTokens = nesso::core::tokenizeQuery(request.query);
    const EffectiveMode effective = resolveEffectiveMode(request.mode, nesso::core::classifyQuery(request.query));
    const bool needLexical = effective != EffectiveMode::Dense;
    ProgressReporter progress(request.progress);
    GrepEmbedState state;
    state.request = &request;
    state.scanned = &*scanned;
    state.queryEmbedding = &*queryEmbedding;
    state.model = *model;
    state.needLexical = needLexical;
    state.progress = &progress;
    state.batch = streamBatch();
    state.distinct.reserve(scanned->chunks);
    state.sources.reserve(request.files.size());
    if (needLexical)
    {
        state.allLines.reserve(scanned->chunks);
    }
    if (const auto embedded = embedGrepFiles(state); !embedded)
    {
        return std::unexpected(embedded.error());
    }
    progress.finish();

    TextResults results{.matches = {},
                        .skippedLines = state.scanned->counts.skipped,
                        .emptyLines = state.scanned->counts.emptyLines,
                        .malformedLines = state.scanned->counts.malformedLines,
                        .missingFieldLines = state.scanned->counts.missingFieldLines,
                        .truncatedLines = state.scanned->counts.truncated,
                        .binaryEntries = 0,
                        .jsonField = state.scanned->counts.jsonField,
                        .multipleSources = state.severalSources,
                        .staleSources = {},
                        .bundleSkips = state.scanned->bundleSkips,
                        .policyMismatch = false,
                        .lexicalFallback = false,
                        .grouped = request.group};

    if (!needLexical)
    {
        return denseGrepResults(state, std::move(results));
    }

    const GrepBm25 lexical = buildGrepBm25Scores(state.allLines, queryTokens, effective);
    std::vector<GrepHit> hits = lexical.effective == EffectiveMode::Lexical
                                    ? lexicalGrepHits(state.allLines, lexical.scores, queryTokens)
                                    : hybridGrepHits(state.allLines, state.scores, lexical.scores);
    if (request.group)
    {
        hits = groupGrepHits(hits, state.allLines, state.scores, lexical.effective, request.topK);
    }
    else if (hits.size() > request.topK)
    {
        hits.resize(request.topK);
    }

    results.lexicalFallback = lexical.lexicalFallback;
    results.matches.reserve(hits.size());
    const std::string modeLabel{effectiveModeName(lexical.effective)};
    for (const GrepHit &hit : hits)
    {
        TextMatch match{.source = state.sources[hit.source],
                        .line = hit.line,
                        .score = hit.score,
                        .text = hit.text,
                        .arrayElement = hit.arrayElement,
                        .occurrences = state.distinct.occurrences(hit.distinctId),
                        .mode = modeLabel,
                        .dense = std::nullopt,
                        .lexical = std::nullopt};
        if (hit.hasDense)
        {
            match.dense = hit.dense;
        }
        if (hit.hasLexical)
        {
            match.lexical = hit.lexical;
        }
        results.matches.push_back(std::move(match));
    }
    return results;
}

struct IndexSourceCursor
{
    bool archive = false;
    std::uint32_t activeSource = 0;
    bool storeLines = false;
};

struct IndexEmbedState
{
    const IndexRequest *request = nullptr;
    Scan *scanned = nullptr;
    const nesso::embed::Embedder *model = nullptr;
    nesso::core::CorpusWriter *writer = nullptr;
    nesso::core::DistinctIndex distinct;
    ProgressReporter *progress = nullptr;
    std::uint32_t reserved = 0;
    std::uint32_t vectorsWritten = 0;
    size_t linesSeen = 0;
    size_t linesReported = 0;
    size_t batch = 0;
    std::optional<Error> failed;
};

static std::expected<void, Error> guardStableSource(const std::filesystem::path &path, std::string_view jsonField,
                                                    const nesso::core::SourceInfo &expected, const LineCounts &counts)
{
    if (path == "-")
    {
        return {};
    }
    const auto current = nesso::core::sourceInfoFor(path, jsonField);
    if (!current || !sameSource(expected, *current))
    {
        return std::unexpected(makeError(ErrorKind::Parse, ErrorCause::SourceChanged, path, counts));
    }
    return {};
}

static std::expected<void, Error> flushIndexEmbeddings(IndexEmbedState &state)
{
    if (state.distinct.pending().empty())
    {
        return {};
    }
    const LineCounts &counts = state.scanned->counts;
    const size_t linesInFlush = state.linesSeen - state.linesReported;
    auto embeddings = embedDistinct(state.distinct.pending(), *state.model, *state.progress, state.linesReported,
                                    linesInFlush, state.scanned->chunks, counts);
    if (!embeddings)
    {
        return std::unexpected(embeddings.error());
    }
    for (const std::vector<float> &embedding : *embeddings)
    {
        const auto added = state.writer->addVector(state.vectorsWritten, embedding);
        if (!added)
        {
            return std::unexpected(
                makeError(ErrorKind::CorpusWrite, causeOf(added.error()), state.request->output, counts));
        }
        ++state.vectorsWritten;
    }
    state.distinct.clearPending();
    state.linesReported = state.linesSeen;
    state.progress->update(state.linesReported, state.scanned->chunks);
    return {};
}

static std::optional<Error> ingestIndexChunk(IndexEmbedState &state, IndexSourceCursor &cursor,
                                             nesso::parser::ParsedChunk chunk)
{
    const LineCounts &counts = state.scanned->counts;
    if (cursor.archive && chunk.entryStart)
    {
        nesso::core::SourceInfo entry{.path = chunk.source,
                                      .size = 0,
                                      .modifiedTime = 0,
                                      .tracked = false,
                                      .jsonField = std::string{state.request->jsonField},
                                      .storedOnly = true};
        const auto added = state.writer->addSource(std::move(entry));
        if (!added)
        {
            return makeError(ErrorKind::CorpusWrite, causeOf(added.error()), state.request->output, counts);
        }
        cursor.activeSource = *added;
        cursor.storeLines = true;
    }
    std::uint32_t id = nesso::core::NO_DISTINCT;
    if (!chunk.skipped)
    {
        id = state.distinct.add(chunk.text);
        if (id == state.reserved)
        {
            const auto added = state.writer->reserveDistinct(state.distinct.text(id));
            if (!added || *added != id)
            {
                return makeError(ErrorKind::CorpusWrite, added ? ErrorCause::CorruptFile : causeOf(added.error()),
                                 state.request->output, counts);
            }
            ++state.reserved;
        }
        ++state.linesSeen;
    }
    nesso::core::CorpusLine line{.lineNumber = chunk.lineNumber,
                                 .byteOffset = chunk.byteOffset,
                                 .byteLength = chunk.byteLength,
                                 .sourceIndex = cursor.activeSource,
                                 .distinctId = id,
                                 .arrayElement = chunk.arrayElement};
    if (cursor.storeLines)
    {
        line.storedText = chunk.text;
    }
    std::vector<std::string> terms = chunk.skipped ? std::vector<std::string>{} : nesso::core::lexicalTerms(chunk.text);
    if (const auto added = state.writer->addLines(std::span{&line, 1}, std::span{&terms, 1}); !added)
    {
        return makeError(ErrorKind::CorpusWrite, causeOf(added.error()), state.request->output, counts);
    }
    if (state.distinct.pending().size() == state.batch)
    {
        if (const auto flushed = flushIndexEmbeddings(state); !flushed)
        {
            return flushed.error();
        }
    }
    return std::nullopt;
}

static std::expected<void, Error> embedIndexFiles(IndexEmbedState &state)
{
    const ScopedStage stage{"embed"};
    bool stdinUsed = false;
    const LineCounts &counts = state.scanned->counts;
    for (size_t sourceIndex = 0; sourceIndex < state.request->files.size(); ++sourceIndex)
    {
        const std::filesystem::path &path = state.request->files[sourceIndex];
        const nesso::core::SourceInfo &source = state.scanned->sources[sourceIndex];
        if (const auto stable = guardStableSource(path, state.request->jsonField, source, counts); !stable)
        {
            return std::unexpected(stable.error());
        }
        IndexSourceCursor cursor{
            .archive = nesso::parser::isArchivePath(path), .activeSource = 0, .storeLines = source.storedOnly};
        if (!cursor.archive)
        {
            const auto sourceId = state.writer->addSource(source);
            if (!sourceId)
            {
                return std::unexpected(
                    makeError(ErrorKind::CorpusWrite, causeOf(sourceId.error()), state.request->output, counts));
            }
            cursor.activeSource = *sourceId;
        }
        const auto streamed =
            visitRecords(path, *state.scanned, stdinUsed, state.request->jsonField, state.request->archiveLimits,
                         [&](nesso::parser::ParsedChunk chunk)
                         {
                             if (state.failed)
                             {
                                 return;
                             }
                             if (auto error = ingestIndexChunk(state, cursor, std::move(chunk)))
                             {
                                 state.failed = std::move(error);
                             }
                         });
        if (!streamed)
        {
            return std::unexpected(parseFailure(streamed.error(), path, counts, state.request->archiveLimits));
        }
        if (state.failed)
        {
            return std::unexpected(*state.failed);
        }
        if (const auto stable = guardStableSource(path, state.request->jsonField, source, counts); !stable)
        {
            return std::unexpected(stable.error());
        }
    }
    if (const auto flushed = flushIndexEmbeddings(state); !flushed)
    {
        return std::unexpected(flushed.error());
    }
    return {};
}

std::expected<IndexSummary, Error> Nesso::index(const IndexRequest &input) const
{
    const TraceSession session;
    auto expanded = expandRequestFiles(input.files);
    if (!expanded)
    {
        return std::unexpected(expanded.error());
    }
    IndexRequest request = input;
    request.files = std::move(*expanded);
    for (const std::filesystem::path &path : request.files)
    {
        std::error_code error;
        if (path != "-" && std::filesystem::equivalent(path, request.output, error))
        {
            return std::unexpected(makeError(ErrorKind::CorpusWrite, ErrorCause::OutputIsInput, request.output, {}));
        }
    }
    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }

    auto scanned = scanFiles(request.files, request.jsonField, request.archiveLimits, true);
    if (!scanned)
    {
        return std::unexpected(scanned.error());
    }

    auto policy = corpusPolicy(**model, scanned->counts);
    if (!policy)
    {
        return std::unexpected(policy.error());
    }
    // create() rejects an unusable output path; only finish() replaces an existing corpus.
    auto writer = nesso::core::CorpusWriter::create(request.output, std::move(*policy));
    if (!writer)
    {
        return std::unexpected(
            makeError(ErrorKind::CorpusWrite, causeOf(writer.error()), request.output, scanned->counts));
    }
    if (scanned->chunks == 0)
    {
        return nothingToIndex(request, *scanned);
    }

    ProgressReporter progress(request.progress);
    IndexEmbedState state;
    state.request = &request;
    state.scanned = &*scanned;
    state.model = *model;
    state.writer = &*writer;
    state.progress = &progress;
    state.batch = streamBatch();
    state.distinct.reserve(scanned->chunks);
    if (const auto embedded = embedIndexFiles(state); !embedded)
    {
        return std::unexpected(embedded.error());
    }
    progress.finish();

    const LineCounts &counts = scanned->counts;
    const auto written = [&writer]
    {
        const ScopedStage stage{"corpus-write"};
        return writer->finish();
    }();
    if (!written)
    {
        return std::unexpected(makeError(ErrorKind::CorpusWrite, causeOf(written.error()), request.output, counts));
    }
    return IndexSummary{.chunks = scanned->chunks,
                        .skippedLines = counts.skipped,
                        .emptyLines = counts.emptyLines,
                        .malformedLines = counts.malformedLines,
                        .missingFieldLines = counts.missingFieldLines,
                        .truncatedLines = counts.truncated,
                        .binaryEntries = 0,
                        .jsonField = counts.jsonField,
                        .bundleSkips = scanned->bundleSkips};
}

// 0 is the embedder default: usable physical cores. An explicit budget is that
// many scan workers. The scan runs after the query embedding returns, so these
// workers do not sit on top of the ONNX intra-op pool.
static size_t scoringThreads(const Config &config)
{
    if (config.threads != 0)
    {
        return config.threads;
    }
    return nesso::embed::readMachine().physicalCores;
}

struct CorpusScore
{
    std::span<const float> query;
    std::span<const float> matrix;
    size_t dimensions = 0;
    float *scores = nullptr;
    std::mutex mutex;
    std::optional<nesso::core::EngineError> dotError;
    size_t failureAt = std::numeric_limits<size_t>::max();
    bool nonFinite = false;
};

static void scoreCorpusRows(void *raw, size_t begin, size_t end)
{
    auto *job = static_cast<CorpusScore *>(raw);
    for (size_t id = begin; id < end; ++id)
    {
        const auto row = job->matrix.subspan(id * job->dimensions, job->dimensions);
        const auto score = nesso::core::math::DistanceMetrics::dotProduct<float>(job->query, row);
        if (!score)
        {
            const std::scoped_lock lock(job->mutex);
            if (id < job->failureAt)
            {
                job->dotError = score.error();
                job->nonFinite = false;
                job->failureAt = id;
            }
            return;
        }
        if (!std::isfinite(*score))
        {
            const std::scoped_lock lock(job->mutex);
            if (id < job->failureAt)
            {
                job->dotError.reset();
                job->nonFinite = true;
                job->failureAt = id;
            }
            return;
        }
        job->scores[id] = *score;
    }
}

struct LineRank
{
    std::span<const nesso::core::CorpusLine> lines;
    const std::vector<float> *scores = nullptr;
};

static float lineScore(void *raw, size_t index)
{
    const auto *view = static_cast<const LineRank *>(raw);
    return (*view->scores)[view->lines[index].distinctId];
}

static bool lineKeep(void *raw, size_t index)
{
    const auto *view = static_cast<const LineRank *>(raw);
    return view->lines[index].distinctId != nesso::core::NO_DISTINCT;
}

struct FusedLineRank
{
    const std::vector<float> *scores = nullptr;
    const std::vector<char> *keep = nullptr;
};

static float fusedLineScore(void *raw, size_t index)
{
    const auto *view = static_cast<const FusedLineRank *>(raw);
    return (*view->scores)[index];
}

static bool fusedLineKeep(void *raw, size_t index)
{
    const auto *view = static_cast<const FusedLineRank *>(raw);
    return (*view->keep)[index] != 0;
}

struct RankedLine
{
    uint64_t ordinal = 0;
    float score = 0.0F;
    float dense = 0.0F;
    float lexical = 0.0F;
    bool hasDense = false;
    bool hasLexical = false;
};

static std::vector<RankedLine> rankDenseGroups(const nesso::core::MappedCorpus &corpus,
                                               const std::vector<float> &distinctScores, size_t topK)
{
    const std::size_t distinct = corpus.texts().size();
    struct Group
    {
        uint64_t ordinal = 0;
        float score = 0.0F;
    };
    std::vector<Group> groups;
    std::vector<char> seen(distinct, 0);
    groups.reserve(distinct);
    for (std::size_t ordinal = 0; ordinal < corpus.lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT || seen[line.distinctId] != 0)
        {
            continue;
        }
        seen[line.distinctId] = 1;
        groups.push_back(Group{.ordinal = ordinal, .score = distinctScores[line.distinctId]});
    }
    std::ranges::sort(groups, [](const Group &left, const Group &right)
                      { return betterHit(left.score, left.ordinal, right.score, right.ordinal); });
    if (groups.size() > topK)
    {
        groups.resize(topK);
    }
    std::vector<RankedLine> ranked;
    ranked.reserve(groups.size());
    for (const Group &group : groups)
    {
        ranked.push_back({.ordinal = group.ordinal, .score = group.score, .dense = group.score, .hasDense = true});
    }
    return ranked;
}

static std::vector<RankedLine> rankDenseLines(const nesso::core::MappedCorpus &corpus,
                                              const std::vector<float> &distinctScores, size_t topK, size_t threads,
                                              bool group)
{
    if (group)
    {
        return rankDenseGroups(corpus, distinctScores, topK);
    }
    LineRank view{.lines = corpus.lines(), .scores = &distinctScores};
    const std::vector<nesso::core::Hit> hits =
        nesso::core::rankTop(corpus.lines().size(), topK, threads, lineScore, lineKeep, &view);
    std::vector<RankedLine> ranked;
    ranked.reserve(hits.size());
    for (const nesso::core::Hit &hit : hits)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[static_cast<size_t>(hit.index)];
        ranked.push_back(
            {.ordinal = hit.index, .score = hit.score, .dense = hit.score, .hasDense = true, .hasLexical = false});
        (void)line;
    }
    return ranked;
}

static std::vector<RankedLine> rankHybridLines(const nesso::core::MappedCorpus &corpus,
                                               const std::vector<float> &distinctScores,
                                               const std::vector<double> &bm25Scores, size_t topK, size_t threads,
                                               bool group)
{
    std::vector<double> denseValues;
    std::vector<double> lexicalValues;
    std::vector<uint64_t> ordinals;
    denseValues.reserve(corpus.lines().size());
    lexicalValues.reserve(corpus.lines().size());
    ordinals.reserve(corpus.lines().size());
    for (size_t ordinal = 0; ordinal < corpus.lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT)
        {
            continue;
        }
        ordinals.push_back(ordinal);
        denseValues.push_back(static_cast<double>(distinctScores[line.distinctId]));
        lexicalValues.push_back(bm25Scores[ordinal]);
    }
    const std::vector<float> fused = nesso::core::fuseHybridScores(denseValues, lexicalValues);
    if (group)
    {
        const std::size_t distinct = corpus.texts().size();
        std::vector<uint64_t> first(distinct, 0);
        std::vector<char> have(distinct, 0);
        std::vector<float> bestScore(distinct);
        std::vector<float> bestLexical(distinct);
        std::vector<uint64_t> bestOrdinal(distinct);
        std::vector<std::uint32_t> ids;
        ids.reserve(distinct);
        for (std::size_t index = 0; index < ordinals.size(); ++index)
        {
            const uint64_t ordinal = ordinals[index];
            const auto id = corpus.lines()[static_cast<std::size_t>(ordinal)].distinctId;
            const float score = fused[index];
            const auto lexical = static_cast<float>(bm25Scores[static_cast<std::size_t>(ordinal)]);
            if (have[id] == 0)
            {
                have[id] = 1;
                first[id] = ordinal;
                ids.push_back(id);
                bestScore[id] = score;
                bestLexical[id] = lexical;
                bestOrdinal[id] = ordinal;
            }
            else if (betterHit(score, ordinal, bestScore[id], bestOrdinal[id]))
            {
                bestScore[id] = score;
                bestLexical[id] = lexical;
                bestOrdinal[id] = ordinal;
            }
        }
        struct Group
        {
            uint64_t ordinal = 0;
            float score = 0.0F;
            float dense = 0.0F;
            float lexical = 0.0F;
        };
        std::vector<Group> groups;
        groups.reserve(ids.size());
        for (const std::uint32_t id : ids)
        {
            groups.push_back(Group{
                .ordinal = first[id], .score = bestScore[id], .dense = distinctScores[id], .lexical = bestLexical[id]});
        }
        std::ranges::sort(groups, [](const Group &left, const Group &right)
                          { return betterHit(left.score, left.ordinal, right.score, right.ordinal); });
        if (groups.size() > topK)
        {
            groups.resize(topK);
        }
        std::vector<RankedLine> ranked;
        ranked.reserve(groups.size());
        for (const Group &row : groups)
        {
            ranked.push_back({.ordinal = row.ordinal,
                              .score = row.score,
                              .dense = row.dense,
                              .lexical = row.lexical,
                              .hasDense = true,
                              .hasLexical = true});
        }
        return ranked;
    }
    std::vector<float> lineScores(corpus.lines().size(), 0.0F);
    std::vector<char> keep(corpus.lines().size(), 0);
    for (size_t index = 0; index < ordinals.size(); ++index)
    {
        lineScores[static_cast<size_t>(ordinals[index])] = fused[index];
        keep[static_cast<size_t>(ordinals[index])] = 1;
    }
    FusedLineRank view{.scores = &lineScores, .keep = &keep};
    const std::vector<nesso::core::Hit> hits =
        nesso::core::rankTop(corpus.lines().size(), topK, threads, fusedLineScore, fusedLineKeep, &view);
    std::vector<RankedLine> ranked;
    ranked.reserve(hits.size());
    for (const nesso::core::Hit &hit : hits)
    {
        const auto ordinal = static_cast<size_t>(hit.index);
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        ranked.push_back({.ordinal = hit.index,
                          .score = hit.score,
                          .dense = distinctScores[line.distinctId],
                          .lexical = static_cast<float>(bm25Scores[ordinal]),
                          .hasDense = true,
                          .hasLexical = true});
    }
    return ranked;
}

struct LexicalCandidate
{
    uint64_t ordinal = 0;
    float score = 0.0F;
    bool substring = false;
    std::string text;
};

static bool lexicalCandidateBefore(const LexicalCandidate &left, const LexicalCandidate &right)
{
    if (left.substring != right.substring)
    {
        return left.substring && !right.substring;
    }
    return betterHit(left.score, left.ordinal, right.score, right.ordinal);
}

static void rematerializeLexicalSubstrings(const nesso::core::MappedCorpus &corpus, std::string_view needle,
                                           std::vector<LexicalCandidate> &hits)
{
    for (LexicalCandidate &hit : hits)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[static_cast<std::size_t>(hit.ordinal)];
        std::string own;
        if (line.storedText)
        {
            own = std::string{*line.storedText};
        }
        else
        {
            const nesso::core::SourceInfo &source = corpus.sources()[line.sourceIndex];
            auto reread =
                nesso::parser::Chunker::readRecord(source.path, line.byteOffset, line.byteLength, line.lineNumber,
                                                   line.arrayElement, source.jsonField, MAX_LINE_LENGTH);
            if (reread)
            {
                own = std::move(*reread);
            }
            else
            {
                own = std::string{corpus.texts()[line.distinctId]};
            }
        }
        hit.substring = !needle.empty() && own.find(needle) != std::string::npos;
    }
    std::ranges::sort(hits, lexicalCandidateBefore);
}

static std::vector<LexicalCandidate> collapseLexicalGroups(const nesso::core::MappedCorpus &corpus,
                                                           const std::vector<LexicalCandidate> &hits)
{
    const std::size_t distinct = corpus.texts().size();
    std::vector<uint64_t> first(distinct, std::numeric_limits<uint64_t>::max());
    for (std::size_t ordinal = 0; ordinal < corpus.lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT ||
            first[line.distinctId] != std::numeric_limits<uint64_t>::max())
        {
            continue;
        }
        first[line.distinctId] = ordinal;
    }
    std::vector<char> taken(distinct, 0);
    std::vector<LexicalCandidate> groups;
    groups.reserve(hits.size());
    for (const LexicalCandidate &hit : hits)
    {
        const auto id = corpus.lines()[static_cast<std::size_t>(hit.ordinal)].distinctId;
        if (taken[id] != 0)
        {
            continue;
        }
        taken[id] = 1;
        LexicalCandidate row = hit;
        row.ordinal = first[id];
        groups.push_back(std::move(row));
    }
    std::ranges::sort(groups, lexicalCandidateBefore);
    return groups;
}

static std::vector<RankedLine> rankLexicalLines(const nesso::core::MappedCorpus &corpus,
                                                const std::vector<double> &bm25Scores, std::string_view needle,
                                                size_t topK, bool group)
{
    std::vector<LexicalCandidate> hits;
    for (size_t ordinal = 0; ordinal < corpus.lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus.lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT || !(bm25Scores[ordinal] > 0.0))
        {
            continue;
        }
        const std::string_view text = line.storedText.value_or(corpus.texts()[line.distinctId]);
        hits.push_back({.ordinal = ordinal,
                        .score = static_cast<float>(bm25Scores[ordinal]),
                        .substring = !needle.empty() && text.find(needle) != std::string_view::npos,
                        .text = std::string{text}});
    }
    std::ranges::sort(hits, lexicalCandidateBefore);
    if (group)
    {
        rematerializeLexicalSubstrings(corpus, needle, hits);
        hits = collapseLexicalGroups(corpus, hits);
    }
    if (hits.size() > topK)
    {
        hits.resize(topK);
    }
    std::vector<RankedLine> ranked;
    ranked.reserve(hits.size());
    for (const LexicalCandidate &hit : hits)
    {
        ranked.push_back(
            {.ordinal = hit.ordinal, .score = hit.score, .lexical = hit.score, .hasDense = false, .hasLexical = true});
    }
    return ranked;
}

std::expected<TextResults, Error> Nesso::search(const SearchRequest &request) const
{
    const TraceSession session;
    std::uint32_t version = 0;
    const auto corpus = [&request, &version]
    {
        const ScopedStage stage{"corpus-read"};
        return nesso::core::mapCorpusFile(request.index, &version);
    }();
    if (!corpus)
    {
        Error error = makeError(ErrorKind::CorpusRead, causeOf(corpus.error()), request.index, {});
        if (corpus.error() == nesso::core::EngineError::UnsupportedVersion)
        {
            error.corpusVersion = version;
        }
        return std::unexpected(std::move(error));
    }
    if (corpus->texts().empty())
    {
        return std::unexpected(makeError(ErrorKind::EmptyInput, ErrorCause::EmptyCorpus, request.index, {}));
    }

    const auto model = embedder();
    if (!model)
    {
        return std::unexpected(model.error());
    }
    const auto current = corpusPolicy(**model, {});
    if (!current)
    {
        return std::unexpected(current.error());
    }
    const auto &stored = corpus->policy();
    constexpr std::array<std::string_view, 5> identityKeys{"model.id", "model.sha256", "vocab.sha256",
                                                           "model.quantization", "dimensions"};
    for (const std::string_view key : identityKeys)
    {
        if (stored.find(key)->second != current->find(key)->second)
        {
            return std::unexpected(makeError(ErrorKind::CorpusRead, ErrorCause::ModelMismatch, request.index, {}));
        }
    }
    constexpr std::array<std::string_view, 5> policyKeys{"batch", "truncate", "dedup", "mask", "tokens.max"};
    const bool policyMismatch = std::ranges::any_of(policyKeys, [&](std::string_view key)
                                                    { return stored.find(key)->second != current->find(key)->second; });
    auto sourceWarnings = staleSources(corpus->sources());
    const auto fail = [&](ErrorKind kind, ErrorCause cause)
    {
        Error error = makeError(kind, cause, {}, {});
        error.policyMismatch = policyMismatch;
        error.staleSources = sourceWarnings;
        return std::unexpected(std::move(error));
    };
    const auto sourceIsStale = [&](const std::string &path)
    {
        return std::ranges::any_of(sourceWarnings,
                                   [&path](const StaleSource &warning) { return warning.path == path; });
    };

    if (const auto lexicalReady =
            requireLexicalReady(corpus->lexicalStatus(), corpus->corpusLexicalTermsVersion(), request.mode);
        !lexicalReady)
    {
        Error error = lexicalReady.error();
        error.policyMismatch = policyMismatch;
        error.staleSources = sourceWarnings;
        return std::unexpected(std::move(error));
    }

    const nesso::core::QueryTokens queryTokens = nesso::core::tokenizeQuery(request.query);
    EffectiveMode effective = resolveEffectiveMode(request.mode, nesso::core::classifyQuery(request.query));
    bool lexicalFallback = false;

    const ScopedStage stage{"rank"};
    const auto queryEmbedding = (*model)->embed(request.query);
    if (!queryEmbedding)
    {
        return fail(ErrorKind::QueryEmbed, causeOf(queryEmbedding.error()));
    }
    const size_t dimensions = corpus->dimensions();
    if (queryEmbedding->size() != dimensions)
    {
        return fail(ErrorKind::Search, ErrorCause::MismatchedDimensions);
    }
    const size_t threads = scoringThreads(config_);
    std::vector<float> scores(corpus->texts().size());
    CorpusScore scored;
    scored.query = *queryEmbedding;
    scored.matrix = corpus->matrix();
    scored.dimensions = dimensions;
    scored.scores = scores.data();
    nesso::core::detail::parallelRows(threads, scores.size(), scoreCorpusRows, &scored);
    if (scored.dotError)
    {
        return fail(ErrorKind::Search, causeOf(*scored.dotError));
    }
    if (scored.nonFinite)
    {
        return fail(ErrorKind::Search, ErrorCause::InvalidInput);
    }

    std::vector<std::uint32_t> occurrences(corpus->texts().size(), 0);
    std::optional<std::string_view> firstSource;
    bool multipleSources = false;
    for (size_t ordinal = 0; ordinal < corpus->lines().size(); ++ordinal)
    {
        const nesso::core::CorpusLine &line = corpus->lines()[ordinal];
        if (line.distinctId == nesso::core::NO_DISTINCT)
        {
            continue;
        }
        ++occurrences[line.distinctId];
        const std::string &source = corpus->sources()[line.sourceIndex].path;
        if (!firstSource)
        {
            firstSource = source;
        }
        else if (*firstSource != source)
        {
            multipleSources = true;
        }
    }

    std::vector<double> bm25Scores;
    if (effective != EffectiveMode::Dense)
    {
        const std::vector<std::string> queryTerms = nesso::core::lexicalTerms(queryTokens.stripped);
        bm25Scores = bm25ScoresFromCorpus(*corpus, queryTermViews(queryTerms));
    }

    if (effective == EffectiveMode::Lexical)
    {
        const bool anyHit = std::ranges::any_of(bm25Scores, [](double score) { return score > 0.0; });
        if (!anyHit)
        {
            effective = EffectiveMode::Hybrid;
            lexicalFallback = true;
        }
    }

    std::vector<RankedLine> ranked;
    if (effective == EffectiveMode::Dense)
    {
        ranked = rankDenseLines(*corpus, scores, request.topK, threads, request.group);
    }
    else if (effective == EffectiveMode::Lexical)
    {
        ranked = rankLexicalLines(*corpus, bm25Scores, queryTokens.stripped, request.topK, request.group);
    }
    else
    {
        ranked = rankHybridLines(*corpus, scores, bm25Scores, request.topK, threads, request.group);
    }
    if (ranked.empty())
    {
        return fail(ErrorKind::NoMatches, ErrorCause::None);
    }

    TextResults results{};
    results.grouped = request.group;
    results.multipleSources = multipleSources;
    results.policyMismatch = policyMismatch;
    results.lexicalFallback = lexicalFallback;
    results.matches.reserve(ranked.size());
    const std::string modeLabel{effectiveModeName(effective)};
    for (const RankedLine &hit : ranked)
    {
        const nesso::core::CorpusLine &line = corpus->lines()[static_cast<size_t>(hit.ordinal)];
        const nesso::core::SourceInfo &source = corpus->sources()[line.sourceIndex];
        std::string text{line.storedText.value_or(corpus->texts()[line.distinctId])};
        if (!line.storedText && !sourceIsStale(source.path))
        {
            auto reread =
                nesso::parser::Chunker::readRecord(source.path, line.byteOffset, line.byteLength, line.lineNumber,
                                                   line.arrayElement, source.jsonField, MAX_LINE_LENGTH);
            if (!reread)
            {
                addSourceWarning(sourceWarnings, {.path = source.path, .unreadable = true});
            }
            else
            {
                const auto after = nesso::core::sourceInfoFor(source.path, source.jsonField);
                if (!after || !sameSource(source, *after))
                {
                    addSourceWarning(sourceWarnings, {.path = source.path, .missing = !after});
                }
                else
                {
                    text = std::move(*reread);
                }
            }
        }
        TextMatch match{.source = source.path,
                        .line = line.lineNumber,
                        .score = hit.score,
                        .text = std::move(text),
                        .arrayElement = line.arrayElement,
                        .occurrences = occurrences[line.distinctId],
                        .mode = modeLabel,
                        .dense = std::nullopt,
                        .lexical = std::nullopt};
        if (hit.hasDense)
        {
            match.dense = hit.dense;
        }
        if (hit.hasLexical)
        {
            match.lexical = hit.lexical;
        }
        results.matches.push_back(std::move(match));
    }
    for (size_t index = 0; index < ranked.size(); ++index)
    {
        const nesso::core::CorpusLine &line = corpus->lines()[static_cast<size_t>(ranked[index].ordinal)];
        if (!line.storedText && sourceIsStale(corpus->sources()[line.sourceIndex].path))
        {
            results.matches[index].text = corpus->texts()[line.distinctId];
        }
    }
    results.staleSources = std::move(sourceWarnings);
    return results;
}

} // namespace nesso
