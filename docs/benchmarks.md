# Benchmarks

Use a Release build. `NESSO_TRACE=1` prints one JSON line on stderr per command, with per-stage microseconds and peak RSS:

```bash
NESSO_TRACE=1 ./build/Release/src/nesso grep "connection refused" /var/log/app.log --model-dir models/
```

`NESSO_FORCE_KERNEL` pins the float distance kernel for tests and benchmarks. It is read once, on the first distance calculation in the process. `scalar` uses the scalar kernel. `avx2` uses AVX2+FMA when the CPU supports both, and the scalar kernel otherwise, so it does not execute AVX2 instructions the CPU lacks. An unset variable, an empty value, or any other value keeps automatic selection (AVX2+FMA when the CPU supports both, otherwise scalar). Leave it unset for that automatic kernel. Command output is unchanged.

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
python3 scripts/bench --ttfa
python3 scripts/bench --dataset real-log
python3 scripts/bench-real-log-compare --result bench/results/<sha>.real-log.json
```

`bench/baseline.json` is the committed shared-runner sample. Replace it on purpose with `bench-baseline`, which keeps the slower wall time of each tier when given more than one result:

```bash
go run -C scripts/ci . bench-baseline --output bench/baseline.json bench/results/<sha>.json
```

`python3 scripts/bench --dataset hdfs` indexes the Loghub HDFS_v1 2k sample and writes `bench/results/<sha>.hdfs.json`. `scripts/fetch-loghub` downloads that file into `build/bench/` and checks its sha256. The sample is repeated to 100000 lines unless `--lines` is set. The result records input lines, wall time, lines per second, peak RSS, corpus bytes, the corpus-to-input byte ratio, the CPU model, `nproc`, and `NESSO_THREADS`. The sample is research data and is not committed.

`python3 scripts/bench --dataset logdir` generates a seeded synthetic log directory and writes `bench/results/<sha>.logdir.json`. The tree has syslog files and two rotations, an auth log, kernel and boot logs, a cron log, a package-manager log, a JSON Lines application log, a journal text export, and small non-log files under `etc/`, `proc/`, and `sys/`. The lines are templates filled from a fixed seed. Nothing in the tree is downloaded or committed. `--lines` sets the line count (default 256). `--mib` grows the indexed files to that many MiB instead. `nesso index` reads regular files, so the run passes every file in the tree. The result records the index wall time, the first search wall time, their sum, peak RSS, the line count, the file count, and the corpus-to-input byte ratio, plus the CPU model, `nproc`, and `NESSO_THREADS`.

`python3 scripts/bench --ttfa` (same as `--dataset ttfa`) generates a seeded synthetic support-style bundle directory and writes `bench/results/<sha>.ttfa.json`. The tree has rotated plain logs, a `.gz` log, a JSON Lines file, an extensionless journal export, and a `.tar.gz` whose members are more generated logs. Gzip and tar bytes use fixed `mtime=0` metadata so a given seed is byte-identical. Nothing in the tree is downloaded or committed. `--lines` sets the line count (default 256). `--mib` grows the on-disk files to that many MiB instead. The run passes the bundle directory as the single `nesso index` path so directory walk and in-place `.gz` / `.tar.gz` reads are exercised; the archive is not extracted. Each timed command gets a fresh empty `XDG_CACHE_HOME`. The result records the index wall time, the first search wall time, their sum, peak RSS, the line count, the file count, and the corpus-to-input byte ratio, plus the CPU model, `nproc`, and `NESSO_THREADS`. This mode is report-only: it is not part of the Pipeline `bench` job and is not compared against `bench/baseline.json`.

`python3 scripts/bench --dataset search-profile` indexes one seeded log and times one `nesso search`. Non-query lines carry a letter id so the template mask does not collapse them. The timed search sets `NESSO_TRACE=1`. The stages `corpus-read`, `query-embed`, and `distance` are the corpus read, the ONNX query embedding, and the distance scan. `model-load` is the ONNX session load and is recorded on its own. The script runs that search again with `NESSO_TRACE` empty and fails when stdout differs. With `NESSO_TRACE` unset, nesso does not print those timings. `--lines` sets the log size (default 1000). The result is `bench/results/<sha>.search-profile.json`. It is not part of the Pipeline `bench` job and is not compared against `bench/baseline.json`.

`python3 scripts/bench --dataset real-log` times `nesso index`, `nesso grep`, `nesso search`, and `nesso search --mode dense` on the first 1000000 lines of selected Loghub logs from Zenodo record 8196385. Available corpora are HDFS_v1 (`block replication`), BGL (`kernel fatal`), and Thunderbird (`authentication failure`). The default is `hdfs-v1,thunderbird`: HDFS is a cheap smoke; Thunderbird is the peak-RSS / wall canary. Pass `--corpora all` (or `hdfs-v1,bgl,thunderbird`) to include BGL. `scripts/fetch-loghub` downloads each selected archive, checks its sha256, writes that prefix, and deletes the archive. Thunderbird is read from the tar member and is never extracted. The lines are real log lines. A short file is an error. Nothing is repeated, and nothing is committed. Each selected set runs at `NESSO_THREADS=1` and `NESSO_THREADS=8`. Every command sets `NESSO_TRACE=1`. The result is one file, `bench/results/<sha>.real-log.json`, with wall time, lines per second, peak RSS, input bytes, corpus bytes, and the trace stages. `--lines` changes the prefix length. `--real-log-root` points at a directory that already holds the selected `*_1m.log` files.

`scripts/bench-real-log-compare` reads that JSON. Peak RSS is a hard failure when any command is above 1.5 GiB times `(1 + NESSO_BENCH_RSS_TOLERANCE)`. The nightly workflow sets that tolerance to `0.15`. Wall time is compared with the median of the last 20 recorded nights for the same command and only prints a warning. A missing history prints a warning and exits 0. `--history` reads a local JSON `runs` list or a `dev/bench-real-log/data.js` file. `--repo owner/name` reads that file from the `gh-pages` branch.

[`.github/workflows/bench-real-log.yml`](../.github/workflows/bench-real-log.yml) runs this on the schedule `47 5 * * *` (main only) and on `workflow_dispatch` (any branch, for a manual check). Nightly uses `hdfs-v1,thunderbird`. Manual runs can set the `corpora` input to `all` or another comma-separated list. It does not run on pull requests, and it does not change the Pipeline `bench` job or `bench/baseline.json`. From Actions, open **Real-log bench**, choose **Run workflow**, pick the branch, and optionally set `corpora`, `force_log_refetch` (ignore the truncated-log cache), or clear `record_history` (skip the gh-pages append; history writes still require `main`). The job timeout is 300 minutes. The truncated logs are cached by corpus set and the pin in `scripts/fetch-loghub`. A cold Thunderbird run downloads that archive (about 2 GiB) and deletes it after the prefix is written. The history job appends wall times to `dev/bench-real-log` on `gh-pages`. It needs `contents: write`. The measure job needs `contents: read`, `packages: read`, and `actions: write`. The chart is [dev/bench-real-log/index.html](https://github.com/lorenzocaprari/nesso/blob/gh-pages/dev/bench-real-log/index.html) on that branch. GitHub Pages does not serve this repository.

`./scripts/check-index-rss` measures `nesso index` peak RSS on a repeated Loghub HDFS sample (default 1M lines, budget 1.5 GiB, threads 1 and 8). [`.github/workflows/index-rss.yml`](../.github/workflows/index-rss.yml) runs that gate on schedule `47 4 * * *` and on `workflow_dispatch`. It is not a Pipeline job.

`scripts/eval-retrieval` scores `nesso search --json -k 10` against labelled queries on the pinned Loghub 2k samples (HDFS, BGL, Thunderbird, OpenSSH, Linux). Pass `--dataset all-2k` (or a comma-separated list such as `hdfs-2k,bgl-2k`) to select systems; each maps to its log, structured CSV, and `test/eval/queries/<system>_2k.jsonl` seed file. Without `--dataset`, the defaults stay the HDFS_2k paths. It indexes each selected log once, runs each query for the requested modes (default `auto`, `hybrid`, `dense`, `lexical`), and reports recall@10, MRR, and nDCG@10. A query sets exactly one of `relevant_lines` (1-based LineIds) or `relevant_events` (Loghub EventIds from the structured CSV that `scripts/fetch-loghub` also downloads). The relevant set R is the listed lines, or every line whose EventId is listed. Gains are binary on lines: recall@10 is `|top 10 ∩ R| / min(10, |R|)`, MRR is `1 / rank` of the first line in R (0 if none), and nDCG@10 is DCG@10 divided by the ideal DCG of `min(10, |R|)` relevant lines. Exact-ID seeds use `relevant_lines`. Paraphrase and cause seeds use `relevant_events`. Each listed line must exist and contain the query as a whole token. The CSV row count must equal the log line count so LineId matches nesso's 1-based lines. The script prints one text table per system plus an overall table, and writes deterministic JSON (`schema_version` 3, per-dataset modes and an overall aggregate, per-query `ranked_lines`) to `--out`. It is report-only: any scores exit 0; it fails only on missing inputs, a bad query file, a line label or CSV alignment check that fails, or unparseable search output. The committed seed sets in `test/eval/queries/` are for local smoke scoring and are not the published retrieval benchmark. [`.github/workflows/eval-retrieval.yml`](../.github/workflows/eval-retrieval.yml) runs that report on schedule `47 6 * * *` (main only) and on `workflow_dispatch`; it is not a Pipeline job and does not write `gh-pages` yet.

```bash
./scripts/fetch-loghub --dataset all-2k
python3 scripts/eval-retrieval --dataset all-2k --out build/eval/all_2k.json
```

Optional keyword and DIY semantic baselines share the same labels and metrics. Pass `--engine` repeatedly (`nesso`, `grep`, `ripgrep`, `bm25`, `diy-faiss`, `xerj`, `xerj-neural`; default is nesso only). Without `--engine` (or with `--engine nesso` alone) the JSON stays `schema_version` 3 and is byte-compatible with prior nesso-only reports. Any other engine writes `schema_version` 4 with an `engines[]` axis and a side-by-side text table. Scoring is unchanged: each engine only supplies ranked 1-based line ids.

- **grep:** `grep -n -F -i -e <query> -- <log>`. If that has no hits, OR the query's whitespace tokens with one `-e` per token. Rank is file order (ascending line id), top 10. This is the "what people do today" baseline.
- **ripgrep:** the same rule with `rg -n -F -i`. If `rg` is missing, the engine is reported as `skipped` (not zeros).
- **bm25:** Python stdlib `sqlite3` FTS5, one row per line with `rowid = line id`, query = OR of FTS5-quoted tokens, `ORDER BY bm25(lines), rowid LIMIT 10`. The run fails clearly if the sqlite3 build lacks FTS5.
- **diy-faiss:** the stack a team would hack together — Hugging Face `sentence-transformers/all-MiniLM-L6-v2` at the pinned revision in `bench/competitors/diy_faiss.py` (fp32, `normalize_embeddings=True`), one vector per log line (no dedup, no masking), FAISS `IndexFlatIP`, top 10, ties broken by line id. CPU only, one thread, fixed seeds. The engine lives in `bench/competitors/diy_faiss.py` and is imported only when selected. Missing packages fail with an install hint. The report's diy-faiss entry records `versions` (model revision plus sentence-transformers, torch, and faiss). Dependencies are hash-pinned in `bench/competitors/diy-faiss.hashes.txt` for a local venv; they are not in the build image, Pipeline, or the nightly job. The pin file avoids the `*requirements*.txt` name so PR dependency-review does not index it.
- **xerj / xerj-neural:** [XERJ](https://github.com/xerj-org/xerj) over its Elasticsearch-compatible API (stdlib `urllib` only). One document per log line (`_id` = line id, fields `line` and `text`). `xerj` uses a plain `match` on `text` against the default lexical embedder (no custom analyzer). `xerj-neural` maps `text` as `semantic_text` and runs the documented `semantic` query; it needs the three MiniLM files staged locally (`--xerj-model-dir`) and never downloads them. Pass `--xerj-url http://127.0.0.1:9200` for an already running node, or `--xerj-bin` / `xerj` on `PATH` to spawn `--insecure --data-dir <tmp> --port <free>` (neural adds `--embed-mode neural` and a temp TOML `local_model_dir`). Missing URL and binary, or missing neural model files when spawning, report `skipped` (not zeros). Report `versions` carry xerj version, `url_mode` (`external` or `spawned`), index wall time, and `~90 MB` for neural. Local or per-release only — not Pipeline and not the nightly `eval-retrieval` job.

