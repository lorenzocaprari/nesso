# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.1] - 2026-10-27

### Added

- The `coverage` job uploads the cleaned lcov trace to Codecov on pull requests and on pushes to `main`. The README shows that coverage badge. A missing Codecov token or app does not fail the job. The coverage floors are unchanged.

### Fixed

- Package `resolve-image` waits, on a pull request, for Pipeline to publish a new `nesso-build-env` tag. A base-image digest bump used to fail that job because Package and Pipeline start together and the new `deps-<hash>` tag does not exist yet. Schedule, manual, and release runs still fail on the first miss. The runtime-image scan test reads the final `FROM` from `packaging/Dockerfile.runtime` instead of a fixed digest, so the next digest bump does not fail `ci_report_test`.
- A tag release downloads the `.deb` provenance with `gh run download` and then uploads the GitHub Release. The `release` job sets `GH_REPO` to the repository, because that job has no checkout. The v0.2.0 run stopped at "Download .deb provenance" with `failed to run git: fatal: not a git repository` and skipped the upload.

## [0.2.0] - 2026-10-27

### Added

- Pipeline adds `ci-gate`, shown on GitHub as `CI gate`, so one required status check can cover the whole workflow. It fails unless every other Pipeline job succeeded, or was skipped for a docs-only pull request, for `fuzz-test` on a pull request, or for `bench-history` off a push to `main`. A failure, a cancellation, or any other skip fails it. `lint` fails when a new job is left out of that list.
- Host jobs in the GitHub workflows start with StepSecurity Harden-Runner in audit mode. Jobs that run inside a container, and the SLSA generator workflows, do not. Audit mode records outbound calls for a later allow-list and does not block on that list. Each hardened step links to that run's insights.
- Package and tag releases scan the runtime image with Trivy 0.76.1 for operating-system and library vulnerabilities and upload SARIF to code scanning. The binary is the checksum-pinned GitHub release, not the Trivy GitHub Action. Findings are reported and do not fail the job. `bash scripts/scan-runtime-image` runs the same scan locally.
- The `.deb` and the runtime image ship `/usr/share/doc/nesso/copyright` and `/usr/share/doc/nesso/THIRD_PARTY_NOTICES`. They hold the license and notice texts of every statically linked or compiled-in library from the release dependency graph, ONNX Runtime's third-party notices, the MiniLM model and vocabulary, and the Unicode data tables. `copyright` also carries the nesso license, so minimized images that keep only `copyright` still have every notice. `scripts/third-party-notices` builds both files from the `conan install` graph.
- The `lint` job runs actionlint and zizmor on `.github/workflows` for every pull request and push to `main`. `bash scripts/actionlint` and `bash scripts/zizmor` run the same checks locally. Pull requests also run dependency review in that job. It blocks new dependencies at severity moderate or higher, and licenses that cannot be satisfied from the Apache-2.0 allow list. GitHub's dependency graph covers Actions, the godog Go module, and the devcontainer pip lock. Conan is not in that graph.
- Pushes to `main` append the end-to-end benchmark to history on the `gh-pages` branch. Pull requests do not write that branch. They add an informational comparison against the stored history to the `bench` job summary. The benchmark job still fails only when a tier regresses against the latest baseline from `main`.
- Pull requests fail the `coverage` job when fewer than 80% of the changed executable lines under `src/` are covered. The job summary lists the uncovered lines. A change with no executable product lines passes. Pushes to `main` still report only the tree-wide coverage.
- Test jobs write ctest JUnit XML and list failing tests in the job summary. The XML is kept as an artifact.
- The Package workflow builds the release `.deb` twice per architecture from a clean tree and requires the bytes to match. amd64 runs on `ubuntu-latest` and arm64 on `ubuntu-24.04-arm`. It then installs that package with apt on Ubuntu 26.04 and Ubuntu 26.10, on both architectures, runs the release smoke command, and checks `apt-get remove` and `apt-get purge`. It runs nightly and before a tag publish.
- Tag releases publish `nesso_<version>_amd64.deb` and `nesso_<version>_arm64.deb`, an SPDX SBOM for each, and `ghcr.io/lorenzocaprari/nesso` as a `linux/amd64` + `linux/arm64` manifest list tagged `vX.Y.Z`, `X.Y.Z`, and `latest`. Keyless cosign signs the index digest and each platform digest. SLSA Build Level 3 provenance covers both packages, both SBOMs, and those image digests. Verification commands are in `docs/verifying.md`.
- `--threads` and `NESSO_THREADS` set the int8 session count and the fp32 intra-op thread count. `nesso --version --verbose` prints that policy.
- `scripts/check-determinism --index-runs` compares corpus bytes and the rankings for four queries, including `PAY-E1042`. `--emit-input` writes the 3500-line input the gate uses.

