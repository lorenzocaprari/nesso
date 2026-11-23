---
status: "accepted"
date: "2026-11-23"
decision-makers: Lorenzo Caprari
---

# Embed each text alone so its vector does not depend on its batch

## Context and Problem Statement

[ADR 0004](0004-streaming-batch-pipeline.md) runs ONNX inference on planned runs of up to 32 texts. Each run is padded to its longest text. [#362](https://github.com/lorenzocaprari/nesso/issues/362) measured on a 100000-line BGL window that, with the default int8 model, the dense score of one unchanged line moved from 0.3365 (embedded alone) to 0.3408 and 0.3673 in two indexes whose batches differed. Over the 277 dedup keys of that index, in-index minus alone had a standard deviation of 0.0092. The median gap between neighbouring keys in the top 15 was 0.0034. So appending unrelated lines and re-indexing could reorder results for lines that did not change.

The int8 model (`model_quantized.onnx`, see `scripts/fetch-model`) quantizes activations dynamically. `DynamicQuantizeLinear`, which ONNX Runtime fuses into `DynamicQuantizeMatMul` at `ORT_ENABLE_ALL`, takes one scale and zero point from the minimum and maximum of the whole `[batch, sequence, 384]` tensor. Other rows and padded positions both move that scale. Padded positions carry position embeddings through every layer, so they are not zeros. The attention mask hides them from real tokens, but not from the scale.

A synthetic check isolated the two causes. MiniLM could not be downloaded in that session. The check used a BERT-like encoder of the same shape quantized with `quantize_dynamic(QInt8)`, run on ONNX Runtime 1.29 with nesso's session options. It embedded 40 lines alone and then in each of four conditions:

| condition | fp32 identical to alone | int8 identical to alone | int8 score delta sd |
| --- | --- | --- | --- |
| padded to 64, no other rows | 40/40 | 0/40 | 0.00137 |
| 31 other rows of the same length, no padding | 40/40 | 0/40 | 0.00156 |
| 31 other rows of different lengths | 40/40 | 0/40 | 0.00176 |
| the same rows, all padded to a fixed 64 | 40/40 | 0/40 | 0.00149 |

fp32 was bit-identical in every case, so padding and masking are not the cause. int8 differed in every case, including with no padding and with no other rows.

How should a text be run so that its vector depends only on the text?

## Decision Drivers

* A line's vector, and so its score, depends only on the line ([#362](https://github.com/lorenzocaprari/nesso/issues/362)).
* Byte-identical output for a given input, binary, model and thread budget ([ADR 0003](0003-byte-identical-determinism.md)).
* Indexing throughput stays close to today.
* No new model artifact to host, verify, and keep reproducible.

## Considered Options

* Run each text alone and unpadded
* Pad every text to a fixed bucket
* Rewrite the model to per-token activation scales
* Statically quantize the model with calibrated scales

## Decision Outcome

Chosen option: "Run each text alone and unpadded". It is the only option that is exactly batch-independent and keeps the published model file. It also runs an indexed line the same way as a query, which `search` and `grep` always embed alone.

`PlanBatches` and `BatchWaves` are unchanged. A planned run is now a scheduling group: one session runs its texts one at a time, each as a `[1, n]` input with no padding (`OnnxEmbedder::SessionImpl::Infer` in `src/embed/onnx_embedder.cpp`). Every session in the pool has the same options, so which session runs a text does not change its bytes. The rule applies to fp32 too, so there is one code path. The corpus `batch` policy value gains `run:single`.

### Consequences

* Good, because appending or removing other lines cannot change a line's vector. A line's `index` score equals its `grep` score on a one-line file, on the same CPU.
* Good, because the model file, its sha256 and `fetch-model` are unchanged.
* Good, because a one-text call, such as a query, produces the same bytes as before.
* Bad, because each text pays the per-run overhead. On the synthetic MiniLM-sized model, single-thread throughput fell 7.0% for int8 (177.3 to 164.9 lines/s) and 11.5% for fp32 (157.2 to 139.1 lines/s). That run used 2048 lines with a median of 27 tokens and nesso's plan rules, timed from Python. The CI `bench` job measures the real cost.
* Bad, because every int8 vector in an existing corpus differs from what `index` now writes. The model identity is unchanged, so `search` does not refuse the corpus. The `batch` value differs, so it prints the existing policy warning, and re-running `nesso index` gives batch-independent vectors.
* Neutral, because dense retrieval metrics shift once.
* Neutral, because vectors still depend on the CPU. Different int8 and fp32 kernels (AVX-512 VNNI or AVX2, and aarch64) round the floating-point parts differently. Making vectors CPU-independent would mean pinning ONNX Runtime's kernel dispatch to one instruction set. That is not part of this decision.

### Confirmation

* `test/unit/embed/onnx_embedder_test.cpp` requires a text embedded alone to equal, byte for byte, the same text at the start, middle and end of 40 texts of other lengths. It checks this with one session and with two.
* The batched-versus-single tests compare with `==`.
* A `grep.feature` scenario requires the same `--json` output for a line with neighbours of other lengths and for the line alone.
* `scripts/check-determinism` still compares corpus bytes across processes.

## Pros and Cons of the Options

### Run each text alone and unpadded

* Good, because it is exact by construction, for int8 and fp32.
* Good, because it is a small change to `Infer`.
* Bad, because of the per-run overhead measured above.

### Pad every text to a fixed bucket

* Good, because a text's padded length no longer depends on its neighbours.
* Bad, because it does not fix int8: rows still share one scale, and the bucket's padded positions still move it. All 40 lines differed in the synthetic check.
* Bad, because padding to a bucket adds work.

### Rewrite the model to per-token activation scales

* Good, because it is batch- and padding-independent and keeps batching.
* Good, because per-token scales are usually closer to fp32 than per-tensor scales.
* Bad, because it is a derived model. That needs a rewrite step (Python and onnx in `fetch-model`, or a protobuf dependency in C++) or a hosted artifact with its own checksum and supply-chain checks.
* Bad, because it loses the fused `DynamicQuantizeMatMul` kernel. Its speed is unmeasured.

### Statically quantize the model with calibrated scales

* Good, because constant scales are batch-independent and usually fast.
* Bad, because it needs a calibration set, a reproducible generation pipeline, and a hosted model with a new sha256.
* Bad, because the quality change depends on the calibration and is unmeasured.

## More Information

This record amends the int8 consequence in [ADR 0004](0004-streaming-batch-pipeline.md) and the batch bullet in [ADR 0010](0010-corpus-format-v3.md). If the measured cost on CI is too high, per-token activation scales are the next option to measure.
