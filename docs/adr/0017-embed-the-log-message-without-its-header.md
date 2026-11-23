---
status: "accepted"
date: "2026-11-23"
decision-makers: Lorenzo Caprari
---

# Embed the log message without its header

## Context and Problem Statement

`index` and `grep` embed each line as read (`detail::EmbedDistinct`, `src/nesso/nesso.cpp`). Most log lines start with a header: a timestamp, often an epoch or a second date, a host or node, a process and pid, a level, and a component. MiniLM gives those tokens the same weight as the message, so lines from the same host or the same minute look alike, and a symptom query ("authentication failed for root") has to compete with a dozen tokens that say nothing about the symptom. Headers also take part of the 256-token budget (`tokens.max`) before the message is reached.

Which part of a line should be embedded, how is it cut, and what does a corpus record about it?

## Decision Drivers

* Dense recall on symptom queries, measured on the nesso-bench dev split only.
* `index` and `grep` give the same vector for the same line ([ADR 0004](0004-streaming-batch-pipeline.md)), and runs stay byte-identical ([ADR 0003](0003-byte-identical-determinism.md)).
* What a user sees does not change: stored text, printed lines, `--json`, lexical scoring ([ADR 0011](0011-hybrid-search-and-identifier-routing.md)), and the dedup key ([ADR 0010](0010-corpus-format-v3.md)).
* One place cuts a line. A later parser change reads structured header fields, the template, and the variables out of the same cut instead of adding a second one.
* Unknown formats are not damaged: when in doubt, the whole line is embedded.

## Considered Options

* Keep embedding the whole line
* Mask header tokens and embed the masked line
* Cut a lossless `{header, message}` split with a fixed grammar and embed the message
* Infer each file's header shape from its first lines

## Decision Outcome

Chosen option: "Cut a lossless `{header, message}` split with a fixed grammar and embed the message".

* **Splitter.** `parser::SplitLogLine` (`src/parser/include/parser/line_header.hpp`) returns two views, `header` and `message`, with `header + message` equal to the line, byte for byte. It is one left-to-right pass with no regular expression, locale, or allocation. A header needs a timestamp anchor at the start (ISO 8601 / RFC 3339, RFC 3164, RFC 5424, a bracketed timestamp, an epoch followed by a date, or a compact `YYMMDD HHMMSS` / `YYYYMMDD-HH:MM:SS` form), optionally after a syslog priority, a label column before an epoch, or a log file name (a token containing `.log`) before an ISO timestamp. After the anchor it reads up to 8 fields within 256 bytes, and never takes the last token of the line. A level, the fields after a level, a `name[pid]:` / `component:` tag, and pipe-delimited fields end the header; other fields (numbers, hosts, dates, `[...]`, upper-case words, dotted names) join it only when such a field follows. The header doc comment is the full grammar.
* **Fallback.** With no anchor (prose, JSON, `key=value` lines, a leading blank), or when the message would hold no ASCII letter and no non-ASCII byte, the header is empty and the whole line is embedded.
* **Where the cut is used.** Only in `EmbedDistinct`, which `index` and `grep` both call. Queries are embedded whole. Dedup keys, stored text, BM25 terms, the window promise, and output keep the whole line.
* **Dedup.** The key stays `MaskTemplate(line)`. A key's vector is the embedding of its first raw line's message, as before it was the first raw line. Two lines with one key differ only in masked tokens, but a range check can still cut them differently (`081109` is a date, `081399` is not; both mask to `<*>`). They share the first line's vector, so `index` and `grep` agree. Lines with different keys and the same message are embedded once per key, as today.
* **Policy.** `index` records `embed.text=message-v1` (`parser::HEADER_RULE_VERSION`). A corpus without the key reads as `line`. `search` warns once when the value differs from this build's, like the batch, truncation, dedup, and mask keys, and continues: the query is embedded alone, so its vector stays comparable. The corpus format stays v3 ([ADR 0015](0015-index-migration-and-the-model-header.md), option A1). Any rule change bumps the value.
* **Eval hygiene.** Shapes come from public log formats and the nesso-bench dev systems. No shape exists only for a held-out system; formats seen only there (for example `MM-DD` or `YY/MM/DD` dates) stay unsupported until a dev system needs them.

### Consequences

* Good, because the model sees the message, and long lines keep more message tokens within `tokens.max`.
* Good, because the split is lossless and kept in one function, so the structured-header work extends it.
* Neutral, because embedding cost per distinct text is unchanged; shorter inputs make each run cheaper.
* Bad, because a query whose only clue was in the header (a host name, a level word) loses that clue in dense scoring. BM25 still sees it, so `auto` and `hybrid` keep part of it.
* Bad, because a message that starts with a word shaped like a tag (`Failed: …`) loses that word to the header.
* Bad, because existing corpora warn until re-indexed.

### Confirmation

* `test/unit/parser/line_header_test.cpp`: every shape, every fallback, the field and byte limits, and 10,000 generated lines that must split losslessly.
* `test/unit/nesso/nesso_facade_test.cpp`: `index` and `grep` pass only messages to the embedder and the query whole; lines that share a key embed the first line's message; a corpus without `embed.text` warns; with the model, `search` and `grep` give the same dense scores at 1 and 4 threads.
* BDD (`index_search.feature`): `grep` and `search` print the same dense `--json` for header lines, and a whole-line corpus warns.
* The parser fuzz harness aborts when a split is not lossless. `scripts/check-determinism` input has a header line in every five.

## Pros and Cons of the Options

### Keep embedding the whole line

* Good, because nothing changes.
* Bad, because header tokens dilute every vector.

### Mask header tokens and embed the masked line

* Good, because no grammar is needed: `MaskTemplate` already replaces timestamps and numbers.
* Bad, because the model then sees `<*>` tokens and the literal words around them (level, program, host words without digits), which still dilute the vector.

### Lossless split with a fixed grammar

* Good, because it is deterministic, cheap, testable byte for byte, and gives the later parser one place to extend.
* Bad, because a format the grammar does not know is embedded whole until a rule is added.

### Infer the header shape per file

* Good, because it adapts to unknown formats.
* Bad, because the cut of a line would depend on the other lines in its file and on input order, so `grep` on part of a file could cut a line differently from `index` on all of it.
