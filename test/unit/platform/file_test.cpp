// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <platform/file.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

TEST_CASE("a mapped file views its bytes", "[platform]")
{
    const auto path = std::filesystem::temp_directory_path() / ("nesso_map_" + std::to_string(getpid()));
    {
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output);
        output << "nesso";
    }

    const auto mapped = nesso::platform::MappedFile::open(path);
    REQUIRE(mapped);
    REQUIRE(mapped->bytes().size() == 5);
    REQUIRE(mapped->bytes()[0] == std::byte{'n'});

    std::filesystem::remove(path);
    REQUIRE_FALSE(nesso::platform::MappedFile::open(path));
}

TEST_CASE("mapping handles directories, empty files, and a failed replace", "[platform]")
{
    const auto directory = std::filesystem::temp_directory_path() / ("nesso_mapdir_" + std::to_string(getpid()));
    std::filesystem::create_directory(directory);

    const auto directoryMap = nesso::platform::MappedFile::open(directory);
    REQUIRE_FALSE(directoryMap);
    REQUIRE(directoryMap.error() == nesso::platform::FileError::MapFailure);

    const auto empty = directory / "empty";
    {
        std::ofstream output(empty, std::ios::binary);
        REQUIRE(output);
    }
    const auto mapped = nesso::platform::MappedFile::open(empty);
    REQUIRE(mapped);
    REQUIRE(mapped->bytes().empty());

    const auto first = directory / "first";
    const auto second = directory / "second";
    {
        std::ofstream output(first, std::ios::binary);
        REQUIRE(output);
        output << "one";
    }
    {
        std::ofstream output(second, std::ios::binary);
        REQUIRE(output);
        output << "two!";
    }
    auto left = nesso::platform::MappedFile::open(first);
    auto right = nesso::platform::MappedFile::open(second);
    REQUIRE(left);
    REQUIRE(right);
    *left = std::move(*right);
    REQUIRE(left->bytes().size() == 4);
    REQUIRE(left->bytes()[0] == std::byte{'t'});

    const auto nested = directory / "nested";
    std::filesystem::create_directory(nested);
    REQUIRE_FALSE(nesso::platform::replaceFileAtomically(first, nested));

    std::filesystem::remove_all(directory);
}

TEST_CASE("atomic replace keeps the destination name", "[platform]")
{
    const auto directory = std::filesystem::temp_directory_path() / ("nesso_replace_" + std::to_string(getpid()));
    std::filesystem::create_directory(directory);
    const auto destination = directory / "corpus.nesso";
    const auto source = directory / "corpus.nesso.tmp";
    {
        std::ofstream output(source, std::ios::binary);
        REQUIRE(output);
        output << "v2";
    }

    REQUIRE(nesso::platform::replaceFileAtomically(source, destination));
    REQUIRE_FALSE(std::filesystem::exists(source));
    std::ifstream input(destination);
    std::string text;
    REQUIRE(input >> text);
    REQUIRE(text == "v2");
    std::filesystem::remove_all(directory);
}

TEST_CASE("peak resident bytes is positive and monotonic", "[platform]")
{
    const std::int64_t before = nesso::platform::peakResidentBytes();
    REQUIRE(before > 0);
    REQUIRE(before % 1024 == 0);

    std::vector<std::byte> block(8 * 1024 * 1024);
    std::uint64_t dirty = 0;
    for (std::size_t offset = 0; offset < block.size(); offset += 4096)
    {
        block[offset] = std::byte{1};
        dirty += static_cast<unsigned char>(block[offset]);
    }
    REQUIRE(dirty == block.size() / 4096);

    const std::int64_t after = nesso::platform::peakResidentBytes();
    REQUIRE(after >= before);
    REQUIRE(after % 1024 == 0);
}
