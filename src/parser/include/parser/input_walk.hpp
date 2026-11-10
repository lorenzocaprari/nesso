// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PARSER_INPUT_WALK_HPP
#define PARSER_INPUT_WALK_HPP

#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace nesso::parser
{

/// `path` is the input that could not be read.
struct InputWalkError
{
    std::filesystem::path path;
};

/// Files `grep` and `index` parse for `inputs`, in argument order.
/// `-` stays in place. A directory is replaced by the regular files under it, sorted by unsigned
/// byte order of each path. Directory symlinks are not entered. A file symlink is kept and later
/// read through the link. Archive files stay as one path; members are read from that file.
/// Anything else fails.
[[nodiscard]] std::expected<std::vector<std::filesystem::path>, InputWalkError>
expandInputs(std::span<const std::filesystem::path> inputs);

} // namespace nesso::parser

#endif // PARSER_INPUT_WALK_HPP
