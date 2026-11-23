# Usage

`nesso grep` searches files in one shot. `nesso index` writes a corpus, and `nesso search` queries that file. `nesso overview` lists the rare or new event types of files without a model. The embedder is a local ONNX MiniLM model. Ranking uses dense embeddings, BM25, or hybrid fusion depending on `--mode`.

Matches go to stdout. Skipped lines, the index summary, and errors go to stderr. Every command exits 0 when matches or event types are printed or a corpus is written, 1 when there is nothing to search, index, or list, and 2 on error. Argument errors, including `-k 0`, also exit 2. `--help` lists these codes. A missing model names the directories that were tried and suggests `scripts/fetch-model` or `--model-dir`.

## Model

The `.deb`, the `.rpm`, and the container image ship the MiniLM files in `/usr/share/nesso`. A source build downloads them once:

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

`grep` and `search` accept `--mode auto|hybrid|dense|lexical` (default `auto`). Under `auto`, a pure identifier query (one token such as a UUID, a long hex or decimal number, `blk_-123`, `ERR-5001`, `attempt_1445144423722_0020_m_000002_1`, an IP address, or an absolute path) uses lexical ranking; other queries use hybrid fusion. For a natural-language query, a line's BM25 weight is scaled by how much of the query it contains, so a line that shares one word with a long description does not outrank meaning matches. A query with an identifier token keeps the full BM25 weight on every line. `dense` matches the pre-hybrid ranking. `hybrid` and `lexical` need corpus v3 BM25 postings from `nesso index` (grep builds terms in memory); without postings, or with a different term-rule version, those modes exit 2. Forced `lexical` with no BM25 hit falls back to hybrid and prints `Warning: no lexical match for the query; using hybrid`.

Index the same files once, then search the corpus. `-o` and `-i` are required:

```bash
nesso index -o corpus.nesso logs/ events.jsonl
nesso search "payment timeout" -i corpus.nesso -k 5
nesso search "payment timeout" -i corpus.nesso --group
```

`index` exits 2 without writing when `-o` names one of the input files, including through a hard link or symlink. Directories are expanded before that check, so an output file inside an input directory is rejected once that file exists.

From a source tree the binary is `build/Debug/src/nesso` or `build/Release/src/nesso`.

## Overview

`overview` takes inputs like `grep` (files, directories, bundles, or `-`) and lists event types instead of matches. It loads no model and has no `--model-dir` or `--threads`, so it runs where the MiniLM files are missing.

```bash
nesso overview app.log
nesso overview logs/ sosreport-host.tar.xz -k 50
nesso overview app.log.2 app.log.1 app.log --by new --json
```

An event type is the masked dedup key of a kept line, the same key `--group` uses: the same `--json-field`, after the 4096-code-point cut. Skipped lines are not counted. The inputs are parsed once.

- `--by rare` (default) lists the types with the fewest lines first. Equal counts list the type that appeared later first.
- `--by new` lists the type whose first line appeared latest first. "Latest" is input order: arguments as given, the files of a directory in byte order, archive members in archive order. There are no timestamps, so pass files oldest first (`app.log.2 app.log.1 app.log`).
- `-k`/`--top-k` caps the rows (default 20). `--json-field` and the `--archive-max-*` caps work as on `grep`. `--mode`, `--group`, `--provisional`, `--quiet`, `-o`, `--threads`, and `--model-dir` are not options of `overview` and exit 2.

A text row cites the type's first line and its line count: `SOURCE:line N (COUNT): TEXT`, or `element N` for a JSON array slot. The source is always printed; an archive member is `bundle.tar:entry`. The source and text are escaped like `grep` rows. There is no score. Stderr gets the skip and truncation counts and one summary line, `Listed 20 of 146 event types in 2000 lines.`

`--json` prints one object per type. Keys, in order: `schema_version` (2), `record` (`event_type`), `rank`, `count`, `source`, `kind`, `line`, `text`, `key` (the masked key), `first_seen` and `last_seen` (1-based ordinals over all kept lines, in input order), `lines` (kept lines), and `types` (distinct types). Invalid UTF-8 becomes U+FFFD. Match records of `grep` and `search` are unchanged and have no `record` key.

`overview` exits 0 when it prints rows, 1 when no line is kept, and 2 on a usage, parse, or archive error, or past 1,000,000 distinct event types. The same inputs print the same bytes on every run.

## Provisional results

