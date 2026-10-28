# Releases

A `vX.Y.Z` tag publishes `nesso_<version>_amd64.deb` and `nesso_<version>_arm64.deb`, an SPDX SBOM for each architecture, and `ghcr.io/lorenzocaprari/nesso` as a manifest list of `linux/amd64` and `linux/arm64`. The tags `vX.Y.Z`, `X.Y.Z`, and `latest` point at that list. [v0.2.1](https://github.com/lorenzocaprari/nesso/releases/tag/v0.2.1) is the current release. Install commands are in the [README](../README.md). Signature and provenance checks are in [Verifying a release](verifying.md).

## Packages

Both v0.2.1 packages depend on `libc6` (>= 2.43), `libstdc++6`, and `libgomp1`, and need Ubuntu 26.04+ (glibc 2.43+). Each ships the MiniLM files in `/usr/share/nesso`. An omitted `--model-dir` uses `NESSO_MODEL_DIR`, then `./models` when both files are present, then `$XDG_DATA_HOME/nesso` (or `~/.local/share/nesso`), then `/usr/share/nesso`.

The Package workflow builds each architecture's `.deb` twice and requires the bytes to match. amd64 builds on `ubuntu-latest`. arm64 builds on `ubuntu-24.04-arm`. It then installs that package with apt on Ubuntu 26.04 and Ubuntu 26.10, on the runner for that architecture. The release image job builds the runtime image, then packs the `.deb` the GitHub Release uploads from that image. That job runs on `ubuntu-24.04` for amd64 and `ubuntu-24.04-arm` for arm64.

## Image

`ghcr.io/lorenzocaprari/nesso:v0.2.1`, `:0.2.1`, and `:latest` are one manifest list (`linux/amd64` and `linux/arm64`). Docker pulls the architecture of the host. `v0.2.1-amd64` and `v0.2.1-arm64` are the single-platform tags that keep each digest addressable. `.github/workflows/release.yml` pushes `vX.Y.Z`, `X.Y.Z`, `latest`, `vX.Y.Z-amd64`, and `vX.Y.Z-arm64`.

The entrypoint is `nesso`. `NESSO_MODEL_DIR` is `/usr/share/nesso`, where the image stores the model. The image config leaves the working directory empty, so mount the log and set the working directory to that mount:

```bash
docker run --rm -v "$PWD:/data:ro" -w /data ghcr.io/lorenzocaprari/nesso:v0.2.1 \
  grep "the disk is full" app.log
```

The runtime stage is Ubuntu 26.04 (`packaging/Dockerfile.runtime`). The image job builds it on the native runner for that architecture, from the `nesso-build-env` image CI has already published.

## Signing and provenance

Keyless cosign 2.6.1 signs the manifest-list digest and each platform digest. The certificate identity is this repository's release workflow at that tag. SLSA Build Level 3 provenance comes from slsa-github-generator v2.1.0. The generic generator covers both `.deb` packages and both SPDX SBOMs in `nesso_<version>.intoto.jsonl`. The container generator attests the index digest and each platform digest. The GitHub Release copies those attestations to `nesso_<version>_ghcr.intoto.jsonl`, `nesso_<version>_ghcr_amd64.intoto.jsonl`, and `nesso_<version>_ghcr_arm64.intoto.jsonl`. `SHA256SUMS` lists the two packages, the two SBOMs, and those four provenance files.

v0.2.1 is the GitHub Release that carries those files plus `SHA256SUMS`. The v0.2.0 GitHub Release has no assets. Its image tags `v0.2.0` and `0.2.0` are one `linux/amd64` and `linux/arm64` manifest list. v0.1.0 publishes `nesso_0.1.0_amd64.deb` and `SHA256SUMS`. Its image tags `v0.1.0` and `0.1.0` are one `linux/amd64` image, and that release has no cosign signatures and no SLSA provenance files.

## Publishing

A release pull request bumps `version` in `conanfile.py`. After that commit is on `main`, publish the `vX.Y.Z` draft. Publishing creates the tag. `.github/workflows/release.yml` checks the tag against `version` in `conanfile.py`, runs the Package workflow, and waits for approval on the `release` environment. After approval it pushes each architecture image, publishes the manifest list, signs the digests, writes provenance, and uploads the GitHub Release.

`workflow_dispatch` builds, runs the package and image gates, and uploads workflow artifacts. Approval on the `release` environment, the image push, cosign, provenance, and the GitHub Release run on a `vX.Y.Z` tag push.

The runtime image build pulls the published `nesso-build-env` tag for amd64 (`deps-<hash>`) and arm64 (`deps-<hash>-arm64`).
