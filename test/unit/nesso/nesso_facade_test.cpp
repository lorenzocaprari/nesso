// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/lexical_terms.hpp>
#include <embed/batch_plan.hpp>
#include <embed/embedder.hpp>
#include <embed/onnx_embedder.hpp>
#include <nesso/model_paths.hpp>
#include <nesso/nesso.hpp>
#include <parser/template_mask.hpp>

#include "test_embedder_identity.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

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

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    TempDir(TempDir &&) = delete;
    TempDir &operator=(TempDir &&) = delete;

    ~TempDir() { std::filesystem::remove_all(path_); }

    [[nodiscard]] const std::filesystem::path &Path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void WriteBytes(const std::filesystem::path &path, const void *data, size_t size)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    REQUIRE(output);
}

nesso::core::CorpusPolicy TestPolicy()
{
    const auto identity = TestEmbedderIdentity();
    return {{"batch", "window:1024,max:32,tokens:1024,order:length-then-input,run:single"},
            {"bm25", std::string{nesso::core::BM25_POLICY}},
            {"dedup", "masked-v1"},
            {"dimensions", std::to_string(identity.dimensions)},
            {"lexical.terms", std::string{nesso::core::LEXICAL_TERMS_VERSION}},
            {"mask", std::string{nesso::parser::MASK_RULE_VERSION}},
            {"model.id", identity.model_id},
            {"model.quantization", identity.quantized ? "int8" : "fp32"},
            {"model.sha256", identity.model_sha256},
            {"tokens.max", "256"},
            {"truncate", "4096-codepoints"},
            {"vocab.sha256", identity.vocab_sha256}};
}

void WriteFixtureCorpus(const std::filesystem::path &path, nesso::core::CorpusPolicy policy, bool empty = false)
{
    auto writer = nesso::core::CorpusWriter::Create(path, std::move(policy));
    REQUIRE(writer);
    if (!empty)
    {
        REQUIRE(writer->AddSource({.path = "a.log", .stored_only = true}) == 0);
        REQUIRE(writer->AddDistinct("alpha", std::array<float, 4>{1.F, 0.F, 0.F, 0.F}) == 0);
        const nesso::core::CorpusLine line{.line_number = 1, .distinct_id = 0, .stored_text = "alpha"};
        const std::vector<std::string> terms = nesso::core::LexicalTerms("alpha");
        REQUIRE(writer->AddLines(std::span{&line, 1}, std::span{&terms, 1}));
    }
    REQUIRE(writer->Finish());
}

nesso::Nesso WithMissingModel(const std::filesystem::path &root)
{
    return nesso::Nesso{nesso::Config{.model_dir = root / "no-models"}};
}

std::uint32_t MixByte(std::uint32_t hash, unsigned char byte)
{
    hash ^= byte;
    hash *= 16777619u;
    return hash;
}

std::uint32_t MixText(std::uint32_t hash, std::string_view text)
{
    for (const char byte : text)
    {
        hash = MixByte(hash, static_cast<unsigned char>(byte));
    }
    return hash;
}

std::vector<float> VectorFor(std::string_view text, std::uint32_t window)
{
    std::vector<float> values(4);
    std::uint32_t hash = MixText(2166136261u, text);
    hash = MixByte(hash, static_cast<unsigned char>(window & 0xffu));
    hash = MixByte(hash, static_cast<unsigned char>((window >> 8U) & 0xffu));
    hash = MixByte(hash, static_cast<unsigned char>((window >> 16U) & 0xffu));
    hash = MixByte(hash, static_cast<unsigned char>((window >> 24U) & 0xffu));
    for (size_t index = 0; index < values.size(); ++index)
    {
        hash = MixByte(hash, static_cast<unsigned char>(index));
        values[index] = static_cast<float>(hash % 1000U) / 1000.F;
    }
    return values;
}

class RecordingEmbedder final : public nesso::embed::Embedder
{
  public:
    explicit RecordingEmbedder(size_t sessions = 1, nesso::embed::EmbedderIdentity identity = TestEmbedderIdentity())
        : sessions_(sessions), identity_(std::move(identity))
    {
    }

    [[nodiscard]] size_t sessions() const noexcept override { return sessions_; }

