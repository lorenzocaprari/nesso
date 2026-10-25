# AGENTS.md

Instructions for coding agents on nesso. Product behavior is in [README.md](README.md). Contributor workflow is in [CONTRIBUTING.md](CONTRIBUTING.md); follow that file instead of copying it.

## Project

Nesso is a Linux-native C++26 CLI for semantic search over local text (`nesso grep`, `nesso index`, `nesso search`). Embeddings come from an ONNX MiniLM model. Ranking is a brute-force scan. The toolchain is Conan 2, CMake 4.2+ (`CMakeLists.txt`), and Ninja. Profiles live in `conan/profiles/`.

## Commands

Run these from the repository root. Profiles: `gcc-26` (Release), `gcc-26-debug` (Debug, ASan, UBSan), `gcc-26-fuzz`, `gcc-26-armv8`, `clang-26`, and `code-coverage`.

```bash
# Conan 2 install. Inside the nesso-build-env image (CONAN_HOME=/opt/conan2) keep --lockfile=conan.lock and --no-remote. Outside that image, drop --no-remote.
conan install . -pr:h ./conan/profiles/gcc-26-debug -pr:b default --lockfile=conan.lock --build=missing --no-remote

# Configure and compile the same profile. Debug binary: build/Debug/src/nesso. Release: swap in gcc-26 and build/Release/src/nesso.
conan build . -pr:h ./conan/profiles/gcc-26-debug -pr:b default --lockfile=conan.lock --build=missing --no-remote

# Fetch the default int8 MiniLM into models/. The fp32 gate uses the second argument: ./scripts/fetch-model models-fp32 fp32.
./scripts/fetch-model

# ctest labels unit, integration, and functional, set on the tests added under test/.
ctest --test-dir build/Debug -L unit
ctest --test-dir build/Debug -L integration
ctest --test-dir build/Debug -L functional

# CI adds --output-on-failure and -j "$(nproc)" on every ctest invocation, including scripts/code-coverage.
# The bdd test runs godog scenarios concurrently, one per logical CPU (runtime.NumCPU()).

# Local gate. clang-tidy needs compile_commands.json. Coverage floors are REQUIRED_* in scripts/code-coverage.
# scripts/lint --format-only is clang-format and cmake-format. --hygiene-only is the whitespace, line-ending, YAML, merge-marker, and large-file hooks.
bash scripts/lint
bash scripts/clang-tidy
./scripts/code-coverage conan/profiles/code-coverage

# AFL++ harnesses nesso_fuzz_parser and nesso_fuzz_tokenizer (conan/profiles/gcc-26-fuzz).
./scripts/fuzz-test

# fp32 determinism gate. --input must already exist. CI writes determinism-input.log for this command.
./scripts/check-determinism --binary build/Release/src/nesso --model-dir models-fp32 --input determinism-input.log

# End-to-end bench. CI pull requests run 1k and 10k and fail the bench job on a regression
# against the latest main baseline. Pushes to main and the nightly schedule also run 100k.
python3 scripts/bench
python3 scripts/bench-compare
```

## Misreads

