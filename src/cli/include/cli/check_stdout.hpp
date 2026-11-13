// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef NESSO_CHECK_STDOUT_HPP
#define NESSO_CHECK_STDOUT_HPP

#include <cstdio>

namespace nesso::cli
{

/// True when flushing `stream` fails or the stream already has an error.
[[nodiscard]] inline bool stdoutWriteFailed(FILE *stream) noexcept
{
    return std::fflush(stream) != 0 || std::ferror(stream) != 0;
}

} // namespace nesso::cli

#endif // NESSO_CHECK_STDOUT_HPP
