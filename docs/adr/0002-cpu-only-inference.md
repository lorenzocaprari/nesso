---
status: "accepted"
date: "2026-10-11"
---

# Run embeddings on CPU only

## Context and Problem Statement

Tag `v0.1.0` embeds text with ONNX Runtime and a MiniLM model. The
binary has to run on Linux without a GPU driver, and two runs of the
same input have to be comparable. No decision document, issue, or
pull request records this choice. This record reconstructs it from
the tag, the current tree, and the pull requests that landed the
behavior.

Which execution provider should run the embedding model?

## Considered Options

* CPU-only ONNX Runtime
* GPU execution providers (CUDA, ROCm, DirectML, and similar) at build time or at runtime
* A runtime `--device` switch

## Decision Outcome

Chosen option: "CPU-only ONNX Runtime", because tag `v0.1.0` links
ONNX Runtime 1.18.1 statically and registers no execution provider,
so 1.18.1 adds the CPU execution provider.

### Consequences

* Good, because one `nesso` binary links ONNX Runtime statically
  (`shared=False`) and the published package does not depend on a
  GPU driver.
* Good, because inference is offline. A run reads `model.onnx` and
  `vocab.txt` from disk.
* Good, because every session is sequential, with one inter-op
  thread. A quantized model uses a pool of sessions and one intra-op
  thread on each (physical cores by default, capped by half of
  available memory over the estimated 480 MiB per session from
  [#134](https://github.com/lorenzocaprari/nesso/pull/134); #92 had
  divided by the model file size).
  An fp32 model uses one session, with intra-op threads set to the
  requested count or `hardware_concurrency` (at least 1).
* Bad, because throughput is that CPU provider. `embedBatch` submits
  at most 32 sequences per `Ort::Session::Run`.

## More Information

No issue and no pull request compares the options above. Sources: [v0.1.0 `9c5773d`](https://github.com/lorenzocaprari/nesso/commit/9c5773d), [#72 `3b7a8c6`](https://github.com/lorenzocaprari/nesso/pull/72), [#80 `8849bb4`](https://github.com/lorenzocaprari/nesso/pull/80), [#92 `0615854`](https://github.com/lorenzocaprari/nesso/pull/92). #92 is the session policy on `main`. Determinism is [ADR 0003](0003-byte-identical-determinism.md).

Open question: whether a future `--device auto` should select an execution provider. This record leaves that undecided.
