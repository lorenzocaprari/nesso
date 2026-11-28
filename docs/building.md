# Building

## Prerequisites

- **Compiler:** GCC 15+ with C++26 support. Ubuntu CI stays on GCC 15. Release binaries use GCC Toolset 16.2.1 (`conan/profiles/gcc-26-el9`, `conan/profiles/gcc-26-el9-armv8`). Windows release uses clang-cl (`conan/profiles/clang-cl-26`).
- **Build system:** CMake 4.2+
- **Package manager:** Conan 2.x
- **OS:** Linux (day-to-day and `.devcontainer` images). Native Windows builds use a Windows host or GitHub `windows-latest`, not the Ubuntu/EL9 build-env images.

## Build

### Sources that vanished upstream

`microsoft/onnxruntime` returns 404 on GitHub since 18 Nov 2026, so Conan cannot download the onnxruntime 1.18.1 source from its recipe URL. A cache that does not already hold the onnxruntime package needs the fallback the build-env images use. Append [conan/global.conf](../conan/global.conf) to your Conan home's `global.conf` once:

```bash
cat conan/global.conf >> "$(conan config home)/global.conf"
```

Conan then tries the original URL first and falls back to the `conan-sources` release of [nesso-vendor](https://github.com/lorenzocaprari/nesso-vendor), checking the sha256 pinned in the recipe. That archive is byte-identical to the original; nesso-vendor's README shows how to rebuild it from Software Heritage.

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

### Windows Release (clang-cl)

Requires Visual Studio Build Tools (MSVC STL/CRT), LLVM clang-cl, Ninja, Conan 2, and CMake 4.2+ (CI installs 4.4.5). Static CRT; no VCRedist. Example:

```bash
conan install . -pr:h ./conan/profiles/clang-cl-26 \
  -pr:b ./conan/profiles/windows-msvc-build \
  --lockfile=conan.lock --lockfile-partial --build=missing
conan build . -pr:h ./conan/profiles/clang-cl-26 \
  -pr:b ./conan/profiles/windows-msvc-build \
  --lockfile=conan.lock --lockfile-partial --build=missing
./scripts/fetch-model
ctest --test-dir build/Release --output-on-failure -L unit
python3 packaging/scripts/package-zip 0.3.1 build/Release/src/nesso.exe models dist
```

`clang-cl-26` compiles nesso with clang-cl and resolves host dependencies as MSVC (static CRT) so Conan Center binaries match. Missing packages build with `cl.exe`. `--lockfile-partial` keeps recipe revisions from `conan.lock` while allowing Windows-only build tools such as `nasm`. CI uses `windows-2022` (VS 17 / `compiler.version=194`, MSVC 14.4x) and caches `CONAN_HOME` (`.conan2` in the workspace) keyed on `conan.lock`, the Windows profiles, and `conanfile.py`, so later runs reuse the static-CRT package graph instead of rebuilding ORT/boost every time.

Package and Release call `.github/workflows/windows.yml`. That lane is not part of Pipeline `ci-gate`.

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

### Optimised ASan and UBSan

`conan/profiles/gcc-26-asan-release` uses the `gcc-26` compiler tuple. Dependency package ids stay Release, including ONNX Runtime. Nesso itself is RelWithDebInfo, so the compile is `-O2 -g -DNDEBUG` plus `-D_GLIBCXX_ASSERTIONS`, `-fsanitize=address,undefined`, `-fno-sanitize-recover=all`, and `-fno-omit-frame-pointer`. There is no `-flto` and no `-Werror`. On a low-RAM host, pass `-c user.nesso:link_jobs=1` on **both** commands. The binaries are under `build/RelWithDebInfo`.

```bash
conan install . -pr:h ./conan/profiles/gcc-26-asan-release -pr:b default \
  --lockfile=conan.lock --build=missing -c user.nesso:link_jobs=1
conan build . -pr:h ./conan/profiles/gcc-26-asan-release -pr:b default \
  --lockfile=conan.lock --build=missing -c user.nesso:link_jobs=1
./scripts/fetch-model
ctest --test-dir build/RelWithDebInfo --output-on-failure -j "$(nproc)" -L unit
ctest --test-dir build/RelWithDebInfo --output-on-failure -j "$(nproc)" -L integration
ctest --test-dir build/RelWithDebInfo --output-on-failure -j "$(nproc)" -L functional
```

### Clang ASan and UBSan

`conan/profiles/clang-26-asan` compiles and links nesso with Clang 23.1.6 and libstdc++. Dependency package ids match `gcc-26-debug` (`compiler=gcc`, `compiler.version=15`, Debug, ONNX Runtime Release), so `--no-remote` inside the build image reuses that cache. Consumer flags are `-O1 -g`, `-fno-omit-frame-pointer`, `-D_GLIBCXX_ASSERTIONS`, and `-fsanitize=address,undefined,float-divide-by-zero,implicit-conversion,local-bounds` with `-fno-sanitize-recover=all`. The profile does not define `_GLIBCXX_DEBUG`, does not enable `unsigned-integer-overflow` or MemorySanitizer, and does not pass `-Werror`. It is not a build-env image hash input. Clang's `implicit-integer-sign-change` check reports `size_t __prec = -1` in libstdc++ `<format>` (`_M_get_precision`). That function is inlined into the caller, so the profile passes `-fsanitize-ignorelist=conan/sanitizer/libstdcxx-format.ignorelist`. The path is relative to the recipe root. `conanfile.py` rewrites it to an absolute path, because CMake's compiler check runs under `build/<Config>/CMakeFiles/CMakeScratch` and would not find a path relative to `build/<Config>`. The list ignores only that check in the `<format>` header. Other checks, including `implicit-integer-sign-change` in nesso's own files, stay on, so functional tests that print a score run. On a low-RAM host, pass `-c user.nesso:link_jobs=1` on both commands. This profile and `gcc-26-debug` both write `build/Debug`; reconfigure when switching.

```bash
conan install . -pr:h ./conan/profiles/clang-26-asan -pr:b default \
  --lockfile=conan.lock --build=missing -c user.nesso:link_jobs=1
conan build . -pr:h ./conan/profiles/clang-26-asan -pr:b default \
  --lockfile=conan.lock --build=missing -c user.nesso:link_jobs=1
./scripts/fetch-model
ctest --test-dir build/Debug --output-on-failure -j "$(nproc)" -L unit
ctest --test-dir build/Debug --output-on-failure -j "$(nproc)" -L integration
ctest --test-dir build/Debug --output-on-failure -j "$(nproc)" -L functional
```

### ThreadSanitizer

`conan/profiles/clang-26-tsan` matches `clang-26`: Clang 23.1.6, libstdc++, and `compiler=gcc` so the Release package ids stay the ones baked for `gcc-26`. It adds `-g`, `-fno-omit-frame-pointer`, `-fsanitize=thread`, and `-fno-sanitize-recover=all`. There is no `-flto` and no `-Werror`. The binaries are under `build/Release` (the same directory as `clang-26`).

`ctest -L tsan` runs `top_k_test`, `scan_threads_test`, and `inference_wave_test`. `session_pool_test` is not in that label: it opens ONNX Runtime sessions, and those threads are not instrumented.

```bash
conan install . -pr:h ./conan/profiles/clang-26-tsan -pr:b default \
  --lockfile=conan.lock --build=missing
conan build . -pr:h ./conan/profiles/clang-26-tsan -pr:b default \
  --lockfile=conan.lock --build=missing
ctest --test-dir build/Release --output-on-failure -L tsan
```

Pipeline runs `clang-26-tsan-amd64` (`ctest -L tsan`) on the amd64 image with `--no-remote`. [`.github/workflows/clang-asan-nightly.yml`](../.github/workflows/clang-asan-nightly.yml) runs `gcc-26-asan-release-amd64` and `clang-26-asan-amd64` (unit, integration, and functional) and the Clang Release lane (`build-clang-26-amd64`, then `test-clang-26-amd64` with the same three labels). That workflow is the nightly schedule and `workflow_dispatch`. Pull requests and pushes to `main` do not run it.

The binary is `build/Debug/src/nesso`, `build/Release/src/nesso`, or `build/RelWithDebInfo/src/nesso`. For CI-strict configure (fail instead of skip when the model or Go toolchain is missing), set `NESSO_REQUIRE_MODEL=1` and `NESSO_REQUIRE_BDD=1` on the `conan build` environment, as Pipeline does.

## Compile options

`nesso_options` is an INTERFACE library in `CMakeLists.txt`. Every nesso target links it, including tests, the fuzz harnesses, and the benchmark. It requests C++26 (`cxx_std_26`), turns compiler extensions off, and adds the warning set below for GCC and Clang. Those options are usage requirements of the nesso targets. They are not applied when ONNX Runtime, Catch2, CLI11, or nlohmann-json is built. Profile optimization, sanitizer, and LTO flags stay in `user.nesso:cxxflags` for the same reason.

`-Werror` is off unless `NESSO_WERROR` is ON (`-DNESSO_WERROR=ON` for a plain CMake build). No profile sets it. `conanfile.py` turns it on when the command passes `-c user.nesso:werror=True`. `conan build` writes the toolchain again, so omit the flag there and the build comes back without `-Werror`. `NESSO_WERROR=1` makes `scripts/code-coverage` pass that `-c` on `conan build` and `scripts/fuzz-test` pass it on `conan install`. The default is off.

`NESSO_LINK_JOBS` caps concurrent Ninja link jobs when it is a positive integer (`user.nesso:link_jobs` from Conan, or the environment for a plain CMake configure). Unset leaves link parallelism unlimited, which is what CI uses. Compile jobs stay unlimited. A single LTO link can still spawn many codegen threads under `-flto=auto`; packaging and ship-faithful local builds use `NESSO_REPRODUCIBLE=1` so that becomes `-flto=1`.

`conan/profiles/gcc-26-debug` and `conan/profiles/gcc-26-asan-release` pass `-fno-sanitize-recover=all` with `-fsanitize=address,undefined`. ASan and UBSan stop on the first error without `UBSAN_OPTIONS`. `conan/profiles/clang-26-tsan` does the same for ThreadSanitizer. `conan/profiles/clang-26-asan` does the same for Clang, and adds `float-divide-by-zero`, `implicit-conversion`, and `local-bounds`. Its ignorelist drops only `implicit-integer-sign-change` in the libstdc++ `<format>` header (`conan/sanitizer/libstdcxx-format.ignorelist`).

`conan/profiles/gcc-26-debug` and `conan/profiles/code-coverage` pass `-D_GLIBCXX_ASSERTIONS`. Those checks are inline libstdc++ preconditions and do not change the layout of standard containers. Neither profile passes `-D_GLIBCXX_DEBUG` or `-D_GLIBCXX_DEBUG_PEDANTIC`. `user.nesso:cxxflags` is not applied when Catch2, abseil, or ONNX Runtime is built, and those packages keep one shared Debug package ID. A debug-mode container must not cross that boundary.

`conan/profiles/gcc-26`, `conan/profiles/gcc-26-armv8`, `conan/profiles/gcc-26-el9`, and `conan/profiles/gcc-26-el9-armv8` pass hardening through those same consumer lists. Compile flags are `-U_FORTIFY_SOURCE`, `-D_FORTIFY_SOURCE=3`, `-fstack-protector-strong`, `-fstack-clash-protection`, `-ftrivial-auto-var-init=zero`, and `-fPIE`. The executable link adds `-pie` and `-Wl,-z,relro,-z,now,-z,noexecstack`. x86_64 adds `-fcf-protection=full` on compile and link. armv8 adds `-mbranch-protection=standard` on compile and link. `user.nesso:sharedlinkflags` carries the same link flags except `-pie`. This is not `-fhardened`. `-U_FORTIFY_SOURCE` is first so a toolchain spec that already defines the macro (Ubuntu injects level 3 under `-O`, and skips that inject when `-U` is present) cannot leave a different level in place.

GCC and Clang:

`-Wall` `-Wextra` `-Wpedantic` `-Wconversion` `-Wsign-conversion` `-Wshadow` `-Wformat=2` `-Wold-style-cast` `-Wnon-virtual-dtor` `-Woverloaded-virtual` `-Wnull-dereference` `-Wdouble-promotion` `-Wimplicit-fallthrough` `-Wcast-align` `-Wextra-semi`

GCC only:

`-Wduplicated-cond` `-Wduplicated-branches` `-Wlogical-op` `-Wuseless-cast`

Inside the `nesso-build-env` image (`CONAN_HOME=/opt/conan2`) keep `--lockfile=conan.lock` and `--no-remote`. Outside that image, drop `--no-remote`.

The black-box specs in [test/functional/](../test/functional/) run the `nesso` binary through [godog](https://github.com/cucumber/godog) as the `bdd` ctest case. Go 1.24+ is a test-only dependency. Without it CMake skips `bdd`, and `NESSO_REQUIRE_BDD=1` turns that into a configure error (CI sets it).

Unit, integration, and functional tests are separate ctest labels. CI runs unit and integration in one job per lane. Functional shares that job on gcc Release and arm64, and has its own gcc Debug job. The nightly Clang Release lane runs functional in the same job as unit and integration:

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

See [.github/workflows/ci.yml](../.github/workflows/ci.yml) for `lint`, `clang-tidy`, and `cppcheck`, the gcc Release, gcc Debug, and arm64 builds, unit, integration, and functional tests, coverage, and the fp32 determinism gate. The `coverage` job publishes the cleaned lcov trace to Codecov on pull requests and pushes to `main`. The README badge reads that report. AFL++ fuzzing outside Pipeline is [`.github/workflows/fuzz-test.yml`](../.github/workflows/fuzz-test.yml) (pushes to `main`, nightly, `workflow_dispatch`); pull requests are fuzzed by [ClusterFuzzLite](../.github/workflows/cifuzz.yml). The nightly real-log benchmark, which gates peak RSS, is [`.github/workflows/bench-real-log.yml`](../.github/workflows/bench-real-log.yml). The Clang Release lane and the optimized ASan lanes are [`.github/workflows/clang-asan-nightly.yml`](../.github/workflows/clang-asan-nightly.yml) (schedule and `workflow_dispatch`). The pipeline runs on pull requests, on pushes to `main`, and nightly. Require the `CI gate` check (`ci-gate` in that workflow) instead of listing every job. [.github/workflows/release-drafter.yml](../.github/workflows/release-drafter.yml) drafts the next release from `version` in `conanfile.py`. Publishing the `vX.Y.Z` draft creates the tag, and tag `v*` publishes via [.github/workflows/release.yml](../.github/workflows/release.yml). Weekly [CodeQL](../.github/workflows/codeql.yml) (`c-cpp`, `build-mode: none`) and [OpenSSF Scorecard](../.github/workflows/scorecard.yml) run on `main`.

Benchmark commands are in [benchmarks.md](benchmarks.md). The embedding gate is in [determinism.md](determinism.md).