`grep` reads its input once, then embeds it one window of 1024 distinct lines at a time, the most promising window first: windows where a line contains the query (with surrounding quotes removed) come first, then windows whose best line contains more of the query's words, then windows with more lines that occur once, then input order. A window keeps the same lines in the same order as in input order, so every score, and the final output, is the same as when the windows are embedded in input order. While `grep` embeds, it keeps the best dense matches seen so far. On a terminal it draws them on stderr, above the progress line: a header such as `Provisional (dense) top-5 after 2048/9120 lines:`, then one row per match in the text format of the final output, escaped and cut to the terminal width. The block is redrawn after a window of 1024 distinct lines is embedded and the list changed, at most once every 100 ms. Its rows are always dense scores, also under `auto`, `hybrid`, and `lexical`, which rank only at the end. When the scan ends, or fails, the block is erased and the progress line is drawn again, so the terminal ends as it does without the view. Stdout gets only the final matches, byte for byte the same.

The view is on when stderr is a terminal, `TERM` is set and is not `dumb`, and `--quiet` is not given. `--no-provisional` turns it off. `--provisional` cannot turn it on where one of those conditions fails.

`--json --provisional` writes each provisional list to stdout as JSON records instead, before the final records, and draws no view. A provisional record is the final record with `,"final":false,"refresh":N` before the closing brace: `N` counts the updates from 1 and `rank` starts at 1 in each update. Final records have no `final` key, and `schema_version` stays 2. Without the lines that contain `"final":false`, stdout is the plain `--json` output. The provisional records depend only on the input, the query, and the model, like the final ones. Since windows are embedded best first, the provisional sequence differs from the one an input-order scan would show: `after N/M lines` counts the input lines whose window is embedded so far. `--json` alone writes final records only.

## Version

`nesso --version` reports the package version. Builds from an untagged commit also include `+g` and the first 12 characters of its commit SHA, for example `0.3.1+g0123456789ab`. A build from its matching `vX.Y.Z` release tag reports only `X.Y.Z`.

## Threads

`--threads` and `NESSO_THREADS` set the same CPU budget `N`. An explicit `N` must be an integer from 1 to 1024; a value outside that range exits 2. Omit the option and leave the variable unset (or empty) for the default. `--threads` wins when both are set. `nesso search` uses that budget to score the corpus. One thread, and a small corpus, stay on the calling thread. The scores and the ranking match that single-thread scan. `grep` and `index` use the budget for embedding.

For the int8 model on x86, a budget of 4 or less is one intra-op thread and `N` sessions, and the arena is not shrunk. A larger budget uses `I = min(4, N/2)` (integer division) and `S = max(1, N/I)`, and shrinks the arena after each run. `N=8` is 2 sessions of 4 intra-op threads. `N=16` is 4 sessions of 4. On aarch64 `I` is at least 2, including when `N` is 1, so a session does not use the intra-op 1 path. When the option and the variable are both unset, `N` is the number of usable physical cores: topology cores in the process affinity mask (`Cpus_allowed_list`), then reduced by a cgroup `cpu.max` quota when a parent sets one. A positive quota smaller than one period counts as one CPU. The default session count is also reduced while `S` times the per-session estimate exceeds half of `MemAvailable` (total RAM when that key is missing), further capped by remaining cgroup memory headroom when a finite v1 or v2 memory limit applies. An explicit `N` is not reduced; nesso warns on stderr and continues.

For the fp32 model, `N` is the intra-op thread count of the one session. The default is the logical CPU count. On aarch64 that count is at least 2, including when `N` is 1.

On x86-64 Linux, nesso refuses Intel AMX tile data for this process before the first ONNX session, so the int8 model uses the AVX-512 VNNI or AVX2 kernel. When that refusal is not in effect, because the filter could not be installed or tile permission was already granted, stderr gets one line, `Warning: the AMX guard is not in effect; the int8 model may use the AMX kernel`, and the command continues. Stdout and `--json` do not include it. Other builds do not print it.

`nesso --version --verbose` prints the policy for both models.

## Files

For a short sosreport or tarball triage path, see [Quick start](quick-start.md).

