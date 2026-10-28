---
status: "proposed"
date: "2026-10-28"
decision-makers: Lorenzo Caprari
---

# Build release binaries on a glibc 2.34 baseline

## Context and Problem Statement

[ADR 0007](0007-release-amd64-and-arm64.md) publishes amd64 and arm64 `.deb` files and a multi-arch image from `nesso-build-env` (`ubuntu:26.04`, Ubuntu GCC 15). `conan/profiles/gcc-26` and `conan/profiles/gcc-26-armv8` set `compiler.version=15` and `compiler.cppstd=26`. `scripts/build-release-binary` selects the profile from `uname -m`.

Both [v0.2.1](https://github.com/lorenzocaprari/nesso/releases/tag/v0.2.1) packages depend on `libc6 (>= 2.43), libstdc++6, libgomp1`. `scripts/package-deb` sets the libc6 floor from the highest `GLIBC_` version. Each binary references `GLIBC_2.38`, `GLIBC_2.43`, `GLIBCXX_3.4.34`, and `CXXABI_1.3.15`, and `NEEDED` is libstdc++, libm, libgcc_s, libc, and the loader (`/lib64/ld-linux-x86-64.so.2` on amd64, `/lib/ld-linux-aarch64.so.1` on arm64). Package installs that `.deb` only on Ubuntu 26.04 and 26.10.

Ubuntu 24.04 publishes libc6 2.39, so `libc6 (>= 2.43)` does not install there, and Debian 13 publishes libc6 2.41. Ubuntu 22.04 publishes libc6 2.35. UBI 9 and Rocky Linux 9 publish glibc 2.34 on x86_64 and aarch64. `libstdc++-11.5.0-14.el9` defines nothing newer than `GLIBCXX_3.4.29` and `CXXABI_1.3.13`.

How do we keep GCC 15 and C++26, run on those systems, and publish a package for RHEL-family hosts?

## Decision Drivers

* One release binary per architecture, still under the determinism check and the reproducibility, signature, and provenance checks ADR 0007 requires.
* GCC 15 with `compiler.cppstd=26`. A raised symbol version fails the code pull request that introduces it.
* No QEMU. arm64 stays native.

## Considered Options

* Build in an EL9 image with GCC Toolset 15
* Keep `ubuntu:26.04` and link a glibc 2.34 sysroot
* Build on `ubuntu:22.04` with GCC 15 built from source
* Link fully static (glibc static, or musl)
* Use the `manylinux_2_34` image

## Decision Outcome

Chosen option: "Build in an EL9 image with GCC Toolset 15", because glibc 2.34 is the oldest of those hosts and Rocky 9 AppStream publishes `gcc-toolset-15-gcc-c++-15.2.1-7.1.el9_8` for x86_64 and aarch64. On both, `libstdc++.so` is a linker script whose `INPUT` line is `/usr/lib64/libstdc++.so.6 -lstdc++_nonshared`, plus `AS_NEEDED` of that shared library. A link that succeeds cannot require a newer `GLIBCXX_` or `CXXABI_` version. C++26 through that archive is untested.

* **Images and profiles.** One EL9 image per architecture, `FROM` a Rocky Linux 9 index pinned by digest, toolset major pinned, `gcc --version` recorded as a label. Profiles `gcc-26-el9` and `gcc-26-el9-armv8` point at the toolset, and `scripts/build-release-binary` selects them there. An EL9 marker enters the package ID through `tools.info.package_id:confs`.
* **Symbol gate.** In the EL9 image that script fails when a referenced version is above `GLIBC_2.34`, `GLIBCXX_3.4.29`, or `CXXABI_1.3.13`; when any version, `GCC_` and `GOMP_` included, is missing from the EL9 library it comes from; when a `NEEDED` entry is outside libc, libm, libstdc++, libgcc_s, libgomp, and the loader; or when the interpreter is not a v0.2.1 path above. libgomp stays allowed because the `.deb` already depends on `libgomp1`, which v0.2.1 does not list as `NEEDED`.
* **Pipeline.** Code pull requests gain one required lane, `build-el9-amd64`: the release binary and the symbol gate. Other lanes stay in the Ubuntu 26.04 `nesso-build-env` images.
* **Packages.** That binary ships in the `.deb`, the runtime image, and a reproducible `rpmbuild` `.rpm`. The libc6 floor is what `scripts/package-deb` derives, at most 2.34, and `libstdc++6` and `libgomp1` stay. Package installs the `.deb` on Ubuntu 22.04, 24.04, 26.04, and 26.10, and the `.rpm` on Rocky Linux 9 and UBI 9, both architectures. Each RPM gets an SBOM, a `SHA256SUMS` line, and an SLSA provenance subject, as the `.deb` does. RPMs are not GPG-signed until there is a dnf repository.

This extends ADR 0007. Published architectures stay amd64 and arm64, and that record's `.deb` and image checks stay. ADR 0006 stays superseded.

### Consequences

* Good, because one binary per architecture runs on glibc 2.34 and newer, and a newer dependency fails the pull request that adds it.
* Good, because `rpmbuild` records glibc symbol versions as automatic Requires, so `dnf install` of the `.rpm` can resolve them.
* Bad, because release GCC is Toolset 15.2.1 and Ubuntu 26.04's `gcc-15` package is 15.2.0, so determinism has to run on the release binary.
* Bad, because libm stays dynamic. Floats may differ between glibc 2.34 and 2.43, so corpus bytes are gated per distro, not across distros.
* Bad, because every Conan dependency, including `onnxruntime/1.18.1`, is rebuilt in a second image per architecture.

### Confirmation

* Pipeline fails when `build-el9-amd64` or its symbol gate fails.
* Package fails when two `.deb` or two `.rpm` builds differ, or when install, `nesso -V`, or the smoke grep fails on a listed distro. The `.deb` also fails on `apt-get remove` or `apt-get purge`, which `scripts/smoke-deb` already checks.
* `scripts/check-determinism` runs the release binary on Ubuntu 22.04 and Rocky Linux 9.

## Pros and Cons of the Options

* EL9 with GCC Toolset 15: good, glibc 2.34 and the x86-64-v2 ISA `conan/profiles/gcc-26` sets with `-march=x86-64-v2`; bad, C++26 under `libstdc++_nonshared` is untested here.
* A glibc 2.34 sysroot on `ubuntu:26.04`: good, one image; bad, the sysroot has to be carried for every Conan recipe.
* `ubuntu:22.04` with GCC 15 from source: bad, glibc 2.35 is above EL9, and the project would own the compiler.
* Fully static: good, no runtime library dependency; bad, static glibc breaks `dlopen` and NSS, and musl needs a second toolchain and a different libm.
* `manylinux_2_34`: good, a maintained AlmaLinux 9 image; bad, its README marks that image alpha and names GCC 14 as the toolchain ([pypa/manylinux README](https://github.com/pypa/manylinux/blob/main/README.rst)), while every profile pins `compiler.version=15`.
