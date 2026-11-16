# Verifying a release

From v0.2.0. v0.1.0 was published before these signatures and is amd64-only. That release has no cosign signatures and no SLSA provenance files.

A `vX.Y.Z` tag signs `ghcr.io/lorenzocaprari/nesso` by digest with keyless cosign. The certificate identity is this repository's release workflow at that tag. `cosign verify` of the tag checks the manifest-list digest: that is the digest the tag resolves to. Each platform digest is signed with the same identity, because cosign does not treat the index signature as a signature of the amd64 or arm64 manifest. Use the platform digest when you pin `image@sha256:…` to one architecture.

The GitHub Release carries SLSA Build Level 3 provenance. `nesso_<version>.intoto.jsonl` covers both `.deb` packages, both `.rpm` packages (`nesso-<version>-1.x86_64.rpm` and `nesso-<version>-1.aarch64.rpm`), and both SPDX SBOMs (`nesso_<version>_amd64.spdx.json` and `nesso_<version>_arm64.spdx.json`). `nesso_<version>_ghcr.intoto.jsonl` is the index attestation copied from GHCR. `nesso_<version>_ghcr_amd64.intoto.jsonl` and `nesso_<version>_ghcr_arm64.intoto.jsonl` are the platform attestations. `SHA256SUMS` lists those ten files.

Set `version` to the release you are checking, without the leading `v`. `digest` below is the manifest-list digest.

```bash
version=1.2.3
digest="$(docker buildx imagetools inspect --format '{{index .Manifest.Digest}}' "ghcr.io/lorenzocaprari/nesso:v${version}")"

cosign verify \
  --certificate-identity "https://github.com/lorenzocaprari/nesso/.github/workflows/release.yml@refs/tags/v${version}" \
  --certificate-oidc-issuer "https://token.actions.githubusercontent.com" \
  "ghcr.io/lorenzocaprari/nesso@${digest}"

slsa-verifier verify-artifact \
  "nesso_${version}_amd64.deb" \
  "nesso_${version}_arm64.deb" \
  "nesso-${version}-1.x86_64.rpm" \
  "nesso-${version}-1.aarch64.rpm" \
  "nesso_${version}_amd64.spdx.json" \
  "nesso_${version}_arm64.spdx.json" \
  --provenance-path "nesso_${version}.intoto.jsonl" \
  --source-uri github.com/lorenzocaprari/nesso \
  --source-tag "v${version}"

slsa-verifier verify-image \
  "ghcr.io/lorenzocaprari/nesso@${digest}" \
  --source-uri github.com/lorenzocaprari/nesso \
  --source-tag "v${version}"
```

To verify one platform, read its digest from `docker buildx imagetools inspect ghcr.io/lorenzocaprari/nesso:v${version}` and pass that digest to `cosign verify` and to `slsa-verifier verify-image`. The matching file on the release is `nesso_${version}_ghcr_amd64.intoto.jsonl` or `nesso_${version}_ghcr_arm64.intoto.jsonl`.

`cosign` 2.x checks the signature. `slsa-verifier` v2.7.1 or later checks the provenance. `verify-image` reads the attestation from GHCR. When the registry asks for credentials, set `GH_TOKEN` for that command.

## RPM packages

A tag release publishes `nesso-<version>-1.x86_64.rpm` and `nesso-<version>-1.aarch64.rpm` next to the `.deb` files. Package and Release both build those RPMs with `rpmbuild` in the EL9 image (`SOURCE_DATE_EPOCH`, clamped mtimes, fixed build host). Two clean builds per architecture must match byte-for-byte, and `/usr/bin/nesso` inside the RPM must match the same architecture's `.deb`. Install smoke runs `dnf install` on Rocky Linux 9 and UBI 9 (amd64 and arm64), then `nesso -V`, a fixture grep, and `dnf remove`. Each RPM is an SLSA subject and has a `SHA256SUMS` line; the per-architecture SPDX SBOM covers the same software as the matching `.deb`. RPMs are not GPG-signed; trust is the SLSA provenance and `SHA256SUMS`.
