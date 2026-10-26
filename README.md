# Nesso

[![Pipeline](https://github.com/lorenzocaprari/nesso/actions/workflows/ci.yml/badge.svg)](https://github.com/lorenzocaprari/nesso/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/lorenzocaprari/nesso)](https://github.com/lorenzocaprari/nesso/releases/latest)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/lorenzocaprari/nesso/badge)](https://scorecard.dev/viewer/?uri=github.com/lorenzocaprari/nesso)

grep for meaning: find the log lines you're looking for even when the words differ. Offline, one binary.

Semantic grep for logs, fully offline, no server, no cloud, no API keys, [Apache-2.0](LICENSE).

## Demo

![nesso search for "the disk is full" matching lines that say "No space left on device"](docs/assets/demo.gif)

The log does not contain the query. Index it, then search:

```bash
nesso index -o app.nesso app.log
```

```text
Indexed 5000 chunks into 'app.nesso'.
```

```bash
nesso search "the disk is full" -i app.nesso -k 3
```

```text
line 746: 0.4580: Oct 18 14:07:25 web-01 app[2699]: write /var/lib/app/queue.db: No space left on device
line 3940: 0.4352: Oct 18 14:39:39 web-01 app[1930]: write /var/lib/app/queue.db: No space left on device
line 918: 0.4322: Oct 18 14:09:17 web-01 app[3859]: write /var/lib/app/queue.db: No space left on device
```

## Install

Ubuntu 26.04+ (glibc 2.43+), amd64. The package includes the MiniLM model.

```bash
curl -fL -o nesso_0.1.0_amd64.deb \
  https://github.com/lorenzocaprari/nesso/releases/download/v0.1.0/nesso_0.1.0_amd64.deb
sudo apt install ./nesso_0.1.0_amd64.deb
```

## More

- [Usage](docs/usage.md)
- [Building](docs/building.md)
- [Releases](docs/releases.md)
- [Verifying a release](docs/verifying.md) (from v0.2.0)
- [Benchmarks](docs/benchmarks.md)
- [Determinism](docs/determinism.md)
- [Decisions](docs/adr/README.md)
- [Changelog](CHANGELOG.md), [contributing](CONTRIBUTING.md), [security](SECURITY.md), [license](LICENSE)
