// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_THREAD_COUNT_HPP
#define NESSO_THREAD_COUNT_HPP

#include <charconv>
#include <cstdint>
#include <expected>
#include <iterator>
#include <string_view>
#include <system_error>

namespace nesso
{

/// Largest CPU budget accepted by --threads and NESSO_THREADS.
inline constexpr size_t MAX_THREAD_BUDGET = 1024;

enum class ThreadCountError : uint8_t
{
    NotPositiveInteger,
    OutOfRange
};

/// CPU budget that can only hold 1..MAX_THREAD_BUDGET.
class ThreadCount
{
  public:
    [[nodiscard]] static std::expected<ThreadCount, ThreadCountError> parse(std::string_view text)
    {
        if (text.empty() || text.find_first_not_of("0123456789") != std::string_view::npos ||
            text.find_first_not_of('0') == std::string_view::npos)
        {
            return std::unexpected(ThreadCountError::NotPositiveInteger);
        }
        size_t parsed = 0;
        const char *const first = std::to_address(text.begin());
        const char *const last = std::to_address(text.end());
        const std::from_chars_result result = std::from_chars(first, last, parsed);
        if (result.ec != std::errc{} || result.ptr != last)
        {
            return std::unexpected(ThreadCountError::NotPositiveInteger);
        }
        if (parsed > MAX_THREAD_BUDGET)
        {
            return std::unexpected(ThreadCountError::OutOfRange);
        }
        return ThreadCount{parsed};
    }

    [[nodiscard]] constexpr size_t get() const noexcept { return value_; }

  private:
    explicit constexpr ThreadCount(size_t value) noexcept : value_(value) {}

    size_t value_;
};

} // namespace nesso

#endif
