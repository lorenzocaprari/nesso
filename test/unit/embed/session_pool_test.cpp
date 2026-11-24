// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/onnx_embedder.hpp>
#include <embed/session_policy.hpp>

#include <filesystem>
#include <string>
#include <vector>

#ifndef NESSO_MODELS_DIR
#error "NESSO_MODELS_DIR must be defined"
#endif

namespace
{

auto SampleTexts() -> std::vector<std::string>
{
    std::vector<std::string> texts;
    texts.reserve(33);
    for (int index = 0; index < 33; ++index)
    {
        texts.push_back("database connection refused " + std::to_string(index));
    }
    return texts;
}

auto ModelPresent() -> bool
{
    const std::filesystem::path model_dir{NESSO_MODELS_DIR};
    return std::filesystem::is_regular_file(model_dir / "model.onnx") &&
           std::filesystem::is_regular_file(model_dir / "vocab.txt");
}

} // namespace

#if defined(__aarch64__)

TEST_CASE("aarch64 intra-op widths from 2 upward match", "[session-pool][model]")
{
    if (!ModelPresent())
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const std::filesystem::path model_dir{NESSO_MODELS_DIR};
    const auto two = nesso::embed::OnnxEmbedder::CreateWithIntraOp(model_dir, 2);
    const auto four = nesso::embed::OnnxEmbedder::CreateWithIntraOp(model_dir, 4);
    const auto eight = nesso::embed::OnnxEmbedder::CreateWithIntraOp(model_dir, 8);
    REQUIRE(two.has_value());
    REQUIRE(four.has_value());
    REQUIRE(eight.has_value());
    if (!(*two)->Policy().quantized)
    {
        SKIP("the model is fp32; intra-op thread count is not a second session");
    }
    CHECK((*two)->Policy().intra_op_threads == 2);
    CHECK((*four)->Policy().intra_op_threads == 4);
    CHECK((*eight)->Policy().intra_op_threads == 8);
    CHECK((*two)->Sessions() == 1);
    CHECK((*four)->Sessions() == 1);
    CHECK((*eight)->Sessions() == 1);

    const auto budget_one = nesso::embed::OnnxEmbedder::Create(model_dir, 1);
    const auto budget_two = nesso::embed::OnnxEmbedder::Create(model_dir, 2);
    const auto budget_four = nesso::embed::OnnxEmbedder::Create(model_dir, 4);
    const auto budget_eight = nesso::embed::OnnxEmbedder::Create(model_dir, 8);
    REQUIRE(budget_one.has_value());
    REQUIRE(budget_two.has_value());
    REQUIRE(budget_four.has_value());
    REQUIRE(budget_eight.has_value());
    CHECK((*budget_one)->Policy().sessions == 1);
    CHECK((*budget_one)->Policy().intra_op_threads == nesso::embed::IntraOpThreadFloor());
    CHECK((*budget_two)->Policy().sessions == 1);
    CHECK((*budget_two)->Policy().intra_op_threads == 2);
    CHECK((*budget_four)->Policy().sessions == 1);
    CHECK((*budget_four)->Policy().intra_op_threads == 4);
    CHECK((*budget_eight)->Policy().sessions == 2);
    CHECK((*budget_eight)->Policy().intra_op_threads == 4);

    const auto texts = SampleTexts();
    const auto from_two = (*two)->EmbedBatch(texts);
    const auto from_four = (*four)->EmbedBatch(texts);
    const auto from_eight = (*eight)->EmbedBatch(texts);
    const auto from_budget_one = (*budget_one)->EmbedBatch(texts);
    const auto from_budget_two = (*budget_two)->EmbedBatch(texts);
    const auto from_budget_four = (*budget_four)->EmbedBatch(texts);
    const auto from_budget_eight = (*budget_eight)->EmbedBatch(texts);
    const auto again = (*eight)->EmbedBatch(texts);
    REQUIRE(from_two.has_value());
    REQUIRE(from_four.has_value());
    REQUIRE(from_eight.has_value());
    REQUIRE(from_budget_one.has_value());
    REQUIRE(from_budget_two.has_value());
    REQUIRE(from_budget_four.has_value());
    REQUIRE(from_budget_eight.has_value());
    REQUIRE(again.has_value());
    REQUIRE(*from_two == *from_four);
    REQUIRE(*from_two == *from_eight);
    REQUIRE(*from_two == *from_budget_one);
    REQUIRE(*from_two == *from_budget_two);
    REQUIRE(*from_two == *from_budget_four);
    REQUIRE(*from_two == *from_budget_eight);
    REQUIRE(*from_eight == *again);
    CHECK(from_eight->front() != from_eight->back());
}

