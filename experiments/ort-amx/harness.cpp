// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#include <embed/amx_guard.hpp>
#include <embed/wordpiece_tokenizer.hpp>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

struct Options
{
    std::filesystem::path model;
    std::filesystem::path vocab;
    std::filesystem::path dump;
    int threads = 4;
    int repeats = 1;
    bool guard = false;
    std::string quantPrecision = "1";
    bool verbose = false;
};

// Fixed corpus. One long line pads the whole batch so the int8 GEMMs are wide enough to use AMX tiles.
constexpr std::string_view kTexts[] = {
    "kernel: usb 3-2: USB disconnect, device number 4, after the host controller reset the high-speed hub and "
    "re-enumerated the mass-storage endpoint while the backup job was still writing the nightly snapshot",
    "sshd[2142]: Accepted publickey for deploy from 10.1.8.24 port 52418 ssh2: ED25519 SHA256:k3mL9",
    "postgres: checkpoint starting: time",
    "postgres: checkpoint complete: wrote 1842 buffers (0.7%); 0 WAL file(s) added, 0 removed, 3 recycled; "
    "write=12.441 s, sync=0.018 s, total=12.502 s; sync files=48, longest=0.004 s, average=0.000 s; distance=32768 kB, "
    "estimate=40112 kB",
    "nginx: 10.0.0.18 - - [09/Nov/2026:17:04:11 +0000] \"GET /v1/search?q=amx+tile+state HTTP/1.1\" 200 1844",
    "dockerd: time=\"2026-11-09T17:04:12.221Z\" level=info msg=\"Container nesso-bench died\"",
    "kernel: Out of memory: Killed process 3312 (stress-ng) total-vm:1820320kB, anon-rss:904120kB",
    "cron: (root) CMD (test -x /usr/sbin/anacron || ( cd / && run-parts --report /etc/cron.daily ))",
    "systemd[1]: Started Session 42 of User ubuntu.",
    "app: embedding batch=32 model=all-MiniLM-L6-v2 quant=int8 intra_op=4",
    "app: index shard 7 committed 100000 lines sha256=48485f688cab3c972e39a181b2018934bb480229fd02f2229531d7ffabd033d8",
    "app: retrying onnx session after EINTR from the intra-op pool",
    "kernel: sched: DL replenishment delayed on CPU 2",
    "audit: type=1400 msg=audit(1762710000.221:88): apparmor=\"DENIED\" operation=\"open\" profile=\"nesso\"",
    "app: query \"tile data lost on kvm context switch\" hits=12 latency_ms=41",
    "app: query \"avx-512 vnni fallback for int8 gemm\" hits=3 latency_ms=18",
    "kernel: trap invalid opcode ip:0000558a2c10 sp:00007ffe1120 error:0 in harness",
    "app: loaded vocab tokens=30522 max_seq=256",
    "systemd-journald: File /var/log/journal/nesso.journal corrupted or uncleanly shut down, renaming and replacing.",
    "app: grep pattern=\"connection reset by peer\" files=1200 matches=44",
    "kernel: AMX tile data was not restored after migration from CPU 0 to CPU 3",
    "app: reference vector bytes must match; cosine is not a gate",
    "sshd[2201]: Failed password for invalid user admin from 203.0.113.10 port 44122 ssh2",
    "app: session pool layout I=4 S=1 arena_shrink=0",
    "kernel: perf: interrupt took too long (2508 > 2500), lowering kernel.perf_event_max_sample_rate to 50000",
    "app: model file model_quantized.onnx sha256=afdb6f1a0e45b715d0bb9b11772f032c399babd23bfc31fed1c170afc848bdb1",
    "nginx: 10.0.0.9 - - [09/Nov/2026:17:11:02 +0000] \"POST /v1/index HTTP/1.1\" 204 0",
    "app: determinism run 17/200 mismatch at dim 0 expected 0x3dcccccd got 0x3e4ccccd",
    "kernel: workqueue: ata_sff_pio_task hogged CPU for >10000us 4 times",
    "app: fp32 gate skipped; this process embeds the int8 MiniLM graph",
    "postgres: connection received: host=127.0.0.1 port=5432",
    "app: closing index after 200 byte-identical searches",
};

std::vector<float> meanPoolAndNormalize(const float *hiddenStates, const int64_t *attentionMask, size_t sequenceLength,
                                        size_t hiddenSize)
{
    std::vector<float> pooled(hiddenSize, 0.0F);
    size_t activeTokens = 0;
    for (size_t token = 0; token < sequenceLength; ++token)
    {
        if (attentionMask[token] == 0)
        {
            continue;
        }
        ++activeTokens;
        const float *tokenVector = hiddenStates + (token * hiddenSize);
        for (size_t dim = 0; dim < hiddenSize; ++dim)
        {
            pooled[dim] += tokenVector[dim];
        }
    }
    if (activeTokens == 0)
    {
        return pooled;
    }
    for (float &value : pooled)
    {
        value /= static_cast<float>(activeTokens);
    }
    float norm = 0.0F;
    for (const float value : pooled)
    {
        norm += value * value;
    }
    norm = std::sqrt(norm);
    if (norm > 0.0F)
    {
        for (float &value : pooled)
        {
            value /= norm;
        }
    }
    return pooled;
}