    [[nodiscard]] std::expected<nesso::embed::EmbedderIdentity, nesso::embed::EmbedError> identity() const override
    {
        return identity_;
    }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    EmbedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
    {
        batches_.push_back(texts.size());
        std::vector<std::vector<float>> embeddings;
        embeddings.reserve(texts.size());
        constexpr size_t mix_window = 32;
        for (size_t offset = 0; offset < texts.size(); offset += mix_window)
        {
            const size_t count = std::min(mix_window, texts.size() - offset);
            std::uint32_t window = 2166136261u;
            for (size_t index = 0; index < count; ++index)
            {
                window = MixText(window, texts[offset + index]);
            }
            for (size_t index = 0; index < count; ++index)
            {
                embeddings.push_back(VectorFor(texts[offset + index], window));
            }
        }
        if (progress)
        {
            progress(texts.size(), texts.size());
        }
        return embeddings;
    }

    [[nodiscard]] const std::vector<size_t> &Batches() const { return batches_; }

  private:
    size_t sessions_;
    nesso::embed::EmbedderIdentity identity_;
    mutable std::vector<size_t> batches_;
};

std::string LetterSuffix(std::size_t index)
{
    std::string suffix;
    do
    {
        suffix.push_back(static_cast<char>('a' + static_cast<int>(index % 26)));
        index /= 26;
    } while (index > 0);
    return suffix;
}

void WriteLines(const std::filesystem::path &path, size_t count, std::string_view tag)
{
    std::ofstream output(path);
    REQUIRE(output);
    for (size_t index = 0; index < count; ++index)
    {
        output << tag << '-' << LetterSuffix(index) << " database connection\n";
    }
    REQUIRE(output);
}

std::string ReadFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void ExpectFlushSizes(const std::vector<size_t> &batches, size_t total)
{
    const size_t flush = nesso::embed::BATCH_WINDOW;
    REQUIRE(flush > 0);
    REQUIRE_FALSE(batches.empty());
    size_t seen = 0;
    for (size_t index = 0; index + 1 < batches.size(); ++index)
    {
        CHECK(batches[index] == flush);
        seen += batches[index];
    }
    const size_t tail = total % flush == 0 ? flush : total % flush;
    CHECK(batches.back() == tail);
    seen += batches.back();
    CHECK(seen == total);
}

} // namespace

