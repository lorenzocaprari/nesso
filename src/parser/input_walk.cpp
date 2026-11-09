// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <parser/input_walk.hpp>

#include <algorithm>
#include <iterator>
#include <system_error>

namespace nesso::parser
{

// char may be signed. Order by unsigned bytes so a byte above 0x7F does not sort first.
static bool pathBytesLess(const std::filesystem::path &left, const std::filesystem::path &right)
{
    const std::string &leftBytes = left.native();
    const std::string &rightBytes = right.native();
    const std::size_t count = std::min(leftBytes.size(), rightBytes.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto leftByte = static_cast<unsigned char>(leftBytes[index]);
        const auto rightByte = static_cast<unsigned char>(rightBytes[index]);
        if (leftByte != rightByte)
        {
            return leftByte < rightByte;
        }
    }
    return leftBytes.size() < rightBytes.size();
}

static std::expected<std::vector<std::filesystem::path>, InputWalkError>
walkDirectory(const std::filesystem::path &root)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    // directory_options::none does not follow directory symlinks, so a link cycle cannot loop.
    std::filesystem::recursive_directory_iterator cursor(root, std::filesystem::directory_options::none, error);
    if (error)
    {
        return std::unexpected(InputWalkError{.path = root});
    }
    const std::filesystem::recursive_directory_iterator end;
    while (cursor != end)
    {
        const std::filesystem::path current = cursor->path();
        const std::filesystem::file_status link = cursor->symlink_status(error);
        if (error)
        {
            return std::unexpected(InputWalkError{.path = current});
        }
        if (std::filesystem::is_symlink(link))
        {
            const std::filesystem::file_status target = std::filesystem::status(current, error);
            if (error || target.type() == std::filesystem::file_type::not_found)
            {
                return std::unexpected(InputWalkError{.path = current});
            }
            if (std::filesystem::is_regular_file(target))
            {
                files.push_back(current);
            }
        }
        else if (std::filesystem::is_regular_file(link))
        {
            files.push_back(current);
        }
        cursor.increment(error);
        if (error)
        {
            return std::unexpected(InputWalkError{.path = current});
        }
    }
    std::ranges::sort(files, pathBytesLess);
    return files;
}

std::expected<std::vector<std::filesystem::path>, InputWalkError>
expandInputs(std::span<const std::filesystem::path> inputs)
{
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::path &input : inputs)
    {
        if (input == "-")
        {
            files.emplace_back(input);
            continue;
        }
        std::error_code error;
        if (std::filesystem::is_directory(input, error))
        {
            if (error)
            {
                return std::unexpected(InputWalkError{.path = input});
            }
            auto walked = walkDirectory(input);
            if (!walked)
            {
                return std::unexpected(walked.error());
            }
            files.insert(files.end(), std::make_move_iterator(walked->begin()), std::make_move_iterator(walked->end()));
            continue;
        }
        if (error)
        {
            return std::unexpected(InputWalkError{.path = input});
        }
        if (std::filesystem::is_regular_file(input, error))
        {
            files.emplace_back(input);
            continue;
        }
        return std::unexpected(InputWalkError{.path = input});
    }
    return files;
}

} // namespace nesso::parser
