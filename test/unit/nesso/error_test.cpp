// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <error_map.hpp>

#include <string>

TEST_CASE("layer errors map onto a cause", "[nesso][error]")
{
    REQUIRE(nesso::causeOf(parser::ParseError::FileOpenFailure) == nesso::ErrorCause::FileOpen);
    REQUIRE(nesso::causeOf(parser::ParseError::UnsupportedFormat) == nesso::ErrorCause::UnsupportedFormat);
    REQUIRE(nesso::causeOf(parser::ParseError::BinaryFile) == nesso::ErrorCause::BinaryFile);

    REQUIRE(nesso::causeOf(embed::EmbedError::VocabLoadFailure) == nesso::ErrorCause::VocabLoad);
    REQUIRE(nesso::causeOf(embed::EmbedError::TokenizationFailure) == nesso::ErrorCause::Tokenization);
    REQUIRE(nesso::causeOf(embed::EmbedError::ModelLoadFailure) == nesso::ErrorCause::ModelLoad);
    REQUIRE(nesso::causeOf(embed::EmbedError::InferenceFailure) == nesso::ErrorCause::Inference);
    REQUIRE(nesso::causeOf(embed::EmbedError::InvalidInput) == nesso::ErrorCause::InvalidInput);

    REQUIRE(nesso::causeOf(core::EngineError::FileOpenFailure) == nesso::ErrorCause::FileOpen);
    REQUIRE(nesso::causeOf(core::EngineError::MismatchedDimensions) == nesso::ErrorCause::MismatchedDimensions);
    REQUIRE(nesso::causeOf(core::EngineError::DatabaseNotInitialized) == nesso::ErrorCause::EmptyEmbedding);
    REQUIRE(nesso::causeOf(core::EngineError::CorruptDatabase) == nesso::ErrorCause::CorruptFile);
}

TEST_CASE("each cause has a readable phrase", "[nesso][error]")
{
    REQUIRE(nesso::describe(nesso::ErrorCause::None).empty());
    REQUIRE(nesso::describe(nesso::ErrorCause::FileOpen) == "could not open the file");
    REQUIRE(nesso::describe(nesso::ErrorCause::UnsupportedFormat) == "unsupported format");
    REQUIRE(nesso::describe(nesso::ErrorCause::BinaryFile) == "the file is binary");
    REQUIRE(nesso::describe(nesso::ErrorCause::VocabLoad) == "could not load the vocabulary");
    REQUIRE(nesso::describe(nesso::ErrorCause::Tokenization) == "could not tokenize the text");
    REQUIRE(nesso::describe(nesso::ErrorCause::ModelLoad) == "could not load the model");
    REQUIRE(nesso::describe(nesso::ErrorCause::Inference) == "inference failed");
    REQUIRE(nesso::describe(nesso::ErrorCause::InvalidInput) == "the input is invalid");
    REQUIRE(nesso::describe(nesso::ErrorCause::MismatchedDimensions) == "the embedding width does not match");
    REQUIRE(nesso::describe(nesso::ErrorCause::EmptyEmbedding) == "the embedding is empty");
    REQUIRE(nesso::describe(nesso::ErrorCause::CorruptFile) == "the file is corrupt");
    REQUIRE(nesso::describe(nesso::ErrorCause::EmptyFile) == "the file is empty");
    REQUIRE(nesso::describe(nesso::ErrorCause::NothingIndexable) == "no indexable lines");
    REQUIRE(nesso::describe(nesso::ErrorCause::EmptyCorpus) == "the corpus is empty");
}

TEST_CASE("each failure kind names itself", "[nesso][error]")
{
    const auto text = [](nesso::ErrorKind kind, nesso::ErrorCause cause)
    {
        const nesso::Error error{.kind = kind, .cause = cause, .path = "in.log", .skippedLines = 0, .note = {}};
        return nesso::message(error);
    };

    REQUIRE(text(nesso::ErrorKind::EmbedderLoad, nesso::ErrorCause::ModelLoad).find("failed to load the embedder") !=
            std::string::npos);
    REQUIRE(text(nesso::ErrorKind::Parse, nesso::ErrorCause::UnsupportedFormat).find("failed to parse") !=
            std::string::npos);
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
