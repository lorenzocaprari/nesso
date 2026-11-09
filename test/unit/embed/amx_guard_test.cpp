#include <catch2/catch_all.hpp>
#include <embed/amx_guard.hpp>
#include <embed/onnx_embedder.hpp>

#include <array>
#include <cstdio>
#include <memory>
#include <string>

#if defined(__linux__) && defined(__x86_64__)
#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>
#endif

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

std::string captured(std::FILE *file)
{
    REQUIRE(std::fflush(file) == 0);
    REQUIRE(std::fseek(file, 0, SEEK_SET) == 0);
    std::string text;
    std::array<char, 256> chunk{};
    while (std::fgets(chunk.data(), static_cast<int>(chunk.size()), file) != nullptr)
    {
        text.append(chunk.data());
    }
    return text;
}

} // namespace

TEST_CASE("the AMX guard refuses tile data for the rest of the process", "[amx-guard]")
{
    const bool first = nesso::embed::denyAmxTiles();
    const bool second = nesso::embed::denyAmxTiles();
    REQUIRE(first);
    REQUIRE(second);
#if defined(__linux__) && defined(__x86_64__)
    constexpr long kArchGetXcompPerm = 0x1022;
    constexpr long kArchReqXcompPerm = 0x1023;
    constexpr long kArchGetFs = 0x1003;
    constexpr unsigned long kXtiledataBit = 1UL << 18U;
    errno = 0;
    const bool refused = syscall(SYS_arch_prctl, kArchReqXcompPerm, 18L) == -1 && errno == EPERM;
    unsigned long mask = 0;
    const bool granted = syscall(SYS_arch_prctl, kArchGetXcompPerm, &mask) == 0 && (mask & kXtiledataBit) != 0;
    // Other arch_prctl calls still pass the filter.
    unsigned long fsBase = 0;
    const bool otherCallsPass = syscall(SYS_arch_prctl, kArchGetFs, &fsBase) == 0;
    CHECK(refused);
    CHECK_FALSE(granted);
    CHECK(otherCallsPass);
#endif
}

TEST_CASE("a guard that is in effect writes nothing", "[amx-guard]")
{
    const std::unique_ptr<std::FILE, CloseFile> file{std::tmpfile()};
    REQUIRE(file != nullptr);
    nesso::embed::reportAmxGuard(true, file.get());
    CHECK(captured(file.get()).empty());
}

TEST_CASE("a guard that is not in effect names the int8 kernel", "[amx-guard]")
{
    const std::unique_ptr<std::FILE, CloseFile> file{std::tmpfile()};
    REQUIRE(file != nullptr);
    nesso::embed::reportAmxGuard(false, file.get());
    CHECK(captured(file.get()) == "Warning: the AMX guard is not in effect; the int8 model may use the AMX kernel\n");
}

TEST_CASE("the embedder env leaves AMX tile data ungranted", "[amx-guard]")
{
    nesso::embed::OnnxEmbedder::prepareEmbedderEnv();
#if defined(__linux__) && defined(__x86_64__)
    constexpr long kArchGetXcompPerm = 0x1022;
    constexpr unsigned long kXtiledataBit = 1UL << 18U;
    unsigned long mask = 0;
    REQUIRE(syscall(SYS_arch_prctl, kArchGetXcompPerm, &mask) == 0);
    CHECK((mask & kXtiledataBit) == 0);
#endif
}
