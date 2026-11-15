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

std::vector<std::string> SampleTexts()
{
    std::vector<std::string> texts;
    texts.reserve(33);
    for (int index = 0; index < 33; ++index)
    {
        texts.push_back("database connection refused " + std::to_string(index));
    }
    return texts;
}

bool ModelPresent()
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

    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    const auto two = nesso::embed::OnnxEmbedder::CreateWithIntraOp(modelDir, 2);
    const auto four = nesso::embed::OnnxEmbedder::CreateWithIntraOp(modelDir, 4);
    const auto eight = nesso::embed::OnnxEmbedder::CreateWithIntraOp(modelDir, 8);
    REQUIRE(two.has_value());
    REQUIRE(four.has_value());
    REQUIRE(eight.has_value());
    if (!(*two)->policy().quantized)
    {
        SKIP("the model is fp32; intra-op thread count is not a second session");
    }
    CHECK((*two)->policy().intraOpThreads == 2);
    CHECK((*four)->policy().intraOpThreads == 4);
    CHECK((*eight)->policy().intraOpThreads == 8);
    CHECK((*two)->sessions() == 1);
    CHECK((*four)->sessions() == 1);
    CHECK((*eight)->sessions() == 1);

    const auto budgetOne = nesso::embed::OnnxEmbedder::create(modelDir, 1);
    const auto budgetTwo = nesso::embed::OnnxEmbedder::create(modelDir, 2);
    const auto budgetFour = nesso::embed::OnnxEmbedder::create(modelDir, 4);
    const auto budgetEight = nesso::embed::OnnxEmbedder::create(modelDir, 8);
    REQUIRE(budgetOne.has_value());
    REQUIRE(budgetTwo.has_value());
    REQUIRE(budgetFour.has_value());
    REQUIRE(budgetEight.has_value());
    CHECK((*budgetOne)->policy().sessions == 1);
    CHECK((*budgetOne)->policy().intraOpThreads == nesso::embed::IntraOpThreadFloor());
    CHECK((*budgetTwo)->policy().sessions == 1);
    CHECK((*budgetTwo)->policy().intraOpThreads == 2);
    CHECK((*budgetFour)->policy().sessions == 1);
    CHECK((*budgetFour)->policy().intraOpThreads == 4);
    CHECK((*budgetEight)->policy().sessions == 2);
    CHECK((*budgetEight)->policy().intraOpThreads == 4);

    const auto texts = SampleTexts();
    const auto fromTwo = (*two)->EmbedBatch(texts);
    const auto fromFour = (*four)->EmbedBatch(texts);
    const auto fromEight = (*eight)->EmbedBatch(texts);
    const auto fromBudgetOne = (*budgetOne)->EmbedBatch(texts);
    const auto fromBudgetTwo = (*budgetTwo)->EmbedBatch(texts);
    const auto fromBudgetFour = (*budgetFour)->EmbedBatch(texts);
    const auto fromBudgetEight = (*budgetEight)->EmbedBatch(texts);
    const auto again = (*eight)->EmbedBatch(texts);
    REQUIRE(fromTwo.has_value());
    REQUIRE(fromFour.has_value());
    REQUIRE(fromEight.has_value());
    REQUIRE(fromBudgetOne.has_value());
    REQUIRE(fromBudgetTwo.has_value());
    REQUIRE(fromBudgetFour.has_value());
    REQUIRE(fromBudgetEight.has_value());
    REQUIRE(again.has_value());
    REQUIRE(*fromTwo == *fromFour);
    REQUIRE(*fromTwo == *fromEight);
    REQUIRE(*fromTwo == *fromBudgetOne);
    REQUIRE(*fromTwo == *fromBudgetTwo);
    REQUIRE(*fromTwo == *fromBudgetFour);
    REQUIRE(*fromTwo == *fromBudgetEight);
    REQUIRE(*fromEight == *again);
    CHECK(fromEight->front() != fromEight->back());
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
