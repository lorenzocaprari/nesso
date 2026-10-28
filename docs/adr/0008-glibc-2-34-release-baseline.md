---
status: "proposed"
date: "2026-10-28"
decision-makers: Lorenzo Caprari
---

# Build release binaries on a glibc 2.34 baseline

## Context and Problem Statement

[ADR 0007](0007-release-amd64-and-arm64.md) publishes amd64 and arm64 `.deb` files and a multi-arch image. They are built in `nesso-build-env` on `ubuntu:26.04` with Ubuntu GCC 15. The published v0.2.1 amd64 and arm64 binaries reference `GLIBC_2.43`, `GLIBC_2.38`, `GLIBCXX_3.4.34`, and `CXXABI_1.3.15`. `scripts/package-deb` turns the highest `GLIBC_` version into `Depends: libc6 (>= ...)`, and both packages are `libc6 (>= 2.43), libstdc++6, libgomp1`. Those binaries do not list libgomp in `NEEDED` and have no `GOMP_` references. ONNX Runtime is static, so `libonnxruntime.so` is absent. The Package workflow tests installs only on Ubuntu 26.04 and 26.10. Ubuntu 22.04 ships glibc 2.35, Ubuntu 24.04 ships glibc 2.39, and RHEL 9 and Rocky Linux 9 ship glibc 2.34 with GCC 11's libstdc++ (`GLIBCXX_3.4.29`, `CXXABI_1.3.13`). RHEL 9.7 backports `GLIBC_2.35` libc symbols, including `__memcmpeq`, `_dl_find_object`, and `epoll_pwait2`. The loader's `__rseq_*` symbols are `GLIBC_2.35` in every Rocky 9 minor. The gate rejects every `GLIBC_2.35` name, so the floor is any EL9 minor (9.0+). How do we keep GCC 15 and C++26, run on those systems, and give RHEL-family hosts a package?

## Decision Drivers

* One release binary per architecture, gated by today's determinism, reproducibility, signature, and provenance checks.
* GCC 15 with `compiler.cppstd=26`. An amd64 code pull request catches a symbol regression. arm64 is gated in Package, not on every pull request.
* No QEMU; arm64 stays native.

## Considered Options

* Build in an EL9 image with GCC Toolset 15
* Keep `ubuntu:26.04` and link a glibc 2.34 sysroot
* Build on `ubuntu:22.04` with GCC 15 built from source
* Link fully static (glibc static, or musl)
* Use the `manylinux_2_34` image

## Decision Outcome

Chosen option: "Build in an EL9 image with GCC Toolset 15", because its glibc (2.34) is the floor of every target and its GCC 15 is a vendor package. Rocky 9 AppStream ships `gcc-toolset-15-gcc-c++-15.2.1` for x86_64 and aarch64. The toolset's `libstdc++.so` is a linker script that takes symbols newer than the system library from `libstdc++_nonshared.a`, so the dynamic dependency is meant to stay at `GLIBCXX_3.4.29`. `libstdc++` and `libgcc_s` stay dynamic through the toolset linker scripts. No `-static-libgcc`: on x86_64, `unwind-dw2-fde-dip.o` in `gcc-toolset-15-gcc-15.2.1-7.1.el9_8` has an undefined `_dl_find_object`, which is `GLIBC_2.35`-only on EL9. aarch64 not inspected.