#else

TEST_CASE("a wider intra-op pool matches one thread byte for byte", "[session-pool][model]")
{
    if (!ModelPresent())
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const std::filesystem::path model_dir{NESSO_MODELS_DIR};
    const auto single = nesso::embed::OnnxEmbedder::Create(model_dir, 1);
    const auto wider = nesso::embed::OnnxEmbedder::Create(model_dir, 2);
    const auto pooled = nesso::embed::OnnxEmbedder::Create(model_dir, 8);
    const auto pinned_one = nesso::embed::OnnxEmbedder::CreateWithIntraOp(model_dir, 1);
    const auto pinned_eight = nesso::embed::OnnxEmbedder::CreateWithIntraOp(model_dir, 8);
    REQUIRE(single.has_value());
    REQUIRE(wider.has_value());
    REQUIRE(pooled.has_value());
    REQUIRE(pinned_one.has_value());
    REQUIRE(pinned_eight.has_value());
    if (!(*pooled)->Policy().quantized)
    {
        SKIP("the model is fp32; intra-op thread count is not a second session");
    }
    CHECK((*single)->Policy().sessions == 1);
    CHECK((*single)->Sessions() == 1);
    CHECK((*single)->Policy().intra_op_threads == 1);
    CHECK((*wider)->Policy().sessions == 2);
    CHECK((*wider)->Sessions() == 2);
    CHECK((*wider)->Policy().intra_op_threads == 1);
    CHECK_FALSE((*single)->Policy().shrink_arena);
    CHECK((*pooled)->Policy().sessions == 2);
    CHECK((*pooled)->Sessions() == 2);
    CHECK((*pooled)->Policy().intra_op_threads == 4);
    CHECK((*pooled)->Policy().shrink_arena);
    CHECK((*pinned_one)->Policy().intra_op_threads == 1);
    CHECK((*pinned_one)->Sessions() == 1);
    CHECK((*pinned_eight)->Policy().intra_op_threads == 8);
    CHECK((*pinned_eight)->Sessions() == 1);

    const auto texts = SampleTexts();
    const auto from_one = (*single)->EmbedBatch(texts);
    const auto from_wider = (*wider)->EmbedBatch(texts);
    const auto from_pool = (*pooled)->EmbedBatch(texts);
    const auto from_pinned_one = (*pinned_one)->EmbedBatch(texts);
    const auto from_pinned_eight = (*pinned_eight)->EmbedBatch(texts);
    const auto again = (*pooled)->EmbedBatch(texts);
    REQUIRE(from_one.has_value());
    REQUIRE(from_wider.has_value());
    REQUIRE(from_pool.has_value());
    REQUIRE(from_pinned_one.has_value());
    REQUIRE(from_pinned_eight.has_value());
    REQUIRE(again.has_value());
    REQUIRE(from_one->size() == texts.size());
    REQUIRE(*from_one == *from_wider);
    REQUIRE(*from_one == *from_pool);
    REQUIRE(*from_one == *from_pinned_one);
    REQUIRE(*from_one == *from_pinned_eight);
    REQUIRE(*from_pool == *again);
    CHECK(from_pool->front() != from_pool->back());
}

#endif

TEST_CASE("mixed-length batches match across session counts byte for byte", "[session-pool][model]")
{
    if (!ModelPresent())
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const std::filesystem::path model_dir{NESSO_MODELS_DIR};
    const auto one = nesso::embed::OnnxEmbedder::Create(model_dir, 1);
    const auto many = nesso::embed::OnnxEmbedder::Create(model_dir, 8);
    REQUIRE(one.has_value());
    REQUIRE(many.has_value());
    REQUIRE((*one)->Policy().quantized);
    CHECK((*many)->Sessions() > (*one)->Sessions());

    std::vector<std::string> texts;
    texts.reserve(130);
    for (int index = 0; index < 130; ++index)
    {
        if (index % 2 == 0)
        {
            texts.push_back("database connection refused " + std::to_string(index));
        }
        else
        {
            texts.push_back(std::string(80, 'x') + " timeout " + std::to_string(index));
        }
    }
    const auto from_one = (*one)->EmbedBatch(texts);
    const auto from_many = (*many)->EmbedBatch(texts);
    REQUIRE(from_one.has_value());
    REQUIRE(from_many.has_value());
    REQUIRE(*from_one == *from_many);
}
