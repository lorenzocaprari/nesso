# Usage

`nesso grep` searches files in one shot. `nesso index` writes a corpus, and `nesso search` queries that file. The embedder is a local ONNX MiniLM model. Ranking is a brute-force scan.

Matches go to stdout. Skipped lines, the index summary, and errors go to stderr. Every command exits 0 when it prints matches or the index succeeds, 1 when nothing matches, and 2 when the command fails. Argument errors, including `-k 0`, also exit 2. `--help` lists these codes. A missing model names the directories that were tried and suggests `scripts/fetch-model` or `--model-dir`.

## Model

The `.deb` and the container image ship the MiniLM files in `/usr/share/nesso`. A source build downloads them once:

```bash
./scripts/fetch-model
```

An omitted `--model-dir` uses `NESSO_MODEL_DIR`, then `./models` when both files are present, then `$XDG_DATA_HOME/nesso` (or `~/.local/share/nesso`), then `/usr/share/nesso`.

## Commands

Search files by meaning. `-k` is optional (default 5):

```bash
nesso grep "database connection error" app.log
nesso grep "payment timeout" app.log events.jsonl dump.json -k 5
nesso grep "auth failure" app.log --model-dir models/
```

Index the same files once, then search the corpus. `-o` and `-i` are required:

```bash
nesso index -o corpus.nesso app.log events.jsonl
nesso search "payment timeout" -i corpus.nesso -k 5
```

From a source tree the binary is `build/Debug/src/nesso` or `build/Release/src/nesso`.

## Files

- `.json` and `.jsonl` are JSON, matched without regard to case. Every other name is read as lines: no extension, `.log.N` rotations, `.LOG`, `.txt`, `syslog`. `-` reads stdin as lines, and those matches are prefixed with `-:`.
- A file whose first 4 KiB contains a NUL byte is rejected. Directories are rejected.
- Lines: one chunk per non-empty line. Lines longer than 4096 characters are truncated. A trailing CR is stripped.
- JSON: only objects with a string field are indexed (`message` by default, or `--json-field`). Malformed lines and documents are skipped. String values longer than 4096 characters are truncated. A JSON array is numbered `element N`, not `line N`.
- `grep` embeds in batches and keeps the top matches. `index` writes the corpus file. `search` maps that file. Stdin cannot be reread, so that command keeps the stdin text.
- Empty lines and JSON values without that string field are skipped. Overlong log lines and JSON string values are truncated. Skip and truncation counts are printed on stderr.
- `index` records each file source's size and modification time. `search` warns on stderr when a source changed or is missing; stdin sources are not tracked.

## What this is not

- Not a hosted service. Text stays on the machine. There is no server, no cloud call, and no API key.
- Linux only.