[[noreturn]] void usage()
{
    std::cerr << "usage: ort_amx_harness --model PATH --vocab PATH --dump PATH [--threads N] [--repeats N] "
                 "[--guard on|off] [--quant-precision 0|1] [--verbose]\n";
    std::exit(2);
}

Options parseArgs(int argc, char **argv)
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view arg = argv[index];
        const auto need = [&](std::string_view flag) -> std::string
        {
            if (index + 1 >= argc)
            {
                std::cerr << "missing value for " << flag << '\n';
                usage();
            }
            return argv[++index];
        };
        if (arg == "--model")
        {
            options.model = need(arg);
        }
        else if (arg == "--vocab")
        {
            options.vocab = need(arg);
        }
        else if (arg == "--dump")
        {
            options.dump = need(arg);
        }
        else if (arg == "--threads")
        {
            options.threads = std::stoi(need(arg));
        }
        else if (arg == "--repeats")
        {
            options.repeats = std::stoi(need(arg));
        }
        else if (arg == "--guard")
        {
            const std::string value = need(arg);
            if (value == "on")
            {
                options.guard = true;
            }
            else if (value == "off")
            {
                options.guard = false;
            }
            else
            {
                usage();
            }
        }
        else if (arg == "--quant-precision")
        {
            options.quantPrecision = need(arg);
        }
        else if (arg == "--verbose")
        {
            options.verbose = true;
        }
        else
        {
            usage();
        }
    }
    if (options.model.empty() || options.vocab.empty() || options.dump.empty() || options.threads < 1 ||
        options.repeats < 1)
    {
        usage();
    }
    return options;
}

struct EncodedBatch
{
    std::vector<int64_t> inputIds;
    std::vector<int64_t> attentionMask;
    std::vector<int64_t> tokenTypeIds;
    int64_t batch = 0;
    int64_t sequence = 0;
};

EncodedBatch encodeBatch(const nesso::embed::WordPieceTokenizer &tokenizer)
{
    std::vector<nesso::embed::TokenizedInput> encoded;
    encoded.reserve(std::size(kTexts));
    size_t sequence = 0;
    for (const std::string_view text : kTexts)
    {
        auto result = tokenizer.encode(text);
        if (!result)
        {
            throw std::runtime_error("tokenization failed");
        }
        sequence = std::max(sequence, result->inputIds.size());
        encoded.push_back(std::move(*result));
    }

    EncodedBatch batch;
    batch.batch = static_cast<int64_t>(encoded.size());
    batch.sequence = static_cast<int64_t>(sequence);
    const size_t cells = encoded.size() * sequence;
    batch.inputIds.assign(cells, 0);
    batch.attentionMask.assign(cells, 0);
    batch.tokenTypeIds.assign(cells, 0);
    for (size_t row = 0; row < encoded.size(); ++row)
    {
        const auto &item = encoded[row];
        std::copy(item.inputIds.begin(), item.inputIds.end(),
                  batch.inputIds.begin() + static_cast<std::ptrdiff_t>(row * sequence));
        std::copy(item.attentionMask.begin(), item.attentionMask.end(),
                  batch.attentionMask.begin() + static_cast<std::ptrdiff_t>(row * sequence));
        std::copy(item.tokenTypeIds.begin(), item.tokenTypeIds.end(),
                  batch.tokenTypeIds.begin() + static_cast<std::ptrdiff_t>(row * sequence));
    }
    return batch;
}

std::vector<float> poolOutput(const Ort::Value &output, const EncodedBatch &batch)
{
    const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3 || shape[0] != batch.batch || shape[1] != batch.sequence || shape[2] <= 0)
    {
        throw std::runtime_error("unexpected output shape");
    }
    const auto elementType = output.GetTensorTypeAndShapeInfo().GetElementType();
    if (elementType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
    {
        throw std::runtime_error("output is not float32");
    }
    const size_t hidden = static_cast<size_t>(shape[2]);
    const float *hiddenStates = output.GetTensorData<float>();
    std::vector<float> pooled;
    pooled.reserve(static_cast<size_t>(batch.batch) * hidden);
    for (int64_t row = 0; row < batch.batch; ++row)
    {
        const float *rowStates = hiddenStates + static_cast<size_t>(row) * static_cast<size_t>(batch.sequence) * hidden;
        const int64_t *mask =
            batch.attentionMask.data() + static_cast<size_t>(row) * static_cast<size_t>(batch.sequence);
        auto vector = meanPoolAndNormalize(rowStates, mask, static_cast<size_t>(batch.sequence), hidden);
        pooled.insert(pooled.end(), vector.begin(), vector.end());
    }
    return pooled;
}

