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

`scripts/eval-retrieval` scores `nesso search --json -k 10` against labelled queries on the pinned Loghub 2k samples (HDFS, BGL, Thunderbird, OpenSSH, Linux, Apache). Pass `--dataset all-2k` (or a comma-separated list such as `hdfs-2k,bgl-2k`) to select systems; each maps to its log, structured CSV, and `test/eval/queries/<system>_2k.jsonl` seed file. Without `--dataset`, the defaults stay the HDFS_2k paths. It indexes each selected log once, runs each query for the requested modes (default `auto`, `hybrid`, `dense`, `lexical`), and reports recall@10, MRR, and nDCG@10. A query sets exactly one of `relevant_lines` (1-based LineIds) or `relevant_events` (Loghub EventIds from the structured CSV that `scripts/fetch-loghub` also downloads). The relevant set R is the listed lines, or every line whose EventId is listed. Gains are binary on lines: recall@10 is `|top 10 ∩ R| / min(10, |R|)`, MRR is `1 / rank` of the first line in R (0 if none), and nDCG@10 is DCG@10 divided by the ideal DCG of `min(10, |R|)` relevant lines. Exact-ID seeds use `relevant_lines`. Paraphrase and cause seeds use `relevant_events`. Each listed line must exist and contain the query as a whole token. The CSV row count must equal the log line count so LineId matches nesso's 1-based lines. The script prints one text table per system plus an overall table, and writes deterministic JSON (`schema_version` 3, per-dataset modes and an overall aggregate, per-query `ranked_lines`) to `--out`. It is report-only: any scores exit 0; it fails only on missing inputs, a bad query file, a line label or CSV alignment check that fails, or unparseable search output. The committed seed sets in `test/eval/queries/` are for local smoke scoring and are not the published retrieval benchmark. [`.github/workflows/eval-retrieval.yml`](../.github/workflows/eval-retrieval.yml) runs that report with nesso, grep, and BM25 (`--engine nesso --engine grep --engine bm25`), plus the `bgl-needle-100k` needle window with nesso (`auto`, `dense`, `lexical`), grep, and BM25. It runs on schedule `47 6 * * *` (main only), on `workflow_dispatch`, and on pull requests that change `src/`, `CMakeLists.txt`, `conanfile.py`, `conan.lock`, the eval and fetch scripts, `test/eval/`, `bench/competitors/`, or the workflow itself. It is not a Pipeline job and not part of the `CI gate`; scores never fail it. It does not write `gh-pages` yet.

```bash
./scripts/fetch-loghub --dataset all-2k
python3 scripts/eval-retrieval --dataset all-2k --out build/eval/all_2k.json
```

Optional keyword and DIY semantic baselines share the same labels and metrics. Pass `--engine` repeatedly (`nesso`, `grep`, `ripgrep`, `bm25`, `diy-faiss`, `xerj`, `xerj-neural`, `opensearch`, `opensearch-knn`, `ck`, `vecgrep`; default is nesso only). Without `--engine` (or with `--engine nesso` alone) the JSON stays `schema_version` 3 and is byte-compatible with prior nesso-only reports. Any other engine writes `schema_version` 4 with an `engines[]` axis and a side-by-side text table. Scoring is unchanged: each engine only supplies ranked 1-based line ids.

