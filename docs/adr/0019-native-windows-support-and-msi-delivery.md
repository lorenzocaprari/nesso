---
status: "proposed"
date: "2026-11-28"
decision-makers: Lorenzo Caprari
---

# Native Windows support and MSI delivery

## Context and Problem Statement

[ADR 0018](0018-windows-x86_64-zip-clang-cl.md) chose a first native Windows release: an x86_64 zip built with clang-cl and a static CRT, produced by `.github/workflows/windows.yml`, called from Package and Release, and kept out of Pipeline `ci-gate`. It named an MSI installer, Windows arm64, and a merge-blocking Windows job as follow-ups. That zip is the first step, not the end state. Windows users should get the same install experience as Linux users: a signed, versioned installer that can be deployed silently, upgrades in place, puts `nesso` on `PATH`, and is built, tested, and published by a Windows pipeline of its own.

What is full native Windows support, and how is it delivered?

## Decision Drivers

* A support engineer on a Windows host installs nesso without a toolchain, without WSL, and without a Visual C++ redistributable, and the MiniLM files arrive with it ([ADR 0005](0005-offline-local-data.md)).
* Managed fleets install with `msiexec` or a deployment tool, silently, and expect clean upgrade and uninstall.
* The zip stays available for hosts where the user has no administrator rights.
* A release asset is checksummed, covered by SLSA provenance, and signed when a signing identity exists, as the Linux packages are.
* The Linux gates stay authoritative for what they measure: the glibc baseline ([ADR 0008](0008-glibc-2-34-release-baseline.md)) and byte-identical determinism ([ADR 0003](0003-byte-identical-determinism.md)).
* Windows regressions are caught before a tag, without holding every Linux pull request on a new runner matrix until the lane is proven.
* Tool choice must keep working in a public repository with free hosted runners and, where possible, no recurring cost.

## Considered Options

MSI authoring and build tool:

* WiX Toolset v4 or later, with a hand-written `.wxs` source and the `wix` command-line tool
* CPack WIX generator from CMake
* MSIX package
* No installer: keep the zip and add a winget portable manifest

Installer scope:

* Per-machine MSI, with the zip as the no-administrator path
* Per-user MSI only
* Both from one source

Code signing:

* Unsigned
* A managed signing service (Azure Artifact Signing)
* A free open-source signing programme (SignPath Foundation)
* A commercial code-signing certificate on a cloud or hardware key

## Decision Outcome

Chosen option: "WiX MSI, per-machine and x64, built and tested in a dedicated Windows pipeline, signed on tags once a signing service is chosen", because it is the form managed Windows deployment expects, it wraps the artifact ADR 0018 already builds, and it leaves the Linux gates as they are. In detail:

