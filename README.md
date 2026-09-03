# Nesso

**Local semantic search for unstructured text** (C++26)

by **Lorenzo Caprari**

Nesso is a Linux-native CLI for searching local text by meaning.

## Today

- `nesso grep QUERY FILE...` — one-shot semantic search over `.log`, `.json`, and `.jsonl`
- `nesso init` / `index` / `search` — raw float32 vector store (mmap, cosine top-k)
- Local ONNX MiniLM embedder and an in-memory embedding store (no persisted text index)
- Brute-force linear scan (no ANN index yet)
- Conan 2 toolchain with ASan/UBSan debug builds and CI coverage gates

## Target (in progress)

Semantic search over `.log`, `.json`, and `.jsonl` files via a local ONNX MiniLM embedder and an in-memory embedding store.

## Non-goals

- Not a hosted vector database (Qdrant, pgvector, etc.)
- No approximate nearest-neighbor index yet
- Linux only (POSIX `mmap`)

## Prerequisites

- **Compiler:** GCC 15+ with C++26 support
- **Build system:** CMake 3.28+
- **Package manager:** Conan 2.x
- **OS:** Linux

## Build

```bash
conan install . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing
conan build . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing
ctest --test-dir build/Debug --output-on-failure
```

Release profile: replace `gcc-26-debug` with `gcc-26`.

## Usage

Download the embedding model once:

```bash
./scripts/fetch-model
```

Search files by meaning. `-k` is optional (default 5):

```bash
./build/Debug/nesso grep "database connection error" app.log
./build/Debug/nesso grep "payment timeout" app.log events.jsonl dump.json -k 5
./build/Debug/nesso grep "auth failure" app.log --model-dir models/
```

### File limits

- Formats: `.log`, `.json`, `.jsonl` only (by extension). Directories and other files are rejected.
- `.log`: one chunk per non-empty line; lines longer than 4096 characters are skipped.
- `.json` / `.jsonl`: only objects with a string `message` field are indexed; malformed lines/documents are skipped.
- The whole corpus is held in memory for that invocation (parse + embeddings). Very large files will be slow and RAM-heavy until embedding is batched (see later work).

## Vector store

Initialize a database container, ingest raw float32 vectors, and search by cosine similarity:

```bash
./build/Debug/nesso -p vectors.nesso -d 128 init
./build/Debug/nesso -p vectors.nesso -d 128 index -f vectors.bin
./build/Debug/nesso -p vectors.nesso -d 128 search -q query.bin -k 10
```

Each record in `vectors.bin` / `query.bin` is `dimensions * sizeof(float)` bytes.

## Development

Local CI gate (run before pushing):

```bash
bash scripts/lint
./scripts/code-coverage conan/profiles/code-coverage
```

See [.github/workflows/ci.yml](.github/workflows/ci.yml) for the full pipeline (lint, clang-tidy, Release/Debug builds, unit tests, fuzz, coverage).

## License

MIT — see LICENSE.
