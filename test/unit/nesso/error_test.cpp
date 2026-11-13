// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso/error_map.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

TEST_CASE("layer errors map onto a cause", "[nesso][error]")
{
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::FileOpenFailure) == nesso::ErrorCause::FileOpen);
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::UnsupportedFormat) == nesso::ErrorCause::UnsupportedFormat);
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::BinaryFile) == nesso::ErrorCause::BinaryFile);
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::CorruptGzip) == nesso::ErrorCause::CorruptGzip);
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::CorruptArchive) == nesso::ErrorCause::CorruptArchive);
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::EncryptedArchive) == nesso::ErrorCause::EncryptedArchive);
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::ArchiveEntryLimit) == nesso::ErrorCause::ArchiveLimit);
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::ArchiveTotalLimit) == nesso::ErrorCause::ArchiveLimit);
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::ArchiveCountLimit) == nesso::ErrorCause::ArchiveLimit);
    REQUIRE(nesso::causeOf(nesso::parser::ParseError::ArchiveRatioLimit) == nesso::ErrorCause::ArchiveLimit);

    REQUIRE(nesso::causeOf(nesso::embed::EmbedError::VocabLoadFailure) == nesso::ErrorCause::VocabLoad);
    REQUIRE(nesso::causeOf(nesso::embed::EmbedError::TokenizationFailure) == nesso::ErrorCause::Tokenization);
    REQUIRE(nesso::causeOf(nesso::embed::EmbedError::ModelLoadFailure) == nesso::ErrorCause::ModelLoad);
    REQUIRE(nesso::causeOf(nesso::embed::EmbedError::InferenceFailure) == nesso::ErrorCause::Inference);
    REQUIRE(nesso::causeOf(nesso::embed::EmbedError::InvalidInput) == nesso::ErrorCause::InvalidInput);

    REQUIRE(nesso::causeOf(nesso::core::EngineError::FileOpenFailure) == nesso::ErrorCause::FileOpen);
    REQUIRE(nesso::causeOf(nesso::core::EngineError::MismatchedDimensions) == nesso::ErrorCause::MismatchedDimensions);
    REQUIRE(nesso::causeOf(nesso::core::EngineError::DatabaseNotInitialized) == nesso::ErrorCause::EmptyEmbedding);
    REQUIRE(nesso::causeOf(nesso::core::EngineError::CorruptDatabase) == nesso::ErrorCause::CorruptFile);
    REQUIRE(nesso::causeOf(nesso::core::EngineError::UnsupportedVersion) ==
            nesso::ErrorCause::UnsupportedCorpusVersion);
}

TEST_CASE("each cause has a readable phrase", "[nesso][error]")
{
    REQUIRE(nesso::describe(nesso::ErrorCause::None).empty());
    REQUIRE(nesso::describe(nesso::ErrorCause::FileOpen) == "could not open the file");
    REQUIRE(nesso::describe(nesso::ErrorCause::UnsupportedFormat) == "unsupported format");
    REQUIRE(nesso::describe(nesso::ErrorCause::BinaryFile) == "the file is binary");
    REQUIRE(nesso::describe(nesso::ErrorCause::CorruptGzip) == "the gzip stream is truncated or corrupt");
    REQUIRE(nesso::describe(nesso::ErrorCause::CorruptArchive) == "the archive is truncated or corrupt");
    REQUIRE(nesso::describe(nesso::ErrorCause::EncryptedArchive) == "the archive is encrypted; decrypt it first");
    REQUIRE(nesso::describe(nesso::ErrorCause::ArchiveLimit) == "an archive limit was exceeded");
    REQUIRE(nesso::describe(nesso::ErrorCause::VocabLoad) == "could not load the vocabulary");
    REQUIRE(nesso::describe(nesso::ErrorCause::Tokenization) == "could not tokenize the text");
    REQUIRE(nesso::describe(nesso::ErrorCause::ModelLoad) == "could not load the model");
    REQUIRE(nesso::describe(nesso::ErrorCause::Inference) == "inference failed");
    REQUIRE(nesso::describe(nesso::ErrorCause::InvalidInput) == "the input is invalid");
    REQUIRE(nesso::describe(nesso::ErrorCause::MismatchedDimensions) == "the embedding width does not match");
    REQUIRE(nesso::describe(nesso::ErrorCause::EmptyEmbedding) == "the embedding is empty");
    REQUIRE(nesso::describe(nesso::ErrorCause::CorruptFile) == "the file is corrupt");
    REQUIRE(nesso::describe(nesso::ErrorCause::UnsupportedCorpusVersion) ==
            "the corpus format is not supported by this nesso; re-run nesso index");
    REQUIRE(nesso::describe(nesso::ErrorCause::ModelMismatch) ==
            "the corpus was built with a different model; re-run nesso index");
    REQUIRE(nesso::describe(nesso::ErrorCause::EmptyFile) == "the file is empty");
    REQUIRE(nesso::describe(nesso::ErrorCause::NothingIndexable) == "no indexable lines");
    REQUIRE(nesso::describe(nesso::ErrorCause::EmptyCorpus) == "the corpus is empty");
    REQUIRE(nesso::describe(nesso::ErrorCause::OutputIsInput) == "the output is one of the inputs");
}

