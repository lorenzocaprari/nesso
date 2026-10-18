---
name: coverage-floor
description: "Use when the CI coverage job or ./scripts/code-coverage fails a REQUIRED_LINES, REQUIRED_FUNCTIONS, or REQUIRED_BRANCHES floor, or when adding code that must keep coverage at those floors. Do not use for clang-tidy, cppcheck, or unit-test failures, dependency bumps, or docs-only changes."
---

# Coverage floor

Never lower `REQUIRED_LINES`, `REQUIRED_FUNCTIONS`, or `REQUIRED_BRANCHES` in `scripts/code-coverage`. That is an AGENTS.md hard rule. Those constants are the floors. Read them from that file when you need the numbers. Do not copy the numbers into comments, docs, or commits.

## Run

From the repository root:

```bash
./scripts/code-coverage conan/profiles/code-coverage
```

The script writes the measured rows to `build/coverage/report/summary.txt`. A floor miss prints `ERROR: <lines|functions|branches> <measured>% < <required>%` and exits 2. Success prints `Coverage OK`.

## Fix a miss

Add or extend tests under `test/` and keep the ctest label the suite already uses (`unit`, `integration`, or `functional`). The label and the `lcov --remove` list are in [reference.md](reference.md). Do not restate `scripts/code-coverage`; run it.

## Capture errors

An lcov `negative taken count` error is a capture problem, not a floor miss. Do not change `REQUIRED_*` to get past it. See [reference.md](reference.md).
