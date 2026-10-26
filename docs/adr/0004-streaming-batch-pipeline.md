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
  `STREAM_BATCH` in `src/nesso.cpp` and `EMBED_BATCH_SIZE` are both 32, so
  `inferAll` gets one batch per flush and only one session runs
  (`src/embed/onnx_embedder.cpp`). `src/cli_parser.cpp` has no batch option.
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
