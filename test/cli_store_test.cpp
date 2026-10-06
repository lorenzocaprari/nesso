#include <catch2/catch_all.hpp>

#include <array>
#include <cstring>
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

#ifndef NESSO_MODELS_DIR
#error "NESSO_MODELS_DIR must be defined"
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

TEST_CASE("store is not a command", "[cli][store]")
{
    const auto missing = runNesso({"store"});
    REQUIRE(missing.exitCode != 0);
}

TEST_CASE("grep reports a missing model, empty input, and ranked lines", "[cli][grep]")
{
    const auto logPath = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const auto directory = std::filesystem::temp_directory_path() / ("nesso_cli_grep_" + std::to_string(getpid()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directory(directory);
    const auto emptyModels = directory / "empty-models";
    std::filesystem::create_directory(emptyModels);
    const auto emptyLog = directory / "empty.log";
    const auto otherLog = directory / "other.log";
    const auto notesPath = directory / "notes.txt";
    const auto longLog = directory / "long.log";
    {
        std::ofstream empty(emptyLog);
        REQUIRE(empty);
        std::ofstream other(otherLog);
        REQUIRE(other);
        other << "unrelated noise\n";
        std::ofstream notes(notesPath);
        REQUIRE(notes);
        notes << "not a log\n";
        std::ofstream longLine(longLog);
        REQUIRE(longLine);
        longLine << "keep this line\n" << std::string(5000, 'x') << '\n';
    }

    const auto missingModel =
        runNesso({"grep", "--model-dir", emptyModels.string(), "database connection error", logPath.string()});
    REQUIRE(missingModel.exitCode != 0);
    REQUIRE(missingModel.stderrText.find("Failed to load embedder") != std::string::npos);

    const auto modelOnnx = std::filesystem::path(NESSO_MODELS_DIR) / "model.onnx";
    const auto vocab = std::filesystem::path(NESSO_MODELS_DIR) / "vocab.txt";
    if (!std::filesystem::is_regular_file(modelOnnx) || !std::filesystem::is_regular_file(vocab))
    {
        std::filesystem::remove_all(directory);
        SKIP("models/ not present; coverage runs scripts/fetch-model");
    }

    const auto rejectedFormat =
        runNesso({"grep", "--model-dir", NESSO_MODELS_DIR, "database connection error", notesPath.string()});
    REQUIRE(rejectedFormat.exitCode != 0);
    REQUIRE(rejectedFormat.stderrText.find("Failed to parse") != std::string::npos);

    const auto skipped =
        runNesso({"grep", "--model-dir", NESSO_MODELS_DIR, "database connection error", longLog.string()});
    REQUIRE(skipped.exitCode == 0);
    REQUIRE(skipped.stderrText.find("Skipped ") != std::string::npos);

    const auto empty =
        runNesso({"grep", "--model-dir", NESSO_MODELS_DIR, "database connection error", emptyLog.string()});
    REQUIRE(empty.exitCode != 0);

    const auto ranked =
        runNesso({"grep", "--model-dir", NESSO_MODELS_DIR, "database connection error", logPath.string()});
    REQUIRE(ranked.exitCode == 0);
    REQUIRE(ranked.stdoutText.find("database connection refused") != std::string::npos);
    REQUIRE(ranked.stdoutText.find("line ") != std::string::npos);

    const auto noMatches =
        runNesso({"grep", "--model-dir", NESSO_MODELS_DIR, "database connection error", logPath.string(), "-k", "0"});
    REQUIRE(noMatches.exitCode != 0);

    const auto withSource = runNesso(
        {"grep", "--model-dir", NESSO_MODELS_DIR, "database connection error", logPath.string(), otherLog.string()});
    REQUIRE(withSource.exitCode == 0);
    REQUIRE(withSource.stdoutText.find(logPath.string()) != std::string::npos);

    std::filesystem::remove_all(directory);
}

void writeEmptyCorpus(const std::filesystem::path &path)
{
    std::array<unsigned char, 24> header{};
    header[0] = 'N';
    header[1] = 'E';
    header[2] = 'S';
    header[3] = 'C';
    header[4] = 1;
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));
    REQUIRE(output);
}

void writeTwoDimensionalCorpus(const std::filesystem::path &path)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    std::array<unsigned char, 24> header{};
    header[0] = 'N';
    header[1] = 'E';
    header[2] = 'S';
    header[3] = 'C';
    header[4] = 1;
    const uint64_t dimensions = 2;
    const uint64_t chunkCount = 1;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    std::memcpy(header.data() + 8, &dimensions, sizeof(dimensions));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    std::memcpy(header.data() + 16, &chunkCount, sizeof(chunkCount));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));

    const uint64_t lineNumber = 1;
    const uint64_t sourceSize = 1;
    const uint64_t textSize = 1;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(&lineNumber), static_cast<std::streamsize>(sizeof(lineNumber)));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(&sourceSize), static_cast<std::streamsize>(sizeof(sourceSize)));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(&textSize), static_cast<std::streamsize>(sizeof(textSize)));
    output.write("st", 2);
    const std::array<float, 2> embedding{1.F, 0.F};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(embedding.data()), static_cast<std::streamsize>(sizeof(embedding)));
    REQUIRE(output);
}

