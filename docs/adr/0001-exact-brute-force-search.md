---
status: "accepted"
date: "2026-10-07"
---

# Exact brute-force search without an ANN index

## Context and Problem Statement

Nesso ranks one query embedding against stored chunk embeddings and
returns the top k. The score is a dot product. The float kernel is
AVX2+FMA or scalar (`selectedKernel` in `src/core/distance.cpp`).

`nesso search` passes the corpus float matrix to `core::topK`, which
scores every row. `nesso grep` scores every embedding as each batch is
produced and does not write a corpus. `nesso index` writes the corpus
and builds no neighbor structure. Keep that exact scan, or add an
approximate nearest-neighbor (ANN) index?

## Considered Options

* Exact scan of every vector
* Approximate nearest-neighbor index (HNSW, IVF, or a FAISS-style library)
* Separate mmap vector store (`StorageEngine`)

## Decision Outcome

Chosen option: "Exact scan of every vector", because text search already
scored every embedding, no approximate index was added, and the separate
vector store was not on that path.

The date is 2026-10-07, the Europe/Rome calendar day on which
[#63](https://github.com/lorenzocaprari/nesso/pull/63) merged (2026-10-06
22:00:58 UTC) and replaced `EmbeddingStore` with the flat matrix and
`core::topK`.

### Consequences

* Good, because every stored vector is scored, so the hits are the exact
  top k of those scores. Equal scores keep the lower index.
* Good, because `nesso index` embeds and writes the corpus and does not
  build a neighbor graph. The installed target is the `nesso`
  executable. `conanfile.py` requires CLI11, nlohmann_json, and ONNX
  Runtime (`shared = False`), not an ANN library.
* Bad, because the scan is linear in the chunk count and the dimension
  (MiniLM rows are 384 floats), `core::topK` allocates one hit per row,
  and `nesso grep` trims to k per batch while still scoring every
  embedding.

## More Information

Revisit when an index exceeds about 5M unique vectors, or when p95
search latency exceeds 100 ms on the benchmark. The candidate then is
an ANN index (e.g. HNSW) with a deterministic build plus exact re-rank of the candidates.
Before that candidate, cheaper exact options come first: parallel scoring across cores, and more compact stored vectors (int8/fp16).
* [#62](https://github.com/lorenzocaprari/nesso/pull/62) [`68353d0`](https://github.com/lorenzocaprari/nesso/commit/68353d0d8c0fb5abc3d26886683a6409282b3282)
* [#63](https://github.com/lorenzocaprari/nesso/pull/63) [`037545f`](https://github.com/lorenzocaprari/nesso/commit/037545f98a3d8dbb506e0b57c685e25985fac16d)
* [#64](https://github.com/lorenzocaprari/nesso/pull/64) [`672f6a2`](https://github.com/lorenzocaprari/nesso/commit/672f6a26212823f47f612e190f62a128fc179a56)
* [#60](https://github.com/lorenzocaprari/nesso/pull/60) [`8585740`](https://github.com/lorenzocaprari/nesso/commit/8585740f52aded75f503b92a6e0844df8b5c42c0)
* [#78](https://github.com/lorenzocaprari/nesso/pull/78) [`9c5773d`](https://github.com/lorenzocaprari/nesso/commit/9c5773d86b5caab49b5dd6f09bca0cdf0775913b)
* [#88](https://github.com/lorenzocaprari/nesso/pull/88) [`064f888`](https://github.com/lorenzocaprari/nesso/commit/064f888871babcc8d842c4b48233d9ac647c61c1)
