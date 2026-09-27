#include <catch2/catch_all.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef NESSO_BINARY
#error "NESSO_BINARY must be defined"
#endif

#ifndef NESSO_TEST_FIXTURES
#error "NESSO_TEST_FIXTURES must be defined"
#endif

namespace
{

struct CommandResult
{
    int exitCode = 1;
    std::string stdoutText;
    std::string stderrText;
};

CommandResult runNesso(const std::vector<std::string> &args)
{
    std::array<int, 2> outPipe{};
    std::array<int, 2> errPipe{};
    REQUIRE(pipe(outPipe.data()) == 0);
    REQUIRE(pipe(errPipe.data()) == 0);

    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0)
    {
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);
        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);

        std::vector<std::string> owned;
        owned.reserve(args.size() + 1);
        owned.emplace_back(NESSO_BINARY);
        owned.insert(owned.end(), args.begin(), args.end());

        std::vector<char *> argv;
        argv.reserve(owned.size() + 1);
        for (std::string &arg : owned)
        {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        execv(owned.front().c_str(), argv.data());
        _exit(127);
    }

    close(outPipe[1]);
    close(errPipe[1]);

    const auto readAll = [](int fd)
    {
        std::string text;
        std::array<char, 256> buffer{};
        ssize_t readCount = 0;
        while ((readCount = read(fd, buffer.data(), buffer.size())) > 0)
        {
            text.append(buffer.data(), static_cast<size_t>(readCount));
        }
        close(fd);
        return text;
    };

    CommandResult result;
    result.stdoutText = readAll(outPipe[0]);
    result.stderrText = readAll(errPipe[0]);

    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return result;
}

void writeFloats(const std::filesystem::path &path, const std::array<float, 4> &values)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(values.data()), static_cast<std::streamsize>(sizeof(values)));
    REQUIRE(output);
}

} // namespace

TEST_CASE("grep rejects the raw store path flag", "[cli][store]")
{
    const auto logPath = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const auto rejected = runNesso({"-p", "ignored.nesso", "grep", "query", logPath.string()});
    REQUIRE(rejected.exitCode != 0);
    REQUIRE(rejected.stderrText.find("-p") != std::string::npos);

    const auto rejectedOnGrep = runNesso({"grep", "-p", "query", logPath.string()});
    REQUIRE(rejectedOnGrep.exitCode != 0);
    REQUIRE(rejectedOnGrep.stderrText.find("-p") != std::string::npos);
}

TEST_CASE("root-level init is no longer a command", "[cli][store]")
{
    const auto dbPath = std::filesystem::temp_directory_path() / "nesso_cli_store_rejected.nesso";
    std::filesystem::remove(dbPath);

    const auto rejected = runNesso({"-p", dbPath.string(), "-d", "4", "init"});
    REQUIRE(rejected.exitCode != 0);
    REQUIRE_FALSE(std::filesystem::exists(dbPath));
}

TEST_CASE("store init creates the database and store without a subcommand fails", "[cli][store]")
{
    const auto missing = runNesso({"store"});
    REQUIRE(missing.exitCode != 0);

    const auto dbPath =
        std::filesystem::temp_directory_path() / ("nesso_cli_store_" + std::to_string(getpid()) + ".nesso");
    std::filesystem::remove(dbPath);

    const auto created = runNesso({"store", "-p", dbPath.string(), "-d", "4", "init"});
    REQUIRE(created.exitCode == 0);
    REQUIRE(std::filesystem::is_regular_file(dbPath));
    std::filesystem::remove(dbPath);
}

TEST_CASE("store index and search round-trip one raw vector", "[cli][store]")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("nesso_cli_store_roundtrip_" + std::to_string(getpid()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directory(directory);
    const auto dbPath = directory / "vectors.nesso";
    const auto vectorPath = directory / "vectors.bin";
    const auto queryPath = directory / "query.bin";
    const std::array<float, 4> record{1.0F, 0.0F, 0.0F, 0.0F};
    writeFloats(vectorPath, record);
    writeFloats(queryPath, record);

    REQUIRE(runNesso({"store", "-p", dbPath.string(), "-d", "4", "init"}).exitCode == 0);
    REQUIRE(runNesso({"store", "-p", dbPath.string(), "-d", "4", "index", "-f", vectorPath.string()}).exitCode == 0);

    const auto searched =
        runNesso({"store", "-p", dbPath.string(), "-d", "4", "search", "-q", queryPath.string(), "-k", "1"});
    REQUIRE(searched.exitCode == 0);
    REQUIRE(searched.stdoutText.find("index: 0, score: 1") != std::string::npos);

    std::filesystem::remove_all(directory);
}
