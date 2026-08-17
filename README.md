# VecGrep

**Offline hybrid search for system and robotics logs.**

VecGrep is a C++26 CLI that runs **semantic + lexical (BM25)** search over unstructured logs with no cloud APIs. Embeddings come from an INT8 `all-MiniLM-L6-v2` ONNX model (or a deterministic hash stub when no model is supplied). Rank lists are fused with Reciprocal Rank Fusion.

## Features

* **Hybrid search:** 384-d cosine k-NN plus BM25, combined with RRF
* **Edge inference:** ONNX Runtime C++ API, INT8 MiniLM, native WordPiece tokenizer
* **Zero-copy store:** mmap packing of embeddings; JSONL sidecar maps ids to original log lines
* **SIMD cosine:** AVX2 with a scalar fallback (`-march=x86-64-v2` portable builds)
* **Async ingest:** `std::jthread` pipeline (I/O → embed → index); `--sync` for single-threaded mode

## Prerequisites

* GCC 15+ (C++26)
* CMake 3.28+
* Conan 2.x
* Linux (POSIX mmap)

## Build

```bash
conan install . -pr=./conan/profiles/gcc-26-debug --build=missing
conan build . -pr:h ./conan/profiles/gcc-26 -pr:b default --build=missing
```

The CLI binary is `vecgrep`.

## Usage

Index a log (hash stub embedder — no model required):

```bash
vecgrep index --log-file robot.log --path /tmp/robot.vecgrep
vecgrep search --query "CAN timeout 0x1A4" --path /tmp/robot.vecgrep -k 10
```

With MiniLM INT8:

```bash
vecgrep index --log-file robot.log --path /tmp/robot.vecgrep \
  --model models/all-MiniLM-L6-v2-int8.onnx --vocab models/vocab.txt
vecgrep search --query "actuator overcurrent on joint 3" --path /tmp/robot.vecgrep \
  --model models/all-MiniLM-L6-v2-int8.onnx --vocab models/vocab.txt
```

Output is TSV:

```
LINE	RRF	TEXT
2	0.032258	CAN timeout on bus 0x1A4
```

`--sync` disables the ingest thread pipeline.

## ONNX model contract

Expected MiniLM graph:

| Tensor | Type | Shape |
|---|---|---|
| `input_ids` | int64 | `[batch, seq]` |
| `attention_mask` | int64 | `[batch, seq]` |
| `token_type_ids` | int64 | `[batch, seq]` (zeros if the graph requires it) |
| output | float32 | `[batch, 384]` or `[batch, seq, 384]` |

If the output is `[batch, seq, 384]`, VecGrep mean-pools with the attention mask and L2-normalizes.

`models/all-MiniLM-L6-v2-int8.onnx` is the official sentence-transformers `onnx/model_qint8_avx512.onnx` (~22 MB) plus `models/vocab.txt`. CI still uses `test/fixtures/tiny_minilm.onnx` and does not download weights.

## Architecture

```
log lines ─┬─► WordPiece + ONNX (or HashEmbedder) ─► mmap vector store
           └─► lexical tokens ─► BM25 inverted index
query ─► both legs ─► RRF ─► original line + line number
```

The Conan package name remains `mach1`; the product/CLI name is VecGrep.
