#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mach_core/embedder.hpp>
#include <mach_core/log_index.hpp>
#include <mach_core/onnx_embedder.hpp>

#ifndef FIXTURES_DIR
#define FIXTURES_DIR "."
#endif

namespace fs = std::filesystem;
using namespace mach_core;

namespace
{
fs::path makeTempPath(const char *prefix)
{
    return fs::temp_directory_path() / (std::string(prefix) + std::to_string(std::rand()));
}

class IndexGuard
{
  public:
    explicit IndexGuard(fs::path db) : m_db(std::move(db)), m_sidecar(sidecarPathFor(m_db)) {}
    ~IndexGuard()
    {
        fs::remove(m_db);
        fs::remove(m_sidecar);
    }

  private:
    fs::path m_db;
    fs::path m_sidecar;
};
} // namespace

TEST_CASE("LogIndex stub ingest and search recover an exact line", "[LogIndex][core][Unit]")
{
    const auto db = makeTempPath("vecgrep_index_");
    IndexGuard guard(db);
    const auto logPath = fs::path(FIXTURES_DIR) / "sample.log";

    HashEmbedder embedder;
    LogIndex index;
    REQUIRE(index.open(db).has_value());
    auto added = index.ingestFile(logPath, embedder, false);
    REQUIRE(added.has_value());
    REQUIRE(*added == 5);

    auto hits = index.search("CAN timeout on bus 0x1A4", 3, embedder);
    REQUIRE(hits.has_value());
    REQUIRE_FALSE(hits->empty());
    REQUIRE(hits->front().lineNo == 2);
}

TEST_CASE("LogIndex parallel ingest matches sync vector counts", "[LogIndex][core][Unit]")
{
    const auto db = makeTempPath("vecgrep_index_par_");
    IndexGuard guard(db);
    const auto logPath = fs::path(FIXTURES_DIR) / "sample.log";

    HashEmbedder embedder;
    LogIndex index;
    REQUIRE(index.open(db).has_value());
    auto added = index.ingestFile(logPath, embedder, true);
    REQUIRE(added.has_value());
    REQUIRE(*added == 5);
    REQUIRE(index.vectorCount() == 5);
}

TEST_CASE("OnnxEmbedder loads the tiny fixture model", "[OnnxEmbedder][core][Unit]")
{
    const auto model = fs::path(FIXTURES_DIR) / "tiny_minilm.onnx";
    const auto vocab = fs::path(FIXTURES_DIR) / "vocab.txt";
    if (!fs::exists(model))
    {
        SKIP("tiny_minilm.onnx fixture is not present");
    }

    auto embedder = OnnxEmbedder::create(model, vocab);
    REQUIRE(embedder.has_value());
    auto values = embedder->embed("hello world");
    REQUIRE(values.has_value());
    REQUIRE(values->size() == kEmbeddingDims);
}

TEST_CASE("OnnxEmbedder fails on a missing model", "[OnnxEmbedder][core][Unit]")
{
    const auto vocab = fs::path(FIXTURES_DIR) / "vocab.txt";
    auto embedder = OnnxEmbedder::create("/no/such/model.onnx", vocab);
    REQUIRE_FALSE(embedder.has_value());
    REQUIRE(embedder.error() == EngineError::ModelLoadFailure);
}

TEST_CASE("makeEmbedder wires ONNX when a model path is provided", "[OnnxEmbedder][core][Unit]")
{
    const auto model = fs::path(FIXTURES_DIR) / "tiny_minilm.onnx";
    const auto vocab = fs::path(FIXTURES_DIR) / "vocab.txt";
    if (!fs::exists(model))
    {
        SKIP("tiny_minilm.onnx fixture is not present");
    }
    auto embedder = makeEmbedder(model, vocab);
    REQUIRE(embedder.has_value());
    auto values = (*embedder)->embed("timeout");
    REQUIRE(values.has_value());
}
