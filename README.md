# Nesso

**Local semantic search for unstructured text** (C++26)

by **Lorenzo Caprari**

Nesso is a Linux-native CLI for searching local text by meaning.

## Today

- `nesso grep QUERY FILE...` — one-shot semantic search over `.log`, `.json`, and `.jsonl`
- `nesso index` / `nesso search` — persist that text and its embeddings, then search the file
- Local ONNX MiniLM embedder. Ranking is a brute-force scan (no ANN index yet)
- Conan 2 toolchain with ASan/UBSan debug builds and CI coverage gates

## Non-goals

- Not a hosted vector database (Qdrant, pgvector, etc.)
- No approximate nearest-neighbor index yet
- Linux only

## Install

Ubuntu 26.04+, from a GitHub Release `.deb`:

```bash
sudo apt install ./nesso_*_amd64.deb
nesso grep "database connection error" app.log
```

The package ships the MiniLM files in `/usr/share/nesso`. An omitted `--model-dir` uses `NESSO_MODEL_DIR`, then `./models` when both files are present, then `$XDG_DATA_HOME/nesso` (or `~/.local/share/nesso`), then `/usr/share/nesso`.

Anywhere with Docker:

```bash
docker pull ghcr.io/lorenzocaprari/nesso:latest
docker run --rm -v "$PWD:/data" -w /data ghcr.io/lorenzocaprari/nesso:latest \
  grep "database connection error" app.log
```

Push a `vX.Y.Z` tag that matches `version` in `conanfile.py` to publish the image, the `.deb`, and the GitHub Release. CI must already have published `nesso-build-env`.

## Prerequisites

- **Compiler:** GCC 15+ with C++26 support
- **Build system:** CMake 3.28+
- **Package manager:** Conan 2.x
- **OS:** Linux

## Build

```bash
conan install . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing
conan build . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing
ctest --test-dir build/Debug --output-on-failure
```

Release profile: replace `gcc-26-debug` with `gcc-26`.

The black-box specs in [test/functional/](test/functional/) run the `nesso` binary through [godog](https://github.com/cucumber/godog) as the `bdd` ctest case. Go 1.24+ is a test-only dependency. Without it CMake skips `bdd`, and `NESSO_REQUIRE_BDD=1` turns that into a configure error (CI sets it).

Unit, integration, and functional tests are separate ctest labels. CI runs each label as its own Release job and its own Debug job:

```bash
ctest --test-dir build/Debug -L unit
ctest --test-dir build/Debug -L integration
ctest --test-dir build/Debug -L functional
```

## Usage

Download the embedding model once:

```bash
./scripts/fetch-model
```

Search files by meaning. `-k` is optional (default 5):

```bash
./build/Debug/nesso grep "database connection error" app.log
./build/Debug/nesso grep "payment timeout" app.log events.jsonl dump.json -k 5
./build/Debug/nesso grep "auth failure" app.log --model-dir models/
```

Index the same files once, then search the corpus. `-o` and `-i` are required:

```bash
./build/Debug/nesso index -o corpus.nesso app.log events.jsonl
./build/Debug/nesso search "payment timeout" -i corpus.nesso -k 5
```

Matches go to stdout. Skipped lines, the index summary, and errors go to stderr. Every command exits 0 when it prints matches or the index succeeds, 1 when nothing matches, and 2 when the command fails. Argument errors, including `-k 0`, also exit 2. `--help` lists these codes. A missing model names the directories that were tried and suggests `scripts/fetch-model` or `--model-dir`.

### File limits

- `.json` and `.jsonl` are JSON, matched without regard to case. Every other name is read as lines: no extension, `.log.N` rotations, `.LOG`, `.txt`, `syslog`. `-` reads stdin as lines, and those matches are prefixed with `-:`.
- A file whose first 4 KiB contains a NUL byte is rejected. Directories are rejected.
- Lines: one chunk per non-empty line. Lines longer than 4096 characters are truncated. A trailing CR is stripped.
- JSON: only objects with a string field are indexed (`message` by default, or `--json-field`). Malformed lines and documents are skipped. String values longer than 4096 characters are truncated. A JSON array is numbered `element N`, not `line N`.
- `grep` holds the whole corpus in memory for that invocation. `index` writes it to the corpus file; `search` loads that file back into memory. Very large files will be slow and RAM-heavy.
- Empty lines and JSON values without that string field are skipped. Overlong log lines and JSON string values are truncated. Skip and truncation counts are printed on stderr.

## Development

Local CI gate (run before pushing):

```bash
bash scripts/lint
./scripts/code-coverage conan/profiles/code-coverage
```

See [.github/workflows/ci.yml](.github/workflows/ci.yml) for lint, clang-tidy, cppcheck, Release/Debug builds, unit, integration, and functional tests, fuzz, and coverage. Tag `v*` publishes via [.github/workflows/release.yml](.github/workflows/release.yml).

### Measuring performance

Use a Release build. `NESSO_TRACE=1` prints one JSON line on stderr per command, with per-stage microseconds and peak RSS:

```bash
NESSO_TRACE=1 ./build/Release/src/nesso grep "connection refused" /var/log/app.log --model-dir models/
```

`scripts/profile` runs the same command under `perf` and `heaptrack` and writes `build/profile/flamegraph.svg` and `build/profile/heaptrack-summary.txt`. It needs `perf`, `heaptrack`, and FlameGraph's `stackcollapse-perf.pl` and `flamegraph.pl` on `PATH`, or `NESSO_FLAMEGRAPH_DIR` set to a FlameGraph checkout:

```bash
./scripts/profile grep "connection refused" /var/log/app.log --model-dir models/
```

`scripts/bench` times `grep` on fixed-seed synthetic logs of 1k, 10k, and 100k lines and writes `bench/results/<sha>.json`. `scripts/bench-compare` fails when wall time or peak RSS is more than 15% above [bench/baseline.json](bench/baseline.json):

```bash
python3 scripts/bench
python3 scripts/bench-compare
```

The `bench` CI job runs both on every pull request and uploads the results. It is report-only and never blocks a merge.

Google Benchmark microbenchmarks build with `-o "&:bench=True"` on the Release profile and produce `build/Release/bench/nesso_bench`.

## License

MIT — see LICENSE.
