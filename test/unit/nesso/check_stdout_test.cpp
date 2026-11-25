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

// The Microsoft C runtime rejects the "e" (close-on-exec) mode flag.
#ifdef _WIN32
constexpr const char *WRITE_MODE = "w";
#else
constexpr const char *WRITE_MODE = "we";
#endif

} // namespace

TEST_CASE("StdoutWriteFailed is false for a writable temp file", "[cli][stdout]")
{
    const auto path = std::filesystem::temp_directory_path() / "nesso-check-stdout-ok";
    std::unique_ptr<std::FILE, CloseFile> stream{std::fopen(path.string().c_str(), WRITE_MODE)};
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
