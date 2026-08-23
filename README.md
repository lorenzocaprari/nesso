# Nesso

**Local semantic search for unstructured text** (C++26)

by **Lorenzo Caprari**

Nesso is a Linux-native CLI for searching local text by meaning.

## Today

- Memory-mapped vector store with cosine top-k search
- CLI subcommands: `init`, `index`, `search` on raw float32 vector files, plus `grep` over logs
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

## Semantic grep

Download the embedding model once:

```bash
./scripts/fetch-model
```

Search a log file by meaning:

```bash
./build/Debug/nesso grep --log app.log --query "database connection error" -k 5 --model-dir models/
```

## Usage (current vector CLI)

Initialize a database container:

```bash
./build/Debug/nesso -p vectors.nesso -d 128 init
```

Ingest raw float32 vectors (each record is `dimensions * sizeof(float)` bytes):

```bash
./build/Debug/nesso -p vectors.nesso -d 128 index -f vectors.bin
```

Search by cosine similarity:

```bash
./build/Debug/nesso -p vectors.nesso -d 128 search -q query.bin -k 10
```

## Development

Local CI gate (run before pushing):

```bash
bash scripts/lint
./scripts/code-coverage conan/profiles/code-coverage
```

See [.github/workflows/ci.yml](.github/workflows/ci.yml) for the full pipeline (lint, clang-tidy, Release/Debug builds, unit tests, fuzz, coverage).

## License

MIT — see LICENSE.
