#include <catch2/catch_all.hpp>
#include <mach_embed/wordpiece_tokenizer.hpp>

#include <filesystem>

#ifndef MACH1_TEST_FIXTURES
#error "MACH1_TEST_FIXTURES must be defined"
#endif

TEST_CASE("WordPieceTokenizer encodes known strings", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocabPath = std::filesystem::path(MACH1_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = mach_embed::WordPieceTokenizer::fromVocabFile(vocabPath, 16);
    REQUIRE(tokenizer.has_value());

    const auto encoded = tokenizer->encode("hello world");
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->inputIds == std::vector<int64_t>{2, 4, 5, 3});
    REQUIRE(encoded->attentionMask == std::vector<int64_t>(encoded->inputIds.size(), 1));
}

TEST_CASE("WordPieceTokenizer truncates at max sequence length", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocabPath = std::filesystem::path(MACH1_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = mach_embed::WordPieceTokenizer::fromVocabFile(vocabPath, 4);
    REQUIRE(tokenizer.has_value());

    const auto encoded = tokenizer->encode("hello world testing");
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->inputIds.size() == 4);
    REQUIRE(encoded->inputIds.back() == 3);
}

TEST_CASE("WordPieceTokenizer rejects empty input", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocabPath = std::filesystem::path(MACH1_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = mach_embed::WordPieceTokenizer::fromVocabFile(vocabPath);
    REQUIRE(tokenizer.has_value());
    REQUIRE_FALSE(tokenizer->encode("").has_value());
}
