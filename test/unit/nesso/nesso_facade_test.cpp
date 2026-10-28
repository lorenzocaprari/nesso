// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <core/corpus_index.hpp>
#include <embed/batch_plan.hpp>
#include <embed/embedder.hpp>
#include <embed/onnx_embedder.hpp>
#include <model_paths.hpp>
#include <nesso.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void writeBytes(const std::filesystem::path &path, const void *data, size_t size)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    REQUIRE(output);
}

void writeEmptyCorpus(const std::filesystem::path &path) { REQUIRE(nesso::core::writeCorpusFile(path, {}, "model")); }

nesso::Nesso withMissingModel(const std::filesystem::path &root)
{
    return nesso::Nesso{nesso::Config{.modelDir = root / "no-models"}};
}

std::uint32_t mixByte(std::uint32_t hash, unsigned char byte)
{
    hash ^= byte;
    hash *= 16777619u;
    return hash;
}

std::uint32_t mixText(std::uint32_t hash, std::string_view text)
{
    for (const char byte : text)
    {
        hash = mixByte(hash, static_cast<unsigned char>(byte));
    }
    return hash;
}

std::vector<float> vectorFor(std::string_view text, std::uint32_t window)
{
    std::vector<float> values(4);
    std::uint32_t hash = mixText(2166136261u, text);
    hash = mixByte(hash, static_cast<unsigned char>(window & 0xffu));
    hash = mixByte(hash, static_cast<unsigned char>((window >> 8U) & 0xffu));
    hash = mixByte(hash, static_cast<unsigned char>((window >> 16U) & 0xffu));
    hash = mixByte(hash, static_cast<unsigned char>((window >> 24U) & 0xffu));
    for (size_t index = 0; index < values.size(); ++index)
    {
        hash = mixByte(hash, static_cast<unsigned char>(index));
        values[index] = static_cast<float>(hash % 1000U) / 1000.F;
    }
    return values;
}

class RecordingEmbedder final : public nesso::embed::Embedder
{
  public:
    explicit RecordingEmbedder(size_t sessions = 1) : sessions_(sessions) {}

    [[nodiscard]] size_t sessions() const noexcept override { return sessions_; }

    [[nodiscard]] std::expected<std::vector<float>, nesso::embed::EmbedError> embed(std::string_view) const override
    {
        return std::vector<float>{1.F, 0.F, 0.F, 0.F};
    }

    [[nodiscard]] std::expected<std::vector<std::vector<float>>, nesso::embed::EmbedError>
    embedBatch(std::span<const std::string> texts, ProgressCallback progress) const override
    {
        batches_.push_back(texts.size());
        std::vector<std::vector<float>> embeddings;
        embeddings.reserve(texts.size());
        constexpr size_t mixWindow = 32;
        for (size_t offset = 0; offset < texts.size(); offset += mixWindow)
        {
            const size_t count = std::min(mixWindow, texts.size() - offset);
            std::uint32_t window = 2166136261u;
            for (size_t index = 0; index < count; ++index)
            {
                window = mixText(window, texts[offset + index]);
            }
            for (size_t index = 0; index < count; ++index)
            {
                embeddings.push_back(vectorFor(texts[offset + index], window));
            }
        }
        if (progress)
        {
            progress(texts.size(), texts.size());
        }
        return embeddings;
    }

    [[nodiscard]] const std::vector<size_t> &batches() const { return batches_; }

  private:
    size_t sessions_;
    mutable std::vector<size_t> batches_;
};

std::string letterSuffix(std::size_t index)
{
    std::string suffix;
    do
    {
        suffix.push_back(static_cast<char>('a' + static_cast<int>(index % 26)));
        index /= 26;
    } while (index > 0);
    return suffix;
}

