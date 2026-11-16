// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include "core/core_types.hpp"
#include <algorithm>
#include <core/top_k.hpp>

#include <core/distance.hpp>
#include <platform/assert.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace nesso::core
{
namespace
{

struct HitOrder
{
    bool operator()(const Hit &left, const Hit &right) const
    {
        return RankBefore(left.score, left.index, right.score, right.index);
    }
};

class Job
{
  public:
    Job() = default;
    Job(const Job &) = delete;
    Job &operator=(const Job &) = delete;
    Job(Job &&) = delete;
    Job &operator=(Job &&) = delete;

    ~Job() { RequestStop(); }

    void Start()
    {
        thread_ = std::jthread([this] { WorkerLoop(); });
    }

    void RequestStop()
    {
        {
            const std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
    }

    void Publish(void (*row_fn)(void *, size_t, size_t), void *context, size_t begin, size_t end)
    {
        const std::scoped_lock lock(mutex_);
        fn_ = row_fn;
        context_ = context;
        begin_ = begin;
        end_ = end;
        ++epoch_;
        cv_.notify_all();
    }

    void Wait()
    {
        std::unique_lock lock(mutex_);
        const uint64_t epoch = epoch_;
        cv_.wait(lock, [&] { return epoch != 0 && finished_ == epoch; });
    }

  private:
    void WorkerLoop()
    {
        uint64_t seen = 0;
        std::unique_lock lock(mutex_);
        while (!stop_)
        {
            cv_.wait(lock, [&] { return stop_ || epoch_ != seen; });
            if (stop_)
            {
                return;
            }
            seen = epoch_;
            void *const context = context_;
            const size_t begin = begin_;
            const size_t end = end_;
            lock.unlock();
            // fn_ stays fixed until this worker marks finished_ and the publisher's
            // wait returns.
            fn_(context, begin, end);
            lock.lock();
            finished_ = seen;
            cv_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    void (*fn_)(void *, size_t, size_t) = nullptr;
    void *context_ = nullptr;
    size_t begin_ = 0;
    size_t end_ = 0;
    uint64_t epoch_ = 0;
    uint64_t finished_ = 0;
    bool stop_ = false;
    std::jthread thread_;
};

class Pool
{
  public:
    Pool() = default;
    Pool(const Pool &) = delete;
    Pool &operator=(const Pool &) = delete;
    Pool(Pool &&) = delete;
    Pool &operator=(Pool &&) = delete;

    ~Pool()
    {
        for (const std::unique_ptr<Job> &job : jobs_)
        {
            job->RequestStop();
        }
        jobs_.clear();
    }

    std::mutex &Mutex() { return mutex_; }

    Job &GetJob(size_t index) { return *jobs_[index]; }

    bool Ensure(size_t background)
    {
        try
        {
            while (jobs_.size() < background)
            {
                auto job = std::make_unique<Job>();
                job->Start();
                jobs_.push_back(std::move(job));
            }
            return true;
        }
        catch (const std::system_error &)
        {
            return false;
        }
    }

  private:
    std::mutex mutex_;
    std::vector<std::unique_ptr<Job>> jobs_;
};

struct Range
{
    size_t begin = 0;
    size_t end = 0;
};

class MergedHits
{
  public:
    void Merge(std::vector<Hit> local)
    {
        const std::scoped_lock lock(mutex_);
        hits_.insert(hits_.end(), local.begin(), local.end());
    }

    void NoteError(size_t row, EngineError error)
    {
        const std::scoped_lock lock(mutex_);
        if (row < error_at_)
        {
            error_ = error;
            error_at_ = row;
        }
    }

    [[nodiscard]] const std::optional<EngineError> &Error() const { return error_; }

    std::vector<Hit> Finish(size_t top_k)
    {
        detail::SelectTopKInPlace(hits_, top_k, HitOrder{});
        return std::move(hits_);
    }

  private:
    std::mutex mutex_;
    std::vector<Hit> hits_;
    std::optional<EngineError> error_;
    size_t error_at_ = std::numeric_limits<size_t>::max();
};

class TopScan
{
  public:
    explicit TopScan(const TopKRequest &request)
        : query_(request.query), matrix_(request.matrix), dims_(request.dims), k_(request.k)
    {
    }

    void Run(size_t begin, size_t end)
    {
        NESSO_ASSERT(dims_ > 0);
        NESSO_ASSERT(query_.size() == dims_);
        NESSO_ASSERT(matrix_.size() % dims_ == 0);
        NESSO_ASSERT(k_ > 0);
        NESSO_ASSERT(begin <= end);
        NESSO_ASSERT(end <= matrix_.size() / dims_);
        std::vector<Hit> local;
        if (end > begin)
        {
            local.reserve(std::min(k_, end - begin));
        }
        const HitOrder better;
        for (size_t row = begin; row < end; ++row)
        {
            const std::span<const float> stored = matrix_.subspan(row * dims_, dims_);
            const auto score = math::DistanceMetrics::DotProduct(query_, stored);
            if (!score)
            {
                merged_.NoteError(row, score.error());
                return;
            }
            const Hit hit{.index = static_cast<uint64_t>(row), .score = *score};
            if (local.size() < k_)
            {
                local.push_back(hit);
                std::ranges::push_heap(local, better);
            }
            else if (better(hit, local.front()))
            {
                std::ranges::pop_heap(local, better);
                local.back() = hit;
                std::ranges::push_heap(local, better);
            }
        }
        merged_.Merge(std::move(local));
    }

    [[nodiscard]] const std::optional<EngineError> &Error() const { return merged_.Error(); }

    std::vector<Hit> Finish(size_t top_k) { return merged_.Finish(top_k); }

  private:
    std::span<const float> query_;
    std::span<const float> matrix_;
    size_t dims_ = 0;
    size_t k_ = 0;
    MergedHits merged_;
};

class RankScan
{
  public:
    RankScan(float (*score)(void *, size_t), bool (*keep)(void *, size_t), void *context, size_t top_k)
        : score_(score), keep_(keep), context_(context), k_(top_k)
    {
    }

    void Run(size_t begin, size_t end)
    {
        NESSO_ASSERT(k_ > 0);
        NESSO_ASSERT(score_ != nullptr);
        NESSO_ASSERT(keep_ != nullptr);
        NESSO_ASSERT(begin <= end);
        std::vector<Hit> local;
        if (end > begin)
        {
            local.reserve(std::min(k_, end - begin));
        }
        const HitOrder better;
        for (size_t index = begin; index < end; ++index)
        {
            if (!keep_(context_, index))
            {
                continue;
            }
            const Hit hit{.index = static_cast<uint64_t>(index), .score = score_(context_, index)};
            if (local.size() < k_)
            {
                local.push_back(hit);
                std::ranges::push_heap(local, better);
            }
            else if (better(hit, local.front()))
            {
                std::ranges::pop_heap(local, better);
                local.back() = hit;
                std::ranges::push_heap(local, better);
            }
        }
        merged_.Merge(std::move(local));
    }

    std::vector<Hit> Finish(size_t top_k) { return merged_.Finish(top_k); }

  private:
    float (*score_)(void *, size_t) = nullptr;
    bool (*keep_)(void *, size_t) = nullptr;
    void *context_ = nullptr;
    size_t k_ = 0;
    MergedHits merged_;
};

class WaitGuard
{
  public:
    WaitGuard(Pool &team, size_t background) : team_(&team), background_(background) {}
    WaitGuard(const WaitGuard &) = delete;
    WaitGuard &operator=(const WaitGuard &) = delete;
    WaitGuard(WaitGuard &&) = delete;
    WaitGuard &operator=(WaitGuard &&) = delete;
    ~WaitGuard()
    {
        for (size_t index = 0; index < background_; ++index)
        {
            team_->GetJob(index).Wait();
        }
    }

  private:
    Pool *team_ = nullptr;
    size_t background_ = 0;
};

} // namespace

static Pool &GetPool()
{
    static Pool instance;
    return instance;
}

static std::vector<Range> SplitRows(size_t rows, size_t workers)
{
    NESSO_ASSERT(workers > 0);
    NESSO_ASSERT(rows > 0);
    std::vector<Range> ranges;
    ranges.reserve(workers);
    const size_t base = rows / workers;
    const size_t extra = rows % workers;
    size_t cursor = 0;
    for (size_t index = 0; index < workers; ++index)
    {
        const size_t count = base + (index < extra ? 1U : 0U);
        ranges.push_back(Range{.begin = cursor, .end = cursor + count});
        cursor += count;
    }
    NESSO_ASSERT(cursor == rows);
    return ranges;
}

static void RunTopChunk(void *raw, size_t begin, size_t end) { static_cast<TopScan *>(raw)->Run(begin, end); }

static void RunRankChunk(void *raw, size_t begin, size_t end) { static_cast<RankScan *>(raw)->Run(begin, end); }

namespace detail
{

void ParallelRows(size_t threads, size_t rows, void (*row_fn)(void *context, size_t begin, size_t end), void *context)
{
    if (row_fn == nullptr)
    {
        return;
    }
    if (rows == 0 || threads <= 1 || rows < PARALLEL_MIN_ROWS)
    {
        row_fn(context, 0, rows);
        return;
    }

    const size_t width = std::min({rows, threads, PARALLEL_MAX_THREADS});
    if (width <= 1)
    {
        row_fn(context, 0, rows);
        return;
    }

    Pool &team = GetPool();
    const std::scoped_lock pool_lock(team.Mutex());
    if (!team.Ensure(width - 1))
    {
        row_fn(context, 0, rows);
        return;
    }

    const std::vector<Range> ranges = SplitRows(rows, width);
    const WaitGuard guard(team, width - 1);

    for (size_t index = 1; index < width; ++index)
    {
        team.GetJob(index - 1).Publish(row_fn, context, ranges[index].begin, ranges[index].end);
    }
    row_fn(context, ranges[0].begin, ranges[0].end);
}

} // namespace detail

std::expected<std::vector<Hit>, EngineError> TopK(const TopKRequest &request)
{
    if (request.k == 0 || request.matrix.empty())
    {
        return std::vector<Hit>{};
    }
    if (request.dims == 0)
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (request.query.size() != request.dims)
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (request.matrix.size() % request.dims != 0)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    TopScan scan(request);
    detail::ParallelRows(request.threads, request.matrix.size() / request.dims, RunTopChunk, &scan);
    if (scan.Error())
    {
        return std::unexpected(*scan.Error());
    }
    return scan.Finish(request.k);
}

std::vector<Hit> RankTop(const RankTopRequest &request)
{
    if (request.k == 0 || request.count == 0 || request.score == nullptr || request.keep == nullptr)
    {
        return {};
    }
    RankScan scan(request.score, request.keep, request.context, request.k);
    detail::ParallelRows(request.threads, request.count, RunRankChunk, &scan);
    return scan.Finish(request.k);
}

} // namespace nesso::core
