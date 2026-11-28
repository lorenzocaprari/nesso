// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_all.hpp>
#include <nesso_cli/check_stdout.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>

namespace
{

struct CloseFile
{
    void operator()(std::FILE *file) const noexcept
    {
        if (file != nullptr)
        {
            static_cast<void>(std::fclose(file));
        }
    }
};

auto OpenForWrite(const std::filesystem::path &path) -> std::FILE *
{
#ifdef _WIN32
    // Wide open for non-ACP temp paths; the Microsoft C runtime also rejects
    // the "e" (close-on-exec) mode flag.
    return _wfopen(path.c_str(), L"w");
#else
    return std::fopen(path.c_str(), "we");
#endif
}

} // namespace

TEST_CASE("StdoutWriteFailed is false for a writable temp file", "[cli][stdout]")
{
    const auto path = std::filesystem::temp_directory_path() / "nesso-check-stdout-ok";
    std::unique_ptr<std::FILE, CloseFile> stream{OpenForWrite(path)};
    REQUIRE(stream != nullptr);
    REQUIRE(std::fputs("ok\n", stream.get()) >= 0);
    CHECK_FALSE(nesso::cli::StdoutWriteFailed(stream.get()));
    // Windows cannot remove a file that is still open.
    stream.reset();
    std::filesystem::remove(path);
}

#ifndef _WIN32
// Windows has no /dev/full.
TEST_CASE("StdoutWriteFailed is true for /dev/full", "[cli][stdout]")
{
    const std::unique_ptr<std::FILE, CloseFile> stream{std::fopen("/dev/full", "we")};
    REQUIRE(stream != nullptr);
    (void)std::fputc('x', stream.get());
    CHECK(nesso::cli::StdoutWriteFailed(stream.get()));
}
#endif
