// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <benchmark/benchmark.h>
#include <core/corpus_index.hpp>
#include <core/distance.hpp>
#include <core/vector_search.hpp>
#include <embed/wordpiece_tokenizer.hpp>

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#ifndef NESSO_BENCH_VOCAB
#error "NESSO_BENCH_VOCAB must be defined"
#endif

namespace
{

constexpr size_t EMBEDDING_DIMENSIONS = 384;
constexpr size_t TOP_K = 10;
constexpr size_t TOP_K_CANDIDATES = 10000;
constexpr uint64_t CORPUS_CHUNKS = 256;

std::vector<float> randomVector(std::mt19937 &rng, size_t dimensions)
{
    std::uniform_real_distribution<float> dist(-1.0F, 1.0F);
    std::vector<float> values(dimensions);
    for (float &value : values)
    {
        value = dist(rng);
    }
    return values;
}

void dotProduct384(benchmark::State &state)
{
    std::mt19937 rng{1};
    const std::vector<float> left = randomVector(rng, EMBEDDING_DIMENSIONS);
    const std::vector<float> right = randomVector(rng, EMBEDDING_DIMENSIONS);
    for (auto _ : state)
    {
        auto score = core::math::DistanceMetrics::dotProduct<float>(left, right);
        benchmark::DoNotOptimize(score);
    }
}

void cosine384(benchmark::State &state)
{
    std::mt19937 rng{1};
    const std::vector<float> left = randomVector(rng, EMBEDDING_DIMENSIONS);
    const std::vector<float> right = randomVector(rng, EMBEDDING_DIMENSIONS);
    for (auto _ : state)
    {
        auto score = core::math::CosineSimilarity::calculate<float>(left, right);
        benchmark::DoNotOptimize(score);
    }
}

void l2Squared384(benchmark::State &state)
{
    std::mt19937 rng{1};
    const std::vector<float> left = randomVector(rng, EMBEDDING_DIMENSIONS);
    const std::vector<float> right = randomVector(rng, EMBEDDING_DIMENSIONS);
    for (auto _ : state)
    {
        auto score = core::math::DistanceMetrics::l2SquaredDistance<float>(left, right);
        benchmark::DoNotOptimize(score);
    }
}

void selectTopK(benchmark::State &state)
{
    std::vector<core::SearchResult<float>> candidates;
    candidates.reserve(TOP_K_CANDIDATES);
    for (uint64_t index = 0; index < TOP_K_CANDIDATES; ++index)
    {
        candidates.push_back({.index = index, .score = static_cast<float>(TOP_K_CANDIDATES - index)});
    }
    const auto order = [](const core::SearchResult<float> &left, const core::SearchResult<float> &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };

    for (auto _ : state)
    {
        std::vector<core::SearchResult<float>> working = candidates;
        core::detail::selectTopKInPlace(working, TOP_K, order);
        benchmark::DoNotOptimize(working);
    }
}

void encodeWordPiece(benchmark::State &state)
{
    const auto tokenizer = embed::WordPieceTokenizer::fromVocabFile(NESSO_BENCH_VOCAB);
    if (!tokenizer)
    {
        state.SkipWithError("failed to load bench vocab");
        return;
    }
    constexpr std::string_view text = "hello world testing";
    for (auto _ : state)
    {
        auto encoded = tokenizer->encode(text);
        benchmark::DoNotOptimize(encoded);
    }
}

void readCorpus(benchmark::State &state)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "nesso-bench-corpus.nessc";
    std::mt19937 rng{1};
    std::vector<core::CorpusChunk> chunks;
    chunks.reserve(CORPUS_CHUNKS);
    for (uint64_t index = 0; index < CORPUS_CHUNKS; ++index)
    {
        chunks.push_back(
            {.chunk = {.text = "database connection refused", .lineNumber = index + 1, .source = "bench.log"},
             .embedding = randomVector(rng, EMBEDDING_DIMENSIONS)});
    }
    if (!core::writeCorpusFile(path, chunks))
    {
        state.SkipWithError("failed to write bench corpus");
        return;
    }

    for (auto _ : state)
    {
        auto loaded = core::readCorpusFile(path);
        benchmark::DoNotOptimize(loaded);
    }
    std::filesystem::remove(path);
}

} // namespace

BENCHMARK(dotProduct384);
BENCHMARK(cosine384);
BENCHMARK(l2Squared384);
BENCHMARK(selectTopK);
BENCHMARK(encodeWordPiece);
BENCHMARK(readCorpus);

BENCHMARK_MAIN();