TEST_CASE("each failure kind names itself", "[nesso][error]")
{
    const auto text = [](nesso::ErrorKind kind, nesso::ErrorCause cause)
    {
        const nesso::Error error{.kind = kind,
                                 .cause = cause,
                                 .path = "in.log",
                                 .skippedLines = 0,
                                 .note = {},
                                 .staleSources = {},
                                 .bundleSkips = {},
                                 .policyMismatch = false,
                                 .corpusVersion = {}};
        return nesso::message(error);
    };

    REQUIRE(text(nesso::ErrorKind::EmbedderLoad, nesso::ErrorCause::ModelLoad).find("failed to load the embedder") !=
            std::string::npos);
    REQUIRE(text(nesso::ErrorKind::Parse, nesso::ErrorCause::UnsupportedFormat).find("failed to parse") !=
            std::string::npos);
    REQUIRE(
        text(nesso::ErrorKind::Parse, nesso::ErrorCause::CorruptGzip).find("the gzip stream is truncated or corrupt") !=
        std::string::npos);
    nesso::Error limited{.kind = nesso::ErrorKind::Parse,
                         .cause = nesso::ErrorCause::ArchiveLimit,
                         .path = "bundle.tar.xz",
                         .note = "the archive entry count exceeds 2; raise the limit with --archive-max-entries",
                         .staleSources = {},
                         .bundleSkips = {},
                         .policyMismatch = false,
                         .corpusVersion = {}};
    REQUIRE(nesso::message(limited).find("the archive entry count exceeds 2") != std::string::npos);
    REQUIRE(nesso::message(limited).find("--archive-max-entries") != std::string::npos);
    nesso::Error encrypted{.kind = nesso::ErrorKind::Parse,
                           .cause = nesso::ErrorCause::EncryptedArchive,
                           .path = "secret.zip",
                           .note = "entry 'secret.log' is encrypted; decrypt it first",
                           .staleSources = {},
                           .bundleSkips = {},
                           .policyMismatch = false,
                           .corpusVersion = {}};
    REQUIRE(nesso::message(encrypted).find("entry 'secret.log' is encrypted") != std::string::npos);
    REQUIRE(nesso::formatBundleSkipSummary({.path = "mix.tar", .directories = 2, .binary = 1}) ==
            "Skipped 3 entries in 'mix.tar' (2 directories and 1 binary file).");
    REQUIRE(text(nesso::ErrorKind::Embed, nesso::ErrorCause::Inference).find("failed to embed the input") !=
            std::string::npos);
    REQUIRE(text(nesso::ErrorKind::QueryEmbed, nesso::ErrorCause::Tokenization).find("failed to embed the query") !=
            std::string::npos);
    REQUIRE(text(nesso::ErrorKind::IndexBuild, nesso::ErrorCause::EmptyEmbedding).find("failed to build the index") !=
            std::string::npos);
    REQUIRE(text(nesso::ErrorKind::Search, nesso::ErrorCause::MismatchedDimensions).find("failed to search") !=
            std::string::npos);
    REQUIRE(text(nesso::ErrorKind::CorpusRead, nesso::ErrorCause::CorruptFile).find("failed to read the corpus") !=
            std::string::npos);
    REQUIRE(text(nesso::ErrorKind::CorpusWrite, nesso::ErrorCause::FileOpen).find("failed to write the corpus") !=
            std::string::npos);
    REQUIRE(text(nesso::ErrorKind::CorpusLoad, nesso::ErrorCause::EmptyEmbedding).find("failed to load the corpus") !=
            std::string::npos);
    const std::string emptyFile = text(nesso::ErrorKind::EmptyInput, nesso::ErrorCause::EmptyFile);
    REQUIRE(emptyFile.find("nothing to search") != std::string::npos);
    REQUIRE(emptyFile.find("the file is empty") != std::string::npos);
    REQUIRE(emptyFile.find("in.log") != std::string::npos);
    REQUIRE(text(nesso::ErrorKind::NoMatches, nesso::ErrorCause::None).empty());
}

