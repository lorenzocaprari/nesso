# Usage

`nesso grep` searches files in one shot. `nesso index` writes a corpus, and `nesso search` queries that file. The embedder is a local ONNX MiniLM model. Ranking uses dense embeddings, BM25, or hybrid fusion depending on `--mode`.

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
nesso grep "auth failure" logs/ --model-dir models/
nesso grep "auth failure" app.log --group -k 5
```

`grep` and `search` accept `--mode auto|hybrid|dense|lexical` (default `auto`). Under `auto`, a pure identifier query uses lexical ranking; other queries use hybrid fusion. `dense` matches the pre-hybrid ranking. `hybrid` and `lexical` need corpus v3 BM25 postings from `nesso index` (grep builds terms in memory); without postings, or with a different term-rule version, those modes exit 2. Forced `lexical` with no BM25 hit falls back to hybrid and prints `Warning: no lexical match for the query; using hybrid`.

Index the same files once, then search the corpus. `-o` and `-i` are required:

```bash
nesso index -o corpus.nesso logs/ events.jsonl
nesso search "payment timeout" -i corpus.nesso -k 5
nesso search "payment timeout" -i corpus.nesso --group
```

`index` exits 2 without writing when `-o` names one of the input files, including through a hard link or symlink. Directories are expanded before that check, so an output file inside an input directory is rejected once that file exists.

From a source tree the binary is `build/Debug/src/nesso` or `build/Release/src/nesso`.

## Version

`nesso --version` reports the package version. Builds from an untagged commit also include `+g` and the first 12 characters of its commit SHA, for example `0.2.1+g0123456789ab`. A build from its matching `vX.Y.Z` release tag reports only `X.Y.Z`.

## Threads

`--threads` and `NESSO_THREADS` set the same CPU budget `N`. `--threads` wins when both are set. `nesso search` uses that budget to score the corpus. One thread, and a small corpus, stay on the calling thread. The scores and the ranking match that single-thread scan. `grep` and `index` use the budget for embedding.

For the int8 model on x86, a budget of 4 or less is one intra-op thread and `N` sessions, and the arena is not shrunk. A larger budget uses `I = min(4, N/2)` (integer division) and `S = max(1, N/I)`, and shrinks the arena after each run. `N=8` is 2 sessions of 4 intra-op threads. `N=16` is 4 sessions of 4. On aarch64 `I` is at least 2, including when `N` is 1, so a session does not use the intra-op 1 path. When the option and the variable are both unset, `N` is the number of usable physical cores: topology cores in the process affinity mask (`Cpus_allowed_list`), then reduced by a cgroup `cpu.max` quota when a parent sets one. A positive quota smaller than one period counts as one CPU. The default session count is also reduced while `S` times the per-session estimate exceeds half of `MemAvailable` (total RAM when that key is missing). An explicit `N` is not reduced; nesso warns on stderr and continues.

For the fp32 model, `N` is the intra-op thread count of the one session. The default is the logical CPU count. On aarch64 that count is at least 2, including when `N` is 1.

On x86-64 Linux, nesso refuses Intel AMX tile data for this process before the first ONNX session, so the int8 model uses the AVX-512 VNNI or AVX2 kernel. When that refusal is not in effect, because the filter could not be installed or tile permission was already granted, stderr gets one line, `Warning: the AMX guard is not in effect; the int8 model may use the AMX kernel`, and the command continues. Stdout and `--json` do not include it. Other builds do not print it.

`nesso --version --verbose` prints the policy for both models.

## Files

- `.json` and `.jsonl` are JSON, matched without regard to case. Every other name is read as lines: no extension, `.log.N` rotations, `.LOG`, `.txt`, `syslog`. `-` reads stdin as lines, and those matches are prefixed with `-:`. Stdin is not decompressed.
- A path may be a directory. nesso reads the regular files under it, recursively, and orders those files by unsigned byte order of the full path. The same paths produce the same chunks for any `--threads` value. Directory symlinks are not followed. A symlink passed as an argument is followed once, so a link to a directory is read and a link to a file is that file. File symlinks found while walking are kept; the link path is the source name and the bytes come from the target. Hidden files and directories are included. Fifos, sockets, and device nodes are skipped. A missing path, a dangling symlink, or a directory that cannot be listed fails the command. An unreadable file fails the command with the same open error as a file argument.
- A file whose first 4 KiB contains a NUL byte is rejected as binary. For `.gz`, that probe is the first 4 KiB of the inflated bytes. Bytes that are not UTF-8 are still text: an invalid sequence counts as one code point and is not rejected.
- A final extension `.gz`, in any case, is gzip, unless the name is `.tar.gz`. Concatenated members are one byte stream. The name under `.gz` selects the format (`app.log.gz` is lines, `events.json.gz` is one JSON document, `events.jsonl.gz` is JSON lines). A truncated or corrupt gzip stream fails with exit 2 and `Error: failed to parse '<path>': the gzip stream is truncated or corrupt`. Indexed gzip lines are stored in the corpus, like stdin, because a gzip member is not re-read by file offset. The corpus format is unchanged.
- `.tar`, `.tgz`, `.tar.gz`, `.tar.xz`, and `.zip` are read in place. Nothing is extracted. Members are visited in archive order, which does not depend on `--threads`. Each line's source is `bundle.tgz:var/log/syslog` (the path as given, a colon, and the member name). A leading `./` is removed and `\` is written as `/`. An absolute member name or a name that contains `..` is only that label. Directories, symlinks, hard links, and other non-regular members are skipped. A member whose first 4 KiB contains a NUL byte is skipped as binary. Those skips are summarized on stderr as `Skipped N entries in '<bundle>' (…)`. Stdout and `--json` stay the match lines only. A `.gz` member is inflated in memory and then read as lines, JSON, or JSONL from the name under `.gz`. An encrypted zip entry exits 2 and names the entry (`entry '…' is encrypted; decrypt it first`). A whole-file OpenPGP payload (armored `-----BEGIN PGP MESSAGE-----` or a binary PKESK/SKESK/SEIPD packet), checked only after open or read fails, exits 2 with a decrypt hint (`gpg --decrypt`). Other corrupt or unreadable archives exit 2 with `the archive is truncated or corrupt`. A `.gz` member that is not a complete gzip stream exits 2 with the gzip error. An empty archive, or one with nothing to rank, exits 1. Indexed member lines are stored in the corpus, like stdin. The corpus format is unchanged.
- Archive reads stop at a limit and exit 2. The message names the number and the override flag: `an archive entry is larger than N bytes; raise the limit with --archive-max-entry-bytes`, and the same pattern for `--archive-max-bytes`, `--archive-max-entries`, and `--archive-max-ratio`. The defaults are 256 MiB for one member, 2 GiB of uncompressed payload, 100000 regular-file members, and a ratio of 1000 (uncompressed payload divided by the archive's size on disk). `grep` and `index` accept those flags to change the caps. The ratio is checked against the bytes nesso would parse, after inflating a `.gz` member.
- Lines: one chunk per nonblank line. Lines longer than 4096 code points are truncated after the 4096th code point; a UTF-8 character is never split. A trailing CR is stripped.
- JSON: only objects with a nonblank string field are indexed (`message` by default, or `--json-field`). Malformed lines and documents are skipped. String values longer than 4096 code points are truncated the same way. A JSON array is numbered `element N`, not `line N`.
- `grep` and `index` embed the first raw line of each masked dedup key once, in first-occurrence order, in a fixed window of 1024 texts sorted by token count. The key replaces variable tokens (numbers, hex runs of 6 or more characters, UUIDs, IP addresses with an optional port, absolute paths that contain a digit, HDFS `blk_` ids, and timestamps) with `<*>`. Lines that share a key share a dense embedding and the same dense score. Each line is still printed and stored as its own text. Ties break by input order. `index` writes corpus format v3: one vector per distinct key, every input line in order (skipped lines included), each source's size and modification time, and the indexing policy (including dedup and mask rule versions). Stdin cannot be reread, so its text is stored in the corpus. `search` maps that file, scores each distinct vector once, and lists matching lines by score, then input order.
- `--json` on `grep` and `search` prints one JSON object per match. Keys, in order: `schema_version` (2), `rank`, `source`, `kind`, `line`, `score`, `text`, `occurrences`, `mode`, `dense`, `lexical`. `mode` is the effective mode after routing. `dense` and `lexical` are the raw dot product and BM25 score, or `null` when not computed. `score` is the value used for ranking in that mode. The object adds `occurrences`, how many input lines share that masked dedup key / distinct embedding id, always at least 1. Text output does not include the count unless `--group` is set. Counts stay on stderr. `index` has no `--json`.
- `--group` on `grep` and `search` prints one row per masked dedup key instead of one row per input line. The row's source, kind, line, and text are the first raw line of that key. `occurrences` is the number of input lines in the key, the same count an ungrouped row already reports. `-k` is applied after that collapse, so it counts groups. Text rows put the count on the location: `line 1 (3): 0.8123: first raw line`, or `app.log:line 1 (3): 0.8123: first raw line` when the source is printed. JSON keeps `schema_version` 2 and the same keys; `occurrences` is the group size. The corpus format does not change. Without `--group`, text and JSON are unchanged.
- A group's `score` is the ranking score of its best member, and `dense` and `lexical` are that member's values. Groups then sort with the mode's comparator. Ties break by the first line's input ordinal. `dense`: every member shares the key's dot product, so the first line is the best member; `score` and `dense` are that dot product and `lexical` is null. `lexical`: a key is a hit when any member has BM25 above 0. Members whose own text contains the query as a case-sensitive substring rank before the others; within a tier the higher BM25 wins, then the lower ordinal. `score` and `lexical` are that member's BM25 and `dense` is null. `search` reads a file line's own text for that substring test, and uses the stored text when the line is stored or the source cannot be re-read. A key with no positive BM25 is omitted. With no positive BM25 anywhere, lexical still falls back to hybrid. `hybrid`: the best member is the highest fused score (`0.8·z(dense) + 0.2·z(bm25)` over the same lines as an ungrouped hybrid search), then the lower ordinal. `score` is that fused value, `dense` is the shared dot product, and `lexical` is the best member's BM25. `auto` routes the same way as an ungrouped search and then uses that effective mode's rule. `mode` is the effective mode. Any `--threads` value prints the same bytes.
- Hybrid and lexical `grep` keep per-line terms and BM25 state in memory for the duration of the command; large inputs use more RAM than dense-only search.
- Empty and ASCII-whitespace-only lines and JSON string values are skipped and counted as empty lines. ASCII whitespace is space, tab, newline, carriage return, vertical tab, and form feed; other Unicode whitespace is not treated as blank. Nonblank text is not trimmed. JSON values without the selected string field are also skipped. Overlong log lines and JSON string values are truncated. Skip and truncation counts are printed on stderr. Re-index an existing corpus to remove previously indexed blank records.
- `search` re-reads each matching line from its source file. When a source changed, is missing, or cannot be read, `search` warns on stderr and prints the indexed text instead. Stdin, gzip, and archive members are stored in the corpus and are not re-read.
- `search` refuses a corpus written by an older format (v2) or a different model, vocabulary, quantization, or vector size; re-run `nesso index`. When only the batch, truncation, dedup, or mask policy differs, it warns once on stderr and continues.

## What this is not

- Not a hosted service. Text stays on the machine. There is no server, no cloud call, and no API key.
- Linux only.