void writeDump(const std::filesystem::path &path, std::span<const float> values)
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
    {
        throw std::runtime_error("could not write dump");
    }
    output.write(reinterpret_cast<const char *>(values.data()),
                 static_cast<std::streamsize>(values.size() * sizeof(float)));
    if (!output)
    {
        throw std::runtime_error("could not write dump");
    }
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        const Options options = parseArgs(argc, argv);
        if (options.guard && !nesso::embed::denyAmxTiles())
        {
            std::cerr << "AMX guard did not install\n";
            return 3;
        }

        auto tokenizer = nesso::embed::WordPieceTokenizer::fromVocabFile(options.vocab);
        if (!tokenizer)
        {
            std::cerr << "could not load vocab\n";
            return 1;
        }
        EncodedBatch batch = encodeBatch(*tokenizer);

        const auto initStart = std::chrono::steady_clock::now();
        const OrtLoggingLevel logLevel = options.verbose ? ORT_LOGGING_LEVEL_VERBOSE : ORT_LOGGING_LEVEL_WARNING;
        Ort::Env env{logLevel, "ort-amx-harness"};
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetExecutionMode(ORT_SEQUENTIAL);
        sessionOptions.SetInterOpNumThreads(1);
        sessionOptions.SetIntraOpNumThreads(options.threads);
        sessionOptions.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        if (!options.quantPrecision.empty())
        {
            sessionOptions.AddConfigEntry("session.x64quantprecision", options.quantPrecision.c_str());
        }
        Ort::Session session{env, options.model.c_str(), sessionOptions};

        Ort::AllocatorWithDefaultOptions allocator;
        std::string inputIdsName;
        std::string attentionName;
        std::string tokenTypeName;
        for (size_t index = 0; index < session.GetInputCount(); ++index)
        {
            const auto allocated = session.GetInputNameAllocated(index, allocator);
            const std::string name = allocated.get();
            if (name == "input_ids")
            {
                inputIdsName = name;
            }
            else if (name == "attention_mask")
            {
                attentionName = name;
            }
            else if (name == "token_type_ids")
            {
                tokenTypeName = name;
            }
        }
        if (inputIdsName.empty() || attentionName.empty() || tokenTypeName.empty() || session.GetOutputCount() < 1)
        {
            throw std::runtime_error("model inputs are not the MiniLM triple");
        }
        const auto outputAllocated = session.GetOutputNameAllocated(0, allocator);
        const std::string outputName = outputAllocated.get();

        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 2> shape{batch.batch, batch.sequence};
        const size_t cells = static_cast<size_t>(batch.batch * batch.sequence);
        std::vector<Ort::Value> inputs;
        inputs.push_back(
            Ort::Value::CreateTensor<int64_t>(memory, batch.inputIds.data(), cells, shape.data(), shape.size()));
        inputs.push_back(
            Ort::Value::CreateTensor<int64_t>(memory, batch.attentionMask.data(), cells, shape.data(), shape.size()));
        inputs.push_back(
            Ort::Value::CreateTensor<int64_t>(memory, batch.tokenTypeIds.data(), cells, shape.data(), shape.size()));
        const char *inputNames[] = {inputIdsName.c_str(), attentionName.c_str(), tokenTypeName.c_str()};
        const char *outputNames[] = {outputName.c_str()};
        const auto initMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initStart).count();

        std::vector<float> first;
        bool intraMismatch = false;
        double embedMs = 0.0;
        for (int repeat = 0; repeat < options.repeats; ++repeat)
        {
            const auto embedStart = std::chrono::steady_clock::now();
            auto outputs =
                session.Run(Ort::RunOptions{nullptr}, inputNames, inputs.data(), inputs.size(), outputNames, 1);
            auto pooled = poolOutput(outputs[0], batch);
            embedMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - embedStart).count();
            if (repeat == 0)
            {
                first = std::move(pooled);
            }
            else if (pooled.size() != first.size() ||
                     std::memcmp(pooled.data(), first.data(), first.size() * sizeof(float)) != 0)
            {
                intraMismatch = true;
            }
        }

        writeDump(options.dump, first);
        const size_t hidden = first.size() / static_cast<size_t>(batch.batch);
        std::cout << "threads=" << options.threads << '\n'
                  << "guard=" << (options.guard ? "on" : "off") << '\n'
                  << "quant_precision=" << options.quantPrecision << '\n'
                  << "repeats=" << options.repeats << '\n'
                  << "batch=" << batch.batch << '\n'
                  << "max_seq=" << batch.sequence << '\n'
                  << "dims=" << hidden << '\n'
                  << "vectors=" << batch.batch << '\n'
                  << "bytes=" << (first.size() * sizeof(float)) << '\n'
                  << "init_ms=" << initMs << '\n'
                  << "embed_ms=" << embedMs << '\n'
                  << "intra_mismatch=" << (intraMismatch ? 1 : 0) << '\n';
        return intraMismatch ? 4 : 0;
    }
    catch (const Ort::Exception &error)
    {
        std::cerr << "ort: " << error.what() << '\n';
        return 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
