---
status: "accepted"
date: "2026-10-26"
decision-makers: Lorenzo Caprari
---

# Release amd64 and arm64 Linux

## Context and Problem Statement

[ADR 0006](0006-supported-platforms-and-toolchains.md) kept arm64 as a CI gate and published only an amd64 `.deb` and an amd64 runtime image. Native arm64 Release builds already block Pipeline (`conan/profiles/gcc-26-armv8` on `ubuntu-24.04-arm`), and the arm64 `nesso-build-env` image is published. What should a `vX.Y.Z` tag publish now?

Distance on arm64 is still the scalar path. `src/core/CMakeLists.txt` compiles `distance_avx2.cpp` only for x86_64. Cross-architecture corpus equality is not measured.

## Decision Drivers

* A tag must not replace `:latest` with an arm64-only image.
* arm64 bytes are built on `ubuntu-24.04-arm`, not under QEMU.
* Both architectures get the same `.deb` reproducibility check, distro install check, SBOM, checksum, cosign signature, and SLSA provenance.
* Ordinary pull requests stay on the path-filtered Package workflow. arm64 packaging runs in parallel with amd64.
* Clang 21 stays a CI compiler. It is not a release compiler.

## Considered Options

* Publish an arm64 `.deb` and a multi-arch manifest list
* Keep tags amd64-only until arm64 has SIMD kernels and a cross-arch corpus gate
* Publish arm64 as a separate image tag and leave `:latest` amd64-only

## Decision Outcome

Chosen option: "Publish an arm64 `.deb` and a multi-arch manifest list", because the native arm64 Release build, the arm64 build-env image, and the amd64 trust machinery already exist. Scalar distance and the missing cross-arch corpus comparison are follow-ups, not a reason to ship an image that arm64 hosts cannot run.

`scripts/package-deb` names the package from the binary's ELF `e_machine` (`amd64` or `arm64`). `scripts/build-release-binary` selects `conan/profiles/gcc-26` or `conan/profiles/gcc-26-armv8` from `uname -m`. `.github/workflows/release.yml` builds each runtime image on its native runner, pushes `vX.Y.Z-amd64` and `vX.Y.Z-arm64`, and `scripts/publish-runtime-index` points `vX.Y.Z`, `X.Y.Z`, and `latest` at one manifest list. Keyless cosign signs the index digest and each platform digest. One generic SLSA attestation covers both `.deb` files and both SPDX SBOMs. The container generator attests the index and each platform digest.

This supersedes ADR 0006. Clang 21 with libstdc++ (`conan/profiles/clang-26`) still gates Pipeline and is not published.

### Consequences

* Good, because `docker pull` of the release tag selects the host architecture, and an arm64 machine can `apt install` `nesso_<version>_arm64.deb`.
* Good, because Package compares two arm64 `.deb` builds and installs that package on Ubuntu 26.04 and 26.10 on `ubuntu-24.04-arm`.
* Bad, because arm64 search still uses the scalar distance loop, so arm64 ranking speed is not the x86_64 AVX2 path.
* Bad, because int8 and fp32 corpus bytes are gated per architecture and are not compared across architectures.

## Pros and Cons of the Options

### Publish an arm64 `.deb` and a multi-arch manifest list

* Good, because both published digests are built natively and covered by the same signature and provenance story as amd64.
* Good, because `:latest` stays one tag and does not become arm64-only.
* Bad, because the arm64 binary does not yet have ARM SIMD kernels.
* Bad, because a numerical difference between the architectures would not fail the release.

### Keep tags amd64-only until arm64 has SIMD kernels and a cross-arch corpus gate

* Good, because the published binary would match the performance and equality story ADR 0006 asked for.
* Bad, because arm64 CI would keep producing a binary users cannot install.
* Bad, because the runtime image would stay unusable on arm64 hosts.

### Publish arm64 as a separate image tag and leave `:latest` amd64-only

* Good, because an existing amd64 `:latest` pull would not start resolving an index.
* Bad, because arm64 users would need a different tag, and `cosign verify` of `:latest` would not cover the arm64 image.
