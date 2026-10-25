# Contributing

Nesso is a solo-maintained Linux CLI. Pull requests are squash-merged: the PR title becomes the commit on `main`.

## Reports

Bugs and feature requests use the issue forms. Vulnerabilities follow [SECURITY.md](SECURITY.md). Conduct reports go to [@lorenzocaprari](https://github.com/lorenzocaprari) through GitHub private vulnerability reporting, as described in [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## What you need

- Linux
- GCC 15+ with C++26 (`conan/profiles/gcc-26*`)
- CMake 4.2+ (`cmake_minimum_required` in `CMakeLists.txt`) and Ninja
- Conan 2 (`conanfile.py`, `conan.lock`)
- Python 3 for pre-commit, `scripts/bench`, and `scripts/bench-compare`
- Go 1.24+ only for the godog specs (`test/functional/go.mod`)
- AFL++, lld 21, and llvm-21 only for `scripts/fuzz-test`. Clang 21 is that toolchain and `conan/profiles/clang-26`

## Devcontainer image

CI builds `.devcontainer/Dockerfile` (Ubuntu 26.04, GCC 15, Clang 21, AFL++, lld 21, llvm-21, GNU time, Go, Conan 2, pre-commit, ccache, stress-ng, zstd) and pushes `ghcr.io/lorenzocaprari/nesso-build-env:deps-<hash>`. AFL++, lld-21, llvm-21, and GNU time are installed again in the layer after the Conan installs. The compiler layer still has AFL++, Clang 21, and the compiler-rt symlinks. The arm64 image is `.devcontainer/Dockerfile.arm64`, tagged `deps-<hash>-arm64`. The amd64 CI job is `prepare-image-amd64`. `scripts/build-env-hash amd64` covers that Dockerfile, `.devcontainer/requirements.txt`, `conan.lock`, `conan/profiles/gcc-26`, `conan/profiles/gcc-26-debug`, `test/functional/go.mod`, and `test/functional/go.sum`. It includes `conanfile.py` as the resolved dependency spec, not the recipe text. There is no `devcontainer.json`. The image sets `CONAN_HOME=/opt/conan2` with the locked dependencies already installed; CI configures with `--no-remote`. It sets `GOMODCACHE=/opt/go/pkg/mod` with the godog modules already downloaded.

```bash
docker build -f .devcontainer/Dockerfile -t nesso-build-env .
docker run --rm -it -v "$PWD:/src" -w /src nesso-build-env bash
```

## Build

Profiles in `conan/profiles/`:

| Profile | Use |
| --- | --- |
| `gcc-26` | Release (`-O3`, `-march=x86-64-v2`, LTO) |
| `gcc-26-debug` | Debug with ASan and UBSan |
| `gcc-26-fuzz` | AFL++ library harnesses |
| `gcc-26-armv8` | arm64 Release (`-O3`, `-march=armv8-a`, LTO) |
| `clang-26` | Clang 21 Release with libstdc++, no LTO (`-O3`, `-march=x86-64-v2`) |
| `code-coverage` | Debug plus `--coverage` |

`conanfile.py` uses Conan `cmake_layout` and `CMakeToolchain` and sets the generator to Ninja. `conan install` writes `CMakeUserPresets.json` at the repo root; that file includes `build/<Config>/generators/CMakePresets.json`. Both are generated and should stay uncommitted. `cmake --list-presets` prints the names (Conan uses `conan-<build type>` in lowercase, so `conan-debug` and `conan-release` for these two profiles). `cmake --preset` and `cmake --build --preset` drive those presets. CI and the README call `conan build`, which configures and builds through the same toolchain.

Debug:

```bash
conan install . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing
conan build . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing
ctest --test-dir build/Debug --output-on-failure
```

Release: the same commands with `gcc-26` and `build/Release`. The binary is `build/<Config>/src/nesso`. Fetch the MiniLM files once with `./scripts/fetch-model`.

Inside the published build image, add `--no-remote` so Conan does not leave the baked cache.

## Tests

Catch2 unit and integration tests, and the godog functional suite, are separate ctest labels:

```bash
ctest --test-dir build/Debug -L unit
ctest --test-dir build/Debug -L integration
ctest --test-dir build/Debug -L functional
```

The functional label includes the `bdd` test. It runs `go test` in `test/functional/` (godog v0.16.0). Specs are `test/functional/features/*.feature`. Without Go, CMake skips `bdd`. `NESSO_REQUIRE_BDD=1` turns a missing Go toolchain into a configure error; CI sets it. `NESSO_REQUIRE_MODEL=1` fails model-backed tests instead of skipping them; CI sets that too.

`bdd` runs godog scenarios concurrently, one per logical CPU. Each scenario has its own temporary directory, environment, and pty. CI passes `-j "$(nproc)"` and `--output-on-failure` to every ctest invocation.

Debug ctest in CI also sets `ASAN_OPTIONS` and `UBSAN_OPTIONS` so sanitizer findings abort the job.

## Fuzz

`./scripts/fuzz-test` builds AFL++ LTO and ASan harnesses `nesso_fuzz_parser` and `nesso_fuzz_tokenizer` with `conan/profiles/gcc-26-fuzz`, then runs `afl-fuzz`. Seeds live in `test/fuzz/seeds/parser` and `test/fuzz/seeds/tokenizer`. `NESSO_FUZZ_MAX_TOTAL_TIME` defaults to 60 seconds per target; CI uses 60. The script needs `afl-clang-lto++`, `llvm-ar` (`llvm-ar-21`), and `llvm-ranlib` (`llvm-ranlib-21`), which the amd64 build-env image provides along with `ld.lld-21`. The Pipeline job does not install them. It runs on pushes to `main`, on the nightly schedule, and on `workflow_dispatch`. Pull requests are fuzzed by ClusterFuzzLite (`.github/workflows/cifuzz.yml`).

## Lint and the local gate

```bash
pre-commit install
bash scripts/lint
```

`scripts/lint` runs pre-commit on the whole tree and `scripts/check-versions`. Hooks in `.pre-commit-config.yaml` fix trailing whitespace and line endings, reject merge markers and large files, check YAML, and run clang-format (LLVM, Allman braces, indent 4, column 120) and cmake-format. cppcheck runs only when `build/Release/compile_commands.json` or `build/Debug/compile_commands.json` is present. CI runs `scripts/clang-tidy` after a Release configure.

The README local gate, before pushing:

```bash
bash scripts/lint
./scripts/code-coverage conan/profiles/code-coverage
```

Coverage floors are the `REQUIRED_*` values in `scripts/code-coverage`. The Pipeline workflow (`.github/workflows/ci.yml`) is the merge gate: lint, clang-tidy, cppcheck, Release and Debug builds, the Clang 21 and arm64 Release builds, unit, integration, and functional tests, coverage, and `scripts/check-determinism` (the `determinism` job: fp32 and the default int8 policy gate; int8 with `NESSO_THREADS` set to the logical CPU count is report-only). Pull requests set `NESSO_DETERMINISM_MODE=pr` (256-line input, 2 runs, 2 index runs). Pushes to `main`, the nightly schedule, manual runs, and the tag release keep the full 3500-line gate (`--runs 10`, `--index-runs 3`). The `bench` job fails when a tier regresses against the latest baseline saved from `main`. Pull requests run the 1k and 10k tiers. Pushes to `main` and the nightly schedule also run 100k. `fuzz-test` runs on pushes to `main` and nightly. A pull request that changes only `*.md`, `docs/`, `LICENSE`, or `.github/ISSUE_TEMPLATE/` skips the build, test, coverage, bench, and determinism jobs. `lint` still runs. Pushes to `main`, the nightly schedule, and manual runs do not skip.

## Pull requests

Title format: `type(scope): subject` or `type: subject`. Types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`. A `!` immediately before `:` is a breaking change (`feat(core)!: ...`). `.github/workflows/pr-title.yml` enforces this.

The only author on commits and pull requests is Lorenzo Caprari <caprarilorenzo5@gmail.com>. No `Co-authored-by`, `Signed-off-by`, or other agent or tool trailers.

Every GitHub Action is pinned to a commit SHA with a `# vX.Y.Z` comment. New or bumped actions keep that.

Put user-facing notes under `## [Unreleased]` in [CHANGELOG.md](CHANGELOG.md). On release, move that section to `## [X.Y.Z] - YYYY-MM-DD`.

`.github/release-drafter.yml` maps labels to changelog sections. The Release Drafter workflow applies those labels from the PR title. It does not create labels. These already exist: `enhancement` (`feat`), `bug` (`fix`), `documentation` (`docs`), `breaking`, `changed`, `chore`, `deprecated`, `removed`, and `security`. On push to `main`, the same workflow updates the draft release `v$RESOLVED_VERSION`. Copy that draft into `CHANGELOG.md` before tagging.

`version` in `conanfile.py` is the version (`scripts/check-versions`). Pushing a `vX.Y.Z` tag that matches it runs `.github/workflows/release.yml`, which publishes the `.deb`, `ghcr.io/lorenzocaprari/nesso`, and the GitHub Release. CI must already have published `nesso-build-env`.

`.github/CODEOWNERS` requests a review from `@lorenzocaprari`.
