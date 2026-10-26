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

`bdd` runs godog scenarios concurrently, one per logical CPU. Each scenario has its own temporary directory, environment, and pty. CI passes `-j "$(nproc)"` and `--output-on-failure` to every ctest invocation. The test jobs also pass `--output-junit` and list failing tests in the job summary.

Debug ctest in CI also sets `ASAN_OPTIONS` and `UBSAN_OPTIONS` so sanitizer findings abort the job.

## Fuzz

`./scripts/fuzz-test` builds AFL++ LTO and ASan harnesses `nesso_fuzz_parser` and `nesso_fuzz_tokenizer` with `conan/profiles/gcc-26-fuzz`, then runs `afl-fuzz`. Seeds live in `test/fuzz/seeds/parser` and `test/fuzz/seeds/tokenizer`. `NESSO_FUZZ_MAX_TOTAL_TIME` defaults to 60 seconds per target; CI uses 60. The script needs `afl-clang-lto++`, `llvm-ar` (`llvm-ar-21`), and `llvm-ranlib` (`llvm-ranlib-21`), which the amd64 build-env image provides along with `ld.lld-21`. The Pipeline job does not install them. It runs on pushes to `main`, on the nightly schedule, and on `workflow_dispatch`. Pull requests are fuzzed by ClusterFuzzLite (`.github/workflows/cifuzz.yml`).

## Lint and the local gate

```bash
pre-commit install
bash scripts/lint
bash scripts/actionlint
bash scripts/zizmor
```

`scripts/lint` runs pre-commit on the whole tree, `scripts/check-versions`, and `scripts/check-agent-docs`. Hooks in `.pre-commit-config.yaml` fix trailing whitespace and line endings, reject merge markers and large files, check YAML, and run clang-format (LLVM, Allman braces, indent 4, column 120) and cmake-format. `scripts/lint --format-only` runs clang-format and cmake-format. `scripts/lint --hygiene-only` runs the other hooks. cppcheck is `scripts/cppcheck` after a Release configure, not part of `scripts/lint`. `scripts/actionlint` and `scripts/zizmor` are not part of `scripts/lint` either. Each downloads a pinned Linux binary (actionlint 1.7.12, zizmor 1.31.1), checks sha256, and lints `.github/workflows`. actionlint does not invoke shellcheck or pyflakes. zizmor uses the regular persona. It runs online audits when `GH_TOKEN` or `GITHUB_TOKEN` is set, and `--offline` otherwise. The `lint` job runs format, hygiene, versions, agent docs, actionlint, and zizmor on the runner and does not use the build image. On `pull_request` it also runs dependency review in that job, so the check name stays `lint`. The config is `.github/dependency-review-config.yml`: fail on severity `moderate` and above, across runtime, development, and unknown scopes, and on a detected license outside the Apache-2.0 allow list. GitHub's graph for this repo is Actions, `test/functional/go.mod`, and `.devcontainer/requirements.txt`. `conanfile.py` and `conan.lock` are not in the graph. `test/scripts/go.mod` has no requirements. Pushes to `main`, the nightly schedule, and manual runs skip the dependency-review step. `clang-tidy` and `cppcheck` stay separate jobs in the image, each after one Release configure.

The README local gate, before pushing:

```bash
bash scripts/lint
bash scripts/actionlint
bash scripts/zizmor
./scripts/code-coverage conan/profiles/code-coverage
```

Coverage floors are the `REQUIRED_*` values in `scripts/code-coverage`. On a pull request, `coverage` also fails when fewer than 80% of the changed executable lines under `src/` are covered (`scripts/ci` runs `patch-coverage` on the lcov trace the floor already wrote). The job summary lists those lines as `file:line`. A diff with no executable `src/` lines passes. Other events keep the tree-wide report only. The Pipeline workflow (`.github/workflows/ci.yml`) is the merge gate. Job ids are the check names: `lint`, `clang-tidy`, `cppcheck`, `build-gcc-26-amd64`, `build-gcc-26-debug-amd64`, `build-clang-26-amd64`, `build-gcc-26-arm64`, `test-gcc-26-amd64`, `test-gcc-26-debug-amd64`, `test-gcc-26-debug-amd64-functional`, `test-clang-26-amd64`, `test-gcc-26-arm64`, `coverage`, `bench`, and `determinism`. `lint` does not use the build image. Its steps are format, hygiene, versions, agent docs, actionlint, zizmor, and, on pull requests, dependency review. Each `test-*` job runs the unit and integration labels. Functional joins that job except on gcc Debug, where it is `test-gcc-26-debug-amd64-functional`. `determinism` gates fp32 and the default int8 policy; int8 with `NESSO_THREADS` set to the logical CPU count is report-only. Pull requests set `NESSO_DETERMINISM_MODE=pr` (256-line input, 2 runs, 2 index runs). Pushes to `main`, the nightly schedule, manual runs, and the tag release keep the full 3500-line gate (`--runs 10`, `--index-runs 3`). The `bench` job fails when a tier regresses against the latest baseline saved from `main`. Pull requests run the 1k and 10k tiers and append an informational comparison with `gh-pages` history to that job's summary (`scripts/ci` runs `bench-history` on the result file). That comparison does not fail the job. Pushes to `main` and the nightly schedule also run 100k. A push to `main` records the result from the `bench-history` job. That job does not run on pull requests and is not a merge check. `fuzz-test` runs on pushes to `main` and nightly. A pull request that changes only `*.md`, `docs/`, `LICENSE`, or `.github/ISSUE_TEMPLATE/` skips the build, test, coverage, bench, determinism, `clang-tidy`, and `cppcheck` jobs. `lint` still runs, and its agent-docs step runs `scripts/check-agent-docs`. Pushes to `main`, the nightly schedule, and manual runs do not skip.