void writeLines(const std::filesystem::path &path, size_t count, std::string_view tag)
{
    std::ofstream output(path);
    REQUIRE(output);
    for (size_t index = 0; index < count; ++index)
    {
        output << tag << '-' << letterSuffix(index) << " database connection\n";
    }
    REQUIRE(output);
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void expectFlushSizes(const std::vector<size_t> &batches, size_t total)
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
    const auto corrupt = dir.path() / "corrupt.nesso";
    const auto empty = dir.path() / "empty.nesso";
    const auto otherModel = dir.path() / "other-model.nesso";
    const auto futureVersion = dir.path() / "future-version.nesso";
    const std::array<char, 4> junk{'b', 'a', 'd', '!'};
    writeBytes(corrupt, junk.data(), junk.size());
    writeEmptyCorpus(empty);
    const nesso::core::CorpusChunk chunk{.chunk = {.text = "alpha", .lineNumber = 1, .source = "a.log"},
                                         .embedding = {1.F}};
    REQUIRE(
        nesso::core::writeCorpusFile(otherModel, std::span<const nesso::core::CorpusChunk>(&chunk, 1), "other/model"));
    REQUIRE(nesso::core::writeCorpusFile(futureVersion, std::span<const nesso::core::CorpusChunk>(&chunk, 1), "model"));
    {
        std::fstream patch(futureVersion, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(patch);
        patch.seekp(4);
        const std::array<char, 4> version{3, 0, 0, 0};
        patch.write(version.data(), version.size());
        REQUIRE(patch);
    }

    const nesso::Nesso app = withMissingModel(dir.path());

    const auto grepped = app.grep({.query = "database", .files = {log}});
    REQUIRE_FALSE(grepped.has_value());
    REQUIRE(grepped.error().kind == nesso::ErrorKind::EmbedderLoad);
    REQUIRE(grepped.error().cause == nesso::ErrorCause::ModelLoad);
    REQUIRE(grepped.error().path == dir.path() / "no-models");

    const auto indexed = app.index({.files = {log}, .output = dir.path() / "out.nesso"});
    REQUIRE_FALSE(indexed.has_value());
    REQUIRE(indexed.error().kind == nesso::ErrorKind::EmbedderLoad);

    const auto unreadable = app.search({.query = "database", .index = corrupt});
    REQUIRE_FALSE(unreadable.has_value());
    REQUIRE(unreadable.error().kind == nesso::ErrorKind::CorpusRead);
    REQUIRE(unreadable.error().cause == nesso::ErrorCause::CorruptFile);
    REQUIRE(unreadable.error().path == corrupt);

    const auto nothing = app.search({.query = "database", .index = empty});
    REQUIRE_FALSE(nothing.has_value());
    REQUIRE(nothing.error().kind == nesso::ErrorKind::EmptyInput);
    REQUIRE(nothing.error().cause == nesso::ErrorCause::EmptyCorpus);

    const auto mismatched = app.search({.query = "database", .index = otherModel});
    REQUIRE_FALSE(mismatched.has_value());
    REQUIRE(mismatched.error().kind == nesso::ErrorKind::CorpusRead);
    REQUIRE(mismatched.error().cause == nesso::ErrorCause::ModelMismatch);
    REQUIRE(mismatched.error().path == otherModel);
    REQUIRE(nesso::message(mismatched.error()).find("built with a different model") != std::string::npos);

    const auto newer = app.search({.query = "database", .index = futureVersion});
    REQUIRE_FALSE(newer.has_value());
    REQUIRE(newer.error().kind == nesso::ErrorKind::CorpusRead);
    REQUIRE(newer.error().cause == nesso::ErrorCause::UnsupportedCorpusVersion);
}

TEST_CASE("grep, index, and search run the text stages", "[nesso][text]")
{
    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    if (!std::filesystem::is_regular_file(modelDir / "model.onnx") ||
        !std::filesystem::is_regular_file(modelDir / "vocab.txt"))
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const TempDir dir{"nesso_facade_text_stages"};
    const auto log = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const nesso::Nesso app{nesso::Config{.modelDir = modelDir}};

    const auto grepped = app.grep({.query = "database connection error", .files = {log}});
    REQUIRE(grepped.has_value());
    REQUIRE_FALSE(grepped->matches.empty());

    const auto corpus = dir.path() / "corpus.nesso";
    const auto indexed = app.index({.files = {log}, .output = corpus});
    REQUIRE(indexed.has_value());
    REQUIRE(indexed->chunks >= 1);

    const auto found = app.search({.query = "database connection error", .index = corpus});
    REQUIRE(found.has_value());
    REQUIRE_FALSE(found->matches.empty());
}

TEST_CASE("a missing default model lists every directory it tried", "[nesso][text]")
{
    if (nesso::containsModelFiles("/usr/share/nesso"))
    {
        SKIP("packaged model directory is installed");
    }

    const TempDir dir{"nesso_facade_default_model"};
    const auto previousDir = std::filesystem::current_path();
    const char *modelEnv = std::getenv("NESSO_MODEL_DIR");
    const char *xdgEnv = std::getenv("XDG_DATA_HOME");
    const std::optional<std::string> savedModel =
        modelEnv == nullptr ? std::nullopt : std::optional<std::string>{modelEnv};
    const std::optional<std::string> savedXdg = xdgEnv == nullptr ? std::nullopt : std::optional<std::string>{xdgEnv};
    const auto restore = [&]()
    {
        std::filesystem::current_path(previousDir);
        if (savedModel)
        {
            setenv("NESSO_MODEL_DIR", savedModel->c_str(), 1);
        }
        else
        {
            unsetenv("NESSO_MODEL_DIR");
        }
        if (savedXdg)
        {
            setenv("XDG_DATA_HOME", savedXdg->c_str(), 1);
        }
        else
        {
            unsetenv("XDG_DATA_HOME");
        }
    };

    unsetenv("NESSO_MODEL_DIR");
    setenv("XDG_DATA_HOME", (dir.path() / "xdg").c_str(), 1);
    std::filesystem::current_path(dir.path());
    const nesso::Nesso app{nesso::Config{}};
    const auto log = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const auto grepped = app.grep({.query = "database", .files = {log}});
    restore();

    REQUIRE_FALSE(grepped.has_value());
    REQUIRE(grepped.error().kind == nesso::ErrorKind::EmbedderLoad);
    REQUIRE(grepped.error().note.find(" and ") != std::string::npos);
    REQUIRE(grepped.error().note.find(',') != std::string::npos);
}

TEST_CASE("index fails when no chunks are produced and does not replace the corpus", "[nesso][text]")
{
    const TempDir dir{"nesso_index_zero_chunks"};
    auto embedder = std::make_unique<RecordingEmbedder>();
    const RecordingEmbedder *recorder = embedder.get();
    const nesso::Nesso app{nesso::Config{}, std::move(embedder)};
    const auto empty = dir.path() / "empty.log";
    const auto alsoEmpty = dir.path() / "also-empty.log";
    const auto kept = dir.path() / "kept.nesso";
    const auto blockedParent = dir.path() / "not-a-dir";
    const char keep[] = {'k', 'e', 'e', 'p'};
    writeBytes(empty, keep, 0);
    writeBytes(alsoEmpty, keep, 0);
    writeBytes(kept, keep, sizeof(keep));
    writeBytes(blockedParent, keep, sizeof(keep));

    const auto several = app.index({.files = {empty, alsoEmpty}, .output = kept});
    REQUIRE_FALSE(several.has_value());
    CHECK(several.error().cause == nesso::ErrorCause::EmptyFile);
    CHECK(several.error().path.empty());
    CHECK(readFile(kept) == "keep");
    CHECK_FALSE(std::filesystem::exists(kept.string() + ".records"));

    const auto blocked = app.index({.files = {empty}, .output = dir.path()});
    REQUIRE_FALSE(blocked.has_value());
    CHECK(blocked.error().kind == nesso::ErrorKind::CorpusWrite);
    CHECK(blocked.error().cause == nesso::ErrorCause::FileOpen);

    const auto unopenable = app.index({.files = {empty}, .output = blockedParent / "out.nesso"});
    REQUIRE_FALSE(unopenable.has_value());
    CHECK(unopenable.error().kind == nesso::ErrorKind::CorpusWrite);
    CHECK(recorder->batches().empty());
}

TEST_CASE("Nesso is movable", "[nesso]")
{
    const TempDir dir{"nesso_facade_move"};
    nesso::Nesso first{nesso::Config{}};
    nesso::Nesso second = std::move(first);
    first = std::move(second);
    const auto missing = first.search({.query = "database", .index = dir.path() / "missing.nesso"});
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().kind == nesso::ErrorKind::CorpusRead);
    REQUIRE(missing.error().cause == nesso::ErrorCause::FileOpen);
}

