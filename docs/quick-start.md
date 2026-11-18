# Quick start: triage a support bundle

Get a first answer from a sosreport, tarball, zip, or log directory without extracting it.

## Install

Ubuntu 22.04+, Debian 12+, and RHEL/Rocky 9, amd64/x86_64 or arm64/aarch64. The `.deb` and `.rpm` install `/usr/bin/nesso` and the MiniLM files in `/usr/share/nesso`.

```bash
curl -fL -o nesso_0.3.1_amd64.deb \
  https://github.com/lorenzocaprari/nesso/releases/download/v0.3.1/nesso_0.3.1_amd64.deb
sudo apt install ./nesso_0.3.1_amd64.deb

curl -fL -o nesso_0.3.1_arm64.deb \
  https://github.com/lorenzocaprari/nesso/releases/download/v0.3.1/nesso_0.3.1_arm64.deb
sudo apt install ./nesso_0.3.1_arm64.deb

curl -fL -o nesso-0.3.1-1.x86_64.rpm \
  https://github.com/lorenzocaprari/nesso/releases/download/v0.3.1/nesso-0.3.1-1.x86_64.rpm
sudo dnf install ./nesso-0.3.1-1.x86_64.rpm

curl -fL -o nesso-0.3.1-1.aarch64.rpm \
  https://github.com/lorenzocaprari/nesso/releases/download/v0.3.1/nesso-0.3.1-1.aarch64.rpm
sudo dnf install ./nesso-0.3.1-1.aarch64.rpm
```

`ghcr.io/lorenzocaprari/nesso` stores the model at `/usr/share/nesso` and sets `NESSO_MODEL_DIR` to that directory. The entrypoint is `nesso` (`/usr/local/bin/nesso`). Mount the directory that holds the bundle:

```bash
docker run --rm -v "$PWD:/data:ro" -w /data ghcr.io/lorenzocaprari/nesso:v0.3.1 \
  grep "the disk is full" sosreport-host.tar.xz
```

## Point nesso at the bundle

Pass the path as you received it. nesso reads directories, `.gz` files, and `.tar`, `.tgz`, `.tar.gz`, `.tar.xz` (including sosreports), and `.zip` in place. Nothing is extracted.

```bash
nesso grep "the disk is full" sosreport-host.tar.xz
```

Each match names the member as `sosreport-host.tar.xz:var/log/messages` (the path as given, a colon, and the member name).

## First answer

`-k` caps how many matches to print (default 5). `--group` collapses lines that share a masked dedup key. `--json` prints one JSON object per match.

```bash
nesso grep "the disk is full" sosreport-host.tar.xz -k 10
nesso grep "auth failure" sosreport-host.tar.xz --group -k 5
nesso grep "payment timeout" sosreport-host.tar.xz --json
```

## Index, then search

Index once when you will run several queries on the same bundle:

```bash
nesso index -o case.nesso sosreport-host.tar.xz
nesso search "the disk is full" -i case.nesso -k 10
nesso search "auth failure" -i case.nesso --group
nesso search "payment timeout" -i case.nesso --json
```

`index` does not accept `--json` or `--group`. Archive size caps (`--archive-max-entry-bytes`, `--archive-max-bytes`, `--archive-max-entries`, `--archive-max-ratio`) apply to `grep` and `index` only.

## Shell output

Matches are lines on stdout. Skip counts, warnings, and errors stay on stderr, so a pipe reads only matches. `grep` draws a progress line on stderr when that stream is a terminal. `--quiet` turns the line off, which keeps it from drawing over `less`. `search` does not draw one. With nothing to rank, stdout is empty and the command exits 1. `set -o pipefail` makes the pipeline use that status.

A text line is for reading. One file prints `line N: SCORE: TEXT`, with four digits after the decimal. An archive member, stdin (`-`), or more than one file prints `SOURCE:line N: SCORE: TEXT`. `--group` inserts the count, as `line N (COUNT): SCORE: TEXT` or `SOURCE:line N (COUNT): SCORE: TEXT`. A JSON array slot prints `element`. The log text can contain `: `, and an archive source already contains a colon, so read fields from `--json`. `grep -F` matches the rendered line, including that source prefix.

`--json` prints one JSON object per line. The keys, in order, are `schema_version`, `rank`, `source`, `kind`, `line`, `score`, `text`, `occurrences`, `mode`, `dense`, and `lexical`. `schema_version` is 2. `kind` is `line` or `element`. `mode` is `dense`, `hybrid`, or `lexical`. `rank` starts at 1. The text score has four digits after the decimal. The JSON `score` is the shortest decimal that reads back as the same value. `dense` and `lexical` are numbers or `null`. `occurrences` is at least 1. `jq` reads each line as its own value. The index summary (`Indexed N chunks into 'case.nesso'.`) stays on stderr.

```bash
nesso grep "the disk is full" sosreport-host.tar.xz --quiet -k 20 | less
nesso grep "the disk is full" sosreport-host.tar.xz --quiet -k 20 \
  | grep -F 'sosreport-host.tar.xz:var/log/messages'
nesso grep "the disk is full" sosreport-host.tar.xz --json -k 10 \
  | jq -r '[.rank, .score, .source, .line, .text] | @tsv'
nesso grep "auth failure" sosreport-host.tar.xz --json --group -k 5 \
  | jq -r 'select(.occurrences > 1) | "\(.occurrences)\t\(.text)"'
nesso search "payment timeout" -i case.nesso --json | jq -r .text
```

## Encrypted, over the limit, or skipped

Matches stay on stdout. Parse errors and skip summaries go to stderr. Exit 2 means a hard stop for that path.

**Encrypted zip entry** (exit 2):

```text
Error: failed to parse 'bundle.zip': entry 'secret.log' is encrypted; decrypt it first
```

Decrypt the entry, then run nesso on the decrypted file or archive.

**Encrypted OpenPGP file** (exit 2):

```text
Error: failed to parse 'bundle.pgp': the archive is an encrypted OpenPGP file; decrypt it first (for example with gpg --decrypt)
```

Decrypt (for example with `gpg --decrypt`), then point nesso at the result.

**Archive limit** (exit 2). The note names the number and the flag to raise. Examples:

```text
Error: failed to parse 'bundle.tar.xz': an archive entry is larger than 268435456 bytes; raise the limit with --archive-max-entry-bytes
Error: failed to parse 'bundle.tar.xz': the archive expands past 2147483648 uncompressed bytes; raise the limit with --archive-max-bytes
Error: failed to parse 'bundle.tar.xz': the archive entry count exceeds 100000; raise the limit with --archive-max-entries
Error: failed to parse 'bundle.tar.xz': the archive compression ratio exceeds 1000; raise the limit with --archive-max-ratio
```

Re-run `grep` or `index` with a higher value for the named flag.

**Skipped entries** (stderr only; the command continues):

```text
Skipped 3 entries in 'mix.tar' (2 directories and 1 binary file).
```

Directories, symlinks, hard links, other non-regular members, and binary members are skipped. That line is informational. Stdout and `--json` still hold only the match rows.

## Next

The full input contract is in [Usage](usage.md).