- `NESSO_REQUIRE_MODEL=1` fails model-backed tests instead of skipping them, and drops Catch2 `--allow-running-no-tests` (`test/CMakeLists.txt`). `NESSO_REQUIRE_BDD=1` turns a missing Go toolchain into a configure error (`test/functional/CMakeLists.txt`). CI sets both. Unset, missing `models/` or a missing `go` binary skips those tests.
- The fp32 step and the default int8 policy gate the `determinism` job. A pull request sets `NESSO_DETERMINISM_MODE=pr` (`scripts/check-determinism --mode pr`: 256-line input, 2 runs, 2 index runs). Pushes to `main`, the nightly schedule, and `workflow_dispatch` set that variable to `full` (3500-line input, 10 runs, `--index-runs 3`). The int8 step with `NESSO_THREADS=$(nproc)` is `continue-on-error` and report-only (`.github/workflows/ci.yml`). The release workflow calls the script with `--runs 10` and `--index-runs 3` and does not set the mode.
- `conan install` writes `CMakeUserPresets.json` at the repository root. It is generated, `.gitignore` does not list it, and it stays uncommitted. `build/` is ignored, which covers `build/<Config>/generators/CMakePresets.json`.
- `scripts/build-env-hash` hashes each Dockerfile and every path it copies. `conanfile.py` enters the hash as the resolved dependency spec, not as raw text. The amd64 image copies `conan.lock`, `.devcontainer/requirements.txt`, `conan/profiles/gcc-26`, `conan/profiles/gcc-26-debug`, `test/functional/go.mod`, and `test/functional/go.sum`. The arm64 image copies `conan.lock`, `conan/profiles/gcc-26-armv8`, `test/functional/go.mod`, and `test/functional/go.sum`. Changing one of those makes `prepare-image-amd64` or `prepare-image-arm64` rebuild `ghcr.io/<owner>/nesso-build-env:deps-<hash>` (the arm64 tag appends `-arm64`). That job's timeout is 360 minutes. `clang-26`, `code-coverage`, and `gcc-26-fuzz` are not copied, so editing only those profiles does not rebuild the image. Both images install stress-ng, zstd, and ccache, and set `GOMODCACHE` to `/opt/go/pkg/mod` so a GitHub Actions `HOME` override does not hide the godog modules. The amd64 image also installs AFL++, lld-21, llvm-21, and GNU time (`/usr/bin/time`) in that post-Conan layer. afl++, Clang 21, and the compiler-rt symlinks stay in the compiler layer above the Conan installs, so adding the linker and GNU time does not rebuild those layers. The arm64 image does not install AFL++, lld, llvm, or GNU time. `bench` is amd64-only.
- `build-gcc-26-amd64`, `build-gcc-26-debug-amd64`, `build-clang-26-amd64`, and `build-gcc-26-arm64` set `CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER` to ccache, with `CCACHE_MAXSIZE` at 150M. `coverage` does not use ccache, so gcov output is not served from the cache. `conanfile.py` copies a set launcher into the CMake toolchain and sets `CMAKE_CXX_SCAN_FOR_MODULES` off: GCC 14+ adds `-fmodules-ts` while scanning is on, ccache refuses that flag, and the tree has no C++ modules. The cache key is `ccache-<runner.arch>-<lane>-<github.sha>`. Pull requests restore with the lane prefix. Only a push to `main` saves. `fuzz-test` does not wrap `afl-clang-lto`. The statistics step runs under bash and fails the job when ccache records an uncacheable compilation.
- `prepare-image-amd64` exports BuildKit cache to `ghcr.io/<owner>/nesso-build-env:buildcache` and `prepare-image-arm64` exports `:buildcache-arm64`, both `mode=max`, when that image is rebuilt. The Conan installs are Dockerfile layers above ccache, Go, stress-ng, and zstd, and on amd64 also above AFL++, lld-21, llvm-21, and GNU time, so a change to those tools does not rebuild the dependency layers. Those jobs free disk only when they rebuild and the runner has under 40 GiB free. They then delete `/usr/local/lib/android`, `/usr/share/dotnet`, and `/opt/ghc` in parallel. They do not apt-purge or prune Docker images.
- `conan/profiles/clang-26` keeps `compiler=gcc` and `compiler.version=15` so `--no-remote` reuses the `gcc-26` packages baked into the image. `compiler=clang` changes package ids and rebuilds dependencies from source.
- The godog test is named `bdd` and its label is `functional`. `ctest -L bdd` selects nothing. CI runs the `unit`, `integration`, and `functional` labels. `test-gcc-26-amd64`, `test-clang-26-amd64`, and `test-gcc-26-arm64` run all three, in that order, in one job. `test-gcc-26-debug-amd64` runs unit and integration. `test-gcc-26-debug-amd64-functional` runs functional alone: on a no-image pull request that suite is the wall-clock tail, and the bdd test already uses every logical CPU.
- `scripts/lint` runs pre-commit, `scripts/check-versions`, and `scripts/check-agent-docs`. `--format-only` runs the `clang-format` and `cmake-format` hooks. `--hygiene-only` runs `trailing-whitespace`, `end-of-file-fixer`, `mixed-line-ending`, `check-merge-conflict`, `check-added-large-files`, and `check-yaml`. cppcheck is `scripts/cppcheck`, after a Release configure. The `cppcheck` job runs that script. `scripts/lint` does not. The `lint` job runs on the runner, with no build image. Its steps are format, hygiene, versions, and agent docs.
- `scripts/code-coverage` adds `--no-remote` and exports `NESSO_REQUIRE_MODEL=1` only when `CI=true`. The coverage job also sets `NESSO_REQUIRE_BDD=1`.
- The `bench` job compares only tiers present in both the run and the latest baseline saved from `main`. Pull requests omit 100k. There is no baseline on the first run, and `scripts/bench-compare` warns and exits 0. CI allows wall time up to 5.5× and peak RSS up to 1.2× (`NESSO_BENCH_WALL_TOLERANCE`, `NESSO_BENCH_RSS_TOLERANCE`). Local compare stays at 15% against `bench/baseline.json`.
- `fuzz-test` runs on pushes to `main`, on the nightly schedule, and on `workflow_dispatch`. Pull requests are fuzzed by ClusterFuzzLite (`.github/workflows/cifuzz.yml`). The job does not `apt-get`: the amd64 image provides `afl-clang-lto++`, `ld.lld-21`, `llvm-ar-21`, and `llvm-ranlib-21`, and the compiler-rt symlinks. The seeds stay in `test/fuzz/seeds/parser` and `test/fuzz/seeds/tokenizer`.
- `test-gcc-26-arm64` does not need `prepare-image-amd64`. A code change runs on the arm64 image. A docs-only pull request skips that job. The check name stays `test-gcc-26-arm64`, and GitHub counts the skip as success. `build-gcc-26-arm64` and `prepare-image-arm64` depend only on the arm64 image. Job ids are the check names. Pipeline does not use a matrix, so a skip never reports an unexpanded `${{ matrix.* }}` name.
- A pull request that changes only documentation skips the heavy jobs in `.github/workflows/ci.yml`. Documentation is `*.md`, `docs/`, `LICENSE`, and `.github/ISSUE_TEMPLATE/`. Any other path counts as code. `lint` still runs. It does not use the build image, and its agent-docs step runs `scripts/check-agent-docs`. `clang-tidy` and `cppcheck` run only when the pull request changes code. Pushes to `main`, the nightly schedule, and `workflow_dispatch` run every job. `scripts/ci-build-archive` packs the build artifacts those jobs download.
- `.github/workflows/package.yml` is not a Pipeline job. It runs nightly at 03:47 UTC, on `workflow_dispatch`, and on a pull request that changes that workflow, `.github/workflows/release.yml`, `Dockerfile.runtime`, `CMakeLists.txt`, `scripts/build-release-binary`, `scripts/package-deb`, `scripts/compare-deb`, `scripts/smoke-deb`, or `scripts/fetch-model`. It builds the release `.deb` twice inside `nesso-build-env` (separate directories and file mtimes, `SOURCE_DATE_EPOCH` from the commit, ccache with `CCACHE_MAXSIZE` 150M and `CCACHE_BASEDIR` on the build directory). The `repro-build` job sets `defaults.run.shell` to bash, because a container job's default shell is `sh`. `scripts/build-release-binary` reads the nesso link command with `ninja -t commands` when the generator is Ninja, and fails unless the last `-flto` on that command is `-flto=1`. Checkout writes `safe.directory` only under a temporary `HOME`, so that job adds `safe.directory` for `GITHUB_WORKSPACE` and `/__w/nesso/nesso` before `git log` and `git archive`. `scripts/compare-deb` fails the job when the bytes differ and writes a diffoscope report. `scripts/smoke-deb` then installs that `.deb` with apt on Ubuntu 26.04 and 26.10, runs `nesso -V` and a grep of `test/fixtures/grep_sample.log`, then `apt-get remove` and `apt-get purge`. `.github/workflows/release.yml` calls this workflow and `deploy` waits for it. `NESSO_REPRODUCIBLE` is set for that build and for `Dockerfile.runtime`. Pipeline does not set it. `conan/profiles/gcc-26` is unchanged, so the build-env image is not rebuilt. Packaging script tests are `test/scripts/deb_repro_test.go` (ctest `deb_repro_test`, label `unit`).