* **Images.** One EL9 build-env image per architecture. `prepare-image-el9-amd64` and `prepare-image-el9-arm64` run in the Pipeline and join CI gate's `needs`; `scripts/build-env-hash` gains `el9-amd64` and `el9-arm64`; Package and Release wait for both EL9 images. The Rocky base is pinned by digest. The GCC toolset is pinned to a full version. Dependabot ignores semver-major on that base. Digest-pinning the current arm64 Ubuntu image is a follow-up, out of scope here. Each image installs a pinned CMake (this project requires 4.2; EL9 ships 3.31), ninja-build from CRB (`dnf config-manager --set-enabled crb`), and python3.12 (the scripts need 3.10 or newer). Profiles `gcc-26-el9` and `gcc-26-el9-armv8` point at the toolset compilers. Separate per-image Conan caches keep EL9 and Ubuntu binaries apart. If a shared remote is ever added, add `os.distro` via `settings_user.yml`.
* **Symbol gate.** The release binary fails when it references a version above `GLIBC_2.34`, `GLIBCXX_3.4.29`, or `CXXABI_1.3.13`, including every `GLIBC_2.35` name, or a version its EL9 library does not define. `NEEDED` is exactly `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libc.so.6`, and the dynamic loader. `libonnxruntime.so` must not appear. The `libgomp1` dependency is dropped from `scripts/package-deb` and from the apt line in `packaging/Dockerfile.runtime`. The gate is bash or Go; Go tests in `test/scripts`.
* **Pipeline.** `build-el9-amd64` is required through CI gate: it joins that job's `needs`, and the ruleset keeps CI gate as the only required check. It runs only when code changes. It builds the release binary, runs the symbol gate, and runs `nesso -V` and the unit tests in the Rocky container. There is no arm64 EL9 pull-request lane. arm64 is gated by the symbol gate in Package, not on every pull request. The other lanes stay on `ubuntu:26.04`.
* **Packages.** The binary is built in EL9 and the `.deb` depends on `libc6 (>= 2.34)`. EL9 has no `dpkg-deb`, so `scripts/package-deb` runs on the Ubuntu runner, in Package `repro-build` and the Release `image` job, on the EL9-built binary; the EL9 image is the runtime image's `BUILD_IMAGE`. RPMs are `nesso-<version>-1.x86_64.rpm` and `nesso-<version>-1.aarch64.rpm`. They are built with `rpmbuild`, `SOURCE_DATE_EPOCH`, clamped mtimes, and a fixed build host, and compared the way the `.deb` is. Package installs the `.deb` on Ubuntu 22.04, 24.04, 26.04, and 26.10, and the `.rpm` on Rocky Linux 9 and UBI 9, on both architectures. Each RPM gets an SBOM, a `SHA256SUMS` line, and an SLSA provenance subject. RPMs are not GPG-signed; trust is the SLSA provenance and `SHA256SUMS`, like the `.deb`.

This extends ADR 0007: architectures, runners, and trust machinery are unchanged. ADR 0006 stays superseded.

### Consequences

* Good, because one binary per architecture runs on glibc 2.34 and newer, and a newer dependency fails the amd64 pull request that adds it.
* Good, because `dnf install ./nesso-*.rpm` resolves glibc from RPM's automatic `Requires`.
* Bad, because the release compiler (toolset GCC 15.2.1) differs from the Pipeline compiler (Ubuntu GCC 15.2.0), so determinism must also run on the release binary.
* Bad, because libm is dynamic. Floats may differ between glibc 2.34 and 2.43, so corpus bytes are gated per distro, not across distros.
* Bad, because every Conan dependency, ONNX Runtime included, is rebuilt in a second image per architecture.
* The implementing pull requests update the README, `docs/releases.md`, the RPM section of `docs/verifying.md`, and `AGENTS.md`.

### Confirmation

* CI gate fails when `build-el9-amd64`, either EL9 image job, or the symbol gate fails.
* Package fails when two `.deb` or two `.rpm` builds differ, or when install, `nesso -V`, the smoke grep, remove, or purge fails on any listed distro.
* `scripts/check-determinism` runs the release binary on Ubuntu 22.04 and Rocky Linux 9.

## Pros and Cons of the Options

* EL9 with GCC Toolset 15: good, glibc 2.34 and the x86-64-v2 ISA that `gcc-26` already sets (`-march=x86-64-v2`); bad, C++26 under `libstdc++_nonshared` is untested until the Rocky unit tests run.
* glibc 2.34 sysroot on `ubuntu:26.04`: good, one image; bad, we would maintain the sysroot for every Conan recipe.
* `ubuntu:22.04` with GCC 15 from source: bad, glibc 2.35 is above EL9, and we would own the compiler.
* Fully static: good, no runtime library dependency; bad, static glibc breaks `dlopen` and NSS, and musl needs a second toolchain and changes libm floats.
* `manylinux_2_34`: good, a maintained EL9 (AlmaLinux 9) image; bad, its README marks it alpha and lists GCC 14 as its toolchain ([pypa/manylinux README](https://github.com/pypa/manylinux/blob/main/README.rst)), while every profile pins `compiler.version=15`.