* **Toolchain: clang-cl with a static CRT, as in ADR 0018.** No change. The MSI wraps the same `nesso.exe` and `models/` the zip carries.
* **MSI tool: WiX with a hand-written `.wxs`, built with the `wix` command-line tool**, from `packaging/windows/` and a script beside `packaging/scripts/package-zip`. The CLI is installed in CI as a pinned .NET tool version, not from a floating channel. A hand-written source gives direct control of the `UpgradeCode`, the `PATH` entry, the install scope, the upgrade rules, and the file list, and keeps the packaging step a script that CI and a developer both run, like the `.deb` and `.rpm`.
* **Scope: per-machine, x64, installed under `%ProgramFiles%\nesso`.** The package carries `nesso.exe`, `models\`, and the notices, so the packaged model lookup (`models\` next to the executable) needs no new code. It adds the install directory to the system `PATH` and removes it on uninstall. The `UpgradeCode` is one constant GUID that is never changed; each release raises `ProductVersion` and uses a major upgrade, so a newer MSI replaces an older one and an older one refuses to replace a newer one. The zip remains the portable, no-administrator path. A per-user variant is deferred until someone needs it.
* **Architectures: x64 first.** Windows arm64 is a later, separate MSI (`nesso_<ver>_windows_arm64.msi`) once the dependency graph builds natively on `windows-11-arm`, a free hosted runner for public repositories. On arm64 the distance kernel is the scalar path, as on Linux arm64 ([ADR 0007](0007-release-amd64-and-arm64.md)).
* **Signing: sign `nesso.exe` and the MSI on a release tag only, through a service the maintainer picks.** Which service is the maintainer's decision, because some options cost money and some carry eligibility conditions (see Code signing below). Until it is decided, the MSI ships unsigned, the README and release notes say so, and `SHA256SUMS` plus SLSA provenance are the integrity story. Pull requests build unsigned packages.
* **Pipeline: a Windows part of its own.** `windows.yml` stays the one workflow that builds, tests, packages, and smoke-tests on Windows, and Release consumes its artifacts. Jobs: build, unit tests, black-box tests, package (zip and MSI), smoke-install on a clean runner, and, on a tag, sign. Release adds the MSI to the `subjects` hash list, so it gets a line in `SHA256SUMS` and in the SLSA provenance, and uploads it with the other assets. The lane starts non-blocking and is promoted to a required check after a full release cycle of green runs on the nightly and path-filtered triggers.
* **winget: optional, after the MSI is published.** A manifest for the MSI is generated from the release asset URL and its SHA-256 and submitted to the community repository by the maintainer or a release step that holds its own token. It is not a merge or release gate.

### Consequences

* Good, because Windows installs become one command, silent, upgradeable, and removable, with `nesso` on `PATH` and the model files in place.
* Good, because the MSI wraps the binary and files the zip already ships, so packaging adds no new platform code.
* Good, because the Windows lane gets its own tests, package step, and smoke-install, and cannot hide behind the Linux gates.
* Good, because the Linux ABI, determinism, `.deb`, `.rpm`, and image gates stay as they are.
* Bad, because the maintainer takes on WiX source, a Windows packaging script, and an installer upgrade matrix to keep working.
* Bad, because an unsigned MSI shows Windows SmartScreen warnings, and a signed one still earns reputation only over time.
* Bad, because the WiX binary releases from v6 on carry a maintenance-fee EULA for revenue-generating use above a revenue threshold, and v7 needs an explicit acceptance (see the WiX option below).
* Bad, because per-machine install needs administrator rights, so some hosts keep using the zip.
* Bad, because the lane is not a required check at first, so a Windows regression can reach `main` until it is promoted.

### Confirmation

* `packaging/windows/` holds the `.wxs`, and a packaging script writes `nesso_<ver>_windows_amd64.msi`.
* A smoke-install job on a clean Windows runner installs the MSI silently with a verbose log, checks that `nesso --version` runs from a new shell through `PATH`, runs `grep`, `index`, and `search` on a sample log with the installed models and no `--model-dir`, installs the previous release's MSI and then this one to prove the upgrade, and uninstalls to prove that files and the `PATH` entry are gone.
* The same job runs the smoke test on the extracted zip, because today's smoke test runs the build-tree binary.
* Release lists the MSI in `SHA256SUMS`, in the SLSA subjects, and in the release assets, and fails if it is missing.
* When signing exists, the job verifies the Authenticode signature and timestamp of `nesso.exe` and of the MSI before upload.
* The Windows lane reports unit and black-box test counts and compares its output on a fixed fixture set against the Linux output. A difference is reported first and becomes a gate once the cause of each is understood.

## Pros and Cons of the Options

### WiX v4 or later, hand-written `.wxs`

* Good, because it targets the MSI format directly: upgrade code, major upgrade, `PATH` entry, scope, and features are explicit.
* Good, because the `wix` tool is a .NET tool that installs on a hosted Windows runner and runs as a script step.
* Good, because the schema is the same family from v4 on, so a version bump is a bounded change.
* Neutral, because the maintainer must pick a major version, and v6 and later binaries carry the maintenance-fee EULA; v5 has none but is out of service.
* Bad, because it is another source format to learn and test.

### CPack WIX generator

* Good, because it reuses `install()` rules and keeps packaging in CMake. CMake 3.30 added WiX v4 support through `CPACK_WIX_VERSION`, and this repository already requires CMake 4.2 or newer.
* Neutral, because its default scope is per-machine under policy CMP0172, which matches the recommendation, and `CPACK_WIX_UPGRADE_GUID` pins the upgrade code.
* Bad, because the default template is generic and the `PATH` entry, the model files, and the notices need a patch file or a custom template, so a hand-written source ends up close to the result anyway.
* Bad, because the generator is documented against the WiX v4 tool, so moving to a newer WiX major depends on CMake following it.
* Bad, because this repository packages by script from the Conan build tree (`packaging/scripts/`), not from `cpack`, so it would be a second packaging path.

### MSIX package

* Good, because MSIX gives clean install and uninstall, and it is the native format for the Microsoft Store.
* Bad, because an MSIX installs only when it is signed with a certificate the machine trusts, so it cannot ship unsigned the way an MSI can while signing is undecided.
* Bad, because a command-line tool in an MSIX needs an app execution alias and runs as a packaged app, which differs from the plain `nesso.exe` the zip and the MSI ship.
* Bad, because MSI is what silent and managed deployment tooling expects of a command-line tool today.

### No installer: zip and winget portable manifest

* Good, because it is nearly free: the zip exists, and a portable manifest points at it.
* Bad, because it gives no `PATH` entry, no Add/Remove Programs entry, and no upgrade, and it does not meet the goal of full Windows delivery.

### Per-machine MSI, zip for no-administrator hosts

* Good, because it is the usual shape for managed deployment, and `%ProgramFiles%` is writable only by administrators, which protects the installed binary and models.
* Bad, because a user without administrator rights cannot use it.

### Per-user MSI only

* Good, because it installs without elevation, under the user profile.
* Bad, because fleet tools usually deploy per machine, and a per-user package is installed once per profile there.

### Both scopes from one source

* Good, because one package would serve both audiences.
* Bad, because WiX added dual-purpose packages only for bundles in v7, and the choice dialog, the `PATH` scope, and the upgrade rules multiply the test matrix for a first MSI.

### Code signing

* **Unsigned:** no cost and no setup; SmartScreen and endpoint tools warn or block, and some managed hosts refuse unsigned installers. Acceptable only as a first release.
* **Azure Artifact Signing:** a managed service with a GitHub Action (`azure/artifact-signing-action`) that runs on Windows x64 hosted runners and not on Windows Arm runners, and with OIDC authentication so no secret is stored. It has a recurring cost. Public certificates are available to organizations in a list of regions and to individual developers only in the United States and Canada, so eligibility depends on where the signer is and whether a legal entity exists.
* **SignPath Foundation:** free certificates for qualifying open-source projects, with a GitHub integration. The project must be released and maintained under an OSI-approved license, builds must run on GitHub-hosted runners, every release needs manual approval, and the certificate belongs to the Foundation, which is then the publisher shown to users.
* **Commercial certificate:** the traditional route; current rules put the private key on a hardware or cloud key, so CI needs a cloud signing provider or a self-hosted runner with a token. It has a recurring cost.
* None of these gives instant SmartScreen reputation. Choosing one is a cost and identity decision for the maintainer.

## More Information

### State when this was written

* `windows.yml` runs on `windows-2022` with clang-cl 20, Conan with `conan/profiles/clang-cl-26` and `conan/profiles/windows-msvc-build`, runs `ctest -L unit`, smoke-tests `grep`, `index`, and `search`, and uploads `nesso_<ver>_windows_amd64.zip`. Package calls it as `windows-zip`, and Release lists the zip in `subjects`, `SHA256SUMS`, provenance, and release assets.
* The lane does not run the integration tests, the black-box suite, or the fuzz harnesses, and `test/scripts` is not built on Windows.
* `windows-latest` now maps to a newer Windows image than `windows-2022`. The pin and `compiler.version=194` in the profile are an image-lifecycle risk to track.

### Gaps to close, in order

1. **Black-box suite on Windows.** The suite under `test/functional` drives `nesso` through a pseudo-terminal opened from `/dev/ptmx` and `/dev/pts/N` with `syscall.SYS_IOCTL` (`internal/runner/runner.go`), so it does not compile for Windows. Scenarios that need a terminal either use ConPTY or are tagged Linux-only; the rest run unchanged.
2. **Integration tests** (`-L integration`) are built but not run in the Windows lane.
3. **Command-line text encoding and output mode.** `main` takes `char *argv[]`, and the tree has no Windows application manifest (active code page, long paths) and no binary-mode setting for standard output. Non-ASCII query and path arguments, paths over the legacy path limit, and `\n` versus `\r\n` in JSON Lines output ([ADR 0009](0009-command-line-contract-and-json-lines.md)) need a test on a Windows host before any claim of parity.
4. **Determinism on Windows.** ADR 0018 leaves int8 ONNX Runtime behavior and determinism on Windows to measurement; Linux stays the gate.
5. **Offline check.** The Linux network-free and RUNPATH checks have no Windows form. A Windows form, such as an imports check on `nesso.exe`, is to be defined.

### Gates that do not apply on Windows

* The glibc 2.34 baseline and its ABI check, the RUNPATH check, EL9 builds, the `.deb` and `.rpm` build, compare, and distro install checks, and the runtime image scan ([ADR 0007](0007-release-amd64-and-arm64.md), [ADR 0008](0008-glibc-2-34-release-baseline.md)).
* The GHCR runtime image and its cosign signature. The MSI is covered by SLSA provenance and, when available, Authenticode, not by the image signature.
* Byte-identical reproducibility of the package ([ADR 0003](0003-byte-identical-determinism.md)). The Windows release does not use `NESSO_REPRODUCIBLE`, and an MSI is not reproducible by default. Only the Linux determinism gate keeps that claim.
* AFL++ and libFuzzer fuzzing, the GCC and Clang sanitizer lanes, ThreadSanitizer, and the gcov coverage floors. They stay Linux gates; a Windows equivalent is out of scope here.

### Release ordering

PR #396 (the zip) lands first or together with the decision to accept this ADR. MSI packaging builds on its `nesso.exe`, `models/`, notices, and `windows.yml`. The MSI, signing, and winget steps are independent PRs after it.

### Sources

* WiX releases and the maintenance-fee EULA: <https://docs.firegiant.com/wix/whatsnew/releasenotes/> and <https://github.com/wixtoolset/wix/blob/main/OSMFEULA.txt>.
* CPack WIX generator: <https://cmake.org/cmake/help/latest/cpack_gen/wix.html> and policy CMP0172.
* Azure Artifact Signing: <https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart> and <https://github.com/azure/artifact-signing-action>.
* SignPath Foundation conditions: <https://signpath.org/terms.html>.
* GitHub-hosted runners, including `windows-11-arm` for public repositories: <https://docs.github.com/en/actions/reference/runners/github-hosted-runners>.
