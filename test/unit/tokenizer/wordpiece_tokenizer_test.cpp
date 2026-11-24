#include <catch2/catch_all.hpp>
#include <embed/embed_types.hpp>
#include <embed/wordpiece_tokenizer.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unistd.h>

#ifndef NESSO_TEST_FIXTURES
#error "NESSO_TEST_FIXTURES must be defined"
#endif

namespace
{

class TempDir
{
  public:
    explicit TempDir(const std::string &name)
        : path_(std::filesystem::temp_directory_path() / (name + "_" + std::to_string(getpid())))
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() { std::filesystem::remove_all(path_); }

    TempDir(const TempDir &) = delete;
    auto operator=(const TempDir &) -> TempDir & = delete;

    [[nodiscard]] auto Path() const -> const std::filesystem::path & { return path_; }

  private:
    std::filesystem::path path_;
};

void WriteBytes(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
}

} // namespace

TEST_CASE("WordPieceTokenizer encodes known strings", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocab_path = std::filesystem::path(NESSO_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = nesso::embed::WordPieceTokenizer::FromVocabFile(vocab_path, 16);
    REQUIRE(tokenizer.has_value());

    const auto encoded = tokenizer->Encode("hello world");
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->input_ids == std::vector<int64_t>{2, 4, 5, 3});
    REQUIRE(encoded->attention_mask == std::vector<int64_t>(encoded->input_ids.size(), 1));
}

TEST_CASE("WordPieceTokenizer truncates at max sequence length", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocab_path = std::filesystem::path(NESSO_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = nesso::embed::WordPieceTokenizer::FromVocabFile(vocab_path, 4);
    REQUIRE(tokenizer.has_value());

    const auto encoded = tokenizer->Encode("hello world testing");
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->input_ids.size() == 4);
    REQUIRE(encoded->input_ids.back() == 3);
}

TEST_CASE("WordPieceTokenizer rejects empty input", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocab_path = std::filesystem::path(NESSO_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = nesso::embed::WordPieceTokenizer::FromVocabFile(vocab_path);
    REQUIRE(tokenizer.has_value());
    REQUIRE_FALSE(tokenizer->Encode("").has_value());
    REQUIRE_FALSE(nesso::embed::WordPieceTokenizer::FromVocabFile(vocab_path / "missing").has_value());
}

TEST_CASE("WordPieceTokenizer splits subwords and replaces an unknown word", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocab_path = std::filesystem::path(NESSO_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = nesso::embed::WordPieceTokenizer::FromVocabFile(vocab_path);
    REQUIRE(tokenizer.has_value());

    const auto split = tokenizer->Encode("helloing");
    REQUIRE(split.has_value());
    REQUIRE(split->input_ids == std::vector<int64_t>{2, 4, 6, 3});

    const auto unknown = tokenizer->Encode("hellozzz");
    REQUIRE(unknown.has_value());
    REQUIRE(unknown->input_ids == std::vector<int64_t>{2, 8, 3});

    const auto dropped = tokenizer->Encode("\xFF");
    REQUIRE(dropped.has_value());
    REQUIRE(dropped->input_ids == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->Encode("\xC0\x80")->input_ids == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->Encode("\xC3\x20")->input_ids == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->Encode("\xED\xA0\x80")->input_ids == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->Encode("\xF4\x90\x80\x80")->input_ids == std::vector<int64_t>{2, 3});
    REQUIRE(tokenizer->Encode("\xE2")->input_ids == std::vector<int64_t>{2, 3});
}

TEST_CASE("WordPieceTokenizer decomposes a four-part character", "[WordPieceTokenizer][embed][Unit]")
{
    const std::filesystem::path vocab_path = std::filesystem::path(NESSO_TEST_FIXTURES) / "tiny_vocab.txt";
    const auto tokenizer = nesso::embed::WordPieceTokenizer::FromVocabFile(vocab_path);
    REQUIRE(tokenizer.has_value());
    // U+1F82 decomposes into four parts, including two-byte UTF-8 scalars.
    const auto encoded = tokenizer->Encode("\xE1\xBE\x82");
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->input_ids.front() == 2);
    REQUIRE(encoded->input_ids.back() == 3);
}

TEST_CASE("WordPieceTokenizer rejects a vocabulary without each special token", "[WordPieceTokenizer][embed][Unit]")
{
    const TempDir directory("nesso-vocab-specials");
    const auto write_vocab = [&](std::string_view name, std::string_view body) -> std::filesystem::path
    {
        const auto path = directory.Path() / std::string(name);
        WriteBytes(path, body);
        return path;
    };

    const auto missing_cls =
        nesso::embed::WordPieceTokenizer::FromVocabFile(write_vocab("no-cls.txt", "[SEP]\n[UNK]\nhello\n"));
    REQUIRE_FALSE(missing_cls.has_value());
    REQUIRE(missing_cls.error() == nesso::embed::EmbedError::VocabLoadFailure);

    const auto missing_sep =
        nesso::embed::WordPieceTokenizer::FromVocabFile(write_vocab("no-sep.txt", "[CLS]\n[UNK]\nhello\n"));
    REQUIRE_FALSE(missing_sep.has_value());
    REQUIRE(missing_sep.error() == nesso::embed::EmbedError::VocabLoadFailure);

    const auto missing_unk =
        nesso::embed::WordPieceTokenizer::FromVocabFile(write_vocab("no-unk.txt", "[CLS]\n[SEP]\nhello\n"));
    REQUIRE_FALSE(missing_unk.has_value());
    REQUIRE(missing_unk.error() == nesso::embed::EmbedError::VocabLoadFailure);

    const auto empty = nesso::embed::WordPieceTokenizer::FromVocabFile(write_vocab("empty.txt", ""));
    REQUIRE_FALSE(empty.has_value());
    REQUIRE(empty.error() == nesso::embed::EmbedError::VocabLoadFailure);
}

TEST_CASE("WordPieceTokenizer loads a CRLF vocabulary", "[WordPieceTokenizer][embed][Unit]")
{
    const TempDir directory("nesso-vocab-crlf");
    const auto path = directory.Path() / "vocab.txt";
    WriteBytes(path, "[PAD]\r\n[unused0]\r\n[CLS]\r\n[SEP]\r\nhello\r\nworld\r\n#"
                     "#ing\r\ntesting\r\n[UNK]\r\n");

    const auto tokenizer = nesso::embed::WordPieceTokenizer::FromVocabFile(path, 16);
    REQUIRE(tokenizer.has_value());
    const auto encoded = tokenizer->Encode("hello world");
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->input_ids == std::vector<int64_t>{2, 4, 5, 3});
}