### Changed

- The README demo shows `nesso grep` before `nesso index` and `nesso search`. The sample log is synthetic output from `scripts/demo-log`, and `docs/assets/demo.tape` builds it in the hidden setup.
- A tag release waits for approval on the `release` environment after the package check passes. The manual dry run does not request that approval and does not publish.
- The README is a short intro: what nesso is, a `nesso grep` command demo with a short recording, and the v0.1.0 amd64 install. Usage, building, releases, verification, benchmarks, and determinism are under `docs/`. `scripts/demo-log` writes the deterministic 5000-line log the recording searches. The benchmark history summary links to the chart file on the `gh-pages` branch.
- Release Drafter takes the draft version from `version` in `conanfile.py`. While that git tag already exists, the draft is named `Unreleased (bump conanfile.py to release)` and is not a version tag. After the version is bumped on `main`, the draft is `vX.Y.Z`. Publishing that draft creates the tag, and the release workflow still checks the tag against `conanfile.py`.
- Pipeline check names match the job ids: `lint`, `clang-tidy`, `cppcheck`, `build-gcc-26-amd64`, `build-gcc-26-debug-amd64`, `build-clang-26-amd64`, `build-gcc-26-arm64`, and the `test-*` jobs for those lanes. Unit and integration share a job. Functional shares it too, except gcc Debug, where it stays `test-gcc-26-debug-amd64-functional`. `lint` runs on the runner and does not use the build image. Its steps are format, hygiene, versions, agent docs, actionlint, zizmor, `ci-gate-needs`, and, on pull requests, dependency review. An image rebuild frees disk only when the runner has under 40 GiB free.
- The amd64 build-env image installs AFL++, lld-21, llvm-21, and GNU time in the Dockerfile layer after the Conan installs. `fuzz-test` and `bench` no longer install those packages at runtime. The arm64 test jobs do not wait on the amd64 image. A docs-only pull request runs those checks on the runner with no container. The release determinism gate still installs stress-ng inside its throwaway container: the published runtime image does not include it, and that gate's default load is stress-ng.
- The build-env image includes stress-ng, zstd, and the godog module cache, in Dockerfile layers after the Conan installs. CI compiles the Release, Debug, Clang, and arm64 builds with ccache (150M per lane) and disables C++ module scanning on those builds so GCC's `-fmodules-ts` does not bypass the cache. Coverage does not use ccache. The amd64 image job is `prepare-image-amd64`. The image name and `deps-<hash>` tag are unchanged. The BuildKit registry cache tags are `buildcache` and `buildcache-arm64` (`mode=max`).
- Pull requests run a shorter determinism pass (`scripts/check-determinism --mode pr`: 256-line input, 2 runs, 2 index runs) for fp32, the default int8 policy, and the report-only `NESSO_THREADS` step. Pushes to `main`, the nightly schedule, manual runs, and the release gate keep the 3500-line input, 10 runs, and `--index-runs 3`.
- CI uploads the binaries, ctest manifests, and test fixtures instead of the whole build tree. A pull request that only changes documentation skips the build, test, coverage, bench, and determinism jobs. Lint still runs.
- Pull-request benchmarks run the 1k and 10k tiers. Pushes to `main` and the nightly schedule still run the 100k tier. The benchmark job fails when a tier regresses against the latest baseline saved from `main`.
- AFL++ `scripts/fuzz-test` runs on pushes to `main` and in the nightly Pipeline. Pull requests stay on ClusterFuzzLite.
- Debug builds scan an ONNX model for its quantization marker in chunks. Copying the whole file through checked iterators took about five seconds per embedder load under ASan.
- CI runs ctest with `-j "$(nproc)"`. The godog suite runs scenarios concurrently, up to one per logical CPU.
- The project license is Apache 2.0.
- CI and the release workflow block when int8 embeddings disagree across processes. The run that sets `NESSO_THREADS` to the logical CPU count stays report-only.
- `scripts/cppcheck` and `scripts/scan-runtime-image` quote the comma-separated option values in their argument arrays, so shellcheck reports no SC2054 warnings. The arguments passed to cppcheck and Trivy are unchanged.
- `renovate.json` moved to `.github/renovate.json`, `.cppcheck-suppressions` to `scripts/cppcheck-suppressions`, and `Dockerfile.runtime` to `packaging/Dockerfile.runtime`.