TEST_CASE("text operations report model and corpus failures", "[nesso][text]")
{
    const TempDir dir{"nesso_facade_text"};
    const auto log = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const auto corrupt = dir.Path() / "corrupt.nesso";
    const auto empty = dir.Path() / "empty.nesso";
    const auto other_model = dir.Path() / "other-model.nesso";
    const auto future_version = dir.Path() / "future-version.nesso";
    const std::array<char, 4> junk{'b', 'a', 'd', '!'};
    WriteBytes(corrupt, junk.data(), junk.size());
    WriteFixtureCorpus(empty, TestPolicy(), true);
    auto other_policy = TestPolicy();
    other_policy["model.id"] = "other/model";
    WriteFixtureCorpus(other_model, std::move(other_policy));
    WriteFixtureCorpus(future_version, TestPolicy());
    {
        std::fstream patch(future_version, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(patch);
        patch.seekp(4);
        const std::array<char, 4> version{4, 0, 0, 0};
        patch.write(version.data(), version.size());
        REQUIRE(patch);
    }

    const nesso::Nesso app = WithMissingModel(dir.Path());

    const auto grepped = app.Grep({.query = "database", .files = {log}});
    REQUIRE_FALSE(grepped.has_value());
    REQUIRE(grepped.error().kind == nesso::ErrorKind::EmbedderLoad);
    REQUIRE(grepped.error().cause == nesso::ErrorCause::ModelLoad);
    REQUIRE(grepped.error().path == dir.Path() / "no-models");

    const auto indexed = app.Index({.files = {log}, .output = dir.Path() / "out.nesso"});
    REQUIRE_FALSE(indexed.has_value());
    REQUIRE(indexed.error().kind == nesso::ErrorKind::EmbedderLoad);

    const auto unreadable = app.Search({.query = "database", .index = corrupt});
    REQUIRE_FALSE(unreadable.has_value());
    REQUIRE(unreadable.error().kind == nesso::ErrorKind::CorpusRead);
    REQUIRE(unreadable.error().cause == nesso::ErrorCause::CorruptFile);
    REQUIRE(unreadable.error().path == corrupt);

    const auto nothing = app.Search({.query = "database", .index = empty});
    REQUIRE_FALSE(nothing.has_value());
    REQUIRE(nothing.error().kind == nesso::ErrorKind::EmptyInput);
    REQUIRE(nothing.error().cause == nesso::ErrorCause::EmptyCorpus);

    const nesso::Nesso fake_app{nesso::Config{}, std::make_unique<RecordingEmbedder>()};
    const auto mismatched = fake_app.Search({.query = "database", .index = other_model});
    REQUIRE_FALSE(mismatched.has_value());
    REQUIRE(mismatched.error().kind == nesso::ErrorKind::CorpusRead);
    REQUIRE(mismatched.error().cause == nesso::ErrorCause::ModelMismatch);
    REQUIRE(mismatched.error().path == other_model);
    REQUIRE(nesso::Message(mismatched.error()).contains("built with a different model"));

    const auto newer = app.Search({.query = "database", .index = future_version});
    REQUIRE_FALSE(newer.has_value());
    REQUIRE(newer.error().kind == nesso::ErrorKind::CorpusRead);
    REQUIRE(newer.error().cause == nesso::ErrorCause::UnsupportedCorpusVersion);
}

TEST_CASE("search names the first identity key that differs from the loaded model", "[nesso][text]")
{
    const TempDir dir{"nesso_facade_identity"};
    const nesso::Nesso app{nesso::Config{}, std::make_unique<RecordingEmbedder>()};
    const auto write = [&dir](const std::string &name, const nesso::core::CorpusPolicy &policy)
    {
        const auto path = dir.Path() / name;
        auto writer = nesso::core::CorpusWriter::Create(path, policy);
        REQUIRE(writer);
        REQUIRE(writer->AddSource({.path = "a.log", .stored_only = true}) == 0);
        const std::vector<float> vector(std::stoul(policy.at("dimensions")), 0.5F);
        REQUIRE(writer->AddDistinct("alpha", vector) == 0);
        const nesso::core::CorpusLine line{.line_number = 1, .distinct_id = 0, .stored_text = "alpha"};
        const std::vector<std::string> terms = nesso::core::LexicalTerms("alpha");
        REQUIRE(writer->AddLines(std::span{&line, 1}, std::span{&terms, 1}));
        REQUIRE(writer->Finish());
        return path;
    };
    const auto refused = [&app](const std::filesystem::path &path)
    {
        const auto searched = app.Search({.query = "alpha", .index = path});
        REQUIRE_FALSE(searched.has_value());
        CHECK(searched.error().kind == nesso::ErrorKind::CorpusRead);
        CHECK(searched.error().cause == nesso::ErrorCause::ModelMismatch);
        CHECK(searched.error().path == path);
        REQUIRE(searched.error().model_mismatch.has_value());
        return *searched.error().model_mismatch;
    };

    struct Case
    {
        std::string key;
        std::string stored;
        std::string loaded;
    };
    const std::array<Case, 6> cases{
        Case{.key = "model.id", .stored = "other/model", .loaded = "test/model"},
        Case{.key = "model.sha256", .stored = std::string(64, 'c'), .loaded = std::string(64, 'a')},
        Case{.key = "vocab.sha256", .stored = std::string(64, 'd'), .loaded = std::string(64, 'b')},
        Case{.key = "model.quantization", .stored = "fp32", .loaded = "int8"},
        Case{.key = "dimensions", .stored = "2", .loaded = "4"},
        Case{.key = "embed.prefix", .stored = "query-passage-v1", .loaded = "none"}};
    for (const Case &expected : cases)
    {
        CAPTURE(expected.key);
        auto policy = TestPolicy();
        policy[expected.key] = expected.stored;
        const auto path = write(expected.key + ".nesso", policy);
        const nesso::ModelMismatchDetail detail = refused(path);
        CHECK(detail.key == expected.key);
        CHECK(detail.stored == expected.stored);
        CHECK(detail.loaded == expected.loaded);
        CHECK(nesso::Message(app.Search({.query = "alpha", .index = path}).error()) ==
              std::format("Error: failed to read the corpus '{}': the corpus was built with a different model ({}: "
                          "stored {}, loaded {}); re-run nesso index",
                          path.string(), expected.key, expected.stored, expected.loaded));
    }

    auto both = TestPolicy();
    both["model.id"] = "other/model";
    both["embed.prefix"] = "query-passage-v1";
    CHECK(refused(write("both.nesso", both)).key == "model.id");

    const auto unrecorded = write("unrecorded.nesso", TestPolicy());
    REQUIRE(nesso::core::MapCorpusFile(unrecorded));
    CHECK_FALSE(nesso::core::MapCorpusFile(unrecorded)->corpus.Policy().contains("embed.prefix"));
    CHECK(app.Search({.query = "alpha", .index = unrecorded}).has_value());
    auto recorded = TestPolicy();
    recorded["embed.prefix"] = "none";
    CHECK(app.Search({.query = "alpha", .index = write("recorded.nesso", recorded)}).has_value());

    const auto log = dir.Path() / "app.log";
    const char text[] = {'a', 'l', 'p', 'h', 'a', '\n'};
    WriteBytes(log, text, sizeof(text));
    const auto indexed = dir.Path() / "indexed.nesso";
    REQUIRE(app.Index({.files = {log}, .output = indexed}).has_value());
    const auto mapped = nesso::core::MapCorpusFile(indexed);
    REQUIRE(mapped);
    REQUIRE(mapped->corpus.Policy().contains("embed.prefix"));
    CHECK(mapped->corpus.Policy().at("embed.prefix") == "none");
    CHECK(ReadFile(indexed).contains("\nembed.prefix=none\n"));
}

TEST_CASE("search warns on a corpus embedded in padded runs", "[nesso][text]")
{
    const TempDir dir{"nesso_facade_padded_runs"};
    const nesso::Nesso app{nesso::Config{}, std::make_unique<RecordingEmbedder>()};

    const auto current = dir.Path() / "current.nesso";
    WriteFixtureCorpus(current, TestPolicy());
    const auto current_search = app.Search({.query = "alpha", .index = current});
    REQUIRE(current_search.has_value());
    CHECK_FALSE(current_search->policy_mismatch);

    // The batch value index wrote before each text ran alone (#362).
    auto padded_policy = TestPolicy();
    padded_policy["batch"] = "window:1024,max:32,tokens:1024,order:length-then-input";
    const auto padded = dir.Path() / "padded.nesso";
    WriteFixtureCorpus(padded, std::move(padded_policy));
    const auto padded_search = app.Search({.query = "alpha", .index = padded});
    REQUIRE(padded_search.has_value());
    CHECK(padded_search->policy_mismatch);
    CHECK_FALSE(padded_search->matches.empty());
}

TEST_CASE("grep, index, and search run the text stages", "[nesso][text]")
{
    const std::filesystem::path model_dir{NESSO_MODELS_DIR};
    if (!std::filesystem::is_regular_file(model_dir / "model.onnx") ||
        !std::filesystem::is_regular_file(model_dir / "vocab.txt"))
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const TempDir dir{"nesso_facade_text_stages"};
    const auto log = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const nesso::Nesso app{nesso::Config{.model_dir = model_dir}};

    const auto grepped = app.Grep({.query = "database connection error", .files = {log}});
    REQUIRE(grepped.has_value());
    REQUIRE_FALSE(grepped->matches.empty());

    const auto corpus = dir.Path() / "corpus.nesso";
    const auto indexed = app.Index({.files = {log}, .output = corpus});
    REQUIRE(indexed.has_value());
    REQUIRE(indexed->chunks >= 1);

    const auto found = app.Search({.query = "database connection error", .index = corpus});
    REQUIRE(found.has_value());
    REQUIRE_FALSE(found->matches.empty());
}

TEST_CASE("a missing default model lists every directory it tried", "[nesso][text]")
{
    if (nesso::ContainsModelFiles("/usr/share/nesso"))
    {
        SKIP("packaged model directory is installed");
    }

    const TempDir dir{"nesso_facade_default_model"};
    const auto previous_dir = std::filesystem::current_path();
    const char *model_env = std::getenv("NESSO_MODEL_DIR");
    const char *xdg_env = std::getenv("XDG_DATA_HOME");
    const std::optional<std::string> saved_model =
        model_env == nullptr ? std::nullopt : std::optional<std::string>{model_env};
    const std::optional<std::string> saved_xdg =
        xdg_env == nullptr ? std::nullopt : std::optional<std::string>{xdg_env};
    const auto restore = [&]
    {
        std::filesystem::current_path(previous_dir);
        if (saved_model)
        {
            setenv("NESSO_MODEL_DIR", saved_model->c_str(), 1);
        }
        else
        {
            unsetenv("NESSO_MODEL_DIR");
        }
        if (saved_xdg)
        {
            setenv("XDG_DATA_HOME", saved_xdg->c_str(), 1);
        }
        else
        {
            unsetenv("XDG_DATA_HOME");
        }
    };

    unsetenv("NESSO_MODEL_DIR");
    setenv("XDG_DATA_HOME", (dir.Path() / "xdg").c_str(), 1);
    std::filesystem::current_path(dir.Path());
    const nesso::Nesso app{nesso::Config{}};
    const auto log = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const auto grepped = app.Grep({.query = "database", .files = {log}});
    restore();

    REQUIRE_FALSE(grepped.has_value());
    REQUIRE(grepped.error().kind == nesso::ErrorKind::EmbedderLoad);
    REQUIRE(grepped.error().note.contains(" and "));
    REQUIRE(grepped.error().note.contains(','));
}

TEST_CASE("index fails when no chunks are produced and does not replace the corpus", "[nesso][text]")
{
    const TempDir dir{"nesso_index_zero_chunks"};
    auto embedder = std::make_unique<RecordingEmbedder>();
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto empty = dir.Path() / "empty.log";
    const auto also_empty = dir.Path() / "also-empty.log";
    const auto kept = dir.Path() / "kept.nesso";
    const auto blocked_parent = dir.Path() / "not-a-dir";
    const char keep[] = {'k', 'e', 'e', 'p'};
    WriteBytes(empty, keep, 0);
    WriteBytes(also_empty, keep, 0);
    WriteBytes(kept, keep, sizeof(keep));
    WriteBytes(blocked_parent, keep, sizeof(keep));

    const auto several = app.Index({.files = {empty, also_empty}, .output = kept});
    REQUIRE_FALSE(several.has_value());
    CHECK(several.error().cause == nesso::ErrorCause::EmptyFile);
    CHECK(several.error().path.empty());
    CHECK(ReadFile(kept) == "keep");
    CHECK_FALSE(std::filesystem::exists(kept.string() + ".records"));

    const auto blocked = app.Index({.files = {empty}, .output = dir.Path()});
    REQUIRE_FALSE(blocked.has_value());
    CHECK(blocked.error().kind == nesso::ErrorKind::CorpusWrite);
    CHECK(blocked.error().cause == nesso::ErrorCause::FileOpen);

    const auto unopenable = app.Index({.files = {empty}, .output = blocked_parent / "out.nesso"});
    REQUIRE_FALSE(unopenable.has_value());
    CHECK(unopenable.error().kind == nesso::ErrorKind::CorpusWrite);
    CHECK(recorder->Batches().empty());
}

TEST_CASE("index refuses an output that is one of its inputs", "[nesso][text]")
{
    const TempDir dir{"nesso_index_output_is_input"};
    auto embedder = std::make_unique<RecordingEmbedder>();
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto log = dir.Path() / "app.log";
    const auto other = dir.Path() / "other.log";
    const auto link = dir.Path() / "link.log";
    const char text[] = {'o', 'k', '\n'};
    WriteBytes(log, text, sizeof(text));
    WriteBytes(other, text, sizeof(text));
    std::filesystem::create_hard_link(log, link);

    for (const auto &output : {log, dir.Path() / "." / "app.log", link})
    {
        const auto refused = app.Index({.files = {other, log}, .output = output});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().kind == nesso::ErrorKind::CorpusWrite);
        CHECK(refused.error().cause == nesso::ErrorCause::OutputIsInput);
        CHECK(refused.error().path == output);
        CHECK(ReadFile(log) == "ok\n");
    }
    CHECK(recorder->Batches().empty());
}

