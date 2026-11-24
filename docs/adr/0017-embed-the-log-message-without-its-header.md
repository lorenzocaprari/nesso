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

* **Splitter.** `parser::SplitLogLine` (`src/parser/include/parser/line_header.hpp`) returns two views, `header` and `message`, with `header + message` equal to the line, byte for byte. It is one left-to-right pass with no regular expression, locale, or allocation. A header needs a timestamp anchor at the start (ISO 8601 / RFC 3339, RFC 3164, RFC 5424, a bracketed timestamp, an epoch followed by a date, or a compact `YYMMDD HHMMSS` / `YYYYMMDD-HH:MM:SS` form), optionally after a syslog priority, a label column before an epoch, or a log file name (a token containing `.log`) before an ISO timestamp. After the anchor it reads up to 8 fields within 256 bytes and never takes the last token of the line. Noise fields (dates and times, pids and thread numbers, hosts, `[...]` fields) join the header. A `program[pid]:` tag, and pipe-delimited fields right after the anchor, end the header after them. A level, a dotted component (`org.apache.Main`), or an upper-case subsystem word (`KERNEL`) starts the message, so `FATAL`, `RAS KERNEL`, and Java class names stay embedded. Noise fields join only when such a field follows them; otherwise the header is the anchor alone. The header doc comment is the full grammar.
* **Fallback.** With no anchor (prose, JSON, `key=value` lines, a leading blank), or when the message would hold no ASCII letter and no non-ASCII byte, the header is empty and the whole line is embedded.
* **Where the cut is used.** Only in `EmbedDistinct`, which `index` and `grep` both call. Queries are embedded whole. Dedup keys, stored text, BM25 terms, the window promise, and output keep the whole line.
* **Dedup.** The key stays `MaskTemplate(line)`. A key's vector is the embedding of its first raw line's message, as before it was the first raw line. Two lines with one key differ only in masked tokens, but a range check can still cut them differently (`081109` is a date, `081399` is not; both mask to `<*>`). They share the first line's vector, so `index` and `grep` agree. Lines with different keys and the same message are embedded once per key, as today.
* **Policy.** `index` records `embed.text=message-v1` (`parser::HEADER_RULE_VERSION`). A corpus without the key reads as `line`. `search` warns once when the value differs from this build's, like the batch, truncation, dedup, and mask keys, and continues: the query is embedded alone, so its vector stays comparable. The corpus format stays v3 ([ADR 0015](0015-index-migration-and-the-model-header.md), option A1). Any rule change bumps the value.
* **Eval hygiene.** Shapes come from public log formats and the nesso-bench dev systems. No shape exists only for a held-out system; formats seen only there (for example `MM-DD` or `YY/MM/DD` dates) stay unsupported until a dev system needs them.

### Consequences

* Good, because the model sees the message, and long lines keep more message tokens within `tokens.max`.
* Good, because the split is lossless and kept in one function, so the structured-header work extends it.
* Neutral, because embedding cost per distinct text is unchanged; shorter inputs make each run cheaper.
* Bad, because a query whose only clue was a host name or a syslog program name loses that clue in dense scoring. BM25 still sees it, so `auto` and `hybrid` keep part of it.
* Bad, because a message without a level that starts with a word shaped like a tag (`Failed: …`) loses that word to the header.
* Bad, because existing corpora warn until re-indexed.

### Where the cut ends

The first version cut through the level and the component as well. On the nesso-bench dev split it failed the `eval-retrieval` gate on Hadoop dense paraphrase queries (MRR −0.312, 95% CI [−0.562, −0.062], 8 queries). The losing query looked for "task attempt … no route to host"; the cut had removed `FATAL` and `TaskAttemptListenerImpl`, the words that answered it. Keeping the level, components, and subsystem words, while still cutting syslog program tags and pipe fields, removed every flagged regression on the dev split (local runs, nesso-bench `140bb9e`, `scripts/eval-score-nesso`, baseline `main` after #382):

| Dev report (queries) | Mode | main MRR / nDCG@10 | this rule |
| --- | --- | --- | --- |
| all_2k (316) | auto | 0.7664 / 0.7686 | 0.7667 / 0.7755 |
| all_2k | dense | 0.4380 / 0.4432 | 0.4597 / 0.4709 |
| formats (72) | auto | 0.8171 / 0.8217 | 0.8106 / 0.8211 |
| formats | dense | 0.4831 / 0.4920 | 0.4681 / 0.4845 |
| needle (34) | auto | 0.6201 / 0.6138 | 0.5960 / 0.5901 |
| needle | dense | 0.6201 / 0.6118 | 0.5679 / 0.5569 |

`eval-retrieval-compare --fail-on-worse` reports no WORSE line on any of the three reports. The needle and formats dense scores are lower but within noise on these query counts. The rule was chosen on the dev split only, after four variants, so the held-out systems scored at release are the check on overfitting.

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