TEST_CASE("a multi-session embedder flushes a fixed window", "[nesso][text]")
{
    const TempDir dir{"nesso_facade_sessions"};
    const auto first = dir.path() / "a.log";
    const auto second = dir.path() / "b.log";
    constexpr size_t firstLines = 80;
    constexpr size_t secondLines = 40;
    constexpr size_t total = firstLines + secondLines;
    writeLines(first, firstLines, "alpha");
    writeLines(second, secondLines, "beta");

    const RecordingEmbedder probe;
    CHECK(probe.sessions() == 1);

    auto singleEmbedder = std::make_unique<RecordingEmbedder>();
    auto multiEmbedder = std::make_unique<RecordingEmbedder>(3);
    const RecordingEmbedder *single = singleEmbedder.get();
    const RecordingEmbedder *multi = multiEmbedder.get();
    const nesso::Nesso singleApp{nesso::Config{}, std::move(singleEmbedder)};
    const nesso::Nesso multiApp{nesso::Config{}, std::move(multiEmbedder)};

    const auto singleCorpus = dir.path() / "single.nesso";
    const auto multiCorpus = dir.path() / "multi.nesso";
    const auto singleIndexed = singleApp.index({.files = {first, second}, .output = singleCorpus});
    const auto multiIndexed = multiApp.index({.files = {first, second}, .output = multiCorpus});
    REQUIRE(singleIndexed.has_value());
    REQUIRE(multiIndexed.has_value());
    CHECK(singleIndexed->chunks == total);
    CHECK(multiIndexed->chunks == total);
    expectFlushSizes(single->batches(), total);
    expectFlushSizes(multi->batches(), total);
    CHECK(single->batches() == multi->batches());
    CHECK(readFile(singleCorpus) == readFile(multiCorpus));

    auto singleGrepEmbedder = std::make_unique<RecordingEmbedder>();
    auto multiGrepEmbedder = std::make_unique<RecordingEmbedder>(3);
    const RecordingEmbedder *singleGrep = singleGrepEmbedder.get();
    const RecordingEmbedder *multiGrep = multiGrepEmbedder.get();
    const nesso::Nesso singleGrepApp{nesso::Config{}, std::move(singleGrepEmbedder)};
    const nesso::Nesso multiGrepApp{nesso::Config{}, std::move(multiGrepEmbedder)};
    const nesso::GrepRequest request{.query = "database connection", .files = {first, second}, .topK = total};
    const auto singleGrepped = singleGrepApp.grep(request);
    const auto multiGrepped = multiGrepApp.grep(request);
    REQUIRE(singleGrepped.has_value());
    REQUIRE(multiGrepped.has_value());
    expectFlushSizes(singleGrep->batches(), total);
    expectFlushSizes(multiGrep->batches(), total);
    CHECK(singleGrep->batches() == multiGrep->batches());
    REQUIRE(singleGrepped->matches.size() == multiGrepped->matches.size());
    REQUIRE(singleGrepped->matches.size() == total);
    for (size_t index = 0; index < singleGrepped->matches.size(); ++index)
    {
        const nesso::TextMatch &left = singleGrepped->matches[index];
        const nesso::TextMatch &right = multiGrepped->matches[index];
        CHECK(left.source == right.source);
        CHECK(left.line == right.line);
        CHECK(left.score == right.score);
        CHECK(left.text == right.text);
        CHECK(left.arrayElement == right.arrayElement);
    }
}

