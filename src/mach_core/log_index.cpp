// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT
#include "include/mach_core/log_index.hpp"

#include "include/mach_core/rrf.hpp"
#include "include/mach_core/vector_search.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

namespace mach_core
{
namespace
{

struct LineRecord
{
    uint64_t lineNo{0};
    std::string text;
};

struct EmbeddedRecord
{
    uint64_t lineNo{0};
    std::string text;
    std::vector<float> embedding;
};

template <typename T> class BoundedQueue
{
  public:
    explicit BoundedQueue(size_t capacity) : m_capacity(capacity == 0 ? 1 : capacity) {}

    bool push(T value, std::stop_token stop)
    {
        std::unique_lock lock(m_mutex);
        m_cvNotFull.wait(lock, stop, [&] { return m_closed || m_items.size() < m_capacity; });
        if (stop.stop_requested() || m_closed)
        {
            return false;
        }
        m_items.push_back(std::move(value));
        m_cvNotEmpty.notify_one();
        return true;
    }

    std::optional<T> pop(std::stop_token stop)
    {
        std::unique_lock lock(m_mutex);
        m_cvNotEmpty.wait(lock, stop, [&] { return m_closed || !m_items.empty(); });
        if (m_items.empty())
        {
            return std::nullopt;
        }
        T value = std::move(m_items.front());
        m_items.pop_front();
        m_cvNotFull.notify_one();
        return value;
    }

    void close()
    {
        std::scoped_lock lock(m_mutex);
        m_closed = true;
        m_cvNotEmpty.notify_all();
        m_cvNotFull.notify_all();
    }

  private:
    size_t m_capacity;
    std::deque<T> m_items;
    std::mutex m_mutex;
    std::condition_variable_any m_cvNotEmpty;
    std::condition_variable_any m_cvNotFull;
    bool m_closed{false};
};

} // namespace

std::expected<void, EngineError> LogIndex::open(const std::filesystem::path &dbPath)
{
    close();
    auto opened = m_engine.createOrOpen(dbPath, kEmbeddingDims);
    if (!opened)
    {
        return opened;
    }
    auto sidecar = m_lines.open(sidecarPathFor(dbPath));
    if (!sidecar)
    {
        close();
        return sidecar;
    }
    if (m_lines.size() != m_engine.getVectorCount())
    {
        close();
        return std::unexpected(EngineError::CorruptDatabase);
    }

    m_bm25.clear();
    auto all = m_lines.loadAll();
    if (!all)
    {
        close();
        return std::unexpected(all.error());
    }
    for (const auto &line : *all)
    {
        m_bm25.addDocument(line.id, line.text);
    }
    return {};
}

void LogIndex::close() noexcept
{
    m_engine.close();
    m_lines.close();
    m_bm25.clear();
}

std::expected<void, EngineError> LogIndex::appendIndexed(uint64_t lineNo, std::string_view text,
                                                         std::span<const float> embedding)
{
    const uint64_t id = m_engine.getVectorCount();
    auto appended = m_engine.appendVector(embedding);
    if (!appended)
    {
        return appended;
    }
    auto stored = m_lines.append(id, lineNo, text);
    if (!stored)
    {
        return stored;
    }
    m_bm25.addDocument(id, text);
    return {};
}

std::expected<uint64_t, EngineError> LogIndex::ingestSync(const std::filesystem::path &logFile,
                                                          const IEmbedder &embedder)
{
    std::ifstream in(logFile);
    if (!in)
    {
        return std::unexpected(EngineError::FileOpenFailure);
    }

    uint64_t added = 0;
    uint64_t lineNo = 0;
    std::string line;
    while (std::getline(in, line))
    {
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        auto embedding = embedder.embed(line);
        if (!embedding)
        {
            return std::unexpected(embedding.error());
        }
        auto indexed = appendIndexed(lineNo, line, *embedding);
        if (!indexed)
        {
            return std::unexpected(indexed.error());
        }
        ++added;
    }
    return added;
}

std::expected<uint64_t, EngineError> LogIndex::ingestParallel(const std::filesystem::path &logFile,
                                                              const IEmbedder &embedder)
{
    BoundedQueue<LineRecord> lineQueue(64);
    BoundedQueue<EmbeddedRecord> embedQueue(64);
    std::optional<EngineError> error;
    std::mutex errorMutex;
    const auto fail = [&](EngineError code)
    {
        std::scoped_lock lock(errorMutex);
        if (!error)
        {
            error = code;
        }
        lineQueue.close();
        embedQueue.close();
    };

    std::jthread reader([&](std::stop_token stop)
    {
        std::ifstream in(logFile);
        if (!in)
        {
            fail(EngineError::FileOpenFailure);
            lineQueue.close();
            return;
        }
        uint64_t lineNo = 0;
        std::string line;
        while (!stop.stop_requested() && std::getline(in, line))
        {
            ++lineNo;
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!lineQueue.push(LineRecord{.lineNo = lineNo, .text = std::move(line)}, stop))
            {
                break;
            }
        }
        lineQueue.close();
    });

