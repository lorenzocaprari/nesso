# Scripts

Contributor-facing helpers live in this directory. Pipeline and packaging
helpers live elsewhere so a local `ls` stays short.

| Location | Use |
| --- | --- |
| `scripts/` | Local gates and day-to-day tooling (`lint`, `fetch-model`, `bench`, …) |
| `scripts/ci/` | Pipeline helpers (Go `ci` binary, `build-env-hash`, `ci-build-archive`, `download-pinned`) |
| `packaging/scripts/` | Release `.deb`, runtime image, Trivy, and license/notice gates |

See [AGENTS.md](../AGENTS.md) and [CONTRIBUTING.md](../CONTRIBUTING.md) for the commands to run before a push.
