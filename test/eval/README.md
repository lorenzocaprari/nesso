# Retrieval evaluation seeds

`queries/hdfs_2k.jsonl` is a **seed** query set for `scripts/eval-retrieval`. It has
about five queries in each class (`exact_id`, `paraphrase`, `cause`) against the
pinned Loghub HDFS_2k sample.

A query sets exactly one of `relevant_lines` (1-based LineIds) or `relevant_events`
(Loghub EventIds from `HDFS_2k.log_structured.csv`). Exact-ID seeds use
`relevant_lines`. Paraphrase and cause seeds use `relevant_events`. The script
rejects both, neither, and the old `relevant` key. Each listed line must exist in
the log and contain the query as a whole token (`blk_123` does not match inside
`blk_1234`).

The relevant set R is those lines, or every line whose EventId is listed. Scores
use binary gains on lines:

- recall@10 = |top 10 ∩ R| / min(10, |R|)
- MRR = 1 / rank of the first line in R (0 if none)
- nDCG@10 = DCG@10 / IDCG of min(10, |R|) relevant lines

This seed set is for local development and CI-free smoke scoring. It is **not**
the published retrieval benchmark; the full public set comes later from Product.

```bash
./scripts/fetch-loghub
python3 scripts/eval-retrieval --out build/eval/hdfs_2k.json
```
