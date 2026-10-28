// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <embed/inference_wave.hpp>

#include <atomic>
#include <cstddef>
#include <exception>
#include <new>
#include <system_error>
#include <vector>

namespace
{

struct RestoreTerminate
{
    explicit RestoreTerminate(std::terminate_handler handler) : previous(std::set_terminate(handler)) {}
    RestoreTerminate(const RestoreTerminate &) = delete;
    RestoreTerminate &operator=(const RestoreTerminate &) = delete;
    ~RestoreTerminate() { std::set_terminate(previous); }

    std::terminate_handler previous;
};

void abortIfTerminate() { std::abort(); }

bool failed(const nesso::embed::SlotResult &slot)
{
    if (slot.has_value())
    {
        return false;
    }
    return slot.error() == nesso::embed::EmbedError::InferenceFailure;
}

bool succeeded(const nesso::embed::SlotResult &slot)
{
    if (!slot.has_value() || slot->size() != 1)
    {
        return false;
    }
    const std::vector<float> &row = slot->front();
    if (row.size() != 2)
    {
        return false;
    }
    return row[0] == 1.25F && row[1] == -0.5F;
}

nesso::embed::SlotResult embedding()
{
    std::vector<float> row{1.25F, -0.5F};
    std::vector<std::vector<float>> batch;
    batch.push_back(std::move(row));
    return batch;
}

} // namespace

TEST_CASE("inference wave joins workers and maps resource failures", "[inference_wave]")
{
    const RestoreTerminate restore{abortIfTerminate};

    const auto good = embedding();
    auto wrongHead = embedding();
    wrongHead->front()[0] = 0.0F;
    auto wrongTail = embedding();
    wrongTail->front()[1] = 0.0F;
    auto shortRow = embedding();
    shortRow->front().pop_back();
    const nesso::embed::SlotResult emptyBatch{std::vector<std::vector<float>>{}};
    const nesso::embed::SlotResult other{std::unexpected(nesso::embed::EmbedError::InvalidInput)};
    const nesso::embed::SlotResult inference{std::unexpected(nesso::embed::EmbedError::InferenceFailure)};
    CHECK_FALSE(failed(good));
    CHECK(failed(inference));
    CHECK_FALSE(failed(other));
    CHECK(succeeded(good));
    CHECK_FALSE(succeeded(other));
    CHECK_FALSE(succeeded(emptyBatch));
    CHECK_FALSE(succeeded(shortRow));
    CHECK_FALSE(succeeded(wrongHead));
    CHECK_FALSE(succeeded(wrongTail));

    int step = 0;

    bool called = false;
    const auto none = nesso::embed::runInferenceWave(0,
                                                     [&called](std::size_t) -> nesso::embed::SlotResult
                                                     {
                                                         called = true;
                                                         return embedding();
                                                     });
    if (!(none.has_value() && none->empty() && !called))
    {
        step = 2;
    }

    const auto one =
        nesso::embed::runInferenceWave(1, [](std::size_t) -> nesso::embed::SlotResult { return embedding(); });
    if (!(one.has_value() && one->size() == 1 && succeeded(one->front())))
    {
        step = 3;
    }

    const auto alloc =
        nesso::embed::runInferenceWave(1, [](std::size_t) -> nesso::embed::SlotResult { throw std::bad_alloc(); });
    if (!(alloc.has_value() && alloc->size() == 1 && failed(alloc->front())))
    {
        step = 4;
    }

    const auto sys = nesso::embed::runInferenceWave(
        1, [](std::size_t) -> nesso::embed::SlotResult
        { throw std::system_error(std::make_error_code(std::errc::not_enough_memory)); });
    if (!(sys.has_value() && sys->size() == 1 && failed(sys->front())))
    {
        step = 5;
    }

    std::atomic<int> ran{0};
    const auto several = nesso::embed::runInferenceWave(
        3,
        [&ran](std::size_t slot) -> nesso::embed::SlotResult
        {
            ran.fetch_add(1, std::memory_order_relaxed);
            if (slot == 0)
            {
                throw std::bad_alloc();
            }
            if (slot == 1)
            {
                throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
            }
            return embedding();
        });
    if (!(several.has_value() && several->size() == 3 && ran.load(std::memory_order_relaxed) == 3 &&
          failed((*several)[0]) && failed((*several)[1]) && succeeded((*several)[2])))
    {
        step = 6;
    }
    REQUIRE(step == 0);
}
