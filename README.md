# Nesso

**Local semantic search for unstructured text** (C++26)

by **Lorenzo Caprari**

Nesso is a Linux-native CLI for searching local text by meaning.

## Install

Ubuntu 26.04+ (from a GitHub Release `.deb`):

```bash
sudo apt install ./nesso_*_amd64.deb
nesso grep "database connection error" app.log
```

The `.deb` needs GCC 15 `libstdc++` (Ubuntu 26.04). Anywhere with Docker:

```bash
docker pull ghcr.io/lorenzocaprari/nesso:latest
docker run --rm -v "$PWD:/data" -w /data ghcr.io/lorenzocaprari/nesso:latest \
  grep "database connection error" app.log
```

Push a `vX.Y.Z` tag (must match CMake `VERSION` and Conan `version`) to publish both artifacts. CI must have published `nesso-build-env` first. First release is `v0.1.0`.

From source (GCC 15+, CMake 3.28+, Conan 2, Linux):

```bash
conan install . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing
conan build . -pr:h ./conan/profiles/gcc-26-debug -pr:b default \
  --lockfile=conan.lock --build=missing
ctest --test-dir build/Debug --output-on-failure
```

Release profile: replace `gcc-26-debug` with `gcc-26`.

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

## Usage

Download the embedding model once:

```bash
./scripts/fetch-model
```

Search files by meaning. `-k` is optional (default 5):

```bash
./build/Debug/src/nesso grep "database connection error" app.log
./build/Debug/src/nesso grep "payment timeout" app.log events.jsonl dump.json -k 5
./build/Debug/src/nesso grep "auth failure" app.log --model-dir models/
./build/Debug/src/nesso -V
```

### File limits

- Formats: `.log`, `.json`, `.jsonl` only (by extension). Directories and other files are rejected.
- `.log`: one chunk per non-empty line; lines longer than 4096 characters are skipped.
- `.json` / `.jsonl`: only objects with a string `message` field are indexed; malformed lines/documents are skipped.
- The whole corpus is held in memory for that invocation (parse + embeddings). Very large files will be slow and RAM-heavy until embedding is batched (see later work).

## Vector store

Initialize a database container, ingest raw float32 vectors, and search by cosine similarity:

```bash
./build/Debug/src/nesso -p vectors.nesso -d 128 init
./build/Debug/src/nesso -p vectors.nesso -d 128 index -f vectors.bin
./build/Debug/src/nesso -p vectors.nesso -d 128 search -q query.bin -k 10
```

Each record in `vectors.bin` / `query.bin` is `dimensions * sizeof(float)` bytes.

## Development

Local CI gate (run before pushing):

```bash
bash scripts/lint
./scripts/code-coverage conan/profiles/code-coverage
```

See [.github/workflows/ci.yml](.github/workflows/ci.yml) for lint, clang-tidy, Release/Debug builds, tests, fuzz, and coverage. Tag `v*` publishes via [.github/workflows/release.yml](.github/workflows/release.yml).

## License

MIT — see LICENSE.
