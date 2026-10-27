# Building

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

Release profile: replace `gcc-26-debug` with `gcc-26`. The binary is `build/Debug/src/nesso` or `build/Release/src/nesso`.

Inside the `nesso-build-env` image (`CONAN_HOME=/opt/conan2`) keep `--lockfile=conan.lock` and `--no-remote`. Outside that image, drop `--no-remote`.

The black-box specs in [test/functional/](../test/functional/) run the `nesso` binary through [godog](https://github.com/cucumber/godog) as the `bdd` ctest case. Go 1.24+ is a test-only dependency. Without it CMake skips `bdd`, and `NESSO_REQUIRE_BDD=1` turns that into a configure error (CI sets it).

Unit, integration, and functional tests are separate ctest labels. CI runs unit and integration in one job per lane. Functional shares that job on gcc Release, Clang, and arm64, and has its own gcc Debug job:

```bash
ctest --test-dir build/Debug -L unit
ctest --test-dir build/Debug -L integration
ctest --test-dir build/Debug -L functional
```

`bdd` is the test name. `ctest -L bdd` selects nothing.

## Development

Local gate, before pushing:

```bash
bash scripts/lint
bash scripts/actionlint
bash scripts/zizmor
./scripts/code-coverage conan/profiles/code-coverage
```

See [.github/workflows/ci.yml](../.github/workflows/ci.yml) for `lint`, `clang-tidy`, and `cppcheck`, the gcc Release, gcc Debug, Clang, and arm64 builds, unit, integration, and functional tests, coverage, and the fp32 determinism gate. The `coverage` job publishes the cleaned lcov trace to Codecov on pull requests and pushes to `main`. The README badge reads that report. AFL++ `fuzz-test` runs on pushes to `main` and nightly; pull requests are fuzzed by [ClusterFuzzLite](../.github/workflows/cifuzz.yml). The pipeline runs on pull requests, on pushes to `main`, and nightly. Require the `CI gate` check (`ci-gate` in that workflow) instead of listing every job. [.github/workflows/release-drafter.yml](../.github/workflows/release-drafter.yml) drafts the next release from `version` in `conanfile.py`. Publishing the `vX.Y.Z` draft creates the tag, and tag `v*` publishes via [.github/workflows/release.yml](../.github/workflows/release.yml). Weekly [CodeQL](../.github/workflows/codeql.yml) (`c-cpp`, `build-mode: none`) and [OpenSSF Scorecard](../.github/workflows/scorecard.yml) run on `main`.

Benchmark commands are in [benchmarks.md](benchmarks.md). The embedding gate is in [determinism.md](determinism.md).