TEST_CASE("a zero-chunk index names the path and what to check", "[nesso][error]")
{
    const auto text = [](nesso::ErrorCause cause, std::string path)
    {
        const nesso::Error error{.kind = nesso::ErrorKind::EmptyInput,
                                 .cause = cause,
                                 .path = std::move(path),
                                 .skippedLines = 0,
                                 .note = "check the input path or --json-field",
                                 .staleSources = {},
                                 .bundleSkips = {},
                                 .policyMismatch = false,
                                 .corpusVersion = {}};
        return nesso::message(error);
    };

    REQUIRE(text(nesso::ErrorCause::EmptyFile, "empty.log") ==
            "no indexable lines in 'empty.log': the file is empty; check the input path or --json-field");
    REQUIRE(text(nesso::ErrorCause::EmptyFile, "") ==
            "no indexable lines: the file is empty; check the input path or --json-field");
    REQUIRE(text(nesso::ErrorCause::NothingIndexable, "rows.json") ==
            "no indexable lines in 'rows.json'; check the input path or --json-field");
    REQUIRE(text(nesso::ErrorCause::NothingIndexable, "") ==
            "no indexable lines; check the input path or --json-field");
}

TEST_CASE("message text is byte-stable per kind and optional parts", "[nesso][error]")
{
    const auto make = [](nesso::ErrorKind kind, nesso::ErrorCause cause, std::filesystem::path path = "in.log",
                         std::string note = {}, std::optional<std::uint32_t> corpusVersion = {})
    {
        return nesso::Error{.kind = kind,
                            .cause = cause,
                            .path = std::move(path),
                            .skippedLines = 0,
                            .note = std::move(note),
                            .staleSources = {},
                            .bundleSkips = {},
                            .policyMismatch = false,
                            .corpusVersion = corpusVersion};
    };

    REQUIRE(nesso::message(make(nesso::ErrorKind::EmbedderLoad, nesso::ErrorCause::ModelLoad)) ==
            "Error: failed to load the embedder from 'in.log': could not load the model");
    REQUIRE(nesso::message(make(nesso::ErrorKind::EmbedderLoad, nesso::ErrorCause::ModelLoad, "models",
                                "tried models/model.onnx")) ==
            "Error: failed to load the embedder from 'models': could not load the model. tried models/model.onnx");

    REQUIRE(nesso::message(make(nesso::ErrorKind::Parse, nesso::ErrorCause::UnsupportedFormat)) ==
            "Error: failed to parse 'in.log': unsupported format");
    REQUIRE(nesso::message(make(nesso::ErrorKind::Parse, nesso::ErrorCause::ArchiveLimit, "bundle.tar",
                                "the archive entry count exceeds 2")) ==
            "Error: failed to parse 'bundle.tar': the archive entry count exceeds 2");

    REQUIRE(nesso::message(make(nesso::ErrorKind::Embed, nesso::ErrorCause::Inference)) ==
            "Error: failed to embed the input: inference failed");
    REQUIRE(nesso::message(make(nesso::ErrorKind::QueryEmbed, nesso::ErrorCause::Tokenization)) ==
            "Error: failed to embed the query: could not tokenize the text");
    REQUIRE(nesso::message(make(nesso::ErrorKind::IndexBuild, nesso::ErrorCause::EmptyEmbedding)) ==
            "Error: failed to build the index: the embedding is empty");
    REQUIRE(nesso::message(make(nesso::ErrorKind::CorpusWrite, nesso::ErrorCause::FileOpen, "out.nesso")) ==
            "Error: failed to write the corpus 'out.nesso': could not open the file");
    REQUIRE(nesso::message(make(nesso::ErrorKind::CorpusLoad, nesso::ErrorCause::EmptyEmbedding)) ==
            "Error: failed to load the corpus: the embedding is empty");

    REQUIRE(nesso::message(make(nesso::ErrorKind::Search, nesso::ErrorCause::NoLexicalPostings)) ==
            "Error: corpus has no lexical postings; re-run `nesso index`");
    REQUIRE(nesso::message(make(nesso::ErrorKind::Search, nesso::ErrorCause::LexicalTermsMismatch)) ==
            "Error: corpus lexical terms are version ?, this build uses version v1; re-run `nesso index`");
    REQUIRE(
        nesso::message(make(nesso::ErrorKind::Search, nesso::ErrorCause::LexicalTermsMismatch, "idx.nesso", "v0")) ==
        "Error: corpus lexical terms are version v0, this build uses version v1; re-run `nesso index`");
    REQUIRE(nesso::message(make(nesso::ErrorKind::Search, nesso::ErrorCause::MismatchedDimensions)) ==
            "Error: failed to search: the embedding width does not match");

    REQUIRE(nesso::message(make(nesso::ErrorKind::CorpusRead, nesso::ErrorCause::NoLexicalPostings)) ==
            "Error: corpus has no lexical postings; re-run `nesso index`");
    REQUIRE(nesso::message(
                make(nesso::ErrorKind::CorpusRead, nesso::ErrorCause::LexicalTermsMismatch, "idx.nesso", "v0")) ==
            "Error: corpus lexical terms are version v0, this build uses version v1; re-run `nesso index`");
    REQUIRE(nesso::message(
                make(nesso::ErrorKind::CorpusRead, nesso::ErrorCause::UnsupportedCorpusVersion, "old.nesso", {}, 4)) ==
            "Error: failed to read the corpus 'old.nesso': corpus format v4 is not supported; re-run nesso index");
    REQUIRE(nesso::message(make(nesso::ErrorKind::CorpusRead, nesso::ErrorCause::CorruptFile, "bad.nesso")) ==
            "Error: failed to read the corpus 'bad.nesso': the file is corrupt");

    REQUIRE(nesso::message(make(nesso::ErrorKind::EmptyInput, nesso::ErrorCause::EmptyFile)) ==
            "nothing to search in 'in.log': the file is empty");
    REQUIRE(nesso::message(make(nesso::ErrorKind::EmptyInput, nesso::ErrorCause::EmptyCorpus, "")) ==
            "nothing to search: the corpus is empty");
    REQUIRE(nesso::message(make(nesso::ErrorKind::NoMatches, nesso::ErrorCause::None)) == "");

    nesso::Error withExtras = make(nesso::ErrorKind::Parse, nesso::ErrorCause::UnsupportedFormat);
    const std::string baseline = nesso::message(withExtras);
    withExtras.skippedLines = 3;
    withExtras.emptyLines = 1;
    withExtras.malformedLines = 1;
    withExtras.missingFieldLines = 1;
    withExtras.truncatedLines = 1;
    withExtras.binaryEntries = 2;
    withExtras.bundleSkips = {nesso::BundleSkipSummary{.path = "mix.tar", .directories = 2, .binary = 1}};
    REQUIRE(nesso::message(withExtras) == baseline);
}
