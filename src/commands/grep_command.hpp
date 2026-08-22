// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#ifndef MACH1_GREP_COMMAND_HPP
#define MACH1_GREP_COMMAND_HPP

#include <cstddef>
#include <filesystem>
#include <string>

namespace mach1::commands
{

int runGrep(const std::filesystem::path &logPath, const std::string &query, size_t topK,
            const std::filesystem::path &modelDir);

} // namespace mach1::commands

#endif // MACH1_GREP_COMMAND_HPP
