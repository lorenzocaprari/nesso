// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0
//
// clang-cl / MSVC do not ship <unistd.h>. Unit and fuzz harnesses only need
// getpid() for unique temp paths; provide that under the same include name so
// the sources stay shared.

#ifndef NESSO_TEST_WIN_COMPAT_UNISTD_H
#define NESSO_TEST_WIN_COMPAT_UNISTD_H

#include <process.h>

inline int getpid() { return _getpid(); }

#endif // NESSO_TEST_WIN_COMPAT_UNISTD_H
