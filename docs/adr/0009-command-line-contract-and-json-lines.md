---
status: "proposed"
date: "2026-10-28"
decision-makers: Lorenzo Caprari
---

# Keep a stable command-line contract and add JSON Lines output

## Context and Problem Statement

Scripts depend on the exit status and on stdout. `src/cli_parser.cpp` registers `grep`, `index`, and `search`, plus `-V`/`--version` and `--verbose`. `--json-field` is on `grep` and `index`; `-k`/`--top-k` is on `grep` and `search`; `--quiet` is on `grep` and `index`; `--threads` and `--model-dir` are on all three; `-o`/`--output` is `index`; `-i`/`--index` is `search`. Omitted `--threads` reads `NESSO_THREADS`; omitted `--model-dir` reads `NESSO_MODEL_DIR` (`src/model_paths.cpp`). No command accepts `--json`. There is no `clean` subcommand. What is stable, what does `--json` print, when is stdin read, where does the `search` corpus go, and when is stdout coloured?

## Decision Drivers

* A caller can use the exit status and stdout without reading stderr.
* The JSON printer still finishes when `source` or `text` is not UTF-8.
* The same input, binary, and model keep the bytes [ADR 0003](0003-byte-identical-determinism.md) requires.
* A new JSON key does not break a reader that ignores unknown keys.

## Considered Options

* JSON Lines: one object per match, each with `schema_version`
* One JSON document per run: a header object and a `matches` array
* No JSON; the text line is the only contract

## Decision Outcome

Chosen option: "JSON Lines: one object per match, each with `schema_version`", because a line can be read before the process exits, and text mode is already one line per match.

