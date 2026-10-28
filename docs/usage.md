# Usage

`nesso grep` searches files in one shot. `nesso index` writes a corpus, and `nesso search` queries that file. The embedder is a local ONNX MiniLM model. Ranking is a brute-force scan.

Matches go to stdout. Skipped lines, the index summary, and errors go to stderr. Every command exits 0 when matches are printed or a corpus is written, 1 when there is nothing to search or index, and 2 on error. Argument errors, including `-k 0`, also exit 2. `--help` lists these codes. A missing model names the directories that were tried and suggests `scripts/fetch-model` or `--model-dir`.

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

## Threads

`--threads` and `NESSO_THREADS` set the same CPU budget `N`. `--threads` wins when both are set.

For the int8 model on x86, a budget of 4 or less is one intra-op thread and `N` sessions, and the arena is not shrunk. A larger budget uses `I = min(4, N/2)` (integer division) and `S = max(1, N/I)`, and shrinks the arena after each run. `N=8` is 2 sessions of 4 intra-op threads. `N=16` is 4 sessions of 4. On aarch64 `I` is at least 2, including when `N` is 1, so a session does not use the intra-op 1 path. When the option and the variable are both unset, `N` is the number of usable physical cores: topology cores in the process affinity mask (`Cpus_allowed_list`), then reduced by a cgroup `cpu.max` quota when a parent sets one. A positive quota smaller than one period counts as one CPU. The default session count is also reduced while `S` times the per-session estimate exceeds half of `MemAvailable` (total RAM when that key is missing). An explicit `N` is not reduced; nesso warns on stderr and continues.

For the fp32 model, `N` is the intra-op thread count of the one session. The default is the logical CPU count. On aarch64 that count is at least 2, including when `N` is 1.

`nesso --version --verbose` prints the policy for both models.

## Files

- `.json` and `.jsonl` are JSON, matched without regard to case. Every other name is read as lines: no extension, `.log.N` rotations, `.LOG`, `.txt`, `syslog`. `-` reads stdin as lines, and those matches are prefixed with `-:`.
- A file whose first 4 KiB contains a NUL byte is rejected. Directories are rejected.
- Lines: one chunk per non-empty line. Lines longer than 4096 characters are truncated. A trailing CR is stripped.
- JSON: only objects with a string field are indexed (`message` by default, or `--json-field`). Malformed lines and documents are skipped. String values longer than 4096 characters are truncated. A JSON array is numbered `element N`, not `line N`.
- `grep` and `index` embed each distinct text once, in first-occurrence order, in a fixed window of 1024 texts sorted by token count. Lines that share a text share its score. Ties break by input order. `index` writes one vector per input line. Rebuild a corpus after upgrading so its vectors match that batch. `search` maps that file. Stdin cannot be reread, so that command keeps the stdin text.
- `--json` on `grep` and `search` prints one JSON object per match. The object adds `occurrences`, how many input lines share that text, always at least 1. `schema_version` stays 1. Text output does not include the count. Counts stay on stderr. `index` has no `--json`.
- Empty lines and JSON values without that string field are skipped. Overlong log lines and JSON string values are truncated. Skip and truncation counts are printed on stderr.
- `index` records each file source's size and modification time. `search` warns on stderr when a source changed or is missing; stdin sources are not tracked.

## What this is not

- Not a hosted service. Text stays on the machine. There is no server, no cloud call, and no API key.
- Linux only.