- **grep:** `grep -n -F -i -e <query> -- <log>`. If that has no hits, OR the query's whitespace tokens with one `-e` per token. Rank is file order (ascending line id), top 10. This is the "what people do today" baseline.
- **ripgrep:** the same rule with `rg -n -F -i`. If `rg` is missing, the engine is reported as `skipped` (not zeros).
- **bm25:** Python stdlib `sqlite3` FTS5, one row per line with `rowid = line id`, query = OR of FTS5-quoted tokens, `ORDER BY bm25(lines), rowid LIMIT 10`. The run fails clearly if the sqlite3 build lacks FTS5.
- **diy-faiss:** the stack a team would hack together — Hugging Face `sentence-transformers/all-MiniLM-L6-v2` at the pinned revision in `bench/competitors/diy_faiss.py` (fp32, `normalize_embeddings=True`), one vector per log line (no dedup, no masking), FAISS `IndexFlatIP`, top 10, ties broken by line id. CPU only, one thread, fixed seeds. The engine lives in `bench/competitors/diy_faiss.py` and is imported only when selected. Missing packages fail with an install hint. The report's diy-faiss entry records `versions` (model revision plus sentence-transformers, torch, and faiss). Dependencies are hash-pinned in `bench/competitors/diy-faiss.hashes.txt` for a local venv; they are not in the build image, Pipeline, or the nightly job. The pin file avoids the `*requirements*.txt` name so PR dependency-review does not index it.
- **xerj-semantic:** xerj's out-of-the-box semantic search. `text` is mapped as `semantic_text` and queried with `semantic`, like xerj-neural, but the node keeps xerj's default embedder (lexical feature hashing, offline, no model files). It needs a node that is not in `--embed-mode neural`: in a run that also selects xerj-neural, the shared spawned node is neural, so xerj-semantic reports `skipped`. Run it on its own (the eval-competitors workflow does).
- **xerj / xerj-neural:** [XERJ](https://github.com/xerj-org/xerj) over its Elasticsearch-compatible API (stdlib `urllib` only). One document per log line (`_id` = line id, fields `line` and `text`). `xerj` uses a plain `match` on `text` against the default lexical embedder (no custom analyzer). `xerj-neural` maps `text` as `semantic_text` and runs the documented `semantic` query; it needs the three MiniLM files staged locally (`--xerj-model-dir`) and never downloads them. Pass `--xerj-url http://127.0.0.1:9200` for an already running node, or `--xerj-bin` / `xerj` on `PATH` to spawn `--insecure --data-dir <tmp> --port <free>` (neural adds `--embed-mode neural` and a temp TOML `local_model_dir`). Missing URL and binary, or missing neural model files when spawning, report `skipped` (not zeros). Report `versions` carry xerj version, `url_mode` (`external` or `spawned`), index wall time, and `~90 MB` for neural. Local or per-release only — not Pipeline and not the nightly `eval-retrieval` job.
- **opensearch / opensearch-knn:** single-node [OpenSearch](https://opensearch.org/) (Apache-2.0), the self-hosted search incumbent, through the same stdlib HTTP adapter as xerj. Each index has one primary shard and no replica, so BM25 statistics are not split across shards. `opensearch` maps `text` with the default standard analyzer and runs a plain `match`. `opensearch-knn` stores one `knn_vector` (384 dimensions, HNSW, Lucene engine, `cosinesimil`, default parameters) per line. The vectors come from the same fp32 MiniLM revision and encoder as diy-faiss, computed in the eval process. It needs the diy-faiss venv and reports `skipped` with the install hint when that stack is missing. OpenSearch ML Commons in-engine models are not used. Pass `--opensearch-url http://127.0.0.1:9200` for a node you already run with security off, or `--opensearch-home` (or `OPENSEARCH_HOME`) to spawn one from an extracted tarball. The spawned node is `bin/opensearch` with `discovery.type=single-node`, `network.host=127.0.0.1`, free HTTP and transport ports, a temp `path.data` and `path.logs`, the disk watermark check off, `plugins.security.disabled=true` when the security plugin is installed, and `OPENSEARCH_JAVA_OPTS` (default `-Xms1g -Xmx1g`). With no URL and no home, both engines report `skipped`. A node without the `opensearch-knn` plugin reports `opensearch-knn` as `skipped`. Report `versions` carry `opensearch_version`, `url_mode`, index wall time, `java_opts`, and, for k-NN, the method, model, and revision. Local or per-release only — not Pipeline and not the nightly `eval-retrieval` job.
- **ck / vecgrep:** the two closest semantic-grep CLIs, [ck](https://github.com/BeaconBay/ck) (Apache-2.0) and [vecgrep](https://github.com/martintrojer/vecgrep) (MIT), run as installed, with their default models and chunking. Each gets its own corpus directory holding only a hard link (or copy) of the log, so its index stays out of the repository. vecgrep gets a `.vecgrep` directory there to pin its project root. The index step is `ck --index --quiet .` or `vecgrep --index-only --quiet`. Each query runs `ck --sem --jsonl --no-snippet --quiet --topk 10 --threshold 0 -- <query> <log>` or `vecgrep --json --quiet -k 10 --threshold 0 -- <query> <log>`. `--threshold 0` turns off ck's default 0.6 cutoff so both tools return their top 10 like every other engine. Both tools rank multi-line chunks, not lines. The adapter orders chunks by score, then start line. Each chunk contributes its lines in file order, a line that an earlier overlapping chunk already gave is skipped, and the list stops at 10 lines. So one strong chunk can fill the whole top 10. That is how a reader of their output sees it, and it is the main reason their line scores differ from nesso's. Missing executables report `skipped`. Report `versions` carry the tool's `--version` line. ck embeds with `BAAI/bge-small-en-v1.5` and downloads it to `$XDG_CACHE_HOME/ck` (or `~/.cache/ck`) on first use. vecgrep ships `all-MiniLM-L6-v2` inside the binary. Run ck once before `--measure` so the timed runs do not include that download. Local or per-release only — not Pipeline and not the nightly `eval-retrieval` job.

Install the pinned release binaries and check their sha256 (ck publishes a `.sha256` file; vecgrep's digest is on its GitHub release asset):

```bash
curl -LO https://github.com/BeaconBay/ck/releases/download/0.8.1/ck-0.8.1-x86_64-unknown-linux-gnu.tar.gz
echo "042db74e0ed10cd5995024bc16ea9928de6aaef632ef6463b3b8812a8d1ba1ac  ck-0.8.1-x86_64-unknown-linux-gnu.tar.gz" | sha256sum -c
curl -LO https://github.com/martintrojer/vecgrep/releases/download/v0.10.1/vecgrep-v0.10.1-x86_64-unknown-linux-gnu.tar.gz
echo "389d131e28bc4dc4b8c00aaffde051b4ebaa127cceeb89dcf8ebb4dc03f05021  vecgrep-v0.10.1-x86_64-unknown-linux-gnu.tar.gz" | sha256sum -c
mkdir -p "$HOME/.local/bin"
tar -xzf ck-0.8.1-x86_64-unknown-linux-gnu.tar.gz -C "$HOME/.local/bin" ./ck
tar -xzf vecgrep-v0.10.1-x86_64-unknown-linux-gnu.tar.gz -C "$HOME/.local/bin" vecgrep
python3 scripts/eval-retrieval --dataset all-2k \
  --engine nesso --engine ck --engine vecgrep --out build/eval/chunk_cli.json
```

Fetch the OpenSearch tarball yourself; the script never downloads it. The min distribution has no k-NN plugin, and the plugin is not published on its own, so use the full bundle (about 1.1 GB):

```bash
curl -LO https://artifacts.opensearch.org/releases/bundle/opensearch/3.10.0/opensearch-3.10.0-linux-x64.tar.gz
curl -LO https://artifacts.opensearch.org/releases/bundle/opensearch/3.10.0/opensearch-3.10.0-linux-x64.tar.gz.sha512
sha512sum -c opensearch-3.10.0-linux-x64.tar.gz.sha512
tar -xzf opensearch-3.10.0-linux-x64.tar.gz
PYTHONHASHSEED=0 .venv-diy/bin/python scripts/eval-retrieval --dataset all-2k \
  --engine bm25 --engine diy-faiss --engine opensearch --engine opensearch-knn \
  --opensearch-home opensearch-3.10.0 --out build/eval/opensearch.json
```

OpenSearch refuses to start as root. The bundled JDK means no system Java is needed.

Fairness for published comparisons: same logs, queries, machine, and k=10. DIY and opensearch-knn use the original Hub fp32 MiniLM; nesso uses the shipped int8 ONNX. Compare scores; do not expect identical vectors. opensearch-knn and diy-faiss share vectors, so a score gap between them comes from HNSW being approximate where FAISS `IndexFlatIP` is exact. OpenSearch peak RSS follows the JVM heap in `OPENSEARCH_JAVA_OPTS`; keep that value beside any published number.

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

Set `NESSO_EVAL_DIY=1` to run the opt-in live diy-faiss unit test against a tiny fixture (requires the venv packages). Set `NESSO_EVAL_MEASURE=1` to run the opt-in measure smoke on HDFS 2k.

### Labelled Loghub 2.0 windows

The 2k seed sets are labelled by us and are small. For larger tasks with ground truth we did not write, `scripts/fetch-loghub` cuts windows out of the Loghub 2.0 BGL and Thunderbird logs (Zenodo record [8275861](https://zenodo.org/records/8275861), CC-BY-4.0). In those logs the first token of every line is the system administrators' alert tag: `-` for a normal line, otherwise an alert category such as `KERNDTLB`, `APPSEV`, or `EXT_FS`. The tags come from A. Oliner and J. Stearley, "What Supercomputers Say: A Study of Five System Logs", DSN 2007. Loghub 2.0 is from Z. Jiang et al., "A Large-scale Evaluation for Log Parsing Techniques: How Far Are We?", ISSTA 2024.

```bash
./scripts/fetch-loghub --dataset bgl-loghub2 --window 4100001:100000 --window 3200001:1000000
./scripts/fetch-loghub --dataset thunderbird-loghub2 --window 8550001:1000000
```

Each `--window START:COUNT` (1-based start line, repeatable) writes `build/bench/<Name>_loghub2_<start>_<count>.log` with the tag removed, so the alert category is never searchable text, and a `.labels` file with one tag per line. The archive (BGL 86 MB, Thunderbird 293 MB) is checked against its sha256 pin, read once for all windows, and deleted. The three windows above have pinned output hashes. A cached window that still matches its pin is kept, and a fresh write that does not match fails. Other windows are written unpinned.

| Window | Lines | Alert categories | Alert lines | Rarest categories |
| --- | --- | --- | --- | --- |
| BGL from 4,100,001 | 100,000 | 17 | 21,061 | KERNNOETH 1, LINKBLL 2, KERNSTOR 2, MONPOW 3 |
| BGL from 3,200,001 | 1,000,000 | 27 | 72,300 | KERNEXT 1, MMCS 1, LINKBLL 2, KERNSTOR 3 |
| Thunderbird from 8,550,001 | 1,000,000 | 5 | 32 | NMI 2, CPU 3, ECC 6, EXT_FS 8, SCSI 13 |

The windows were chosen to hold the most distinct alert categories at each size, scanning Loghub 2.0 in 50,000-line steps. Loghub data is for research use and stays out of the repository.

### Needle benchmark

`scripts/eval-retrieval --dataset bgl-needle-100k` (also `bgl-needle-1m`, `thunderbird-needle-1m`, or `all-needle`) scores engines on those windows. Each query names one alert category, and its relevant set is every line the administrators tagged with that category. We write the queries; we do not write the labels. The task is "something like this happened, show me the lines", with real distractors. The same message text often appears untagged at a lower severity, and on Thunderbird 32 alert lines sit among 1,000,000.

`test/eval/needles/bgl.jsonl` (27 categories) and `test/eval/needles/thunderbird.jsonl` (5 categories) hold two queries per category:

- `keyword`: the terse terms an engineer would grep for, for example `data TLB error`.
- `symptom`: an operator's description that avoids the log's own words where it can, for example `processor address translation cache fault`.

Each query records the category's `meaning` for review. The report prints each class's mean token `overlap` with the relevant lines, so a symptom query that drifts into the log's vocabulary shows up instead of hiding. Queries whose category is absent from a window are left out of that window, and categories without a query are listed.

Metrics per engine and class:

| Column | Meaning |
| --- | --- |
| `p@10` | Share of the top 10 lines that carry the category |
| `MRR`, `nDCG@10` | As in the 2k eval, on the tagged line set |
| `found@100` | Share of queries with at least one tagged line in the top 100 |
| `median 1st` | Median rank of the first tagged line, among queries that found one ("how far would I scroll") |
| `overlap` | Mean share of query tokens that occur in the tagged lines |

Every engine works here, nesso in each `--modes` value. diy-faiss, OpenSearch k-NN, ck, and vecgrep embed every line, which takes hours at 1,000,000 lines, so run them on `bgl-needle-100k`. The report is JSON with `"kind": "needle"` and its own `schema_version`; it is not the 2k report shape, and `scripts/eval-retrieval-compare` does not read it. `--measure` works on needle datasets, see below. `bgl-needle-100k` with nesso, grep, and BM25 also runs in the `eval-retrieval` workflow (nightly and on pull requests); the 1M windows and the embedding baselines stay local or per release.

```bash
python3 scripts/eval-retrieval --dataset bgl-needle-100k \
  --engine nesso --engine grep --engine bm25 --out build/eval/needle_bgl_100k.json
python3 scripts/eval-retrieval --dataset thunderbird-needle-1m,bgl-needle-1m \
  --engine nesso --engine grep --engine bm25 --modes auto,dense,lexical --out build/eval/needle_1m.json
```

### Heavy competitors in CI

[`.github/workflows/eval-competitors.yml`](../.github/workflows/eval-competitors.yml) scores the competitors that are too heavy for every night on the 2k seed sets and on `bgl-needle-100k`. It measures retrieval quality only, with no `--measure`, because hosted-runner timings are too noisy to publish. Each of diy-faiss, opensearch, opensearch-knn, ck, vecgrep, xerj, xerj-neural, and xerj-semantic runs in its own job on `ubuntu-24.04`, on the host rather than in the build container, because OpenSearch refuses to run as root. The jobs install the same pinned versions as the local steps above: the diy-faiss venv from `bench/competitors/diy-faiss.hashes.txt`, the OpenSearch 3.10.0 bundle checked by sha512, ck 0.8.1 and xerj 1.0.0-rc.104 (the static musl build) checked by sha256, vecgrep v0.10.1 built from its tag commit with `cargo install --locked` (its release binary needs glibc 2.43, newer than the runner's), and for xerj-neural the three all-MiniLM-L6-v2 files at the revision diy-faiss pins, checked by sha256. The `merge` job combines the reports that finished with `scripts/eval-retrieval-merge`, writes an overall table per engine and class to the job summary, and uploads `eval-competitors` (`competitors-2k.json`, `competitors-needle.json`). A `prepare` job fetches the Loghub inputs once, retrying a failed or mismatched download, and hands them to the engine jobs as an artifact. ck and vecgrep score the 2k seed sets only: on the 100k needle window their multi-line chunk indexes exhaust the hosted runner's 16 GB and the runner shuts down mid-step, so run them on `bgl-needle-100k` locally with enough memory. An engine whose job failed is listed as missing rather than failing the merge. nesso is not built here; the `eval-retrieval` workflow scores it on the same sets. Run it from Actions with **Run workflow**. It also runs on pull requests that change the workflow, the merge script, or `bench/competitors/`. It is not a Pipeline job and not part of the `CI gate`.

Which model each semantic engine uses matters for reading the results. **Same-model controls** use all-MiniLM-L6-v2, the model nesso ships, so a difference comes from the engine, fusion, and routing rather than the model: diy-faiss (fp32, exact search), opensearch-knn (the same vectors, HNSW), and xerj-neural. xerj's neural mode is itself all-MiniLM-L6-v2: without `embedding.local_model_dir` it downloads those three files from Hugging Face. The workflow stages them at the revision diy-faiss pins, so the run is reproducible. xerj's release model bundle is its separate facts model and plays no part in search. **As shipped** engines use their own defaults: vecgrep bundles all-MiniLM-L6-v2, ck uses BAAI/bge-small-en-v1.5, and xerj-semantic is xerj's default `semantic` search, a lexical feature-hashing embedder with no model. A loss to an as-shipped engine can come from its model; that question belongs to a model comparison, not to ranking.

`scripts/eval-retrieval-merge` combines engine reports (`schema_version` 4) or needle reports that cover the same datasets into one report, in input order. It refuses a mix of kinds, different datasets, and an engine that appears twice. `--markdown` also writes the overall table.

### nesso margin

`scripts/eval-retrieval-compare --current report.json --margins` prints, per system and class, nesso against the best other engine by MRR. It needs no previous report. The headline nesso ranker is `auto`, else the first of `hybrid`, `dense`, `lexical` present. The candidates are the engines scored in the same report plus the reports passed with `--reference` (repeatable). A reference that is a different kind (2k engine report or needle) is ignored, and an engine in the current report wins over the same engine in a reference. The margin is nesso MRR minus the best other MRR, and the table notes classes with fewer than 30 queries, where one query moves MRR by more than 1/30 and small margins are noise. `--margins` and `--previous` can be combined.

The `eval-retrieval` workflow prints this table for the 2k report and the needle report in every run. Its candidates are grep and BM25 from that run, plus the committed reports in `test/eval/reference/` when present, which come from the `eval-competitors` workflow (see that directory's README for the refresh steps).

### Retrieval eval `--measure`

Off by default. Without it, quality JSON stays byte-identical with prior runs. With `--measure`, the report gains a top-level `measure` object that is machine-dependent and must not be used as a CI or nightly gate.

Numbers (median across `--repeat`, default 3):

| Field | Meaning |
| --- | --- |
| `indexWallSeconds` / `indexPeakRssBytes` | GNU `/usr/bin/time -f "%e %M"` around the index/build child (`nesso index`, FTS5 build, FAISS encode+build, `ck --index` / `vecgrep --index-only`, or spawned xerj or OpenSearch bulk; opensearch-knn also encodes in this child). grep/ripgrep have no index: JSON `null`, tables print `none` (never `0`). |
| `queryWallSeconds` | Wall for the full query set (GNU `%e` of the query child, or the sum of per-query latencies for xerj and OpenSearch so spawn/re-index is not counted). |
| `queryLatencyMedianSeconds` / `queryLatencyP95Seconds` | Median and nearest-rank p95 of per-query latencies inside that child. |
| `queryPeakRssBytes` | Peak RSS of the query-phase child (same GNU time method). |
| `indexBytes` / `indexToLogRatio` | On-disk index size and ratio to the raw log. |
| `timeToFirstResultSeconds` | Cold one-shot: `nesso grep` / `grep` / `rg` on the first query; `ck` / `vecgrep` search a fresh corpus directory and build their index on demand; for indexed engines, index (and spawn for xerj and OpenSearch) plus the first query in one timed child. |

Peak RSS always comes from GNU time on a child process. In-process Python engines (`bm25`, `diy-faiss`) run under `--measure-worker` so RSS is comparable. xerj measure requires a spawned node (`--xerj-bin` / `PATH`), not `--xerj-url`, so the server is inside the timed process tree. OpenSearch measure likewise requires `--opensearch-home` / `OPENSEARCH_HOME`, not `--opensearch-url`; `indexBytes` is the spawned node's data directory after a flush.

Platform, CPU model, `nproc`, `NESSO_THREADS`, and tool versions are recorded in `measure`. Published numbers should come from one fixed machine with those fields kept beside the table.

Larger inputs (speed/memory only, no quality labels):

```bash
python3 scripts/eval-retrieval --measure --log-prefix-lines 100000 \
  --dataset hdfs-v1 --engine nesso --engine grep --engine bm25 \
  --modes dense --repeat 3 --out build/eval/hdfs_100k_measure.json
```

Default `--log-prefix-lines` (when the flag is present without a value) is 100000. The published comparison size is 1000000 lines (the same pin `scripts/fetch-loghub` uses for full sets).

```bash
python3 scripts/eval-retrieval --dataset hdfs-2k \
  --engine nesso --engine grep --engine ripgrep --engine bm25 \
  --measure --repeat 3 --modes dense \
  --out build/eval/hdfs_2k_measure.json
```

The keyword baselines (grep, BM25) also run in the `eval-retrieval` workflow, because they are cheap and their scores do not depend on the runner. The other baselines, and every `--measure` run, stay local or per release: runner timings are too noisy to publish, and the heavy engines download large stacks. Published comparisons should be run on a fixed machine.

Each run of that workflow, including a pull request run, downloads the `eval-retrieval` artifact from the latest successful scheduled run on `main` and runs `scripts/eval-retrieval-compare` on the 2k report and the needle report. A pull request uploads its own reports as `eval-retrieval-pr`, which never becomes the baseline. The compare script prints a Markdown table of per-system, per-ranker, per-class (and overall) recall@10 / MRR / nDCG@10 deltas, plus precision@10 for needle reports. A ranker is a nesso mode or another engine. A nesso-only report (`schema_version` 3) and an engine report (`schema_version` 4) compare with each other, and a needle report compares only with a needle report. The workflow emits a warning annotation when a score drops by more than 5% relative to the previous value (`--tolerance` or `NESSO_EVAL_TOLERANCE`). A missing previous artifact prints `no previous run to compare`. Scores never fail the job; only unreadable or malformed JSON does.

The `bench` CI job runs on every pull request, on pushes to `main`, and nightly. Pull requests time the 1k and 10k tiers. Pushes to `main` and the nightly schedule also time 100k. The job compares the tiers it ran with `bench/baseline.json` and fails when wall time is more than 1.35× the baseline or peak RSS is more than 1.15× the baseline (`NESSO_BENCH_WALL_TOLERANCE`, `NESSO_BENCH_RSS_TOLERANCE`). A missing baseline file fails the job. The same job runs the 4000-line grep once with `NESSO_THREADS=1` and once with `NESSO_THREADS=4` and fails unless the 4-thread run is at least `threadScaleMinRatio` faster (`scripts/ci`). The job summary lists lines per second for each tier and that ratio. A push to `main` appends the sized-tier result to benchmark history on the `gh-pages` branch. Pull requests do not write that branch. They add an informational comparison with the stored history to the job summary, and that comparison does not fail the job.

The chart file is [dev/bench/index.html](https://github.com/lorenzocaprari/nesso/blob/gh-pages/dev/bench/index.html) on that branch. GitHub Pages does not serve this repository.

Google Benchmark microbenchmarks build with `-o "&:bench=True"` on the Release profile and produce `build/Release/bench/nesso_bench`. That binary times the scalar and AVX2+FMA dot-product and squared-L2 kernels on 384-dimensional vectors (`DotProduct384Scalar`, `DotProduct384Avx2Fma`, `L2Squared384Scalar`, `L2Squared384Avx2Fma`). The AVX2+FMA cases skip when `SelectDistanceKernel("avx2", ...)` would not select that kernel.

### Speed and memory on the needle windows

`--measure` on a needle dataset times every engine on the window's log and the window's queries, with the same columns and method as the 2k measure above (GNU `time` around each phase's child process): index wall and peak RSS, index size and ratio to the raw log, query wall, p50 and p95, query peak RSS, and cold time to first result. That puts quality, memory, and speed on the same task and at 100,000 and 1,000,000 lines, where the 2k samples cannot show the differences.

```bash
python3 scripts/eval-retrieval --dataset bgl-needle-100k \
  --engine nesso --engine grep --engine bm25 --modes auto,dense \
  --measure --repeat 3 --out build/eval/needle_bgl_100k_measure.json
```

`--measure-only` skips the quality scoring and only measures, which saves the second pass over a slow engine. Quality and measurements share one report: the measurements are under `measure` and the scores under `datasets`. Run `--repeat 1` at 1,000,000 lines. `xerj` and OpenSearch run in a spawned node per phase, so their peak RSS includes the server, and `--xerj-url` and `--opensearch-url` are refused. The report records the machine, the tool versions, and, for nesso, a digest of `model.onnx` and `vocab.txt` (`measure.tool["nesso-model"]` and the top-level `nesso.model`), so numbers produced with a different embedding model, quantization, or vocabulary cannot be mistaken for each other. Timings from a laptop are for development. Published numbers should come from one fixed machine, with the platform fields kept beside the table, and are never produced by CI.