## Hard rules

- Author and committer are only Lorenzo Caprari <caprarilorenzo5@gmail.com>. No `Co-authored-by`, `Signed-off-by`, or any other trailer.
- The pull request title matches the pattern in `.github/workflows/pr-title.yml`. [CONTRIBUTING.md](CONTRIBUTING.md) says pull requests are squash-merged and that title becomes the commit on `main`.
- Do not merge, tag, push to `main`, force-push a shared branch, or change repository settings. Lorenzo merges after the Pipeline workflow (`.github/workflows/ci.yml`) is fully green.
- Never lower the `REQUIRED_*` coverage floors in `scripts/code-coverage`, not even to get CI green. If coverage falls below them, add tests. Read the numbers from that file so this doc does not go stale.
- Run `scripts/clang-tidy` and `scripts/lint` on new code before pushing.
- One pull request per agreed scope. Anything outside that scope is a note or a follow-up, not extra commits.
- When told to push to an existing branch or pull request, fetch first and push there. Do not open another pull request, and do not overwrite commits you did not author.
- New GitHub Actions use a full commit SHA and a trailing `# vX.Y.Z` comment, as in `.github/workflows/`. `.github/workflows/release.yml` is the exception: its SLSA provenance generators are called by a `vX.Y.Z` tag because those workflows check their own ref. Scorecard Pinned-Dependencies flags those two refs. Leave them on the tag.
- User-facing notes go under `## [Unreleased]` in `CHANGELOG.md`. A release is one squash-merged pull request that bumps `version` in `conanfile.py` and moves that section to `## [X.Y.Z] - YYYY-MM-DD`. Do not bump `version` on any other pull request. After that commit is on `main`, publish the `vX.Y.Z` draft. Do not publish a draft named `Unreleased (bump conanfile.py to release)`: its tag name is `unreleased`, and `.github/workflows/release.yml` does not run for it. `.github/workflows/release-drafter.yml` sets the draft version from `scripts/check-versions`.

