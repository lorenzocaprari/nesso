---
status: "accepted"
date: "2026-10-24"
decision-makers: Lorenzo Caprari
---

# Release x86_64 Linux; gate arm64 and Clang in CI

## Context and Problem Statement

Release publishes an amd64 `.deb` and `ghcr.io/lorenzocaprari/nesso`.
`conan/profiles/gcc-26` sets `-march=x86-64-v2`. `src/core/distance.cpp`
selects AVX2+FMA at runtime when the x86_64 CPU supports it, and the
scalar path otherwise. `src/core/CMakeLists.txt` compiles
`distance_avx2.cpp` only for x86_64.
Pipeline also builds arm64 and Clang 21. Which of those are released?

## Decision Drivers

* Released bytes are what `.github/workflows/release.yml` publishes.
* An x86-only or GCC-only assumption fails before merge.
* arm64 is released only with ARM SIMD kernels, int8 and fp32
  embeddings and search results measured on x86_64 against arm64, and
  the same SBOM, checksum, and provenance as amd64.

## Considered Options

* Release arm64 now
* Keep arm64 and Clang as CI-only gates
* Drop the arm64 and Clang jobs

## Decision Outcome

Chosen option: "Keep arm64 and Clang as CI-only gates", because x86_64 Linux
is the only released target. Clang 21 with libstdc++
(`conan/profiles/clang-26`) and native arm64 GCC 15
(`conan/profiles/gcc-26-armv8` on `ubuntu-24.04-arm`) gate Pipeline, with no
`continue-on-error`. Tags stay amd64-only (`release.yml` header). The arm64
driver does not hold, so arm64 is not released.

### Consequences

* Good, because the jobs sit outside the x86 `needs` chain. On cached
  [run 39925771395](https://github.com/lorenzocaprari/nesso/actions/runs/39925771395)
  they finished before debug functional tests. `ubuntu-latest` and
  `ubuntu-24.04-arm` are free standard runners on this public repository
  (<https://docs.github.com/en/actions/reference/runners/github-hosted-runners>).
* Bad, because determinism runs only on the GCC x86_64 Release build
  ([ADR 0003](0003-byte-identical-determinism.md), accepted). No sanitizer
  or coverage build runs on Clang or arm64.
* Bad, because cross-architecture corpus equality needs its own ADR.

## Pros and Cons of the Options

### Release arm64 now

* Good, because the arm64 Release jobs already run in Pipeline.
* Bad, because distance is scalar, cross-arch results are unmeasured,
  and `scripts/package-deb` plus `Dockerfile.runtime` publish amd64 only.

### Keep arm64 and Clang as CI-only gates

* Good, because an x86-only or GCC-only break fails Pipeline
  ([#81](https://github.com/lorenzocaprari/nesso/pull/81): `5dd0ca5`
  profiles, `21e864b` jobs, `1418e35` blocking, `85cbce5` release note).
* Neutral, because `prepare-image-arm64` times out at 360 minutes, so a
  rebuild can become the long pole.
* Bad, because the arm64 binary CI accepts is not published.

### Drop the arm64 and Clang jobs

* Good, because Pipeline would build one architecture and one compiler.
* Bad, because an x86-only or GCC-only break would not fail Pipeline.
