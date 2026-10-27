# Benchmarks

Use a Release build. `NESSO_TRACE=1` prints one JSON line on stderr per command, with per-stage microseconds and peak RSS:

```bash
NESSO_TRACE=1 ./build/Release/src/nesso grep "connection refused" /var/log/app.log --model-dir models/
```

`scripts/profile` runs the same command under `perf` and `heaptrack` and writes `build/profile/flamegraph.svg` and `build/profile/heaptrack-summary.txt`. It needs `perf`, `heaptrack`, and FlameGraph's `stackcollapse-perf.pl` and `flamegraph.pl` on `PATH`, or `NESSO_FLAMEGRAPH_DIR` set to a FlameGraph checkout:

```bash
./scripts/profile grep "connection refused" /var/log/app.log --model-dir models/
```

`scripts/bench` times `grep` on fixed-seed synthetic logs of 1k, 10k, and 100k lines and writes `bench/results/<sha>.json`. The result records the CPU model, `nproc`, and `NESSO_THREADS`. `scripts/bench-compare` fails when wall time or peak RSS is more than 15% above [bench/baseline.json](../bench/baseline.json):

```bash
python3 scripts/bench
python3 scripts/bench-compare
python3 scripts/bench --dataset hdfs
python3 scripts/bench --dataset logdir
```

`bench/baseline.json` is the committed shared-runner sample. Replace it on purpose with `bench-baseline`, which keeps the slower wall time of each tier when given more than one result:

```bash
go run -C scripts/ci . bench-baseline --output bench/baseline.json bench/results/<sha>.json
```

`python3 scripts/bench --dataset hdfs` indexes the Loghub HDFS_v1 2k sample and writes `bench/results/<sha>.hdfs.json`. `scripts/fetch-loghub` downloads that file into `build/bench/` and checks its sha256. The sample is repeated to 100000 lines unless `--lines` is set. The result records input lines, wall time, lines per second, peak RSS, corpus bytes, the corpus-to-input byte ratio, the CPU model, `nproc`, and `NESSO_THREADS`. The sample is research data and is not committed.

`python3 scripts/bench --dataset logdir` generates a seeded synthetic log directory and writes `bench/results/<sha>.logdir.json`. The tree has syslog files and two rotations, an auth log, kernel and boot logs, a cron log, a package-manager log, a JSON Lines application log, a journal text export, and small non-log files under `etc/`, `proc/`, and `sys/`. The lines are templates filled from a fixed seed. Nothing in the tree is downloaded or committed. `--lines` sets the line count (default 256). `--mib` grows the indexed files to that many MiB instead. `nesso index` reads regular files, so the run passes every file in the tree. The result records the index wall time, the first search wall time, their sum, peak RSS, the line count, the file count, and the corpus-to-input byte ratio, plus the CPU model, `nproc`, and `NESSO_THREADS`.

The `bench` CI job runs on every pull request, on pushes to `main`, and nightly. Pull requests time the 1k and 10k tiers. Pushes to `main` and the nightly schedule also time 100k. The job compares the tiers it ran with `bench/baseline.json` and fails when wall time is more than 1.35× the baseline or peak RSS is more than 1.15× the baseline (`NESSO_BENCH_WALL_TOLERANCE`, `NESSO_BENCH_RSS_TOLERANCE`). A missing baseline file fails the job. The same job runs the 4000-line grep once with `NESSO_THREADS=1` and once with `NESSO_THREADS=4` and fails unless the 4-thread run is at least `threadScaleMinRatio` faster (`scripts/ci`). The job summary lists lines per second for each tier and that ratio. A push to `main` appends the sized-tier result to benchmark history on the `gh-pages` branch. Pull requests do not write that branch. They add an informational comparison with the stored history to the job summary, and that comparison does not fail the job.

The chart file is [dev/bench/index.html](https://github.com/lorenzocaprari/nesso/blob/gh-pages/dev/bench/index.html) on that branch. GitHub Pages does not serve this repository.

Google Benchmark microbenchmarks build with `-o "&:bench=True"` on the Release profile and produce `build/Release/bench/nesso_bench`.