TEST_CASE("one session and several sessions write the same corpus bytes", "[nesso][text]")
{
    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    if (!std::filesystem::is_regular_file(modelDir / "model.onnx") ||
        !std::filesystem::is_regular_file(modelDir / "vocab.txt"))
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const auto probe = nesso::embed::OnnxEmbedder::create(modelDir, 1);
    REQUIRE(probe.has_value());
    REQUIRE((*probe)->policy().quantized);

    const TempDir dir{"nesso_facade_corpus_bytes"};
    const auto log = dir.path() / "mixed.log";
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

    const nesso::Nesso one{nesso::Config{.modelDir = modelDir, .threads = 1}};
    const nesso::Nesso many{nesso::Config{.modelDir = modelDir, .threads = 8}};
    const auto oneCorpus = dir.path() / "one.nesso";
    const auto manyCorpus = dir.path() / "many.nesso";
    REQUIRE(one.index({.files = {log}, .output = oneCorpus}).has_value());
    REQUIRE(many.index({.files = {log}, .output = manyCorpus}).has_value());
    CHECK(readFile(oneCorpus) == readFile(manyCorpus));
}

TEST_CASE("a distinct text keeps the same vector when duplicates are added", "[nesso][text]")
{
    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    if (!std::filesystem::is_regular_file(modelDir / "model.onnx") ||
        !std::filesystem::is_regular_file(modelDir / "vocab.txt"))
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const TempDir dir{"nesso_dedup_vectors"};
    const auto unique = dir.path() / "unique.log";
    const auto duplicated = dir.path() / "duplicated.log";
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

    const nesso::Nesso app{nesso::Config{.modelDir = modelDir, .threads = 1}};
    const auto plainCorpus = dir.path() / "plain.nesso";
    const auto copyCorpus = dir.path() / "copy.nesso";
    REQUIRE(app.index({.files = {unique}, .output = plainCorpus}).has_value());
    REQUIRE(app.index({.files = {duplicated}, .output = copyCorpus}).has_value());

    const auto plainChunks = nesso::core::readCorpusFile(plainCorpus);
    const auto copyChunks = nesso::core::readCorpusFile(copyCorpus);
    REQUIRE(plainChunks.has_value());
    REQUIRE(copyChunks.has_value());
    REQUIRE(plainChunks->size() == 3);
    REQUIRE(copyChunks->size() == 6);

    const auto sameBytes = [](const std::vector<float> &left, const std::vector<float> &right)
    {
        REQUIRE(left.size() == right.size());
        REQUIRE_FALSE(left.empty());
        return std::memcmp(left.data(), right.data(), left.size() * sizeof(float)) == 0;
    };
    for (size_t index = 0; index < plainChunks->size(); ++index)
    {
        CHECK(sameBytes((*plainChunks)[index].embedding, (*copyChunks)[index * 2].embedding));
        CHECK(sameBytes((*copyChunks)[index * 2].embedding, (*copyChunks)[index * 2 + 1].embedding));
    }

    const auto plainHits = app.grep({.query = "alpha", .files = {unique}, .topK = 3});
    const auto copyHits = app.grep({.query = "alpha", .files = {duplicated}, .topK = 6});
    REQUIRE(plainHits.has_value());
    REQUIRE(copyHits.has_value());
    REQUIRE(plainHits->matches.size() == 3);
    REQUIRE(copyHits->matches.size() == 6);
    for (const nesso::TextMatch &match : copyHits->matches)
    {
        const auto found = std::ranges::find_if(plainHits->matches, [&match](const nesso::TextMatch &plain)
                                                { return plain.text == match.text; });
        REQUIRE(found != plainHits->matches.end());
        CHECK(match.score == found->score);
        CHECK(match.occurrences == 2);
    }
}
