# Determinism

The release gate starts fresh `nesso` processes for each index and search run, compares corpus files and complete rankings byte-for-byte, and keeps a CPU-load process running:

```bash
NESSO_BINARY=./build/Release/src/nesso \
NESSO_MODEL_DIR=models \
./scripts/check-determinism \
  --input /path/to/events.log \
  --query PAY-E1042 \
  --runs 10 \
  --top-k 1000000 \
  --stress-command "stress-ng --cpu 8"
```

Use the same input, model, and gate in Release and Debug ASan builds before tagging. The default stress command is `stress-ng --cpu 1 --cpu-method loop --timeout 0 --cache-size 2M`: one integer busy-loop worker until the gate stops it. Pass `--stress-command` when the machine needs a different load generator. If that process exits, the gate restarts it and records its exit status and stderr; the fp32 comparison does not run unless a load process is alive.

`scripts/fetch-model` downloads the int8 MiniLM. Pass `fp32` as the second argument for the float model (`./scripts/fetch-model models-fp32 fp32`). int8 derives its intra-op threads and session count from a CPU budget `N`. On x86, `N` of 4 or less is one intra-op thread and `N` sessions, without shrinking the arena. A larger x86 budget uses `I = min(4, N/2)` and `S = max(1, N/I)` and shrinks the arena after each run. On aarch64 `I` is at least 2, including when `N` is 1. `--threads` or `NESSO_THREADS` sets `N`. When both are unset, `N` is the usable physical core count. fp32 keeps one session, and the same option sets its intra-op threads, with the same aarch64 floor of 2. Repeated runs are compared as corpus bytes. CI fails the job when fp32 or the default int8 policy disagrees. `--emit-input` writes a 3500-line log, and `--index-runs 3` compares those index files and the rankings for four queries, including `PAY-E1042`. Pull requests run `--mode pr` instead: a 256-line log, 2 runs, and 2 index runs, still across fresh processes under CPU load. Pushes to `main`, the nightly schedule, manual runs, and the release gate stay on the full check. The int8 run with `NESSO_THREADS` set to the logical CPU count stays report-only.

When `grep` or `index` runs in a terminal, embedding progress is shown on stderr. Redirected output stays quiet; pass `--quiet` to suppress progress explicitly.