TEST_CASE("Nesso is movable", "[nesso]")
{
    const TempDir dir{"nesso_facade_move"};
    nesso::Nesso first{nesso::Config{}};
    nesso::Nesso second = std::move(first);
    first = std::move(second);
    const auto missing = first.Search({.query = "database", .index = dir.Path() / "missing.nesso"});
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().kind == nesso::ErrorKind::CorpusRead);
    REQUIRE(missing.error().cause == nesso::ErrorCause::FileOpen);
}

TEST_CASE("a multi-session embedder flushes a fixed window", "[nesso][text]")
{
    const TempDir dir{"nesso_facade_sessions"};
    const auto first = dir.Path() / "a.log";
    const auto second = dir.Path() / "b.log";
    constexpr size_t first_lines = 80;
    constexpr size_t second_lines = 40;
    constexpr size_t total = first_lines + second_lines;
    WriteLines(first, first_lines, "alpha");
    WriteLines(second, second_lines, "beta");

    const RecordingEmbedder probe;
    CHECK(probe.sessions() == 1);

    auto single_embedder = std::make_unique<RecordingEmbedder>();
    auto multi_embedder = std::make_unique<RecordingEmbedder>(3);
    const RecordingEmbedder *single = single_embedder.get();
    const RecordingEmbedder *multi = multi_embedder.get();
    const nesso::Nesso single_app{nesso::Config{}, std::move(single_embedder)};
    const nesso::Nesso multi_app{nesso::Config{}, std::move(multi_embedder)};

    const auto single_corpus = dir.Path() / "single.nesso";
    const auto multi_corpus = dir.Path() / "multi.nesso";
    const auto single_indexed = single_app.Index({.files = {first, second}, .output = single_corpus});
    const auto multi_indexed = multi_app.Index({.files = {first, second}, .output = multi_corpus});
    REQUIRE(single_indexed.has_value());
    REQUIRE(multi_indexed.has_value());
    CHECK(single_indexed->chunks == total);
    CHECK(multi_indexed->chunks == total);
    ExpectFlushSizes(single->Batches(), total);
    ExpectFlushSizes(multi->Batches(), total);
    CHECK(single->Batches() == multi->Batches());
    CHECK(ReadFile(single_corpus) == ReadFile(multi_corpus));

    auto single_grep_embedder = std::make_unique<RecordingEmbedder>();
    auto multi_grep_embedder = std::make_unique<RecordingEmbedder>(3);
    const RecordingEmbedder *single_grep = single_grep_embedder.get();
    const RecordingEmbedder *multi_grep = multi_grep_embedder.get();
    const nesso::Nesso single_grep_app{nesso::Config{}, std::move(single_grep_embedder)};
    const nesso::Nesso multi_grep_app{nesso::Config{}, std::move(multi_grep_embedder)};
    const nesso::GrepRequest request{.query = "database connection", .files = {first, second}, .top_k = total};
    const auto single_grepped = single_grep_app.Grep(request);
    const auto multi_grepped = multi_grep_app.Grep(request);
    REQUIRE(single_grepped.has_value());
    REQUIRE(multi_grepped.has_value());
    ExpectFlushSizes(single_grep->Batches(), total);
    ExpectFlushSizes(multi_grep->Batches(), total);
    CHECK(single_grep->Batches() == multi_grep->Batches());
    REQUIRE(single_grepped->matches.size() == multi_grepped->matches.size());
    REQUIRE(single_grepped->matches.size() == total);
    for (size_t index = 0; index < single_grepped->matches.size(); ++index)
    {
        const nesso::TextMatch &left = single_grepped->matches[index];
        const nesso::TextMatch &right = multi_grepped->matches[index];
        CHECK(left.source == right.source);
        CHECK(left.line == right.line);
        CHECK(left.score == right.score);
        CHECK(left.text == right.text);
        CHECK(left.array_element == right.array_element);
    }
}