Fairness for published comparisons: same logs, queries, machine, and k=10. DIY uses the original Hub fp32 MiniLM; nesso uses the shipped int8 ONNX. Compare scores; do not expect identical vectors.

```bash
python3 scripts/eval-retrieval --dataset all-2k \
  --engine nesso --engine grep --engine ripgrep --engine bm25 \
  --out build/eval/all_2k_baselines.json
```

```bash
python3.12 -m venv .venv-diy
.venv-diy/bin/pip install --require-hashes \
  -r bench/competitors/diy-faiss.hashes.txt \
  --extra-index-url https://download.pytorch.org/whl/cpu
PYTHONHASHSEED=0 .venv-diy/bin/python scripts/eval-retrieval --dataset all-2k \
  --engine diy-faiss --out build/eval/diy_faiss.json
```

Set `NESSO_EVAL_DIY=1` to run the opt-in live diy-faiss unit test against a tiny fixture (requires the venv packages).

Baseline comparisons are a local or per-release tool: they are not Pipeline and are not the nightly `eval-retrieval` workflow. Published comparisons should be run on a fixed machine.

The nightly workflow downloads the newest `eval-retrieval` artifact from the latest successful scheduled run on `main`, then runs `scripts/eval-retrieval-compare` against the fresh report. That script prints a Markdown table of per-system, per-mode, per-class (and overall) recall@10 / MRR / nDCG@10 deltas and emits a warning annotation when a score drops by more than 5% relative to the previous value (`--tolerance` or `NESSO_EVAL_TOLERANCE`). A missing previous artifact prints `no previous run to compare`. Scores never fail the job; only unreadable or malformed JSON does.

