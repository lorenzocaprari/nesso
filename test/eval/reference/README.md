# Competitor reference reports

`competitors-2k.json` and `competitors-needle.json` (when present) are the merged
reports of the `eval-competitors` workflow: diy-faiss, OpenSearch, ck, vecgrep, and
xerj, scored on the Loghub 2k seed sets and on `bgl-needle-100k`. The `eval-retrieval`
workflow reads every `*.json` here with `scripts/eval-retrieval-compare --margins
--reference`, so its job summary can show nesso's margin over the best other engine
without running the heavy engines every night. A reference whose report kind (2k
engine report or needle) does not match the current report is ignored.

They are scores, not timings: a reference is machine-independent, so it stays valid
until the query sets, the labels, or a competitor version change.

Refresh them by hand when that happens:

1. Run **Eval competitors** from nesso-bench's Actions (`workflow_dispatch`) and wait for the
   `merge` job.
2. Download the `eval-competitors` artifact.
3. Copy `competitors-2k.json` and `competitors-needle.json` here and commit them with the
   competitor versions (see `docs/benchmarks.md`) in the message.

The reports are deterministic JSON with sorted keys, so a refresh shows as a real diff.
