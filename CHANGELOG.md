# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `--threads` and `NESSO_THREADS` set the int8 session count and the fp32 intra-op thread count. `nesso --version --verbose` prints that policy.
- `scripts/check-determinism --index-runs` compares corpus bytes and the rankings for four queries, including `PAY-E1042`. `--emit-input` writes the 3500-line input the gate uses.

### Changed

- CI uploads the binaries, ctest manifests, and test fixtures instead of the whole build tree. A pull request that only changes documentation skips the build, test, coverage, bench, and determinism jobs. Lint still runs.
- Pull-request benchmarks run the 1k and 10k tiers. Pushes to `main` and the nightly schedule still run the 100k tier. The benchmark job fails when a tier regresses against the latest baseline saved from `main`.
- AFL++ `scripts/fuzz-test` runs on pushes to `main` and in the nightly Pipeline. Pull requests stay on ClusterFuzzLite.
- Debug builds scan an ONNX model for its quantization marker in chunks. Copying the whole file through checked iterators took about five seconds per embedder load under ASan.
- CI runs ctest with `-j "$(nproc)"`. The godog suite runs scenarios concurrently, up to one per logical CPU.
- The project license is Apache 2.0.
- CI and the release workflow block when int8 embeddings disagree across processes. The run that sets `NESSO_THREADS` to the logical CPU count stays report-only.

### Fixed

- `scripts/bench` treats the current grep trace (`parse`, `query-embed`, `embed`) as a completed run. The old `parse`/`embed`/`rank` check marked every CI run failed, and `continue-on-error` hid that result.
- int8 embeddings use one intra-op thread per session so repeated runs agree under CPU load.

## [0.1.0] - 2026-10-11

First release. Items below are commits reachable from tag `v0.1.0` and the auto-generated [GitHub release notes](https://github.com/lorenzocaprari/nesso/releases/tag/v0.1.0). History before pull request #41 is included where it is still what the tag ships.

### Added

- Semantic grep subcommand and integration test (`7b4426a`), then a positional `grep` command beside init/index/search (`7df5c2a`)
- OnnxEmbedder with `embed` and `embedBatch` (`d8e6aad`), WordPieceTokenizer for MiniLM vocab files (`3b42d17`), and LogChunker for `.log`, `.json`, and `.jsonl` (`3a223cf`)
- `DistanceMetrics::dotProduct` and `DistanceMetrics::l2SquaredDistance` scalar kernels, and AVX2 float distance kernels (`4f4679f`, `6699b53`, `c29b42e`)
- Persist text corpora for `nesso index` and `nesso search` (#43, `accf06f`)
- Versioned `.deb` and runtime image published on `vX.Y.Z` (#47, `951e6ae`)
- godog harness wired into ctest (#65, `c652356`) and specs for grep, index, and search (#67, `f3c9139`)
- AFL library harnesses (`c58e9d0`); at this tag the targets are the parser and the tokenizer (`scripts/fuzz-test`)
- Stage timings when `NESSO_TRACE=1` (`776ae18`)
- Optional Google Benchmark microbenchmarks (`d291305`)
- Deterministic end-to-end benchmark and baseline compare (`0489c58`), and a report-only end-to-end benchmark CI job (#60, `8585740`)
- perf and heaptrack profiling wrapper (`11b3be9`)
- Corpus format v2 (#68, `19f9058`)
- Real log files and stdin (#74, `1f5b707`)
- Warning when corpus sources are stale (#76, `a4d630b`)
- Progress in interactive terminals (#77, `5772a24`)
- Process determinism gate (#75, `db771de`)
- MIT License (#59, `e46a263`)

### Changed

- Raw vector commands nested under `nesso store` (#41, `3683089`)
- Parse and search shared between grep and the corpus (#44, `944b99f`)
- Each CLI command registered from its own file (#46, `a66a4c1`)
- libnesso facade over core, embed, and parser (#48, `014b2d4`)
- argv parsed into typed requests and rendered in one place (#49, `1c214b0`)
- CLI and library sources flattened under `src/` (`c5c3e20`)
- Flat-matrix top-k replaces EmbeddingStore (#63, `037545f`)
- Runtime SIMD dispatch with FMA (#64, `672f6a2`)
- Tests split into unit, integration, and functional (`fae58f2`), and integration and functional tests given their own CI jobs (`5de90a5`). Both commits are in #66 (release notes title: "Refactor/test functional layout")
- Typed errors, readable messages, and grep exit codes 0/1/2 (`ca2b831`). Branch coverage lifted over the 44.3% floor (`2df634a`). Both commits are in #69 (release notes use the typed-errors title)
- Chunker with value stats and `--json-field` (#71, `8a0316b`)
- ONNX Runtime built as Release in debug profiles (`e72c9f2`)
- Models located via `NESSO_MODELS_DIR`, failing when required (#51, `744088a`); CI caches models and requires those tests (#52, `484e7bf`)
- cppcheck unused-function analysis gates CI (#53, `0da980f`)
- CLI sources counted in the coverage floor (#42, `9b80c10`)
- Release benchmark baseline refreshed (#78, `9c5773d`)
- Public name set to Nesso (`65d261b`)
- Conan and CMake layout, the move to C++26, GCC 15 profiles, and the Pipeline workflow (`f153dee`..`a047cf3`)

### Removed

- `store` subcommand and StorageEngine (#62, `68353d0`)

### Fixed

- Store progress sent to stderr (#45, `efc32fd`), before the store command was removed
- Batched inference with dynamic padding (#61, `f2e6512`)
- BERT-compatible UTF-8 basic tokenizer with a golden token-id test (#70, `15d1e0f`)
- Deterministic embeddings (#72, `3b7a8c6`)
- Exit codes 0/1/2 and readable failures (#73, `844954f`)

[Unreleased]: https://github.com/lorenzocaprari/nesso/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/lorenzocaprari/nesso/releases/tag/v0.1.0