TEST_CASE("index and search use a text corpus", "[cli][corpus]")
{
    const auto logPath = std::filesystem::path(NESSO_TEST_FIXTURES) / "grep_sample.log";
    const auto otherLog = std::filesystem::path(NESSO_TEST_FIXTURES) / "sample.log";
    const auto directory = std::filesystem::temp_directory_path() / ("nesso_cli_corpus_" + std::to_string(getpid()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directory(directory);
    const auto emptyModels = directory / "empty-models";
    std::filesystem::create_directory(emptyModels);
    const auto notesPath = directory / "notes.txt";
    const auto emptyLog = directory / "empty.log";
    const auto longLog = directory / "long.log";
    const auto corpusPath = directory / "corpus.nesso";
    const auto emptyCorpus = directory / "empty.nesso";
    const auto corruptPath = directory / "corrupt.nesso";
    const auto shortCorpus = directory / "short.nesso";
    const auto outputDir = directory / "not-a-file";
    std::filesystem::create_directory(outputDir);
    {
        std::ofstream notes(notesPath);
        REQUIRE(notes);
        notes << "not a log\n";
        std::ofstream empty(emptyLog);
        REQUIRE(empty);
        std::ofstream longLine(longLog);
        REQUIRE(longLine);
        longLine << "keep this line\n" << std::string(5000, 'x') << '\n';
        std::ofstream corrupt(corruptPath, std::ios::binary);
        REQUIRE(corrupt);
        corrupt << "bad!";
    }
    writeEmptyCorpus(emptyCorpus);
    writeTwoDimensionalCorpus(shortCorpus);

    const auto missingOutput = runNesso({"index", logPath.string()});
    REQUIRE(missingOutput.exitCode != 0);
    REQUIRE(missingOutput.stderrText.find("-o") != std::string::npos);

    const auto missingModel =
        runNesso({"index", "--model-dir", emptyModels.string(), "-o", corpusPath.string(), logPath.string()});
    REQUIRE(missingModel.exitCode != 0);
    REQUIRE(missingModel.stderrText.find("Failed to load embedder") != std::string::npos);

    const auto emptySearch = runNesso(
        {"search", "--model-dir", emptyModels.string(), "database connection error", "-i", emptyCorpus.string()});
    REQUIRE(emptySearch.exitCode != 0);
    REQUIRE(emptySearch.stdoutText.empty());

    const auto corruptSearch = runNesso(
        {"search", "--model-dir", emptyModels.string(), "database connection error", "-i", corruptPath.string()});
    REQUIRE(corruptSearch.exitCode != 0);
    REQUIRE(corruptSearch.stderrText.find("Failed to read corpus") != std::string::npos);

    const auto modelOnnx = std::filesystem::path(NESSO_MODELS_DIR) / "model.onnx";
    const auto vocab = std::filesystem::path(NESSO_MODELS_DIR) / "vocab.txt";
    if (!std::filesystem::is_regular_file(modelOnnx) || !std::filesystem::is_regular_file(vocab))
    {
        std::filesystem::remove_all(directory);
        SKIP("models/ not present; coverage runs scripts/fetch-model");
    }

    const auto rejectedFormat =
        runNesso({"index", "--model-dir", NESSO_MODELS_DIR, "-o", corpusPath.string(), notesPath.string()});
    REQUIRE(rejectedFormat.exitCode != 0);
    REQUIRE(rejectedFormat.stderrText.find("Failed to parse") != std::string::npos);

    const auto skipped =
        runNesso({"index", "--model-dir", NESSO_MODELS_DIR, "-o", corpusPath.string(), longLog.string()});
    REQUIRE(skipped.exitCode == 0);
    REQUIRE(skipped.stderrText.find("Skipped ") != std::string::npos);

    const auto emptyIndex =
        runNesso({"index", "--model-dir", NESSO_MODELS_DIR, "-o", corpusPath.string(), emptyLog.string()});
    REQUIRE(emptyIndex.exitCode == 0);
    REQUIRE(emptyIndex.stderrText.find("Indexed 0 chunks") != std::string::npos);

    const auto writeFailed =
        runNesso({"index", "--model-dir", NESSO_MODELS_DIR, "-o", outputDir.string(), logPath.string()});
    REQUIRE(writeFailed.exitCode != 0);
    REQUIRE(writeFailed.stderrText.find("Failed to write corpus") != std::string::npos);

    const auto indexed =
        runNesso({"index", "--model-dir", NESSO_MODELS_DIR, "-o", corpusPath.string(), logPath.string()});
    REQUIRE(indexed.exitCode == 0);
    REQUIRE(indexed.stderrText.find("Indexed ") != std::string::npos);

    const auto ranked =
        runNesso({"search", "--model-dir", NESSO_MODELS_DIR, "database connection error", "-i", corpusPath.string()});
    REQUIRE(ranked.exitCode == 0);
    REQUIRE(ranked.stdoutText.find("database connection refused") != std::string::npos);
    REQUIRE(ranked.stdoutText.find("line ") != std::string::npos);

    const auto noMatches = runNesso(
        {"search", "--model-dir", NESSO_MODELS_DIR, "database connection error", "-i", corpusPath.string(), "-k", "0"});
    REQUIRE(noMatches.exitCode != 0);

    const auto mismatch =
        runNesso({"search", "--model-dir", NESSO_MODELS_DIR, "database connection error", "-i", shortCorpus.string()});
    REQUIRE(mismatch.exitCode != 0);
    REQUIRE(mismatch.stderrText.find("Semantic search failed") != std::string::npos);

    const auto withSource = runNesso(
        {"index", "--model-dir", NESSO_MODELS_DIR, "-o", corpusPath.string(), logPath.string(), otherLog.string()});
    REQUIRE(withSource.exitCode == 0);
    const auto sourced =
        runNesso({"search", "--model-dir", NESSO_MODELS_DIR, "database connection error", "-i", corpusPath.string()});
    REQUIRE(sourced.exitCode == 0);
    REQUIRE(sourced.stdoutText.find(logPath.string()) != std::string::npos);

    std::filesystem::remove_all(directory);
}