TEST_CASE("one session and several sessions write the same corpus bytes", "[nesso][text]")
{
    const std::filesystem::path model_dir{NESSO_MODELS_DIR};
    if (!std::filesystem::is_regular_file(model_dir / "model.onnx") ||
        !std::filesystem::is_regular_file(model_dir / "vocab.txt"))
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const auto probe = nesso::embed::OnnxEmbedder::Create(model_dir, 1);
    REQUIRE(probe.has_value());
    REQUIRE((*probe)->Policy().quantized);

    const TempDir dir{"nesso_facade_corpus_bytes"};
    const auto log = dir.Path() / "mixed.log";
    {
        std::ofstream output(log);
        REQUIRE(output);
        for (int index = 0; index < 130; ++index)
        {
            if (index % 2 == 0)
            {
                output << "database connection refused " << index << '\n';
            }
            else
            {
                output << std::string(80, 'x') << " timeout " << index << '\n';
            }
        }
        REQUIRE(output);
    }

    const nesso::Nesso one{nesso::Config{.model_dir = model_dir, .threads = 1}};
    const nesso::Nesso many{nesso::Config{.model_dir = model_dir, .threads = 8}};
    const auto one_corpus = dir.Path() / "one.nesso";
    const auto many_corpus = dir.Path() / "many.nesso";
    REQUIRE(one.Index({.files = {log}, .output = one_corpus}).has_value());
    REQUIRE(many.Index({.files = {log}, .output = many_corpus}).has_value());
    CHECK(ReadFile(one_corpus) == ReadFile(many_corpus));
}

