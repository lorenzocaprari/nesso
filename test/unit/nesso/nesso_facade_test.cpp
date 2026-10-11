// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <core/corpus_index.hpp>
#include <model_paths.hpp>
#include <nesso.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>

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

} // namespace

TEST_CASE("text operations report model and corpus failures", "[nesso][text]")
{
    const TempDir dir{"nesso_facade_text"};
    const auto log = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const auto corrupt = dir.path() / "corrupt.nesso";
    const auto empty = dir.path() / "empty.nesso";
    const std::array<char, 4> junk{'b', 'a', 'd', '!'};
    writeBytes(corrupt, junk.data(), junk.size());
    writeEmptyCorpus(empty);

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
