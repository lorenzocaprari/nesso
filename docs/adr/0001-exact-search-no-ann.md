# ADR 0001: Exact brute-force ranking, no approximate nearest-neighbor index

Status: accepted (2026-10)

## Context

Nesso ranks text chunks by the dot product between a query embedding and
every stored embedding (`core::topK` over a flat row-major matrix). The
question was whether to add an approximate nearest-neighbor (ANN) index such
as HNSW or IVF.

Measured on `scripts/bench` (`bench/baseline.json`, 100k log lines, Release
build, AVX2+FMA kernel):

| stage | time      | share  |
| ----- | --------- | ------ |
| parse | 12 ms     | 0.03%  |
| embed | 35 535 ms | 99.7%  |
| rank  | 69 ms     | 0.2%   |

The scan reads `rows x 384` floats once. Extrapolated to 1M chunks it costs
about 0.7 s single-threaded and well under 0.2 s across 8 threads. Nesso runs
one query per process invocation; `grep` has no persisted corpus at all.

## Decision

Ranking stays an exact scan over a flat `float32` matrix. No ANN index is
built or persisted.

Performance work targets what actually bounds the tool:

- the embedding stage (batching, padding, duplicate elimination, model
  choice);
- memory and I/O for `search` (a flat on-disk matrix read through `mmap`,
  later `fp16`/`int8` storage to cut bandwidth);
- a multi-threaded scan.

## Consequences

- Results are exact and deterministic; no recall/latency tuning surface.
- `index` has zero index-build cost beyond embedding; the corpus file is a
  plain matrix plus a string table.
- A corpus of N chunks costs `N x dims x 4` bytes to scan. This is acceptable
  up to a few million chunks on a laptop.

## Revisit when

- persisted corpora regularly exceed ~5M chunks, or
- a long-lived process (daemon, REPL, editor integration) serves many queries
  against one loaded corpus, so that index build time can be amortised.

Either trigger should come with a benchmark showing the scan, not embedding,
dominates end-to-end latency.
