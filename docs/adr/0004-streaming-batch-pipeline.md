---
status: "accepted"
date: "2026-10-13"
decision-makers: Lorenzo Caprari
---

# Stream parse, embed, and score in fixed-size batches

## Context and Problem Statement

`grep` and `index` used to keep every parsed chunk, embed the whole list,
and then rank or write it. [PR #61](https://github.com/lorenzocaprari/nesso/pull/61)
already runs ONNX inference in windows of 32, each padded to that window's
longest sequence, instead of one run of the corpus padded to 256. The caller
still held every chunk and every embedding until `rankChunks` or
`writeCorpusFile` returned. How should parse, embed, and score run so the
embeddings that stay resident follow the batch?

## Considered Options

* Load every chunk, then embed
* Stream fixed-size batches
* Multi-threaded producer/consumer pipeline

## Decision Outcome

Chosen option: "Stream fixed-size batches", because `grep` keeps only the top
matches and `index` spills each embedding batch. Accepted in
[064f888](https://github.com/lorenzocaprari/nesso/commit/064f888871babcc8d842c4b48233d9ac647c61c1),
merged as [PR #88](https://github.com/lorenzocaprari/nesso/pull/88) on 2026-10-13.

### Consequences

* Good, because `grep` keeps at most `topK` matches after each batch (score
  descending, then ascending ordinal) and `index` spills each batch.
  Each flush is one fixed window of 1024 texts
  (`BATCH_WINDOW` in `src/embed/include/embed/batch_plan.hpp`).
  That window is sorted by token count and cut at 32 texts and 1024 padded
  tokens. The groups do not follow the session count.
  [ADR 0010](0010-corpus-format-v3.md) records the same policy for corpus
  format v3. This tree still writes the current corpus format, so an
  existing corpus needs `nesso index` again. `src/cli/cli_parser.cpp` has no
  batch option.
* Good, because an embed, score, or spill error returns from that batch.
  `index` replaces the destination only when `CorpusWriter::finish` completes,
  and the destructor removes the spill while the writer is still open.
* Bad, because files are parsed twice. Stdin is parsed once and kept, because
  it cannot be rewound. A JSON document is held as one DOM. `search` still
  maps the finished corpus, and the spill file grows with the input.
* Bad, because an int8 embedding depends on the other texts in its padded
  run and on that run's length. Reordering the same rows did not change the
  vector. Changing the batch size, or the order that assigns texts to
  windows, does, so that rule is part of the determinism contract and needs
  a corpus version bump or a re-index note. No unit test compares two batch
  sizes.

## More Information

[PR #61](https://github.com/lorenzocaprari/nesso/pull/61)
([f2e6512](https://github.com/lorenzocaprari/nesso/commit/f2e6512467dc91979199b3340d6bd53dc41e142d)),
[PR #88](https://github.com/lorenzocaprari/nesso/pull/88), and
[064f888](https://github.com/lorenzocaprari/nesso/commit/064f888871babcc8d842c4b48233d9ac647c61c1).
A threaded pipeline is not discussed in those pull requests or in 064f888.

`docs/usage.md` describes `grep` as embedding in batches and keeping the top matches.

## Amendment: grep embeds whole windows best first

`grep` now reads its input once. The second parse is gone, and stdin is
no longer replayed from a kept copy. Each line is added to the distinct index
as it is read, and the pending list is cleared at every 1024 texts. Window
`w` is still ids `[1024w, 1024w + 1024)` in first-occurrence order, and the
text of each id comes from `DistinctIndex::Text`. Only the order in which
whole windows are embedded changes. `EmbedOrder` in
`src/core/embed_order.cpp` sorts windows by three integer keys, each
descending: a line contains the stripped query (the substring test of
lexical mode), the most of the query's first 64 distinct lexical terms one
line contains, and the number of ids that occur once. Ties go by ascending
window index. A window's members and their order are those of input order,
so the int8 rule above holds, every score is unchanged, and the final
output stays byte-identical. The provisional sequence of
`GrepObserver` changes: the most promising window is reported first. Dense
mode keeps, for each id, only the first `topK` lines (the first line with
`--group`), because a later line ties an earlier one on score and loses on
ordinal. Hybrid and lexical keep every line, as before. `index` still uses
the two-pass scan.

## Amendment: each text runs alone

[ADR 0016](0016-embed-each-text-alone.md) replaces the int8 consequence above. A planned run is now a scheduling group whose texts run one at a time and unpadded, so a text's int8 or fp32 vector no longer depends on the other texts in its run, on the batch size, or on the window order. Windows, the length sort, and the cuts at 32 texts and 1024 tokens are unchanged. They decide only which session runs a text and when.