- `.json` and `.jsonl` are JSON, matched without regard to case. Every other name is read as lines: no extension, `.log.N` rotations, `.LOG`, `.txt`, `syslog`. `-` reads stdin as lines, and those matches are prefixed with `-:`. Stdin is not decompressed.
- A path may be a directory. nesso reads the regular files under it, recursively, and orders those files by unsigned byte order of the full path. The same paths produce the same chunks for any `--threads` value. Directory symlinks are not followed. A symlink passed as an argument is followed once, so a link to a directory is read and a link to a file is that file. File symlinks found while walking are kept; the link path is the source name and the bytes come from the target. Hidden files and directories are included. Fifos, sockets, and device nodes are skipped. A missing path, a dangling symlink, or a directory that cannot be listed fails the command. An unreadable file fails the command with the same open error as a file argument.
- A file whose first 4 KiB contains a NUL byte is rejected as binary. For `.gz`, that probe is the first 4 KiB of the inflated bytes. Bytes that are not UTF-8 are still text: an invalid sequence counts as one code point and is not rejected.
- A final extension `.gz`, in any case, is gzip, unless the name is `.tar.gz`. Concatenated members are one byte stream. The name under `.gz` selects the format (`app.log.gz` is lines, `events.json.gz` is one JSON document, `events.jsonl.gz` is JSON lines). A truncated or corrupt gzip stream fails with exit 2 and `Error: failed to parse '<path>': the gzip stream is truncated or corrupt`. Indexed gzip lines are stored in the corpus, like stdin, because a gzip member is not re-read by file offset. The corpus format is unchanged.
- `.tar`, `.tgz`, `.tar.gz`, `.tar.xz`, and `.zip` are read in place. Nothing is extracted. Members are visited in archive order, which does not depend on `--threads`. Each line's source is `bundle.tgz:var/log/syslog` (the path as given, a colon, and the member name). A leading `./` is removed and `\` is written as `/`. An absolute member name or a name that contains `..` is only that label. Directories, symlinks, hard links, and other non-regular members are skipped. A member whose first 4 KiB contains a NUL byte is skipped as binary. Those skips are summarized on stderr as `Skipped N entries in '<bundle>' (…)`. Stdout and `--json` stay the match lines only. A `.gz` member is inflated in memory and then read as lines, JSON, or JSONL from the name under `.gz`. An encrypted zip entry exits 2 and names the entry (`entry '…' is encrypted; decrypt it first`). A whole-file OpenPGP payload (armored `-----BEGIN PGP MESSAGE-----` or a binary PKESK/SKESK/SEIPD packet), checked only after open or read fails, exits 2 with a decrypt hint (`gpg --decrypt`). Other corrupt or unreadable archives exit 2 with `the archive is truncated or corrupt`. A `.gz` member that is not a complete gzip stream exits 2 with the gzip error. An empty archive, or one with nothing to rank, exits 1. Indexed member lines are stored in the corpus, like stdin. The corpus format is unchanged.
- Archive reads stop at a limit and exit 2. The message names the number and the override flag: `an archive entry is larger than N bytes; raise the limit with --archive-max-entry-bytes`, and the same pattern for `--archive-max-bytes`, `--archive-max-entries`, and `--archive-max-ratio`. The defaults are 256 MiB for one member, 2 GiB of uncompressed payload, 100000 regular-file members, and a ratio of 1000 (uncompressed payload divided by the archive's size on disk). `grep` and `index` accept those flags to change the caps. The ratio is checked against the bytes nesso would parse, after inflating a `.gz` member.
- Lines: one chunk per nonblank line. Lines longer than 4096 code points are truncated after the 4096th code point; a UTF-8 character is never split. A trailing CR is stripped.
- JSON: only objects with a nonblank string field are indexed (`message` by default, or `--json-field`). Malformed lines and documents are skipped. String values longer than 4096 code points are truncated the same way. A JSON array is numbered `element N`, not `line N`.
- `grep` and `index` embed the first raw line of each masked dedup key once, in first-occurrence order, in a fixed window of 1024 texts sorted by token count. The key replaces variable tokens (numbers, hex runs of 6 or more characters, UUIDs, IP addresses with an optional port, absolute paths that contain a digit, HDFS `blk_` ids, timestamps, and tokens that mix letters and digits such as host and node names, sizes, and durations: `dn228`, `R02-M1-N0-C:J12-U11`, `126MB`, `30000ms`) with `<*>`. Words of two bytes (`v2`) stay literal, and a dot leader ends a token. Lines that share a key share a dense embedding and the same dense score. Each line is still printed and stored as its own text. Ties break by input order. `index` writes corpus format v3: one vector per distinct key, every input line in order (skipped lines included), each source's size and modification time, and the indexing policy (including dedup and mask rule versions). Stdin cannot be reread, so its text is stored in the corpus. `search` maps that file, scores each distinct vector once, and lists matching lines by score, then input order.
- `--json` on `grep` and `search` prints one JSON object per match. Keys, in order: `schema_version` (2), `rank`, `source`, `kind`, `line`, `score`, `text`, `occurrences`, `mode`, `dense`, `lexical`. `mode` is the effective mode after routing. `dense` and `lexical` are the raw dot product and BM25 score, or `null` when not computed. `score` is the value used for ranking in that mode. The object adds `occurrences`, how many input lines share that masked dedup key / distinct embedding id, always at least 1. Text output does not include the count unless `--group` is set. Counts stay on stderr. `index` has no `--json`. `grep --json --provisional` also writes provisional records (see [Provisional results](#provisional-results)).
- `--group` on `grep` and `search` prints one row per masked dedup key instead of one row per input line. The row's source, kind, line, and text are the first raw line of that key. `occurrences` is the number of input lines in the key, the same count an ungrouped row already reports. `-k` is applied after that collapse, so it counts groups. Text rows put the count on the location: `line 1 (3): 0.8123: first raw line`, or `app.log:line 1 (3): 0.8123: first raw line` when the source is printed. JSON keeps `schema_version` 2 and the same keys; `occurrences` is the group size. The corpus format does not change. Without `--group`, text and JSON are unchanged.
- A group's `score` is the ranking score of its best member, and `dense` and `lexical` are that member's values. Groups then sort with the mode's comparator. Ties break by the first line's input ordinal. `dense`: every member shares the key's dot product, so the first line is the best member; `score` and `dense` are that dot product and `lexical` is null. `lexical`: a key is a hit when any member has BM25 above 0. Members whose own text contains the query as a case-sensitive substring rank before the others; within a tier the higher BM25 wins, then the lower ordinal. `score` and `lexical` are that member's BM25 and `dense` is null. `search` reads a file line's own text for that substring test, and uses the stored text when the line is stored or the source cannot be re-read. A key with no positive BM25 is omitted. With no positive BM25 anywhere, lexical still falls back to hybrid. `hybrid`: the best member is the highest fused score (`0.8·z(dense) + 0.2·z(bm25)·coverage²` over the same lines as an ungrouped hybrid search, where `coverage` is the share of the query's terms in that line, and is 1 for a query with an identifier token), then the lower ordinal. `score` is that fused value, `dense` is the shared dot product, and `lexical` is the best member's BM25. `auto` routes the same way as an ungrouped search and then uses that effective mode's rule. `mode` is the effective mode. Any `--threads` value prints the same bytes.
- Hybrid and lexical `grep` keep per-line terms and BM25 state in memory for the duration of the command; large inputs use more RAM than dense-only search.
- Empty and ASCII-whitespace-only lines and JSON string values are skipped and counted as empty lines. ASCII whitespace is space, tab, newline, carriage return, vertical tab, and form feed; other Unicode whitespace is not treated as blank. Nonblank text is not trimmed. JSON values without the selected string field are also skipped. Overlong log lines and JSON string values are truncated. Skip and truncation counts are printed on stderr. Re-index an existing corpus to remove previously indexed blank records.
- `search` re-reads each matching line from its source file. When a source changed, is missing, or cannot be read, `search` warns on stderr and prints the indexed text instead. Stdin, gzip, and archive members are stored in the corpus and are not re-read.
- `search` refuses a corpus written by an older format (v2) or a different model, vocabulary, quantization, vector size, or prompt-prefix rule (`embed.prefix`; a corpus without that key reads as `none`); re-run `nesso index`. The error names the first key that differs and both values, for example `(model.id: stored X, loaded Y)`. A corpus over this build's source, line, distinct-text, or lexical caps fails with `the corpus exceeds this nesso's limits`, and `index` stops with the same message when its input reaches any of them. When only the batch, truncation, dedup, or mask policy differs, it warns once on stderr and continues.

## Limits and memory

- A corpus holds at most 1,000,000 sources, 10,000,000 lines, 1,000,000 distinct texts (one per masked dedup key), 4,000,000 lexical terms, and 512 MiB of lexical data. `index` stops when the input reaches any of them, and `search` refuses a corpus past them. Both exit 2. The error reports that a limit was exceeded.
- `search` memory grows with the line count. It maps the corpus file and builds about 64 bytes of line metadata per line, about 640 MB at the 10,000,000-line cap, plus per-line lexical postings. The 64-byte and 640 MB figures are approximate, from the line-metadata layout on x86-64.
- `index` bounds its Finish working set (unique-term runs, the merge, and posting encode) to an internal 1.5 GiB budget. It still keeps every distinct text in memory.
- `grep` builds no corpus, so these caps do not apply. In hybrid and lexical mode its memory grows with the input ([Files](#files)).
- The nightly real-log benchmark fails when a command's peak RSS exceeds 1.5 GiB plus tolerance (that workflow sets `NESSO_BENCH_RSS_TOLERANCE` to 0.15). It runs on the first 1,000,000 HDFS lines and the first 1,000,000 Thunderbird lines. That is a CI gate on those inputs, not a guarantee for every input ([Benchmarks](benchmarks.md)).
- Split a very large input into several corpora, for example per host or per day, or run `grep` with `--mode dense` on a subset.

## What this is not

- Not a hosted service. Text stays on the machine. There is no server, no cloud call, and no API key.
- Linux only.
