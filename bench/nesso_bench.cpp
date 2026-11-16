// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <benchmark/benchmark.h>
#include <core/bm25.hpp>
#include <core/corpus_index.hpp>
#include <core/distance.hpp>
#include <core/distance_kernels.hpp>
#include <core/lexical_terms.hpp>
#include <core/top_k.hpp>
#include <embed/wordpiece_tokenizer.hpp>

#include <cstdint>
#include <filesystem>
#include <random>
#include <span>
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

struct Ranked
{
    uint64_t index = 0;
    float score = 0.0F;
};

std::vector<float> RandomVector(std::mt19937 &rng, size_t dimensions)
{
    std::uniform_real_distribution<float> dist(-1.0F, 1.0F);
    std::vector<float> values(dimensions);
    for (float &value : values)
    {
        value = dist(rng);
    }
    return values;
}

[[nodiscard]] bool Avx2FmaAvailable()
{
#if defined(__x86_64__)
    const bool supported = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#else
    const bool supported = false;
#endif
    return nesso::core::math::detail::SelectDistanceKernel("avx2", supported) ==
           nesso::core::math::detail::DistanceKernel::Avx2Fma;
}

enum class ForcedKernel : uint8_t
{
    DotScalar,
    DotAvx2Fma,
    L2Scalar,
    L2Avx2Fma
};

void TimeForcedKernel(benchmark::State &state, ForcedKernel kind)
{
    const bool avx2 = kind == ForcedKernel::DotAvx2Fma || kind == ForcedKernel::L2Avx2Fma;
    if (avx2 && !Avx2FmaAvailable())
    {
        state.SkipWithMessage("CPU lacks AVX2+FMA");
        return;
    }
    const auto kernel =
        avx2 ? nesso::core::math::detail::DistanceKernel::Avx2Fma : nesso::core::math::detail::DistanceKernel::Scalar;
    const bool dot = kind == ForcedKernel::DotScalar || kind == ForcedKernel::DotAvx2Fma;
    std::mt19937 rng{1};
    const std::vector<float> left = RandomVector(rng, EMBEDDING_DIMENSIONS);
    const std::vector<float> right = RandomVector(rng, EMBEDDING_DIMENSIONS);
    for (auto _ : state)
    {
        auto score = dot ? nesso::core::math::detail::DotProductKernel(kernel, left, right)
                         : nesso::core::math::detail::L2SquaredDistanceKernel(kernel, left, right);
        benchmark::DoNotOptimize(score);
        benchmark::ClobberMemory();
    }
}

void DotProduct384Scalar(benchmark::State &state) { TimeForcedKernel(state, ForcedKernel::DotScalar); }

void DotProduct384Avx2Fma(benchmark::State &state) { TimeForcedKernel(state, ForcedKernel::DotAvx2Fma); }

void L2Squared384Scalar(benchmark::State &state) { TimeForcedKernel(state, ForcedKernel::L2Scalar); }

void L2Squared384Avx2Fma(benchmark::State &state) { TimeForcedKernel(state, ForcedKernel::L2Avx2Fma); }

void DotProduct384(benchmark::State &state)
{
    std::mt19937 rng{1};
    const std::vector<float> left = RandomVector(rng, EMBEDDING_DIMENSIONS);
    const std::vector<float> right = RandomVector(rng, EMBEDDING_DIMENSIONS);
    for (auto _ : state)
    {
        auto score = nesso::core::math::DistanceMetrics::DotProduct<float>(left, right);
        benchmark::DoNotOptimize(score);
    }
}

void Cosine384(benchmark::State &state)
{
    std::mt19937 rng{1};
    const std::vector<float> left = RandomVector(rng, EMBEDDING_DIMENSIONS);
    const std::vector<float> right = RandomVector(rng, EMBEDDING_DIMENSIONS);
    for (auto _ : state)
    {
        auto score = nesso::core::math::CosineSimilarity::Calculate<float>(left, right);
        benchmark::DoNotOptimize(score);
    }
}

void L2Squared384(benchmark::State &state)
{
    std::mt19937 rng{1};
    const std::vector<float> left = RandomVector(rng, EMBEDDING_DIMENSIONS);
    const std::vector<float> right = RandomVector(rng, EMBEDDING_DIMENSIONS);
    for (auto _ : state)
    {
        auto score = nesso::core::math::DistanceMetrics::L2SquaredDistance<float>(left, right);
        benchmark::DoNotOptimize(score);
    }
}