**Today** (`src/cli.cpp`, `src/main.cpp`, `src/nesso.cpp`). `--help` and `--version` exit 0 and write stdout; `--version` leaves stderr empty (`test/functional/features/harness.feature`, `test/functional/features/errors.feature`). `grep` and `search` exit 0 when ranking prints matches, and 1 when there is nothing to rank: zero `grep` chunks (`EmptyInput`) or a `search` corpus with zero chunks (`the corpus is empty`). A successful rank of a non-empty input prints up to `-k` lines and exits 0. There is no score cutoff. `failureStatus` maps `NoMatches` to 1 with an empty `message()`. `index` exits 0 when the write succeeds, and stderr is `Indexed <n> chunks into '<path>'.` Zero chunks exit 1 with an actionable stderr reason, stdout stays empty, and the corpus is not created or replaced (`test/functional/features/index_search.feature`). The `src/cli_parser.cpp` footer says 0 when matches are printed or a corpus is written, 1 when there is nothing to search or index, and 2 on error. `docs/usage.md` states the same rule. Usage errors, including `-k 0` and a bad `NESSO_THREADS`, and every other failure exit 2 ([#69](https://github.com/lorenzocaprari/nesso/pull/69), [#73](https://github.com/lorenzocaprari/nesso/pull/73)). `grep` and `index` require a path; `-` is explicit stdin. `search` requires `-i`/`--index` and takes no positional corpus. There is no `--color`. `index -o` is required, and an `-o` value of `-` names a file.

For `grep`, `index`, and `search`, stdout carries match lines only. Skip and truncation counts, stale-source warnings, the index summary, progress, and errors go to stderr. Progress runs for `grep` and `index` only when stderr is a tty and `--quiet` is absent (`src/nesso.cpp`). `NESSO_TRACE=1` adds one stderr JSON line (`src/trace.hpp`). One non-stdin source prints `line N: ` or `element N: ` with a `{:.4f}` score. `element` is a JSON array slot. The `source:` prefix is used when more than one source was ranked, or the source is `-` (`grep` counts paths that produced a chunk; `search` counts corpus sources). Order is score descending, then ascending input ordinal (`Nesso::grep`, `core::topK` in `src/core/include/core/top_k.hpp`). The printer writes the stored `source` and `text`.

**Decided here.** Rules that **Today** does not already describe are not in `src/` yet.

* Zero-chunk `nesso index` exits 1 and writes an actionable stderr reason: why no chunk was produced. The corpus is not created or replaced.
* `--json` on `grep` and `search` prints one object per line. Keys, in order: `schema_version` (1), `rank` (from 1, same order), `source` (always; path as given, or `-`; for `search`, the path `index` stored), `kind` (`line` or `element`), `line` (1-based), `score` (shortest round-trip form), `text`. Invalid UTF-8 in `source` and `text` becomes U+FFFD and does not abort the printer. Text mode stays as stored. Counts stay on stderr. `index` has no `--json`.
* Stable: subcommand and option names and meanings (including `--color` and a positional `search` corpus), `NESSO_THREADS`, `NESSO_MODEL_DIR`, `NO_COLOR`, these exit statuses (zero-chunk `index` included), and the uncoloured text line. Not stable: help text, stderr wording, progress, colour codes, the `--version --verbose` policy text, and `NESSO_TRACE`.
* Breaking: removing or renaming a subcommand, option, environment variable, or JSON key; changing a key's type or meaning; changing an exit status's meaning; changing the text line or the tie-break. Not breaking: a new option, a new JSON key, stderr wording. Also not breaking: implicit stdin and a default `-o` (a call that exited 2 now runs), and rejecting `index -o -`. `schema_version` changes only when that object breaks. `version` in `conanfile.py` is 0.3.0. A breaking change uses a `!` title. While the version is 0.x, the release commit that ships it sets a minor. A deprecated option keeps working for at least one minor release, prints one stderr warning, and is listed in `CHANGELOG.md` before removal.
* Implicit stdin. When `grep` or `index` is given no input file and stdin is not a terminal, read stdin, including `/dev/null` and an open pipe. The source label stays `-`, so the text line is unchanged. A terminal with no file still exits 2. A call that used to fail with exit 2 now runs (exit 0 or 1); this is not breaking. Options considered: require an explicit `-` (today); read stdin only for a pipe; read any non-terminal stdin (chosen). Consequence: `/dev/null` and an empty open pipe have nothing to rank and exit 1.
* Positional corpus. `nesso search QUERY CORPUS`. `--index` stays. `-i` is deprecated: it clashes with grep's ignore-case, keeps working for at least one minor release, prints one stderr warning, and is listed in `CHANGELOG.md`, then is removed in a later minor release as a breaking change (`!` title). Options considered: keep `-i,--index` as the only form (today); drop `-i` now; add the positional and deprecate `-i` (chosen). Consequence: stdout is unchanged while `-i` remains; the warning is stderr, so its wording is not stable. Passing the positional and `--index` (or `-i`) together is a usage error (exit 2).
* Colour. On by default only when stdout is a terminal. Off when stdout is not a terminal, with `--color=never`, or when `NO_COLOR` is set to any non-empty value (per no-color.org). An empty `NO_COLOR` does not turn colour off. `--color=always` forces colour on text output, including when stdout is not a terminal or `NO_COLOR` is set. JSON output is never coloured. The stable text-line contract applies to uncoloured output (stdout is not a terminal, `--color=never`, or a non-empty `NO_COLOR`). Options considered: never colour; always colour; this policy (chosen). Consequence: colour codes are not the stable text line, and they are not the bytes [ADR 0003](0003-byte-identical-determinism.md) compares.
* `index -o -` is rejected with exit 2 instead of writing a file named `-`. Stdout carries match lines only, so the corpus is not written there. This is a fix, not breaking. Options considered: write the corpus to stdout; write a file named `-` (today); reject (chosen). Consequence: a caller cannot send the corpus to stdout or create a file named `-`.
* Omitted `index -o` defaults to `<first input file>.nesso` (the suffix is appended to that path). Options considered: keep `-o` required (today an omission exits 2); this default (chosen). Consequence: an omission that used to exit 2 now writes that file, and that is not breaking. When the first input is `-`, there is no file path, so `-o` stays required and the command still exits 2.

### Consequences

* Good, because matches can be read without splitting on `: `.
* Good, because the tie-break and exit statuses are recorded. The features expect zero-chunk `index` to exit 1.
* Bad, because each JSON line repeats `schema_version`.
* Bad, because the JSON `score` has more digits than `{:.4f}`. ADR 0003 still requires the same binary to repeat its stdout.
* Bad, because `--help` and `docs/usage.md` say 4096 characters, and `limitValue` in `src/parser/log_chunker.cpp` resizes at 4096 bytes, which can split a UTF-8 sequence. Changing that cut changes the embedded bytes.

### Confirmation

`test/functional/features/` checks help, version, exit 0/1/2, the match line, stdin, and a JSON array element. It has no `--json` scenario. The zero-chunk `index` scenario expects exit 1. It does not cover implicit stdin, a positional corpus, `--color`, or a default `-o`.

## Pros and Cons of the Options

### JSON Lines: one object per match, each with `schema_version`

* Good, because a reader can stop after the first line.
* Bad, because skip counts stay on stderr.

### One JSON document per run: a header object and a `matches` array

* Good, because `schema_version` is written once.
* Bad, because the document is incomplete until the process exits.

### No JSON; the text line is the only contract

* Good, because there is no second printer.
* Bad, because the score and the text share a line, and the text can contain `: `.
