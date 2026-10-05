// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef NESSO_TRACE_HPP
#define NESSO_TRACE_HPP

#include <chrono>
#include <string>
#include <string_view>

namespace nesso
{

/// Flushes one JSON line to stderr when the outermost session ends and NESSO_TRACE=1.
class TraceSession
{
  public:
    TraceSession();
    ~TraceSession();
    TraceSession(const TraceSession &) = delete;
    TraceSession &operator=(const TraceSession &) = delete;

  private:
    bool active_ = false;
};

/// Records wall time for one named stage. Nested stages finish before the session flushes.
class ScopedStage
{
  public:
    explicit ScopedStage(std::string_view name);
    ~ScopedStage();
    ScopedStage(const ScopedStage &) = delete;
    ScopedStage &operator=(const ScopedStage &) = delete;

  private:
    bool active_ = false;
    std::string name_;
    std::chrono::steady_clock::time_point start_{};
};

} // namespace nesso

#endif // NESSO_TRACE_HPP
