# Releases

## v0.1.0

The published GitHub Release asset is `nesso_0.1.0_amd64.deb`. The binary's highest glibc symbol is `GLIBC_2.43`, so it needs Ubuntu 26.04+ (glibc 2.43+), amd64. The package depends on `libc6`, `libstdc++6`, and `libgomp1`. Install commands are in the [README](../README.md).

There is no arm64 `.deb` for v0.1.0, and the release has no cosign signatures or SLSA provenance. Those are [from v0.2.0](#from-v020). The `latest` tag today is this amd64 image, not a multi-arch list.

The package ships the MiniLM files in `/usr/share/nesso`. An omitted `--model-dir` uses `NESSO_MODEL_DIR`, then `./models` when both files are present, then `$XDG_DATA_HOME/nesso` (or `~/.local/share/nesso`), then `/usr/share/nesso`.

`ghcr.io/lorenzocaprari/nesso:v0.1.0` is one `linux/amd64` image. The tags `0.1.0` and `latest` point at that same image. It is not a manifest list. The entrypoint is `nesso`, and `NESSO_MODEL_DIR` is `/usr/share/nesso`:

```bash
docker pull ghcr.io/lorenzocaprari/nesso:v0.1.0
docker run --rm -v "$PWD:/data" -w /data ghcr.io/lorenzocaprari/nesso:v0.1.0 \
  grep "database connection error" app.log
```

Two builds of the same commit produce the same `.deb` on that architecture. The Package workflow installs the package it built on Ubuntu 26.04 and Ubuntu 26.10.

<a id="from-v020"></a>

## From v0.2.0

v0.1.0 does not ship these. The `latest` tag on the registry today is the amd64 image above.

arm64 package:

```bash
sudo apt install ./nesso_*_arm64.deb
```

The release workflow publishes `vX.Y.Z`, `X.Y.Z`, and `latest` as a manifest list (`linux/amd64` and `linux/arm64`). Docker then pulls the architecture of the host. Checking the signature and provenance is in [verifying.md](verifying.md).

## Publishing

A release pull request bumps `version` in `conanfile.py`. After that commit is on `main`, publish the `vX.Y.Z` draft. Publishing creates the tag. `.github/workflows/release.yml` checks the tag against `conanfile.py`, waits for approval on the `release` environment, then publishes the image, both `.deb` packages, and the GitHub Release. The only tag published so far is v0.1.0, and its files are the amd64 package and image above. CI must already have published `nesso-build-env` for amd64 and arm64.