## Common pitfalls

| Error | Cause | Fix |
| --- | --- | --- |
| `NESSO_REQUIRE_BDD is set but no go toolchain was found` | `NESSO_REQUIRE_BDD=1` and `go` is missing (`test/functional/go.mod` is Go 1.24) | Install Go, or unset the variable outside CI |
| Model tests skipped while ctest is green | `NESSO_REQUIRE_MODEL` is off, so CMake adds `--allow-running-no-tests` and skips when `models/` is absent | `./scripts/fetch-model`, then configure with `NESSO_REQUIRE_MODEL=1` |
| `ctest -L bdd` runs nothing | `bdd` is the test name; the label is `functional` | `ctest --test-dir build/Debug -L functional` |
| int8 `check-determinism` with `NESSO_THREADS=$(nproc)` fails and Pipeline stays green | That step is `continue-on-error: true`. The default int8 policy blocks | Drop `NESSO_THREADS` and rerun. fp32 is `--model-dir models-fp32` after `./scripts/fetch-model models-fp32 fp32` |
| `python3 scripts/bench` exits on `/usr/bin/time` or a missing binary | It requires GNU time (`/usr/bin/time` from the `time` package) and a built `nesso` (default `build/Release/src/nesso`). The amd64 build-env image includes that package | Outside that image, install `time` and build Release. A tier that does not finish also exits 1 |
| `conan install --no-remote` misses lockfile packages | The baked cache is `CONAN_HOME=/opt/conan2` inside `nesso-build-env` | Drop `--no-remote` outside that image; keep `--lockfile=conan.lock` |
| `CMakeUserPresets.json` shows in `git status` | `conan install` writes it and `.gitignore` does not list it | Leave it uncommitted |
| `prepare-image-amd64` or `prepare-image-arm64` rebuilds for a long time | `scripts/build-env-hash` inputs changed, including `test/functional/go.sum` | Expect that job; its timeout is 360 minutes |
| SLSA provenance fails when the generator `uses` a commit SHA | `.github/workflows/release.yml` calls those workflows by tag | Keep the `vX.Y.Z` tag. The generator checks its own ref |
| Draft release is named `v0.2.0` while `conanfile.py` is `0.1.0` | Release Drafter used to bump from labels. The only git tag is `v0.1.0`, and it matches `conanfile.py` | Delete that unpublished draft. Do not publish it. Publishing would create a `v0.2.0` tag that `scripts/check-versions` rejects |
| Publishing the Unreleased draft did not publish nesso | That draft's tag name is `unreleased`, which is not `vX.Y.Z`, so `.github/workflows/release.yml` does not run | Bump `version` in `conanfile.py` on `main`, then publish the `vX.Y.Z` draft |
