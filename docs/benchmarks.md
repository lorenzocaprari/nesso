# Benchmarks

Use a Release build. `NESSO_TRACE=1` prints one JSON line on stderr per command, with per-stage microseconds and peak RSS:

```bash
NESSO_TRACE=1 ./build/Release/src/nesso grep "connection refused" /var/log/app.log --model-dir models/
```

`scripts/profile` runs the same command under `perf` and `heaptrack` and writes `build/profile/flamegraph.svg` and `build/profile/heaptrack-summary.txt`. It needs `perf`, `heaptrack`, and FlameGraph's `stackcollapse-perf.pl` and `flamegraph.pl` on `PATH`, or `NESSO_FLAMEGRAPH_DIR` set to a FlameGraph checkout:

```bash
./scripts/profile grep "connection refused" /var/log/app.log --model-dir models/
```

`scripts/bench` times `grep` on fixed-seed synthetic logs of 1k, 10k, and 100k lines and writes `bench/results/<sha>.json`. `scripts/bench-compare` fails when wall time or peak RSS is more than 15% above [bench/baseline.json](../bench/baseline.json):

```bash
python3 scripts/bench
python3 scripts/bench-compare
python3 scripts/bench --dataset hdfs
```

`python3 scripts/bench --dataset hdfs` indexes the Loghub HDFS_v1 2k sample and writes `bench/results/<sha>.hdfs.json`. `scripts/fetch-loghub` downloads that file into `build/bench/` and checks its sha256. The sample is repeated to 100000 lines unless `--lines` is set. The result records input lines, wall time, lines per second, peak RSS, corpus bytes, the corpus-to-input byte ratio, the CPU model, `nproc`, and `NESSO_THREADS`. The sample is research data and is not committed.

The `bench` CI job runs on every pull request, on pushes to `main`, and nightly. Pull requests time the 1k and 10k tiers. Pushes to `main` and the nightly schedule also time 100k. The job compares the tiers it ran with the latest baseline saved from `main`, and that failure fails the job. The first run has no baseline and warns. Shared runners disagree on wall time by about 4.5× for the same tier, so CI allows up to 5.5× the baseline wall time and 1.2× the baseline peak RSS. A push to `main` appends the same result to benchmark history on the `gh-pages` branch. Pull requests do not write that branch. They add an informational comparison with the stored history to the job summary, and that comparison does not fail the job.

The chart file is [dev/bench/index.html](https://github.com/lorenzocaprari/nesso/blob/gh-pages/dev/bench/index.html) on that branch. GitHub Pages does not serve this repository.

Google Benchmark microbenchmarks build with `-o "&:bench=True"` on the Release profile and produce `build/Release/bench/nesso_bench`.
