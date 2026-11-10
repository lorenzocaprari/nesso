# Building

## Prerequisites

- **Compiler:** GCC 15+ with C++26 support
- **Build system:** CMake 4.2+
- **Package manager:** Conan 2.x
- **OS:** Linux

## Build

### Debug (day-to-day develop)

Matches the CI Debug lane. On low-RAM hosts (for example ~20 GiB WSL), pass `-c user.nesso:link_jobs=1` on **both** `conan install` and `conan build` so Ninja does not link many ASan binaries at once. Do not put that conf in `conan/profiles/gcc-26-debug` itself: that file is a build-env image hash input.

```bash
conan install . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing -c user.nesso:link_jobs=1
conan build . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing -c user.nesso:link_jobs=1
./scripts/fetch-model
ctest --test-dir build/Debug --output-on-failure -j "$(nproc)" -L unit
ctest --test-dir build/Debug --output-on-failure -j "$(nproc)" -L integration
ctest --test-dir build/Debug --output-on-failure -j "$(nproc)" -L functional
```

### Local Release (no LTO)

`conan/profiles/gcc-26-dev` is `-O3` Release without `-flto`, with `user.nesso:link_jobs=1`. Use it on laptops and WSL. CI Release keeps `gcc-26` (`-flto=auto`).

```bash
conan install . -pr:h ./conan/profiles/gcc-26-dev -pr:b default \
  --lockfile=conan.lock --build=missing
conan build . -pr:h ./conan/profiles/gcc-26-dev -pr:b default \
  --lockfile=conan.lock --build=missing
./scripts/fetch-model
ctest --test-dir build/Release --output-on-failure -j "$(nproc)" -L unit
ctest --test-dir build/Release --output-on-failure -j "$(nproc)" -L integration
ctest --test-dir build/Release --output-on-failure -j "$(nproc)" -L functional
```

Binary-only (skip unit, integration, fuzz, functional, and script ctests): pass `-o tests=False` on **both** `conan install` and `conan build`. Default is `tests=True` (CI unchanged). Reconfigure with `tests=True` before `ctest`.

### Ship-faithful local Release (optional)

`gcc-26` plus `NESSO_REPRODUCIBLE=1` forces `-flto=1` (same pin as packaging) and still needs a link-job cap on low-RAM hosts. Full CI Release (`gcc-26` with `-flto=auto` and unlimited parallel links) is not the recommended laptop path.

```bash
export NESSO_REPRODUCIBLE=1
conan install . -pr:h ./conan/profiles/gcc-26 -pr:b default \
  --lockfile=conan.lock --build=missing -c user.nesso:link_jobs=1
conan build . -pr:h ./conan/profiles/gcc-26 -pr:b default \
  --lockfile=conan.lock --build=missing -c user.nesso:link_jobs=1
```

The binary is `build/Debug/src/nesso` or `build/Release/src/nesso`. For CI-strict configure (fail instead of skip when the model or Go toolchain is missing), set `NESSO_REQUIRE_MODEL=1` and `NESSO_REQUIRE_BDD=1` on the `conan build` environment, as Pipeline does.

## Compile options

`nesso_options` is an INTERFACE library in `CMakeLists.txt`. Every nesso target links it, including tests, the fuzz harnesses, and the benchmark. It requests C++26 (`cxx_std_26`), turns compiler extensions off, and adds the warning set below for GCC and Clang. Those options are usage requirements of the nesso targets. They are not applied when ONNX Runtime, Catch2, CLI11, or nlohmann-json is built. Profile optimization, sanitizer, and LTO flags stay in `user.nesso:cxxflags` for the same reason.

`-Werror` is off unless `NESSO_WERROR` is ON (`-DNESSO_WERROR=ON` for a plain CMake build). No profile sets it. `conanfile.py` turns it on when the command passes `-c user.nesso:werror=True`. `conan build` writes the toolchain again, so omit the flag there and the build comes back without `-Werror`. `NESSO_WERROR=1` makes `scripts/code-coverage` pass that `-c` on `conan build` and `scripts/fuzz-test` pass it on `conan install`. The default is off.

`NESSO_LINK_JOBS` caps concurrent Ninja link jobs when it is a positive integer (`user.nesso:link_jobs` from Conan, or the environment for a plain CMake configure). Unset leaves link parallelism unlimited, which is what CI uses. Compile jobs stay unlimited. A single LTO link can still spawn many codegen threads under `-flto=auto`; packaging and ship-faithful local builds use `NESSO_REPRODUCIBLE=1` so that becomes `-flto=1`.

`conan/profiles/gcc-26-debug` passes `-fno-sanitize-recover=all` with `-fsanitize=address,undefined`. ASan and UBSan stop on the first error without `UBSAN_OPTIONS`.

GCC and Clang:

`-Wall` `-Wextra` `-Wpedantic` `-Wconversion` `-Wsign-conversion` `-Wshadow` `-Wformat=2` `-Wold-style-cast` `-Wnon-virtual-dtor` `-Woverloaded-virtual` `-Wnull-dereference` `-Wdouble-promotion` `-Wimplicit-fallthrough` `-Wcast-align` `-Wextra-semi`

GCC only:

`-Wduplicated-cond` `-Wduplicated-branches` `-Wlogical-op` `-Wuseless-cast`

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