The `bench` CI job runs on every pull request, on pushes to `main`, and nightly. Pull requests time the 1k and 10k tiers. Pushes to `main` and the nightly schedule also time 100k. The job compares the tiers it ran with `bench/baseline.json` and fails when wall time is more than 1.35× the baseline or peak RSS is more than 1.15× the baseline (`NESSO_BENCH_WALL_TOLERANCE`, `NESSO_BENCH_RSS_TOLERANCE`). A missing baseline file fails the job. The same job runs the 4000-line grep once with `NESSO_THREADS=1` and once with `NESSO_THREADS=4` and fails unless the 4-thread run is at least `threadScaleMinRatio` faster (`scripts/ci`). The job summary lists lines per second for each tier and that ratio. A push to `main` appends the sized-tier result to benchmark history on the `gh-pages` branch. Pull requests do not write that branch. They add an informational comparison with the stored history to the job summary, and that comparison does not fail the job.

The chart file is [dev/bench/index.html](https://github.com/lorenzocaprari/nesso/blob/gh-pages/dev/bench/index.html) on that branch. GitHub Pages does not serve this repository.

Google Benchmark microbenchmarks build with `-o "&:bench=True"` on the Release profile and produce `build/Release/bench/nesso_bench`. That binary times the scalar and AVX2+FMA dot-product and squared-L2 kernels on 384-dimensional vectors (`DotProduct384Scalar`, `DotProduct384Avx2Fma`, `L2Squared384Scalar`, `L2Squared384Avx2Fma`). The AVX2+FMA cases skip when `SelectDistanceKernel("avx2", ...)` would not select that kernel.
