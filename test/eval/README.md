# Retrieval evaluation seeds

`queries/*_2k.jsonl` are **seed** query sets for `scripts/eval-retrieval`. Each file
has about five queries in each class (`exact_id`, `paraphrase`, `cause`) against one
pinned Loghub 2k sample (HDFS, BGL, Thunderbird, OpenSSH, Linux).

A query sets exactly one of `relevant_lines` (1-based LineIds) or `relevant_events`
(Loghub EventIds from the matching `*_2k.log_structured.csv`). Exact-ID seeds use
`relevant_lines`. Paraphrase and cause seeds use `relevant_events`. The script
rejects both, neither, and the old `relevant` key. Each listed line must exist in
the log and contain the query as a whole token (`blk_123` does not match inside
`blk_1234`). The structured CSV row count must equal the log line count so LineId
matches nesso's 1-based lines, including a final line with no trailing newline.

The relevant set R is those lines, or every line whose EventId is listed. Scores
use binary gains on lines:

- recall@10 = |top 10 ∩ R| / min(10, |R|)
- MRR = 1 / rank of the first line in R (0 if none)
- nDCG@10 = DCG@10 / IDCG of min(10, |R|) relevant lines

These seed sets are for local development and CI-free smoke scoring. They are
**not** the published retrieval benchmark; the full public set comes later from
Product.

```bash
./scripts/fetch-loghub --dataset all-2k
python3 scripts/eval-retrieval --dataset all-2k --out build/eval/all_2k.json
```

Keyword baselines (`--engine grep`, `ripgrep`, `bm25`), the optional DIY semantic baseline (`--engine diy-faiss`), and the optional XERJ baselines (`--engine xerj`, `xerj-neural`) reuse these seeds and the same line metrics. See [docs/benchmarks.md](../../docs/benchmarks.md) for setup, fairness notes, and query rules. Those paths are local or per-release, not Pipeline or nightly.
