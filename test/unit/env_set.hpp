// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_TEST_ENV_SET_HPP
#define NESSO_TEST_ENV_SET_HPP

#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace nesso::test
{

/// Set or clear a process environment variable. Returns true on success.
inline auto EnvSet(const char *key, const char *value) -> bool
{
#ifdef _WIN32
    return _putenv_s(key, value) == 0;
#else
    return setenv(key, value, 1) == 0;
#endif
}

inline auto EnvClear(const char *key) -> bool
{
#ifdef _WIN32
    return _putenv_s(key, "") == 0;
#else
    return unsetenv(key) == 0;
#endif
}

inline auto EnvSet(const char *key, const std::string &value) -> bool { return EnvSet(key, value.c_str()); }

} // namespace nesso::test

#endif // NESSO_TEST_ENV_SET_HPP
