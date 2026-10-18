---
status: "accepted"
date: "2026-10-18"
decision-makers: Lorenzo Caprari
---

# Byte-identical determinism

## Context and Problem Statement

The same input, binary, and model must produce the same corpus and the
same ranking, including while the CPU is loaded. What equality is
required, and how does the default int8 MiniLM meet it?

`scripts/check-determinism` compares corpus bytes and search stdout
across fresh processes under CPU load. The `determinism` job gates
fp32 (`--model-dir models-fp32`) and the default int8 policy
(`--model-dir models`).
[#92](https://github.com/lorenzocaprari/nesso/pull/92) (`0615854`,
merged 2026-10-18) sets int8 `intraOpThreads` to 1, one session per
thread, and that pool is on `main`.
[#108](https://github.com/lorenzocaprari/nesso/pull/108) (`0cdeda3`)
makes that default int8 policy a blocking check across processes.
int8 with `NESSO_THREADS=$(nproc)` stays report-only.

## Considered Options

* Byte equality
* Tolerance-based (epsilon/rank-stable) equality
* fp32-only default
* int8 with single-thread sessions

## Decision Outcome

Chosen option: "int8 with single-thread sessions", because #92 keeps
the int8 MiniLM and one intra-op thread per session, and #108 gates
that default policy on byte equality. fp32 stays a CI gate
([#80](https://github.com/lorenzocaprari/nesso/pull/80), `8849bb4`),
not a runtime switch. `Nesso::embedder` loads one directory. Those
runs must pass byte equality. Tolerance-based equality and an
fp32-only default are declined.

### Consequences

* Good, because the gate compares corpus bytes and search stdout,
  including under CPU load.
* Good, because the fp32 step and the default int8 step fail the job
  when the bytes differ. There is no runtime switch to the fp32
  files.
* Bad, because one intra-op thread per int8 session costs throughput.
  That cost is unmeasured: neither #92 nor the repository states a
  number.
* Bad, because int8 with `NESSO_THREADS=$(nproc)` stays
  `continue-on-error: true` on "Report int8 determinism at nproc
  sessions" (`.github/workflows/ci.yml`). That override is one
  single-thread session per logical CPU and is not a merge gate.

## More Information

* [#72](https://github.com/lorenzocaprari/nesso/pull/72) (`3b7a8c6`)
* [#75](https://github.com/lorenzocaprari/nesso/pull/75) (`db771de`)
* [#80](https://github.com/lorenzocaprari/nesso/pull/80) (`8849bb4`)
* [#85](https://github.com/lorenzocaprari/nesso/pull/85) (`5b52a5a`)
* [#92](https://github.com/lorenzocaprari/nesso/pull/92) (`0615854`)
* [#108](https://github.com/lorenzocaprari/nesso/pull/108) (`0cdeda3`)
