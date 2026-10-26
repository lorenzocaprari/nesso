# Verifying a release

From v0.2.0. v0.1.0 was published before these signatures and is amd64-only. That release has no cosign signatures and no SLSA provenance files.

A `vX.Y.Z` tag signs `ghcr.io/lorenzocaprari/nesso` by digest with keyless cosign. The certificate identity is this repository's release workflow at that tag. `cosign verify` of the tag checks the manifest-list digest: that is the digest the tag resolves to. Each platform digest is signed with the same identity, because cosign does not treat the index signature as a signature of the amd64 or arm64 manifest. Use the platform digest when you pin `image@sha256:…` to one architecture.

The GitHub Release carries SLSA Build Level 3 provenance. `nesso_<version>.intoto.jsonl` covers both `.deb` packages and both SPDX SBOMs (`nesso_<version>_amd64.spdx.json` and `nesso_<version>_arm64.spdx.json`). `nesso_<version>_ghcr.intoto.jsonl` is the index attestation copied from GHCR. `nesso_<version>_ghcr_amd64.intoto.jsonl` and `nesso_<version>_ghcr_arm64.intoto.jsonl` are the platform attestations. `SHA256SUMS` lists those eight files.

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
