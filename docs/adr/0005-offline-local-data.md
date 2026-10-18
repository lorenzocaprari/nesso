---
status: "accepted"
date: "2026-10-11"
---

# Keep operation offline and data on the machine

## Context and Problem Statement

`nesso grep`, `nesso index`, and `nesso search` read local logs and
JSON. Those files can contain secrets. The model weights are not in
git. Should the process open a connection on its own?

## Considered Options

* Offline-only
* Optional telemetry, opt-in
* Auto-download of models at runtime

## Decision Outcome

Chosen option: "Offline-only", for implicit network use. nesso does
not open a connection on its own. `scripts/fetch-model` is a script
the user runs; the binary does not. An explicit, user-initiated model
download is allowed if one is added later.

### Consequences

* Good, because a run reads local files and writes stdout, stderr,
  and, for `index`, the corpus file. `NESSO_TRACE=1` prints timings
  on stderr. That output is not transmitted.
* Good, because a machine that already has `model.onnx` and
  `vocab.txt` can run with no network. The `.deb` and the runtime
  image ship both files in `/usr/share/nesso`.
* Bad, because a source checkout has no model. Without the two files
  the command exits 2 and names `scripts/fetch-model`.
* Bad, because there is no crash reporter. A failure is exit status 2
  and a line on stderr.
* Bad, because no CI check enforces this yet. The follow-up is a
  check that the built binary or `.deb` has no curl or ssl linkage
  or strings, or that the test suite passes under `unshare -n`.

## More Information

`src/` and the C++ tests do not call socket, HTTP, or curl APIs.
`conan.lock` resolves libcurl 8.21.0 and openssl 3.6.3 because date
3.0.1 requires libcurl when `use_system_tz_db` is false and
`header_only` is false (the defaults on Linux). nesso sets neither.
Both are absent from the shipped v0.1.0 `.deb`. Set
`use_system_tz_db=True` on date so they leave the lock. The v0.1.0
binary has no `curl_` or `SSL_CTX` strings. POSIX telemetry in ONNX
Runtime 1.18.1 is empty.

Date is annotated tag
[v0.1.0](https://github.com/lorenzocaprari/nesso/releases/tag/v0.1.0),
commit
[9c5773d](https://github.com/lorenzocaprari/nesso/commit/9c5773d86b5caab49b5dd6f09bca0cdf0775913b).
`scripts/fetch-model`, `Dockerfile.runtime`,
`.clusterfuzzlite/build.sh`, `SECURITY.md`,
`test/functional/features/errors.feature`.
