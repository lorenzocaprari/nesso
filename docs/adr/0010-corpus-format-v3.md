---
status: "proposed"
date: "2026-10-26"
decision-makers: Lorenzo Caprari
---

# Corpus format v3: one vector per distinct text and a recorded embedding policy

## Context and Problem Statement

Corpus v2 ([#68](https://github.com/lorenzocaprari/nesso/pull/68)) stores a 64-byte header that starts with magic `NESC`, then the model id, a source table, one record and the text per chunk, and one f32 vector per chunk in a block aligned to 64 bytes (`src/core/corpus_index.cpp`). The model id `index` writes is `sentence-transformers/all-MiniLM-L6-v2`. It does not say int8 or fp32. `search` compares that string and, when it differs, exits 2 with `the corpus was built with a different model; re-run nesso index` before the model loads (`src/nesso.cpp`). Both quantizations store the same string, so the check does not separate them. A stored width that differs from the query embedding fails later with `the embedding width does not match`. Nothing records the batch or truncation policy. A version other than 2 exits 2 with `the corpus format is not supported by this nesso; re-run nesso index`.

Planned changes alter the bytes `index` writes:
* Lines that are the same, or the same once variable tokens are masked, get one embedding.
* Batches are sorted by token count inside a fixed window and capped by a token budget. Which texts share a padded window changes int8 vectors ([ADR 0004](0004-streaming-batch-pipeline.md)).
* Lines are truncated at 4096 code points instead of 4096 bytes (`limitValue` in `src/parser/log_chunker.cpp`).
* Vectors are stored once per distinct text.

How should the format change so that users re-index once and a corpus built under other rules is detected?

## Decision Drivers

* One format bump covers all of these changes, shipped in one release.
* `search` refuses a corpus whose vectors are not comparable with its query embedding.
* The same binary, model, input, and policy give the same corpus bytes for any `--threads` value ([ADR 0003](0003-byte-identical-determinism.md)).
* Corpus size follows distinct texts, not lines.

## Considered Options

* v3 with distinct vectors, per-line references, and a policy block; refuse v2
* v3 as above, and keep a v2 reader
* Keep v2 and record the policy in the unused 8-byte header field

## Decision Outcome

Chosen option: "v3 with distinct vectors, per-line references, and a policy block; refuse v2", because one reader stays small and a v2 vector was built under other rules anyway.

* **Layout.** The file starts with `NESC`, version 3, and a fixed header of counts and section offsets. Then come the source table (path, size, and mtime, as the current v2 writer stores them), line records, the distinct texts, stored line texts, and one f32 vector per distinct text, in a block aligned to 64 bytes and mapped as the top-k matrix.
* **Line records.** One record per input line, in source order and then line order, including skipped lines: source, line number, byte offset, length, flags, and a distinct id (none for a skipped line). A line from a source that cannot be re-read (stdin, an entry inside an archive) also points at its own stored text.
* **Policy block.** It holds sorted UTF-8 `key=value` records:
  * the model sha256, quantization, and vocab sha256;
  * `tokens.max=256` and `truncate=4096-codepoints`;
  * `batch=window:1024,max:64,tokens:2048,order:length-then-input`;
  * the dedup key and mask rule versions.

  Values are constants in the source, never derived from core count or RAM.
* **On open.**
  * A version other than 3 exits 2 with "corpus format vN is not supported; re-run `nesso index`" ([ADR 0009](0009-command-line-contract-and-json-lines.md)).
  * A different model sha256, quantization, dimension count, or vocab also exits 2.
  * A different batch, truncation, dedup, or mask rule prints one stderr warning, then searches. The query is embedded alone, so it stays comparable.
* **Results.** Each distinct text is scored once. Its lines share the score and are ordered by score descending, then line ordinal. A line's own text is its stored text if it has one, else it is read through its byte offset while the source's size and mtime match. Otherwise nesso falls back to the stored distinct text, with the existing stale-source warning.
* **Context lines (`-A/-B/-C`).** The neighbors of a hit are the adjacent records of the same source, so the corpus alone says which lines they are. Their text comes from the same places as a hit's: stored text for stdin and archive entries, otherwise a re-read of the source through the byte offset. If the source changed, nesso prints the hit without context, because a masked neighbor's stored distinct text is not that line.

### Consequences

* Good, because users re-index once for dedup, masking, sorted batching, and the truncation fix.
* Good, because an fp32-built corpus is no longer silently searched with int8 queries.
* Bad, because every v2 corpus must be rebuilt.
* Bad, because a changed source shows the stored distinct text for masked lines and loses context lines.
* Bad, because stdin and archive input store every line's text, so their corpora grow with the input.

### Confirmation

`scripts/check-determinism` compares v3 corpus bytes and rankings across fresh processes under load. The int8 run with `NESSO_THREADS=$(nproc)` must match the default run, and that comparison must be a blocking check. Unit tests round-trip every section, reject v2, truncated, and misaligned files, and read context from stored texts. BDD covers the v2 error, the model mismatch, and the policy warning.

## Pros and Cons of the Options

* v3, refuse v2: good, a mismatch is caught from the header; bad, the policy block is a second format to validate.
* v3 plus a v2 reader: good, old corpora keep working; bad, two readers to test, for vectors built under other rules.
* v2 plus the unused header field: good, no layout change; bad, that field cannot hold a model hash, and vectors stay per line.