TEST_CASE("a distinct text keeps the same vector when duplicates are added", "[nesso][text]")
{
    const std::filesystem::path model_dir{NESSO_MODELS_DIR};
    if (!std::filesystem::is_regular_file(model_dir / "model.onnx") ||
        !std::filesystem::is_regular_file(model_dir / "vocab.txt"))
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const TempDir dir{"nesso_dedup_vectors"};
    const auto unique = dir.Path() / "unique.log";
    const auto duplicated = dir.Path() / "duplicated.log";
    {
        std::ofstream plain(unique);
        std::ofstream copies(duplicated);
        REQUIRE(plain);
        REQUIRE(copies);
        const std::array<const char *, 3> lines{"alpha line", "beta line", "gamma line"};
        for (const char *line : lines)
        {
            plain << line << '\n';
            copies << line << '\n';
            copies << line << '\n';
        }
        REQUIRE(plain);
        REQUIRE(copies);
    }

    const nesso::Nesso app{nesso::Config{.model_dir = model_dir, .threads = 1}};
    const auto plain_corpus = dir.Path() / "plain.nesso";
    const auto copy_corpus = dir.Path() / "copy.nesso";
    REQUIRE(app.Index({.files = {unique}, .output = plain_corpus}).has_value());
    REQUIRE(app.Index({.files = {duplicated}, .output = copy_corpus}).has_value());

    const auto plain_chunks = nesso::core::ReadCorpusFile(plain_corpus);
    const auto copy_chunks = nesso::core::ReadCorpusFile(copy_corpus);
    REQUIRE(plain_chunks.has_value());
    REQUIRE(copy_chunks.has_value());
    REQUIRE(plain_chunks->size() == 3);
    REQUIRE(copy_chunks->size() == 6);

    const auto same_bytes = [](const std::vector<float> &left, const std::vector<float> &right)
    {
        REQUIRE(left.size() == right.size());
        REQUIRE_FALSE(left.empty());
        return std::memcmp(left.data(), right.data(), left.size() * sizeof(float)) == 0;
    };
    for (size_t index = 0; index < plain_chunks->size(); ++index)
    {
        CHECK(same_bytes((*plain_chunks)[index].embedding, (*copy_chunks)[index * 2].embedding));
        CHECK(same_bytes((*copy_chunks)[index * 2].embedding, (*copy_chunks)[index * 2 + 1].embedding));
    }

    const auto plain_hits = app.Grep({.query = "alpha", .files = {unique}, .top_k = 3});
    const auto copy_hits = app.Grep({.query = "alpha", .files = {duplicated}, .top_k = 6});
    REQUIRE(plain_hits.has_value());
    REQUIRE(copy_hits.has_value());
    REQUIRE(plain_hits->matches.size() == 3);
    REQUIRE(copy_hits->matches.size() == 6);
    for (const nesso::TextMatch &match : copy_hits->matches)
    {
        const auto found = std::ranges::find_if(plain_hits->matches, [&match](const nesso::TextMatch &plain)
                                                { return plain.text == match.text; });
        REQUIRE(found != plain_hits->matches.end());
        CHECK(match.score == found->score);
        CHECK(match.occurrences == 2);
    }
}
