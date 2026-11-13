// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <nesso/nesso.hpp>

#include <nesso/error_map.hpp>
#include <nesso_detail.hpp>

#include <core/lexical_terms.hpp>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nesso
{

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

namespace detail
{

Error makeError(ErrorKind kind, ErrorCause cause, std::filesystem::path path, LineCounts counts,
                std::vector<BundleSkipSummary> bundleSkips)
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

Error parseFailure(nesso::parser::ParseError code, std::filesystem::path path, const LineCounts &counts,
                   const nesso::parser::ArchiveLimits &limits, std::string_view encryptedEntry,
                   std::vector<BundleSkipSummary> bundleSkips)
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

} // namespace detail

} // namespace nesso
