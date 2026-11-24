// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM_ASSERT_HPP
#define PLATFORM_ASSERT_HPP

namespace nesso::platform
{

/// Report a failed NESSO_ASSERT. The check itself stays at the call site so the
/// success path is only a compare; this function is the cold failure path.
[[noreturn]] void AssertFail(const char *condition, unsigned line, const char *file) noexcept;

} // namespace nesso::platform

// NDEBUG does not disable the check: a broken internal invariant is a bug in
// every build, including release. Define NESSO_ASSERT_ASSUME before including
// this header to turn a failure into std::unreachable() instead of a trap.
// That switch stays off unless a translation unit opts in.
//
// A ternary, not if/else. AssertFail and std::unreachable are noreturn, so an
// else after that call is what clang-tidy 23 flags, and an if without an else
// would capture a caller's else. The expression is one statement either way.
#ifdef NESSO_ASSERT_ASSUME
#include <utility>
#define NESSO_ASSERT(cond) ((cond) ? static_cast<void>(0) : std::unreachable())
#else
#define NESSO_ASSERT(cond)                                                                                             \
    ((cond) ? static_cast<void>(0) : ::nesso::platform::AssertFail(#cond, static_cast<unsigned>(__LINE__), __FILE__))
#endif

#endif // PLATFORM_ASSERT_HPP