## Pull requests

Title format: `type(scope): subject` or `type: subject`. Types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`. A `!` immediately before `:` is a breaking change (`feat(core)!: ...`). `.github/workflows/pr-title.yml` enforces this.

The only author on commits and pull requests is Lorenzo Caprari <caprarilorenzo5@gmail.com>. No `Co-authored-by`, `Signed-off-by`, or other agent or tool trailers.

Every GitHub Action is pinned to a commit SHA with a `# vX.Y.Z` comment. New or bumped actions keep that. `.github/workflows/release.yml` calls the SLSA provenance generators by a `vX.Y.Z` tag. Those reusable workflows check their own ref, so a SHA pin fails the release. OpenSSF Scorecard's Pinned-Dependencies check flags the two tag refs. That is the documented exception. The Scorecard workflow uploads the result and does not fail the job on that finding. zizmor keeps the `unpinned-uses` rule on and ignores those four `uses:` lines. The same-repo call `uses: ./.github/workflows/package.yml` stays in that form: actionlint 1.7.12 rejects the `$/` self-repository syntax.

Host jobs run StepSecurity Harden-Runner v2.22.2 as the first step, with `egress-policy: audit`. Jobs that set `container:` are skipped, because those steps run inside the container and the agent has to install on the runner. The Package `verify-package` caller has no steps of its own. The SLSA generator workflows are the tag-pin exception above and are not edited. Audit mode records outbound calls for a later allow-list. It still blocks StepSecurity's indicator list. The job log and the job summary link to the run's insights on `https://app.stepsecurity.io`. Telemetry (network destinations, process metadata, file-write paths, workflow metadata) goes to StepSecurity in us-east-1 and us-west-2. It does not include file contents or secret values. `disable-telemetry` is unavailable while the policy is audit.

Package job `scan-runtime` and the Release `image` job scan the runtime image with Trivy 0.76.1 (`scripts/trivy` checks the GitHub release sha256; the March 2026 compromise of `trivy-action`, `setup-trivy`, `get.trivy.dev`, and the Docker Hub tags is why those channels are not used). The vulnerability DB is `ghcr.io/aquasecurity/trivy-db:2`, cached per UTC day. SARIF is uploaded to code scanning. `security-events: write` is only on `scan-runtime` and on the Release `image` job. The scan reports OS and library findings and does not fail the job. `scan-runtime (amd64)` and `scan-runtime (arm64)` are Package checks, not Pipeline job ids.

Put user-facing notes under `## [Unreleased]` in [CHANGELOG.md](CHANGELOG.md). On release, move that section to `## [X.Y.Z] - YYYY-MM-DD` in the same commit that bumps the version.

`.github/release-drafter.yml` maps labels to changelog sections. The Release Drafter workflow applies those labels from the PR title. It does not create labels. These already exist: `enhancement` (`feat`), `bug` (`fix`), `documentation` (`docs`), `breaking`, `changed`, `chore`, `deprecated`, `removed`, and `security`. Categories do not choose the version. Do not set `semver-increment` there. On push to `main`, `.github/workflows/release-drafter.yml` reads `version` from [conanfile.py](conanfile.py) with `scripts/check-versions` and passes it as the release-drafter `version` input, so `$RESOLVED_VERSION` is that field.

`scripts/check-versions` reads only `version` in `conanfile.py`. CMake has no project version. `NESSO_VERSION` is copied from that field when the toolchain is generated.

While the git tag for that version already exists, the draft name is `Unreleased (bump conanfile.py to release)` and its tag name is `unreleased`. GitHub requires a tag name on a draft. `unreleased` is not `vX.Y.Z`, so publishing that draft does not create a version tag and does not start `.github/workflows/release.yml`. Do not publish it. After a commit on `main` bumps `version` in `conanfile.py`, the same draft becomes name and tag `vX.Y.Z`.

To release: open one pull request that bumps `version` in `conanfile.py`, moves `## [Unreleased]` in `CHANGELOG.md` to `## [X.Y.Z] - YYYY-MM-DD`, and updates any other file `scripts/check-versions` compares. Today that script only reads `conanfile.py`. Squash-merge it. The draft on `main` becomes `vX.Y.Z`. Publish that draft. Publishing creates the git tag `vX.Y.Z` from your account, so the tag push starts `.github/workflows/release.yml`. That workflow runs `scripts/check-versions` against the tag before it publishes both `.deb` packages, `ghcr.io/lorenzocaprari/nesso`, and the GitHub Release assets. On that tag it also signs the image by digest and attaches the SLSA provenance files. Verification commands are in [README.md](README.md). A tag that does not match `conanfile.py` fails there. CI must already have published `nesso-build-env` for amd64 and for arm64. `.github/workflows/package.yml` builds each architecture's `.deb` twice, on a native runner, and installs it on Ubuntu 26.04 and 26.10. The image jobs wait for that workflow, so a failed check does not push the image or publish the release. The image is a manifest list of the amd64 and arm64 images, tagged `vX.Y.Z`, `X.Y.Z`, and `latest`.

The unpublished draft named `v0.2.0` was opened when Release Drafter bumped from labels. It has no git tag. `conanfile.py` is `0.1.0`, which matches the only tag, `v0.1.0`. Delete that `v0.2.0` draft. Do not publish it. Publishing would create a `v0.2.0` tag that `scripts/check-versions` rejects. The next push to `main` retargets whatever draft is still open to the unreleased name while `0.1.0` is current.

`.github/CODEOWNERS` requests a review from `@lorenzocaprari`.
