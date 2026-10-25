# Nesso

[![Pipeline](https://github.com/lorenzocaprari/nesso/actions/workflows/ci.yml/badge.svg)](https://github.com/lorenzocaprari/nesso/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/lorenzocaprari/nesso)](https://github.com/lorenzocaprari/nesso/releases/latest)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/lorenzocaprari/nesso/badge)](https://scorecard.dev/viewer/?uri=github.com/lorenzocaprari/nesso)

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

Two builds of the same commit produce the same `.deb`. Install is checked on Ubuntu 26.04 and Ubuntu 26.10.

The package ships the MiniLM files in `/usr/share/nesso`. An omitted `--model-dir` uses `NESSO_MODEL_DIR`, then `./models` when both files are present, then `$XDG_DATA_HOME/nesso` (or `~/.local/share/nesso`), then `/usr/share/nesso`.

Anywhere with Docker:

```bash
docker pull ghcr.io/lorenzocaprari/nesso:latest
docker run --rm -v "$PWD:/data" -w /data ghcr.io/lorenzocaprari/nesso:latest \
  grep "database connection error" app.log
```

A release pull request bumps `version` in `conanfile.py`. After that commit is on `main`, publish the `vX.Y.Z` draft. Publishing creates the tag. `.github/workflows/release.yml` checks the tag against `conanfile.py`, then publishes the image, the `.deb`, and the GitHub Release. CI must already have published `nesso-build-env`. Checking the signature and provenance is [below](#verifying-a-release).

## Verifying a release

A `vX.Y.Z` tag signs `ghcr.io/lorenzocaprari/nesso` by digest with keyless cosign. The certificate identity is this repository's release workflow at that tag. The GitHub Release carries SLSA Build Level 3 provenance: `nesso_<version>_amd64.intoto.jsonl` covers the `.deb` and `nesso_<version>.spdx.json`, and `nesso_<version>_ghcr.intoto.jsonl` is the image attestation copied from GHCR. `SHA256SUMS` lists those four files.

`v0.1.0` was published before these signatures. Set `version` to the release you are checking, without the leading `v`. `digest` is the image manifest digest.

```bash
version=1.2.3
digest="$(docker buildx imagetools inspect --format '{{index .Manifest.Digest}}' "ghcr.io/lorenzocaprari/nesso:v${version}")"

cosign verify \
  --certificate-identity "https://github.com/lorenzocaprari/nesso/.github/workflows/release.yml@refs/tags/v${version}" \
  --certificate-oidc-issuer "https://token.actions.githubusercontent.com" \
  "ghcr.io/lorenzocaprari/nesso@${digest}"

slsa-verifier verify-artifact \
  "nesso_${version}_amd64.deb" \
  "nesso_${version}.spdx.json" \
  --provenance-path "nesso_${version}_amd64.intoto.jsonl" \
  --source-uri github.com/lorenzocaprari/nesso \
  --source-tag "v${version}"

slsa-verifier verify-image \
  "ghcr.io/lorenzocaprari/nesso@${digest}" \
  --source-uri github.com/lorenzocaprari/nesso \
  --source-tag "v${version}"
```

`cosign` 2.x checks the signature. `slsa-verifier` v2.7.1 or later checks the provenance. `verify-image` reads the attestation from GHCR. When the registry asks for credentials, set `GH_TOKEN` for that command.

## Prerequisites

- **Compiler:** GCC 15+ with C++26 support
- **Build system:** CMake 4.2+
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

Unit, integration, and functional tests are separate ctest labels. CI runs unit and integration in one job per lane. Functional shares that job on gcc Release, Clang, and arm64, and has its own gcc Debug job:

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
- `index` records each file source's size and modification time. `search` warns on stderr when a source changed or is missing; stdin sources are not tracked.

## Development

Local CI gate (run before pushing):

```bash
bash scripts/lint
./scripts/code-coverage conan/profiles/code-coverage
```

See [.github/workflows/ci.yml](.github/workflows/ci.yml) for `lint`, `clang-tidy`, and `cppcheck`, the gcc Release, gcc Debug, Clang, and arm64 builds, unit, integration, and functional tests, coverage, and the fp32 determinism gate. AFL++ `fuzz-test` runs on pushes to `main` and nightly; pull requests are fuzzed by [ClusterFuzzLite](.github/workflows/cifuzz.yml). The pipeline runs on pull requests, on pushes to `main`, and nightly. [.github/workflows/release-drafter.yml](.github/workflows/release-drafter.yml) drafts the next release from `version` in `conanfile.py`. Publishing the `vX.Y.Z` draft creates the tag, and tag `v*` publishes via [.github/workflows/release.yml](.github/workflows/release.yml). Weekly [CodeQL](.github/workflows/codeql.yml) (`c-cpp`, `build-mode: none`) and [OpenSSF Scorecard](.github/workflows/scorecard.yml) run on `main`.

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

The `bench` CI job runs on every pull request, on pushes to `main`, and nightly. Pull requests time the 1k and 10k tiers. Pushes to `main` and the nightly schedule also time 100k. The job compares the tiers it ran with the latest baseline saved from `main`, and that failure fails the job. The first run has no baseline and warns. Shared runners disagree on wall time by about 4.5× for the same tier, so CI allows up to 5.5× the baseline wall time and 1.2× the baseline peak RSS.

Google Benchmark microbenchmarks build with `-o "&:bench=True"` on the Release profile and produce `build/Release/bench/nesso_bench`.

### Checking embedding determinism

The release gate starts fresh `nesso` processes for each index and search run, compares corpus files and complete rankings byte-for-byte, and keeps a CPU-load process running:

```bash
NESSO_BINARY=./build/Release/src/nesso \
NESSO_MODEL_DIR=models \
./scripts/check-determinism \
  --input /path/to/events.log \
  --query PAY-E1042 \
  --runs 10 \
  --top-k 1000000 \
  --stress-command "stress-ng --cpu 8"
```

Use the same input, model, and gate in Release and Debug ASan builds before tagging. The default stress command is `stress-ng --cpu 1 --cpu-method loop --timeout 0 --cache-size 2M`: one integer busy-loop worker until the gate stops it. Pass `--stress-command` when the machine needs a different load generator. If that process exits, the gate restarts it and records its exit status and stderr; the fp32 comparison does not run unless a load process is alive.

`scripts/fetch-model` downloads the int8 MiniLM. Pass `fp32` as the second argument for the float model (`./scripts/fetch-model models-fp32 fp32`). int8 runs one intra-op thread per session so repeated runs agree; `--threads` or `NESSO_THREADS` sets that session count. fp32 keeps one session, and the same option sets its intra-op threads. CI fails the job when fp32 or the default int8 policy disagrees. `--emit-input` writes a 3500-line log, and `--index-runs 3` compares those index files and the rankings for four queries, including `PAY-E1042`. Pull requests run `--mode pr` instead: a 256-line log, 2 runs, and 2 index runs, still across fresh processes under CPU load. Pushes to `main`, the nightly schedule, manual runs, and the release gate stay on the full check. The int8 run with `NESSO_THREADS` set to the logical CPU count stays report-only.

When `grep` or `index` runs in a terminal, embedding progress is shown on stderr. Redirected output stays quiet; pass `--quiet` to suppress progress explicitly.

## License

Apache 2.0 — see [LICENSE](LICENSE). Changes are in [CHANGELOG.md](CHANGELOG.md). Contributing, security reports, and the code of conduct: [CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md), [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