void SelectTopK(benchmark::State &state)
{
    std::vector<Ranked> candidates;
    candidates.reserve(TOP_K_CANDIDATES);
    for (uint64_t index = 0; index < TOP_K_CANDIDATES; ++index)
    {
        candidates.push_back({.index = index, .score = static_cast<float>(TOP_K_CANDIDATES - index)});
    }
    const auto order = [](const Ranked &left, const Ranked &right)
    { return left.score != right.score ? left.score > right.score : left.index < right.index; };

    for (auto _ : state)
    {
        std::vector<Ranked> working = candidates;
        nesso::core::detail::SelectTopKInPlace(working, TOP_K, order);
        benchmark::DoNotOptimize(working);
    }
}

void TopKFlatMatrix(benchmark::State &state)
{
    constexpr size_t rows = 100000;
    std::mt19937 rng{1};
    std::uniform_real_distribution<float> dist(-1.0F, 1.0F);
    std::vector<float> matrix(rows * EMBEDDING_DIMENSIONS);
    for (float &value : matrix)
    {
        value = dist(rng);
    }
    const std::vector<float> query = RandomVector(rng, EMBEDDING_DIMENSIONS);

    for (auto _ : state)
    {
        auto hits = nesso::core::TopK({.query = query, .matrix = matrix, .dims = EMBEDDING_DIMENSIONS, .k = TOP_K});
        benchmark::DoNotOptimize(hits);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(rows));
}

void EncodeWordPiece(benchmark::State &state)
{
    const auto tokenizer = nesso::embed::WordPieceTokenizer::FromVocabFile(NESSO_BENCH_VOCAB);
    if (!tokenizer)
    {
        state.SkipWithError("failed to load bench vocab");
        return;
    }
    constexpr std::string_view text = "hello world testing";
    for (auto _ : state)
    {
        auto encoded = tokenizer->Encode(text);
        benchmark::DoNotOptimize(encoded);
    }
}

void ReadCorpus(benchmark::State &state)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "nesso-bench-corpus.nessc";
    std::mt19937 rng{1};
    nesso::core::CorpusPolicy policy{{"batch", "window:1024,max:32,tokens:1024,order:length-then-input"},
                                     {"bm25", std::string{nesso::core::BM25_POLICY}},
                                     {"dedup", "masked-v1"},
                                     {"dimensions", std::to_string(EMBEDDING_DIMENSIONS)},
                                     {"lexical.terms", std::string{nesso::core::LEXICAL_TERMS_VERSION}},
                                     {"mask", "template-v1"},
                                     {"model.id", "benchmark"},
                                     {"model.quantization", "int8"},
                                     {"model.sha256", std::string(64, 'a')},
                                     {"tokens.max", "256"},
                                     {"truncate", "4096-codepoints"},
                                     {"vocab.sha256", std::string(64, 'b')}};
    auto writer = nesso::core::CorpusWriter::Create(path, std::move(policy));
    if (!writer || !writer->AddSource({.path = "bench.log"}))
    {
        state.SkipWithError("failed to create bench corpus");
        return;
    }
    for (uint64_t index = 0; index < CORPUS_CHUNKS; ++index)
    {
        const std::string text = "database connection refused " + std::to_string(index);
        const auto id = writer->AddDistinct(text, RandomVector(rng, EMBEDDING_DIMENSIONS));
        if (!id)
        {
            state.SkipWithError("failed to write bench vector");
            return;
        }
        const nesso::core::CorpusLine line{.line_number = index + 1, .distinct_id = *id};
        const std::vector<std::string> terms = nesso::core::LexicalTerms(text);
        if (!writer->AddLines(std::span{&line, 1}, std::span{&terms, 1}))
        {
            state.SkipWithError("failed to write bench line");
            return;
        }
    }
    if (!writer->Finish())
    {
        state.SkipWithError("failed to finish bench corpus");
        return;
    }

    for (auto _ : state)
    {
        auto loaded = nesso::core::ReadCorpusFile(path);
        benchmark::DoNotOptimize(loaded);
    }
    std::filesystem::remove(path);
}

} // namespace

BENCHMARK(DotProduct384);
BENCHMARK(DotProduct384Scalar);
BENCHMARK(DotProduct384Avx2Fma);
BENCHMARK(Cosine384);
BENCHMARK(L2Squared384);
BENCHMARK(L2Squared384Scalar);
BENCHMARK(L2Squared384Avx2Fma);
BENCHMARK(SelectTopK);
BENCHMARK(TopKFlatMatrix);
BENCHMARK(EncodeWordPiece);
BENCHMARK(ReadCorpus);

BENCHMARK_MAIN();