### Fixed

- A manual release dry run uploads the `nesso-<version>-release` workflow artifact (both `.deb` files, both SBOMs, and `SHA256SUMS`). The `subjects` job runs when `image` succeeds, including when `approve-release` is skipped. Push, signing, provenance, and the GitHub Release still wait for `approve-release`.
- An out-of-range `NESSO_THREADS` value is a usage error. A digit string that does not fit in `size_t` used to throw from `std::stoull` and print `Unhandled Runtime Exception: stoull`. It now exits 2 with `NESSO_THREADS must be a positive integer`, the same as any other value that is not a positive integer.
- The trace peak resident size is `getrusage`'s `ru_maxrss`, in bytes. The previous helper placement-newed `rusage` into a byte array aligned to one byte.
- A tag release can call Package. `verify-package` grants `security-events: write`, which `scan-runtime` declares, because GitHub checks that permission before the job's `if`. Release passes `scan_runtime: false`. The Release `image` job still scans the image it built. Package's nightly run, manual run, and packaging pull requests still scan.
- Release builds rewrite source and build paths, pin LTO to one job, and archive the `.deb` with the commit timestamp, fixed ownership, and single-threaded zstd. Two clean builds of the same commit produce the same package bytes.
- `scripts/bench` treats the current grep trace (`parse`, `query-embed`, `embed`) as a completed run. The old `parse`/`embed`/`rank` check marked every CI run failed, and `continue-on-error` hid that result.
- int8 embeddings use one intra-op thread per session so repeated runs agree under CPU load.
- The default int8 session pool is sized by the memory available to nesso (`MemAvailable`, or total RAM when the kernel does not report it) and an estimated 480 MiB per session, keeping half of that memory free. It was sized by the model file, about 23 MB per session, so the cap never applied. `--threads` and `NESSO_THREADS` still set the count, with a warning on stderr when it exceeds the estimate. `--version --verbose` shows the cap.
- `nesso search` reports a corpus written in another format version as `the corpus format is not supported by this nesso; re-run nesso index` instead of `the file is corrupt`. A corpus whose stored model id differs from the embedding model fails with `the corpus was built with a different model; re-run nesso index` before the model loads. Both exit with status 2, as before.
- int8 embeddings no longer change between runs under CPU load on hosts that advertise Intel AMX but do not preserve AMX tile registers across a context switch, which some virtual machines do. ONNX Runtime 1.18 picks its AMX int8 kernel whenever Linux grants tile permission, and a kernel preempted mid-GEMM returned wrong numbers. nesso now refuses that permission for its own process with a seccomp filter (which also sets `no_new_privs`) before the first ONNX session, so int8 uses the AVX-512 VNNI or AVX2 kernel. Integer GEMM is exact, so on a sound host the embeddings are the same bytes as before. fp32 is unchanged.

### Security

- The devcontainer pip lock pins `certifi` 2026.10.19, `filelock` 4.2.1, and `virtualenv` 21.14.13, the lowest releases of those packages with no known vulnerabilities. The previous pins were `certifi` 2026.7.22, `filelock` 4.1.1, and `virtualenv` 21.14.7.
- The release binary no longer has a RUNPATH. Up to 0.1.0 it carried the Conan build-tree library directories followed by an empty entry, and the dynamic loader reads an empty entry as the current working directory. Running `nesso` from a directory that someone else can write, such as a directory of collected logs, could load a shared library planted there instead of the system `libstdc++`, `libm`, or `libgcc_s`. Every dependency is linked statically, so no search path is needed. `scripts/build-release-binary` now fails when the binary has an RPATH or RUNPATH (`scripts/check-no-runpath`).
- `scripts/build-release-binary` fails when the release binary links networking code (`scripts/check-network-free`). libcurl and OpenSSL are in the Conan graph through date and are not linked into the binary. The check rejects a denied `NEEDED` library, a networking libc import that is not listed in `scripts/check-network-free.allow`, and a statically linked curl or OpenSSL symbol or version string. The `.deb` and the runtime image ship that binary.

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

[Unreleased]: https://github.com/lorenzocaprari/nesso/compare/v0.2.1...HEAD
[0.2.1]: https://github.com/lorenzocaprari/nesso/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/lorenzocaprari/nesso/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/lorenzocaprari/nesso/releases/tag/v0.1.0
