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
- The fp32 step and the default int8 policy gate the `determinism` job. The int8 step with `NESSO_THREADS=$(nproc)` is `continue-on-error` and report-only (`.github/workflows/ci.yml`).
- `conan install` writes `CMakeUserPresets.json` at the repository root. It is generated, `.gitignore` does not list it, and it stays uncommitted. `build/` is ignored, which covers `build/<Config>/generators/CMakePresets.json`.
- `scripts/build-env-hash` hashes each Dockerfile and every path it copies. `conanfile.py` enters the hash as the resolved dependency spec, not as raw text. The amd64 image copies `conan.lock`, `.devcontainer/requirements.txt`, `conan/profiles/gcc-26`, and `conan/profiles/gcc-26-debug`. The arm64 image copies `conan.lock` and `conan/profiles/gcc-26-armv8`. Changing one of those makes `prepare-image` rebuild `ghcr.io/<owner>/nesso-build-env:deps-<hash>`. That job's timeout is 360 minutes. `clang-26`, `code-coverage`, and `gcc-26-fuzz` are not copied, so editing only those profiles does not rebuild the image.
- `conan/profiles/clang-26` keeps `compiler=gcc` and `compiler.version=15` so `--no-remote` reuses the `gcc-26` packages baked into the image. `compiler=clang` changes package ids and rebuilds dependencies from source.
- The godog test is named `bdd` and its label is `functional`. `ctest -L bdd` selects nothing. CI's label matrix is `unit`, `integration`, and `functional`.
- `scripts/lint` runs cppcheck only when `build/Release/compile_commands.json` or `build/Debug/compile_commands.json` exists. Otherwise it prints a skip line and still exits 0.
- `scripts/code-coverage` adds `--no-remote` and exports `NESSO_REQUIRE_MODEL=1` only when `CI=true`. The coverage job also sets `NESSO_REQUIRE_BDD=1`.
- The `bench` job compares only tiers present in both the run and the latest baseline saved from `main`. Pull requests omit 100k. There is no baseline on the first run, and `scripts/bench-compare` warns and exits 0. CI allows wall time up to 5.5× and peak RSS up to 1.2× (`NESSO_BENCH_WALL_TOLERANCE`, `NESSO_BENCH_RSS_TOLERANCE`). Local compare stays at 15% against `bench/baseline.json`.
- `fuzz-test` runs on pushes to `main` and on the nightly schedule. Pull requests are fuzzed by ClusterFuzzLite (`.github/workflows/cifuzz.yml`). The seeds stay in `test/fuzz/seeds/parser` and `test/fuzz/seeds/tokenizer`.

## Hard rules

- Author and committer are only Lorenzo Caprari <caprarilorenzo5@gmail.com>. No `Co-authored-by`, `Signed-off-by`, or any other trailer.
- The pull request title matches the pattern in `.github/workflows/pr-title.yml`. [CONTRIBUTING.md](CONTRIBUTING.md) says pull requests are squash-merged and that title becomes the commit on `main`.
- Do not merge, tag, push to `main`, force-push a shared branch, or change repository settings. Lorenzo merges after the Pipeline workflow (`.github/workflows/ci.yml`) is fully green.
- Never lower the `REQUIRED_*` coverage floors in `scripts/code-coverage`, not even to get CI green. If coverage falls below them, add tests. Read the numbers from that file so this doc does not go stale.
- Run `scripts/clang-tidy` and `scripts/lint` on new code before pushing.
- One pull request per agreed scope. Anything outside that scope is a note or a follow-up, not extra commits.
- When told to push to an existing branch or pull request, fetch first and push there. Do not open another pull request, and do not overwrite commits you did not author.
- New GitHub Actions use a full commit SHA and a trailing `# vX.Y.Z` comment, as in `.github/workflows/`.
- User-facing notes go under `## [Unreleased]` in `CHANGELOG.md`.

## Common pitfalls

| Error | Cause | Fix |
| --- | --- | --- |
| `NESSO_REQUIRE_BDD is set but no go toolchain was found` | `NESSO_REQUIRE_BDD=1` and `go` is missing (`test/functional/go.mod` is Go 1.24) | Install Go, or unset the variable outside CI |
| Model tests skipped while ctest is green | `NESSO_REQUIRE_MODEL` is off, so CMake adds `--allow-running-no-tests` and skips when `models/` is absent | `./scripts/fetch-model`, then configure with `NESSO_REQUIRE_MODEL=1` |
| `ctest -L bdd` runs nothing | `bdd` is the test name; the label is `functional` | `ctest --test-dir build/Debug -L functional` |
| int8 `check-determinism` with `NESSO_THREADS=$(nproc)` fails and Pipeline stays green | That step is `continue-on-error: true`. The default int8 policy blocks | Drop `NESSO_THREADS` and rerun. fp32 is `--model-dir models-fp32` after `./scripts/fetch-model models-fp32 fp32` |
| `python3 scripts/bench` exits on `/usr/bin/time` or a missing binary | It requires GNU time and a built `nesso` (default `build/Release/src/nesso`) | Install time and build Release. A tier that does not finish also exits 1 |
| `conan install --no-remote` misses lockfile packages | The baked cache is `CONAN_HOME=/opt/conan2` inside `nesso-build-env` | Drop `--no-remote` outside that image; keep `--lockfile=conan.lock` |
| `CMakeUserPresets.json` shows in `git status` | `conan install` writes it and `.gitignore` does not list it | Leave it uncommitted |
| `prepare-image` rebuilds for a long time | `scripts/build-env-hash` inputs changed | Expect that job; its timeout is 360 minutes |
