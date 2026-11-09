// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <core/top_k.hpp>

#include <core/distance.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
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
        return left.score != right.score ? left.score > right.score : left.index < right.index;
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

    ~Job() { requestStop(); }

    void start()
    {
        thread_ = std::jthread([this] { workerLoop(); });
    }

    void requestStop()
    {
        {
            const std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
    }

    void publish(void (*fn)(void *, size_t, size_t), void *context, size_t begin, size_t end)
    {
        const std::scoped_lock lock(mutex_);
        fn_ = fn;
        context_ = context;
        begin_ = begin;
        end_ = end;
        ++epoch_;
        cv_.notify_all();
    }

    void wait()
    {
        std::unique_lock lock(mutex_);
        const uint64_t epoch = epoch_;
        cv_.wait(lock, [&] { return epoch != 0 && finished_ == epoch; });
    }

  private:
    void workerLoop()
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
            // fn_ stays fixed until this worker marks finished_ and the publisher's wait returns.
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
            job->requestStop();
        }
        jobs_.clear();
    }

    std::mutex &mutex() { return mutex_; }

    Job &job(size_t index) { return *jobs_[index]; }

    bool ensure(size_t background)
    {
        try
        {
            while (jobs_.size() < background)
            {
                auto job = std::make_unique<Job>();
                job->start();
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
    void merge(std::vector<Hit> local)
    {
        const std::scoped_lock lock(mutex_);
        hits_.insert(hits_.end(), local.begin(), local.end());
    }

    void noteError(size_t at, EngineError error)
    {
        const std::scoped_lock lock(mutex_);
        if (at < errorAt_)
        {
            error_ = error;
            errorAt_ = at;
        }
    }

    [[nodiscard]] const std::optional<EngineError> &error() const { return error_; }

    std::vector<Hit> finish(size_t k)
    {
        detail::selectTopKInPlace(hits_, k, HitOrder{});
        return std::move(hits_);
    }

  private:
    std::mutex mutex_;
    std::vector<Hit> hits_;
    std::optional<EngineError> error_;
    size_t errorAt_ = std::numeric_limits<size_t>::max();
};

class TopScan
{
  public:
    TopScan(std::span<const float> query, std::span<const float> matrix, size_t dims, size_t k)
        : query_(query), matrix_(matrix), dims_(dims), k_(k)
    {
    }

    void run(size_t begin, size_t end)
    {
        std::vector<Hit> local;
        if (end > begin)
        {
            local.reserve(std::min(k_, end - begin));
        }
        const HitOrder better;
        for (size_t row = begin; row < end; ++row)
        {
            const std::span<const float> stored = matrix_.subspan(row * dims_, dims_);
            const auto score = math::DistanceMetrics::dotProduct(query_, stored);
            if (!score)
            {
                merged_.noteError(row, score.error());
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
        merged_.merge(std::move(local));
    }

    [[nodiscard]] const std::optional<EngineError> &error() const { return merged_.error(); }

    std::vector<Hit> finish(size_t k) { return merged_.finish(k); }

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
    RankScan(float (*score)(void *, size_t), bool (*keep)(void *, size_t), void *context, size_t k)
        : score_(score), keep_(keep), context_(context), k_(k)
    {
    }

    void run(size_t begin, size_t end)
    {
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
        merged_.merge(std::move(local));
    }

    std::vector<Hit> finish(size_t k) { return merged_.finish(k); }

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
            team_->job(index).wait();
        }
    }

  private:
    Pool *team_ = nullptr;
    size_t background_ = 0;
};

} // namespace

static Pool &pool()
{
    static Pool instance;
    return instance;
}

static std::vector<Range> splitRows(size_t rows, size_t workers)
{
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
    return ranges;
}

static void runTopChunk(void *raw, size_t begin, size_t end) { static_cast<TopScan *>(raw)->run(begin, end); }

static void runRankChunk(void *raw, size_t begin, size_t end) { static_cast<RankScan *>(raw)->run(begin, end); }

namespace detail
{

void parallelRows(size_t threads, size_t rows, void (*fn)(void *context, size_t begin, size_t end), void *context)
{
    if (fn == nullptr)
    {
        return;
    }
    if (rows == 0 || threads <= 1 || rows < PARALLEL_MIN_ROWS)
    {
        fn(context, 0, rows);
        return;
    }

    const size_t width = std::min({rows, threads, PARALLEL_MAX_THREADS});
    if (width <= 1)
    {
        fn(context, 0, rows);
        return;
    }

    Pool &team = pool();
    const std::scoped_lock poolLock(team.mutex());
    if (!team.ensure(width - 1))
    {
        fn(context, 0, rows);
        return;
    }

    const std::vector<Range> ranges = splitRows(rows, width);
    const WaitGuard guard(team, width - 1);

    for (size_t index = 1; index < width; ++index)
    {
        team.job(index - 1).publish(fn, context, ranges[index].begin, ranges[index].end);
    }
    fn(context, ranges[0].begin, ranges[0].end);
}

} // namespace detail

std::expected<std::vector<Hit>, EngineError> topK(std::span<const float> query, std::span<const float> matrix,
                                                  size_t dims, size_t k, size_t threads)
{
    if (k == 0 || matrix.empty())
    {
        return std::vector<Hit>{};
    }
    if (dims == 0)
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (query.size() != dims)
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    if (matrix.size() % dims != 0)
    {
        return std::unexpected(EngineError::CorruptDatabase);
    }

    TopScan scan(query, matrix, dims, k);
    detail::parallelRows(threads, matrix.size() / dims, runTopChunk, &scan);
    if (scan.error())
    {
        return std::unexpected(*scan.error());
    }
    return scan.finish(k);
}

std::vector<Hit> rankTop(size_t count, size_t k, size_t threads, float (*score)(void *context, size_t index),
                         bool (*keep)(void *context, size_t index), void *context)
{
    if (k == 0 || count == 0 || score == nullptr || keep == nullptr)
    {
        return {};
    }
    RankScan scan(score, keep, context, k);
    detail::parallelRows(threads, count, runRankChunk, &scan);
    return scan.finish(k);
}

} // namespace nesso::core
