// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

#include <catch2/catch_all.hpp>
#include <platform/file.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <unistd.h>

TEST_CASE("a mapped file views its bytes", "[platform]")
{
    const auto path = std::filesystem::temp_directory_path() / ("nesso_map_" + std::to_string(getpid()));
    {
        std::ofstream output(path, std::ios::binary);
        REQUIRE(output);
        output << "nesso";
    }

    const auto mapped = platform::MappedFile::open(path);
    REQUIRE(mapped);
    REQUIRE(mapped->bytes().size() == 5);
    REQUIRE(mapped->bytes()[0] == std::byte{'n'});

    std::filesystem::remove(path);
    REQUIRE_FALSE(platform::MappedFile::open(path));
}

TEST_CASE("mapping handles directories, empty files, and a failed replace", "[platform]")
{
    const auto directory = std::filesystem::temp_directory_path() / ("nesso_mapdir_" + std::to_string(getpid()));
    std::filesystem::create_directory(directory);

    const auto directoryMap = platform::MappedFile::open(directory);
    REQUIRE_FALSE(directoryMap);
    REQUIRE(directoryMap.error() == platform::FileError::MapFailure);

    const auto empty = directory / "empty";
    {
        std::ofstream output(empty, std::ios::binary);
        REQUIRE(output);
    }
    const auto mapped = platform::MappedFile::open(empty);
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
    auto left = platform::MappedFile::open(first);
    auto right = platform::MappedFile::open(second);
    REQUIRE(left);
    REQUIRE(right);
    *left = std::move(*right);
    REQUIRE(left->bytes().size() == 4);
    REQUIRE(left->bytes()[0] == std::byte{'t'});

    const auto nested = directory / "nested";
    std::filesystem::create_directory(nested);
    REQUIRE_FALSE(platform::replaceFileAtomically(first, nested));

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

    REQUIRE(platform::replaceFileAtomically(source, destination));
    REQUIRE_FALSE(std::filesystem::exists(source));
    std::ifstream input(destination);
    std::string text;
    REQUIRE(input >> text);
    REQUIRE(text == "v2");
    std::filesystem::remove_all(directory);
}

TEST_CASE("peak resident bytes is a whole number of pages", "[platform]")
{
    const std::int64_t peak = platform::peakResidentBytes();
    REQUIRE(peak >= 0);
    REQUIRE(peak % 1024 == 0);
}
