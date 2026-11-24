// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

// Release profiles already pass -DNDEBUG. Debug does not, so define it here
// when it is unset. Redefining the command-line macro is -Werror.
#ifndef NDEBUG
#define NDEBUG
#endif

#include <platform/assert.hpp>

#include <catch2/catch_all.hpp>

#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <vector>

#ifndef NDEBUG
#error "assert_test includes the header with NDEBUG defined"
#endif

namespace
{

[[nodiscard]] auto AlwaysTrue() noexcept -> bool
{
    const volatile int value = 1;
    return value != 0;
}

[[nodiscard]] auto AlwaysFalse() noexcept -> bool
{
    const volatile int value = 0;
    return value != 0;
}

[[nodiscard]] auto Aborted(int status) noexcept -> bool
{
    if (WIFSIGNALED(status) != 0 && WTERMSIG(status) == SIGABRT)
    {
        return true;
    }
    return WIFEXITED(status) != 0 && WEXITSTATUS(status) == 128 + SIGABRT;
}

struct ChildReport
{
    int status = 0;
    std::string stderr_text;
};

[[nodiscard]] auto RunFailureChild() -> ChildReport
{
    std::vector<std::string> env_storage;
    for (char **entry = environ; *entry != nullptr; ++entry)
    {
        const std::string_view item{*entry};
        if (item.starts_with("NESSO_ASSERT_CHILD="))
        {
            continue;
        }
        env_storage.emplace_back(item);
    }
    env_storage.emplace_back("NESSO_ASSERT_CHILD=1");
    std::vector<char *> env_pointers;
    env_pointers.reserve(env_storage.size() + 1);
    for (std::string &item : env_storage)
    {
        env_pointers.push_back(item.data());
    }
    env_pointers.push_back(nullptr);

    int pipes[2] = {-1, -1};
    REQUIRE(::pipe2(pipes, O_CLOEXEC) == 0);
    const struct rlimit core_limit{.rlim_cur = 0, .rlim_max = 0};
    REQUIRE(::setrlimit(RLIMIT_CORE, &core_limit) == 0);

    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0)
    {
        if (::dup2(pipes[1], STDERR_FILENO) < 0)
        {
            _exit(127);
        }
        ::close(pipes[0]);
        ::close(pipes[1]);
        char arg0[] = "/proc/self/exe";
        char arg1[] = "NESSO_ASSERT aborts and names the condition";
        char *argv[] = {arg0, arg1, nullptr};
        ::execve(arg0, argv, env_pointers.data());
        _exit(127);
    }

    ::close(pipes[1]);
    std::string captured;
    std::vector<char> buffer(4096);
    while (true)
    {
        const ssize_t got = ::read(pipes[0], buffer.data(), buffer.size());
        if (got < 0)
        {
            REQUIRE(errno == EINTR);
            continue;
        }
        if (got == 0)
        {
            break;
        }
        captured.append(buffer.data(), static_cast<std::size_t>(got));
    }
    ::close(pipes[0]);

    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = ::waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    REQUIRE(waited == pid);
    return ChildReport{.status = status, .stderr_text = std::move(captured)};
}

} // namespace

TEST_CASE("NESSO_ASSERT aborts and names the condition", "[assert]")
{
    if (std::getenv("NESSO_ASSERT_CHILD") != nullptr)
    {
        NESSO_ASSERT(AlwaysFalse());
        FAIL("NESSO_ASSERT returned");
    }

    NESSO_ASSERT(AlwaysTrue());

    const ChildReport child = RunFailureChild();
    REQUIRE(Aborted(child.status));

    const std::string marker = "nesso: assertion failed: AlwaysFalse() at ";
    const auto marker_at = child.stderr_text.find(marker);
    REQUIRE(marker_at != std::string::npos);
    const auto path_start = marker_at + marker.size();
    const auto line_at = child.stderr_text.find(':', path_start);
    REQUIRE(line_at != std::string::npos);
    const std::string path = child.stderr_text.substr(path_start, line_at - path_start);
    REQUIRE(path.contains("assert_test.cpp"));
    const auto line_end = child.stderr_text.find('\n', line_at + 1);
    REQUIRE(line_end != std::string::npos);
    const std::string line_text = child.stderr_text.substr(line_at + 1, line_end - (line_at + 1));
    std::size_t parsed = 0;
    const auto consumed = std::from_chars(line_text.data(), line_text.data() + line_text.size(), parsed);
    REQUIRE(consumed.ec == std::errc{});
    REQUIRE(consumed.ptr == line_text.data() + line_text.size());
    REQUIRE(parsed > 0);
}
