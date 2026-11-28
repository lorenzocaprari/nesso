---
status: "accepted"
date: "2026-11-24"
decision-makers: Lorenzo Caprari
---

# Release a Windows x86_64 zip built with clang-cl

## Context and Problem Statement

Nesso released only Linux amd64/arm64 packages ([ADR 0007](0007-release-amd64-and-arm64.md)). Support engineers also run triage on Windows hosts. What should the first native Windows release look like?

## Decision Drivers

* One offline binary with MiniLM files, matching the Linux “no VCRedist / no shared libs” story as closely as PE allows.
* C++26 and ONNX Runtime must build on a maintainable Windows toolchain.
* Platform I/O stays behind common headers with CMake-selected `*_posix.cpp` / `*_win32.cpp` TUs (no OS `#ifdef` in those impl files).
* Linux EL9 `.deb`/`.rpm`/glibc ABI gates stay unchanged.
* A new Windows lane must not block every Linux pull-request merge until it is proven.

## Considered Options

* Ship a Windows x86_64 zip via clang-cl (static CRT); Package/Release (+ nightly) only
* MSI installer and merge-blocking Pipeline job in the first milestone
* MinGW-w64 or pure MSVC as the release compiler
* Document WSL only; no native Windows binary

## Decision Outcome

Chosen option: "Ship a Windows x86_64 zip via clang-cl (static CRT); Package/Release (+ nightly) only", because clang-cl gives a modern C++ front end on the MSVC ABI, a static CRT zip needs no redistributable, and keeping Windows out of Pipeline `ci-gate` avoids holding Linux merges on a new runner matrix.

`conan/profiles/clang-cl-26` builds `nesso.exe`. `packaging/scripts/package-zip` writes `nesso_<version>_windows_amd64.zip` with `nesso.exe`, `models/`, and notices. `.github/workflows/windows.yml` builds, tests, and packs; Package calls it; Release publishes the zip next to the Linux packages. MSI, Windows arm64, and a merge-blocking Pipeline job are follow-ups.

### Consequences

* Good, because Windows users get the same command surface without WSL.
* Good, because Linux packaging and ABI baseline records stay authoritative for `.deb`/`.rpm`.
* Bad, because Windows is not yet a required PR check; regressions can reach `main` until the lane is promoted.
* Bad, because int8 ORT behavior and determinism on Windows need ongoing measurement; Linux remains the determinism gate ([ADR 0003](0003-byte-identical-determinism.md)).

## Pros and Cons of the Options

### Ship a Windows x86_64 zip via clang-cl (static CRT); Package/Release (+ nightly) only

* Good, because the artifact matches “download and run.”
* Good, because clang-cl tracks C++26 better than pure `cl.exe` for this tree.
* Bad, because Conan clang Windows package ids may force rebuilding or MSVC fallback for dependencies.

### MSI installer and merge-blocking Pipeline job in the first milestone

* Good, because install UX and merge safety improve sooner.
* Bad, because WiX and `ci-gate` changes enlarge the first ship.

### MinGW-w64 or pure MSVC as the release compiler

* Good (MinGW), because more GNU flags reuse.
* Bad (MinGW), because CRT/Unicode and ORT packaging are weaker for a shipped PE.
* Bad (MSVC), because warning/LTO/SIMD paths in this tree are GCC/Clang-shaped.

### Document WSL only; no native Windows binary

* Good, because zero platform port.
* Bad, because it does not meet the native Windows release goal.
