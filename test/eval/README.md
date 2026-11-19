# Retrieval evaluation seeds

`queries/hdfs_2k.jsonl` is a **seed** query set for `scripts/eval-retrieval`. It has
about five queries in each class (`exact_id`, `paraphrase`, `cause`) against the
pinned Loghub HDFS_2k sample. Relevance labels are Loghub `EventId` values from
`HDFS_2k.log_structured.csv`, not line numbers.

This seed set is for local development and CI-free smoke scoring. It is **not**
the published retrieval benchmark; the full public set comes later from Product.

```bash
./scripts/fetch-loghub
python3 scripts/eval-retrieval --out build/eval/hdfs_2k.json
```
