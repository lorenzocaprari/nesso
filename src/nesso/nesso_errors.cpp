// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "core/core_types.hpp"
#include "embed/embed_types.hpp"
#include "parser/archive_source.hpp"
#include "parser/log_chunker.hpp"
#include <cstddef>
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

ErrorCause CauseOf(nesso::parser::ParseError error)
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

ErrorCause CauseOf(nesso::embed::EmbedError error)
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

ErrorCause CauseOf(nesso::core::EngineError error)
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
    case nesso::core::EngineError::LimitsExceeded:
        return ErrorCause::CorpusLimitsExceeded;
    }
    std::unreachable();
}

std::string_view Describe(ErrorCause cause)
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
        return "the corpus format is not supported by this nesso; re-run nesso "
               "index";
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
    case ErrorCause::CorpusLimitsExceeded:
        return "the corpus exceeds this nesso's limits";
    case ErrorCause::TooManyEventTypes:
        return "too many distinct event types";
    }
    std::unreachable();
}

static std::optional<std::string> MessageLexicalSpecial(const Error &error)
{
    if (error.cause == ErrorCause::NoLexicalPostings)
    {
        return "Error: corpus has no lexical postings; re-run `nesso index`";
    }
    if (error.cause == ErrorCause::LexicalTermsMismatch)
    {
        return std::format("Error: corpus lexical terms are version {}, this build "
                           "uses version {}; "
                           "re-run `nesso index`",
                           error.note.empty() ? "?" : error.note, nesso::core::LEXICAL_TERMS_VERSION);
    }
    return std::nullopt;
}

static std::string MessageCorpusRead(const Error &error, std::string_view reason, const std::string &path)
{
    if (const auto special = MessageLexicalSpecial(error))
    {
        return *special;
    }
    if (error.cause == ErrorCause::UnsupportedCorpusVersion && error.corpus_version)
    {
        return std::format("Error: failed to read the corpus '{}': corpus format "
                           "v{} is not supported; "
                           "re-run nesso index",
                           path, *error.corpus_version);
    }
    if (error.cause == ErrorCause::ModelMismatch && error.model_mismatch)
    {
        const ModelMismatchDetail &detail = *error.model_mismatch;
        return std::format("Error: failed to read the corpus '{}': the corpus was built with a different model "
                           "({}: stored {}, loaded {}); re-run nesso index",
                           path, detail.key, detail.stored, detail.loaded);
    }
    return std::format("Error: failed to read the corpus '{}': {}", path, reason);
}

static std::string MessageEmptyInput(const Error &error, std::string_view reason, const std::string &path)
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

std::string Message(const Error &error)
{
    const std::string_view reason = Describe(error.cause);
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
        if (error.cause == ErrorCause::TooManyEventTypes)
        {
            return std::format("Error: failed to list the event types of '{}': {}", path,
                               error.note.empty() ? reason : std::string_view{error.note});
        }
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
        if (const auto special = MessageLexicalSpecial(error))
        {
            return *special;
        }
        return std::format("Error: failed to search: {}", reason);
    case ErrorKind::CorpusRead:
        return MessageCorpusRead(error, reason, path);
    case ErrorKind::CorpusWrite:
        return std::format("Error: failed to write the corpus '{}': {}", path, reason);
    case ErrorKind::CorpusLoad:
        return std::format("Error: failed to load the corpus: {}", reason);
    case ErrorKind::EmptyInput:
        return MessageEmptyInput(error, reason, path);
    case ErrorKind::NoMatches:
        return {};
    }
    std::unreachable();
}

static std::string CountPhrase(size_t count, std::string_view singular, std::string_view plural)
{
    return std::format("{} {}", count, count == 1 ? singular : plural);
}

static std::string JoinAnd(const std::vector<std::string> &parts)
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

std::string FormatBundleSkipSummary(const BundleSkipSummary &summary)
{
    const size_t total = BundleSkipTotal(summary);
    if (total == 0)
    {
        return {};
    }
    std::vector<std::string> parts;
    if (summary.directories > 0)
    {
        parts.push_back(CountPhrase(summary.directories, "directory", "directories"));
    }
    if (summary.symlinks > 0)
    {
        parts.push_back(CountPhrase(summary.symlinks, "symlink", "symlinks"));
    }
    if (summary.hardlinks > 0)
    {
        parts.push_back(CountPhrase(summary.hardlinks, "hard link", "hard links"));
    }
    if (summary.other > 0)
    {
        parts.push_back(CountPhrase(summary.other, "non-regular entry", "non-regular entries"));
    }
    if (summary.binary > 0)
    {
        parts.push_back(CountPhrase(summary.binary, "binary file", "binary files"));
    }
    return std::format("Skipped {} in '{}' ({}).", CountPhrase(total, "entry", "entries"), summary.path,
                       JoinAnd(parts));
}

static std::string ArchiveLimitNote(nesso::parser::ParseError code, const nesso::parser::ArchiveLimits &limits)
{
    switch (code)
    {
    case nesso::parser::ParseError::ArchiveEntryLimit:
        return std::format("an archive entry is larger than {} bytes; raise the "
                           "limit with --archive-max-entry-bytes",
                           limits.max_entry_bytes);
    case nesso::parser::ParseError::ArchiveTotalLimit:
        return std::format("the archive expands past {} uncompressed bytes; raise "
                           "the limit with --archive-max-bytes",
                           limits.max_total_bytes);
    case nesso::parser::ParseError::ArchiveCountLimit:
        return std::format("the archive entry count exceeds {}; raise the limit "
                           "with --archive-max-entries",
                           limits.max_entries);
    case nesso::parser::ParseError::ArchiveRatioLimit:
        return std::format("the archive compression ratio exceeds {}; raise the "
                           "limit with --archive-max-ratio",
                           limits.max_ratio);
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

static std::string EncryptedArchiveNote(std::string_view entry)
{
    if (entry.empty())
    {
        return "the archive is an encrypted OpenPGP file; decrypt it first (for "
               "example with gpg --decrypt)";
    }
    return std::format("entry '{}' is encrypted; decrypt it first", entry);
}

namespace detail
{

Error MakeError(ErrorKind kind, ErrorCause cause, std::filesystem::path path, LineCounts counts,
                std::vector<BundleSkipSummary> bundle_skips)
{
    return Error{.kind = kind,
                 .cause = cause,
                 .path = std::move(path),
                 .skipped_lines = counts.skipped,
                 .empty_lines = counts.empty_lines,
                 .malformed_lines = counts.malformed_lines,
                 .missing_field_lines = counts.missing_field_lines,
                 .truncated_lines = counts.truncated,
                 .binary_entries = counts.binary_entries,
                 .json_field = std::move(counts.json_field),
                 .note = {},
                 .stale_sources = {},
                 .bundle_skips = std::move(bundle_skips),
                 .policy_mismatch = false,
                 .corpus_version = {},
                 .model_mismatch = {}};
}

Error ParseFailure(nesso::parser::ParseError code, std::filesystem::path path, const LineCounts &counts,
                   const nesso::parser::ArchiveLimits &limits, std::string_view encrypted_entry,
                   std::vector<BundleSkipSummary> bundle_skips)
{
    Error error = MakeError(ErrorKind::Parse, CauseOf(code), std::move(path), counts, std::move(bundle_skips));
    if (code == nesso::parser::ParseError::EncryptedArchive)
    {
        error.note = EncryptedArchiveNote(encrypted_entry);
    }
    else
    {
        error.note = ArchiveLimitNote(code, limits);
    }
    return error;
}

} // namespace detail

} // namespace nesso