    std::jthread encoder([&](std::stop_token stop)
    {
        while (!stop.stop_requested())
        {
            auto rec = lineQueue.pop(stop);
            if (!rec)
            {
                break;
            }
            auto embedding = embedder.embed(rec->text);
            if (!embedding)
            {
                fail(embedding.error());
                break;
            }
            if (!embedQueue.push(EmbeddedRecord{.lineNo = rec->lineNo, .text = std::move(rec->text),
                                                .embedding = std::move(*embedding)},
                                 stop))
            {
                break;
            }
        }
        embedQueue.close();
    });

    uint64_t added = 0;
    std::jthread indexer([&](std::stop_token stop)
    {
        while (!stop.stop_requested())
        {
            auto rec = embedQueue.pop(stop);
            if (!rec)
            {
                break;
            }
            auto indexed = appendIndexed(rec->lineNo, rec->text, rec->embedding);
            if (!indexed)
            {
                fail(indexed.error());
                break;
            }
            ++added;
        }
    });

    reader.join();
    encoder.join();
    indexer.join();

    if (error)
    {
        return std::unexpected(*error);
    }
    return added;
}

std::expected<uint64_t, EngineError> LogIndex::ingestFile(const std::filesystem::path &logFile,
                                                          const IEmbedder &embedder, bool parallel)
{
    if (!m_engine.isOpen())
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    if (embedder.dimensions() != kEmbeddingDims)
    {
        return std::unexpected(EngineError::MismatchedDimensions);
    }
    return parallel ? ingestParallel(logFile, embedder) : ingestSync(logFile, embedder);
}

std::expected<std::vector<HybridHit>, EngineError> LogIndex::search(std::string_view query, size_t k,
                                                                    const IEmbedder &embedder) const
{
    if (!m_engine.isOpen())
    {
        return std::unexpected(EngineError::DatabaseNotInitialized);
    }
    auto embedding = embedder.embed(query);
    if (!embedding)
    {
        return std::unexpected(embedding.error());
    }

    const size_t pool = std::max(k, size_t{50});
    auto semantic = searchTopKCosine<float>(m_engine, *embedding, pool);
    if (!semantic)
    {
        return std::unexpected(semantic.error());
    }
    const auto lexical = m_bm25.search(query, pool);
    const auto fused = fuseRrf(*semantic, lexical, k);

    std::vector<HybridHit> hits;
    hits.reserve(fused.size());
    for (const auto &item : fused)
    {
        auto line = m_lines.get(item.index);
        if (!line)
        {
            return std::unexpected(line.error());
        }
        hits.push_back(HybridHit{.index = item.index,
                                 .lineNo = line->lineNo,
                                 .rrfScore = item.score,
                                 .text = std::move(line->text)});
    }
    return hits;
}

} // namespace mach_core
