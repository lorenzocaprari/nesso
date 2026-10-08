#include <catch2/catch_all.hpp>
#include <embed/wordpiece_tokenizer.hpp>

#include <filesystem>

#ifndef NESSO_TEST_FIXTURES
#error "NESSO_TEST_FIXTURES must be defined"
#endif

TEST_CASE("WordPieceTokenizer encodes known strings", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocabPath = std::filesystem::path(NESSO_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = embed::WordPieceTokenizer::fromVocabFile(vocabPath, 16);
    REQUIRE(tokenizer.has_value());

    const auto encoded = tokenizer->encode("hello world");
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->inputIds == std::vector<int64_t>{2, 4, 5, 3});
    REQUIRE(encoded->attentionMask == std::vector<int64_t>(encoded->inputIds.size(), 1));
}

TEST_CASE("WordPieceTokenizer truncates at max sequence length", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocabPath = std::filesystem::path(NESSO_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = embed::WordPieceTokenizer::fromVocabFile(vocabPath, 4);
    REQUIRE(tokenizer.has_value());

    const auto encoded = tokenizer->encode("hello world testing");
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->inputIds.size() == 4);
    REQUIRE(encoded->inputIds.back() == 3);
}

TEST_CASE("WordPieceTokenizer rejects empty input", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocabPath = std::filesystem::path(NESSO_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = embed::WordPieceTokenizer::fromVocabFile(vocabPath);
    REQUIRE(tokenizer.has_value());
    REQUIRE_FALSE(tokenizer->encode("").has_value());
    REQUIRE_FALSE(embed::WordPieceTokenizer::fromVocabFile(vocabPath / "missing").has_value());
}

TEST_CASE("WordPieceTokenizer splits subwords and replaces an unknown word", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocabPath = std::filesystem::path(NESSO_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = embed::WordPieceTokenizer::fromVocabFile(vocabPath);
    REQUIRE(tokenizer.has_value());

    const auto split = tokenizer->encode("helloing");
    REQUIRE(split.has_value());
    REQUIRE(split->inputIds == std::vector<int64_t>{2, 4, 6, 3});

    const auto unknown = tokenizer->encode("hellozzz");
    REQUIRE(unknown.has_value());
    REQUIRE(unknown->inputIds == std::vector<int64_t>{2, 0, 3});

    const auto dropped = tokenizer->encode("\xFF");
    REQUIRE(dropped.has_value());
    REQUIRE(dropped->inputIds == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->encode("\xC0\x80")->inputIds == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->encode("\xC3\x20")->inputIds == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->encode("\xED\xA0\x80")->inputIds == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->encode("\xF4\x90\x80\x80")->inputIds == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->encode("\xE2")->inputIds == std::vector<int64_t>{2, 3});
}

TEST_CASE("WordPieceTokenizer matches BERT uncased token ids", "[WordPieceTokenizer][embed][Unit]")
{
#ifndef NESSO_MODELS_DIR
#error "NESSO_MODELS_DIR must be defined"
#endif
    const std::filesystem::path vocabPath = std::filesystem::path(NESSO_MODELS_DIR) / "vocab.txt";
    if (!std::filesystem::is_regular_file(vocabPath))
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const auto tokenizer = embed::WordPieceTokenizer::fromVocabFile(vocabPath);
    REQUIRE(tokenizer.has_value());

    const auto sentence = tokenizer->encode("This is an example sentence");
    REQUIRE(sentence.has_value());
    REQUIRE(sentence->inputIds == std::vector<int64_t>{101, 2023, 2003, 2019, 2742, 6251, 102});

    const auto punctuated = tokenizer->encode("Hello, world!");
    REQUIRE(punctuated.has_value());
    REQUIRE(punctuated->inputIds == std::vector<int64_t>{101, 7592, 1010, 2088, 999, 102});

    const auto accents = tokenizer->encode("café naïve");
    REQUIRE(accents.has_value());
    REQUIRE(accents->inputIds == std::vector<int64_t>{101, 7668, 15743, 102});

    const auto dotted = tokenizer->encode("İstanbul");
    REQUIRE(dotted.has_value());
    REQUIRE(dotted->inputIds == std::vector<int64_t>{101, 9960, 102});

    const auto cjk = tokenizer->encode("你好世界");
    REQUIRE(cjk.has_value());
    REQUIRE(cjk->inputIds == std::vector<int64_t>{101, 100, 100, 1745, 100, 102});

    const auto hangul = tokenizer->encode("가");
    REQUIRE(hangul.has_value());
    REQUIRE(hangul->inputIds == std::vector<int64_t>{101, 1455, 30006, 102});

    const auto control = tokenizer->encode(std::string("hello\0world", 11));
    REQUIRE(control.has_value());
    REQUIRE(control->inputIds == std::vector<int64_t>{101, 7592, 11108, 102});

    const auto tooLong = tokenizer->encode(std::string(101, 'a'));
    REQUIRE(tooLong.has_value());
    REQUIRE(tooLong->inputIds == std::vector<int64_t>{101, 100, 102});

    const auto spaced = tokenizer->encode("hello\u00A0world");
    REQUIRE(spaced.has_value());
    REQUIRE(spaced->inputIds == std::vector<int64_t>{101, 7592, 2088, 102});

    const auto dash = tokenizer->encode("hello—world");
    REQUIRE(dash.has_value());
    REQUIRE(dash->inputIds == std::vector<int64_t>{101, 7592, 1517, 2088, 102});

    const auto syllable = tokenizer->encode("각");
    REQUIRE(syllable.has_value());
    REQUIRE(syllable->inputIds == std::vector<int64_t>{101, 1455, 30006, 30020, 102});

    const auto emoji = tokenizer->encode("😀");
    REQUIRE(emoji.has_value());
    REQUIRE(emoji->inputIds == std::vector<int64_t>{101, 100, 102});

    const auto zeroWidth = tokenizer->encode("\u200Bhello");
    REQUIRE(zeroWidth.has_value());
    REQUIRE(zeroWidth->inputIds == std::vector<int64_t>{101, 7592, 102});

    const auto tabbed = tokenizer->encode("hello\tworld");
    REQUIRE(tabbed.has_value());
    REQUIRE(tabbed->inputIds == std::vector<int64_t>{101, 7592, 2088, 102});
}
