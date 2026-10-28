---
status: "proposed"
date: "2026-10-28"
decision-makers: Lorenzo Caprari
---

# int8 session and thread policy

## Context and Problem Statement

How many int8 sessions, and how many intra-op threads in each, should a
CPU budget `N` open? [#92](https://github.com/lorenzocaprari/nesso/pull/92)
(`0615854`) opened one single-thread session per core and did not shrink
the arena. On a 4-vCPU host that pool peaked at 2189332480 bytes while
indexing 100000 lines.

## Considered Options

* `N` sessions of 1 intra-op thread, arena not shrunk (the layout on `main`)
* 1 session of `min(4, N)` intra-op threads, arena shrunk after each run
* `I = clamp(N/2, 1, 4)` and `S = max(1, N/I)`, arena shrunk after each run
* x86 `N <= 4` keeps `main`; x86 `N >= 5` uses `I = min(4, N/2)` and shrinks

## Decision Outcome

Chosen option: "x86 `N <= 4` keeps `main`; x86 `N >= 5` uses
`I = min(4, N/2)` and shrinks", because a 4-vCPU host is faster with
`main`'s layout, and the wider layouts measured there were slower at
10000 lines and on the 100000-line index.

`N` is the usable physical core count, or `--threads` / `NESSO_THREADS`.
On x86, `N <= 4` is `I = 1` and `S = N`, and the arena is not shrunk. On
x86, `N >= 5` is `I = min(4, N/2)` (integer division) and
`S = max(1, N/I)`, and the arena is shrunk after each run. The memory cap
still limits the default `S`. `N=8` is 2 sessions of 4 intra-op threads
and `N=16` is 4 sessions of 4. On aarch64 `I` is at least 2, including when
`N` is 1. GCC 15 emits two `ComputeImpl` copies in ONNX Runtime 1.18.1 and
only the intra-op 1 copy fuses multiply-add, so intra-op 1 gives different
bytes than 2 or more. x86 byte equality with intra-op greater than 1
depends on the AMX tile-permission deny in
[`amx_guard.cpp`](../../src/embed/amx_guard.cpp).

Medians of three grep runs, and one index of the same 100000-line file, on that 4-vCPU host:

| layout | 1000 lines | 10000 lines | 100000-line index |
| --- | --- | --- | --- |
| `main`, 4×1 | 0.52 s, 1923 lines/s, 251060224 bytes | 2.88 s, 3472 lines/s, 258064384 bytes | 246.49 s, 405.7 lines/s, 2189332480 bytes |
| 1×4, shrink | 0.43 s, 2326 lines/s, 89751552 bytes | 4.19 s, 2387 lines/s, 88637440 bytes | 253.98 s, 393.7 lines/s, 437075968 bytes |
| 2×2, shrink | 0.51 s, 1961 lines/s, 159326208 bytes | 3.55 s, 2817 lines/s, 152027136 bytes | 264.10 s, 378.6 lines/s, 591761408 bytes |

Grep stdout matched. The index sha256 was `48485f688cab3c972e39a181b2018934bb480229fd02f2229531d7ffabd033d8` for all three.
Every 1×4 run and every 2×2 run at 10000 lines was slower than every `main` run.

| layout | 100000-line HDFS-like index, median of 3 | index peak RSS | grep 1k/10k/100k, one run | grep peak RSS |
| --- | --- | --- | --- | --- |
| `main`, 8×1 (one run capped to 7×1 by available memory) | 197.61 s, 506.0 lines/s | 4328345600 bytes | 0.68/4.09/38.29 s | 206254080 bytes |
| 2×4, arena shrink | 150.62 s, 663.9 lines/s | 605888512 bytes | 0.58/3.39/34.32 s | 153628672 bytes |

Index sha256 `2e19ad146e96013a2b634d0032e274447ecab208fa805fbf9b9bc1e7e200595e` for both layouts and `NESSO_THREADS=4` (4×1). It differs from the 4-vCPU sha because the input and the builds differ.
8-vCPU KVM x86 guest (Intel Xeon family 6 model 207, AVX-512 and AMX, 16 GB, default budget 8; seccomp AMX deny in every run; clean runs, `load1` < 1.0 at start, no box freeze). Builds: `main` (`4438b9d` era) and [#168](https://github.com/lorenzocaprari/nesso/pull/168) at `d6d92cd` (merged `3c4def6` differs only by passing `SessionPolicy` by const reference). `bench-compare` ok.
Shared host; runs overlapping host stalls were excluded.

### Consequences

* Good, because a 4-core host keeps `main`'s speed and corpus bytes.
* Bad, because four single-thread sessions still hold about 2189332480 bytes on that index.
* Bad, because an aarch64 corpus built with intra-op 1 will not match.
* `NESSO_THREADS` is a CPU budget, not a session count.

### Confirmation

Byte equality of intra-op widths is `test/unit/embed/session_pool_test.cpp`.
The tile-permission deny is `test/unit/embed/amx_guard_test.cpp`.
`scripts/check-determinism` compares corpus bytes across processes.
[ADR 0003](0003-byte-identical-determinism.md) still requires byte equality.
