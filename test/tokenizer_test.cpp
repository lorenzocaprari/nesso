#include <catch2/catch_all.hpp>
#include <filesystem>
#include <mach_core/tokenizer.hpp>

#ifndef FIXTURES_DIR
#define FIXTURES_DIR "."
#endif

using namespace mach_core;

TEST_CASE("Tokenizer encodes WordPiece with special tokens", "[Tokenizer][core][Unit]")
{
    auto tok = Tokenizer::load(std::filesystem::path(FIXTURES_DIR) / "vocab.txt", 8);
    REQUIRE(tok.has_value());

    auto ids = tok->encodeIds("Hello World");
    REQUIRE(ids.has_value());
    REQUIRE(ids->size() == 8);
    REQUIRE(ids->front() == tok->clsId());
    REQUIRE((*ids)[1] == tok->encodeIds("hello").value()[1]);
    REQUIRE(ids->at(3) == tok->sepId());
    REQUIRE(ids->back() == tok->padId());

    const auto mask = tok->attentionMask(*ids);
    REQUIRE(mask.size() == 8);
    REQUIRE(mask[0] == 1);
    REQUIRE(mask[3] == 1);
    REQUIRE(mask[4] == 0);
}

TEST_CASE("Tokenizer maps unknown pieces to UNK", "[Tokenizer][core][Unit]")
{
    auto tok = Tokenizer::load(std::filesystem::path(FIXTURES_DIR) / "vocab.txt", 8);
    REQUIRE(tok.has_value());
    auto ids = tok->encodeIds("xyzzy");
    REQUIRE(ids.has_value());
    REQUIRE((*ids)[1] == tok->unkId());
}

TEST_CASE("Tokenizer fails on a missing vocab file", "[Tokenizer][core][Unit]")
{
    auto tok = Tokenizer::load("/no/such/vocab.txt");
    REQUIRE_FALSE(tok.has_value());
    REQUIRE(tok.error() == EngineError::TokenizerFailure);
}
