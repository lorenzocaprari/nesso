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

std::vector<std::string> sampleTexts()
{
    std::vector<std::string> texts;
    texts.reserve(33);
    for (int index = 0; index < 33; ++index)
    {
        texts.push_back("database connection refused " + std::to_string(index));
    }
    return texts;
}

bool modelPresent()
{
    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    return std::filesystem::is_regular_file(modelDir / "model.onnx") &&
           std::filesystem::is_regular_file(modelDir / "vocab.txt");
}

} // namespace

#if defined(__aarch64__)

TEST_CASE("aarch64 intra-op widths from 2 upward match", "[session-pool][model]")
{
    if (!modelPresent())
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    const auto two = nesso::embed::OnnxEmbedder::createWithIntraOp(modelDir, 2);
    const auto four = nesso::embed::OnnxEmbedder::createWithIntraOp(modelDir, 4);
    const auto eight = nesso::embed::OnnxEmbedder::createWithIntraOp(modelDir, 8);
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
    CHECK((*budgetOne)->policy().intraOpThreads == nesso::embed::intraOpThreadFloor());
    CHECK((*budgetTwo)->policy().sessions == 1);
    CHECK((*budgetTwo)->policy().intraOpThreads == 2);
    CHECK((*budgetFour)->policy().sessions == 1);
    CHECK((*budgetFour)->policy().intraOpThreads == 4);
    CHECK((*budgetEight)->policy().sessions == 2);
    CHECK((*budgetEight)->policy().intraOpThreads == 4);

    const auto texts = sampleTexts();
    const auto fromTwo = (*two)->embedBatch(texts);
    const auto fromFour = (*four)->embedBatch(texts);
    const auto fromEight = (*eight)->embedBatch(texts);
    const auto fromBudgetOne = (*budgetOne)->embedBatch(texts);
    const auto fromBudgetTwo = (*budgetTwo)->embedBatch(texts);
    const auto fromBudgetFour = (*budgetFour)->embedBatch(texts);
    const auto fromBudgetEight = (*budgetEight)->embedBatch(texts);
    const auto again = (*eight)->embedBatch(texts);
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
    if (!modelPresent())
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    const auto single = nesso::embed::OnnxEmbedder::create(modelDir, 1);
    const auto wider = nesso::embed::OnnxEmbedder::create(modelDir, 2);
    const auto pooled = nesso::embed::OnnxEmbedder::create(modelDir, 8);
    const auto pinnedOne = nesso::embed::OnnxEmbedder::createWithIntraOp(modelDir, 1);
    const auto pinnedEight = nesso::embed::OnnxEmbedder::createWithIntraOp(modelDir, 8);
    REQUIRE(single.has_value());
    REQUIRE(wider.has_value());
    REQUIRE(pooled.has_value());
    REQUIRE(pinnedOne.has_value());
    REQUIRE(pinnedEight.has_value());
    if (!(*pooled)->policy().quantized)
    {
        SKIP("the model is fp32; intra-op thread count is not a second session");
    }
    CHECK((*single)->policy().sessions == 1);
    CHECK((*single)->sessions() == 1);
    CHECK((*single)->policy().intraOpThreads == 1);
    CHECK((*wider)->policy().sessions == 2);
    CHECK((*wider)->sessions() == 2);
    CHECK((*wider)->policy().intraOpThreads == 1);
    CHECK_FALSE((*single)->policy().shrinkArena);
    CHECK((*pooled)->policy().sessions == 2);
    CHECK((*pooled)->sessions() == 2);
    CHECK((*pooled)->policy().intraOpThreads == 4);
    CHECK((*pooled)->policy().shrinkArena);
    CHECK((*pinnedOne)->policy().intraOpThreads == 1);
    CHECK((*pinnedOne)->sessions() == 1);
    CHECK((*pinnedEight)->policy().intraOpThreads == 8);
    CHECK((*pinnedEight)->sessions() == 1);

    const auto texts = sampleTexts();
    const auto fromOne = (*single)->embedBatch(texts);
    const auto fromWider = (*wider)->embedBatch(texts);
    const auto fromPool = (*pooled)->embedBatch(texts);
    const auto fromPinnedOne = (*pinnedOne)->embedBatch(texts);
    const auto fromPinnedEight = (*pinnedEight)->embedBatch(texts);
    const auto again = (*pooled)->embedBatch(texts);
    REQUIRE(fromOne.has_value());
    REQUIRE(fromWider.has_value());
    REQUIRE(fromPool.has_value());
    REQUIRE(fromPinnedOne.has_value());
    REQUIRE(fromPinnedEight.has_value());
    REQUIRE(again.has_value());
    REQUIRE(fromOne->size() == texts.size());
    REQUIRE(*fromOne == *fromWider);
    REQUIRE(*fromOne == *fromPool);
    REQUIRE(*fromOne == *fromPinnedOne);
    REQUIRE(*fromOne == *fromPinnedEight);
    REQUIRE(*fromPool == *again);
    CHECK(fromPool->front() != fromPool->back());
}

#endif

TEST_CASE("mixed-length batches match across session counts byte for byte", "[session-pool][model]")
{
    if (!modelPresent())
    {
#ifdef NESSO_REQUIRE_MODEL
        FAIL("models/ not present; run scripts/fetch-model");
#else
        SKIP("models/ not present; run scripts/fetch-model");
#endif
    }

    const std::filesystem::path modelDir{NESSO_MODELS_DIR};
    const auto one = nesso::embed::OnnxEmbedder::create(modelDir, 1);
    const auto many = nesso::embed::OnnxEmbedder::create(modelDir, 8);
    REQUIRE(one.has_value());
    REQUIRE(many.has_value());
    REQUIRE((*one)->policy().quantized);
    CHECK((*many)->sessions() > (*one)->sessions());

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
    const auto fromOne = (*one)->embedBatch(texts);
    const auto fromMany = (*many)->embedBatch(texts);
    REQUIRE(fromOne.has_value());
    REQUIRE(fromMany.has_value());
    REQUIRE(*fromOne == *fromMany);
}