TEST_CASE("WordPieceTokenizer matches BERT uncased token ids", "[WordPieceTokenizer][embed][Unit]")
{
#ifndef NESSO_MODELS_DIR
#error "NESSO_MODELS_DIR must be defined"
#endif
    const std::filesystem::path vocab_path = std::filesystem::path(NESSO_MODELS_DIR) / "vocab.txt";
    if (!std::filesystem::is_regular_file(vocab_path))
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const auto tokenizer = nesso::embed::WordPieceTokenizer::FromVocabFile(vocab_path);
    REQUIRE(tokenizer.has_value());

    const auto sentence = tokenizer->Encode("This is an example sentence");
    REQUIRE(sentence.has_value());
    REQUIRE(sentence->input_ids == std::vector<int64_t>{101, 2023, 2003, 2019, 2742, 6251, 102});

    const auto punctuated = tokenizer->Encode("Hello, world!");
    REQUIRE(punctuated.has_value());
    REQUIRE(punctuated->input_ids == std::vector<int64_t>{101, 7592, 1010, 2088, 999, 102});

    const auto accents = tokenizer->Encode("café naïve");
    REQUIRE(accents.has_value());
    REQUIRE(accents->input_ids == std::vector<int64_t>{101, 7668, 15743, 102});

    const auto dotted = tokenizer->Encode("İstanbul");
    REQUIRE(dotted.has_value());
    REQUIRE(dotted->input_ids == std::vector<int64_t>{101, 9960, 102});

    const auto cjk = tokenizer->Encode("你好世界");
    REQUIRE(cjk.has_value());
    REQUIRE(cjk->input_ids == std::vector<int64_t>{101, 100, 100, 1745, 100, 102});

    const auto hangul = tokenizer->Encode("가");
    REQUIRE(hangul.has_value());
    REQUIRE(hangul->input_ids == std::vector<int64_t>{101, 1455, 30006, 102});

    const auto control = tokenizer->Encode(std::string("hello\0world", 11));
    REQUIRE(control.has_value());
    REQUIRE(control->input_ids == std::vector<int64_t>{101, 7592, 11108, 102});

    const auto too_long = tokenizer->Encode(std::string(101, 'a'));
    REQUIRE(too_long.has_value());
    REQUIRE(too_long->input_ids == std::vector<int64_t>{101, 100, 102});

    const auto spaced = tokenizer->Encode("hello\u00A0world");
    REQUIRE(spaced.has_value());
    REQUIRE(spaced->input_ids == std::vector<int64_t>{101, 7592, 2088, 102});

    const auto dash = tokenizer->Encode("hello—world");
    REQUIRE(dash.has_value());
    REQUIRE(dash->input_ids == std::vector<int64_t>{101, 7592, 1517, 2088, 102});

    const auto syllable = tokenizer->Encode("각");
    REQUIRE(syllable.has_value());
    REQUIRE(syllable->input_ids == std::vector<int64_t>{101, 1455, 30006, 30020, 102});

    const auto emoji = tokenizer->Encode("😀");
    REQUIRE(emoji.has_value());
    REQUIRE(emoji->input_ids == std::vector<int64_t>{101, 100, 102});

    const auto zero_width = tokenizer->Encode("\u200Bhello");
    REQUIRE(zero_width.has_value());
    REQUIRE(zero_width->input_ids == std::vector<int64_t>{101, 7592, 102});

    const auto tabbed = tokenizer->Encode("hello\tworld");
    REQUIRE(tabbed.has_value());
    REQUIRE(tabbed->input_ids == std::vector<int64_t>{101, 7592, 2088, 102});

    const auto padded = tokenizer->Encode("  hello   world  ");
    REQUIRE(padded.has_value());
    REQUIRE(padded->input_ids == std::vector<int64_t>{101, 7592, 2088, 102});

    const auto soh = tokenizer->Encode("hel\x01lo");
    REQUIRE(soh.has_value());
    REQUIRE(soh->input_ids == std::vector<int64_t>{101, 7592, 102});

    const auto bangs = tokenizer->Encode("!!");
    REQUIRE(bangs.has_value());
    REQUIRE(bangs->input_ids == std::vector<int64_t>{101, 999, 999, 102});

    const auto adjacent_punct = tokenizer->Encode("hello!!world");
    REQUIRE(adjacent_punct.has_value());
    REQUIRE(adjacent_punct->input_ids == std::vector<int64_t>{101, 7592, 999, 999, 2088, 102});
}
