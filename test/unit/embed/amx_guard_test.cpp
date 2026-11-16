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

std::string Captured(std::FILE *file)
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
    const bool first = nesso::embed::DenyAmxTiles();
    const bool second = nesso::embed::DenyAmxTiles();
    REQUIRE(first);
    REQUIRE(second);
#if defined(__linux__) && defined(__x86_64__)
    constexpr long k_arch_get_xcomp_perm = 0x1022;
    constexpr long k_arch_req_xcomp_perm = 0x1023;
    constexpr long k_arch_get_fs = 0x1003;
    constexpr unsigned long k_xtiledata_bit = 1UL << 18U;
    errno = 0;
    const bool refused = syscall(SYS_arch_prctl, k_arch_req_xcomp_perm, 18L) == -1 && errno == EPERM;
    unsigned long mask = 0;
    const bool granted = syscall(SYS_arch_prctl, k_arch_get_xcomp_perm, &mask) == 0 && (mask & k_xtiledata_bit) != 0;
    // Other arch_prctl calls still pass the filter.
    unsigned long fs_base = 0;
    const bool other_calls_pass = syscall(SYS_arch_prctl, k_arch_get_fs, &fs_base) == 0;
    CHECK(refused);
    CHECK_FALSE(granted);
    CHECK(other_calls_pass);
#endif
}

TEST_CASE("a guard that is in effect writes nothing", "[amx-guard]")
{
    const std::unique_ptr<std::FILE, CloseFile> file{std::tmpfile()};
    REQUIRE(file != nullptr);
    nesso::embed::ReportAmxGuard(true, file.get());
    CHECK(Captured(file.get()).empty());
}

TEST_CASE("a guard that is not in effect names the int8 kernel", "[amx-guard]")
{
    const std::unique_ptr<std::FILE, CloseFile> file{std::tmpfile()};
    REQUIRE(file != nullptr);
    nesso::embed::ReportAmxGuard(false, file.get());
    CHECK(Captured(file.get()) == "Warning: the AMX guard is not in effect; the "
                                  "int8 model may use the AMX kernel\n");
}

TEST_CASE("the embedder env leaves AMX tile data ungranted", "[amx-guard]")
{
    nesso::embed::OnnxEmbedder::PrepareEmbedderEnv();
#if defined(__linux__) && defined(__x86_64__)
    constexpr long k_arch_get_xcomp_perm = 0x1022;
    constexpr unsigned long k_xtiledata_bit = 1UL << 18U;
    unsigned long mask = 0;
    REQUIRE(syscall(SYS_arch_prctl, k_arch_get_xcomp_perm, &mask) == 0);
    CHECK((mask & k_xtiledata_bit) == 0);
#endif
}
