---
status: "proposed"
date: "2026-10-28"
decision-makers: Lorenzo Caprari
---

# Hybrid dense and BM25 search with identifier routing

## Context and Problem Statement

`grep` and `search` embed the query and rank by dot product ([ADR 0001](0001-exact-brute-force-search.md)).
Since [#185](https://github.com/lorenzocaprari/nesso/pull/185), each distinct text is embedded once, so lines that share a text share a score, and `index` still writes corpus v2.
JSON `schema_version` is 1, with `score` and `occurrences`, and the order is score descending, then ascending input ordinal ([ADR 0009](0009-command-line-contract-and-json-lines.md)).
There is no lexical index, no identifier handling, and no `--mode`, and corpus v3 ([ADR 0010](0010-corpus-format-v3.md)) is proposed and unreleased.
A query such as `blk_-1608999687919862906` or `0a000086` is embedded as WordPiece fragments, so the line that contains it is often not ranked first.
Measured on an internal evaluation set, hybrid improved ranking quality over dense-only, and on exact-identifier queries BM25 clearly outperformed both dense and hybrid.
Numbers on a public set will be added once the public evaluation harness exists.
How should lexical scoring be added, combined, and stored?

## Decision Drivers

* Exact identifiers are found first. Natural-language queries keep dense quality.
* Byte-identical results for any `--threads` value ([ADR 0003](0003-byte-identical-determinism.md)).
* Users re-index once, and the change follows the CLI contract.

## Considered Options

* Dense only (today)
* BM25 only
* Reciprocal rank fusion (RRF)
* z-score weighted fusion
* z-score weighted fusion with identifier routing

## Decision Outcome

Chosen option: "z-score weighted fusion with identifier routing", because fusion keeps dense quality on prose, and routing sends identifiers to the scorer that finds them.

* **Lexical terms.** ASCII is lowercased. Runs of `[a-z0-9]` and bytes ≥ 0x80 form terms, and camelCase boundaries split them. Each run joined by `-`, `_`, `.`, or `:` is also kept as one compound term, so `blk_-123` and `10.0.0.1` match whole. The term rules have a version.
* **BM25.** Okapi BM25 per line, not per distinct text. Identifiers can differ between lines that share one embedding, so a future dedup that masks variable tokens relies on the lexical scorer.
  * `k1 = 1.5`, `b = 0.75`, and `idf = ln(1 + (N − df + 0.5) / (df + 0.5))`.
  * The sum runs in double, over the distinct query terms in byte order.
* **Fusion.** `score = 0.8·z(dense) + 0.2·z(bm25)`.
  * `z` uses the mean and population standard deviation over all candidate lines, and a zero deviation gives 0.
  * The sums are taken in double, in fixed blocks of 4096 lines in ordinal order, so they never depend on the thread count.
* **Routing.** In the default mode `auto`, a query that is a single whitespace-free token, fully matching one of these patterns, goes to lexical mode. Every other query uses hybrid.
  * a UUID (8-4-4-4-12 hex);
  * `(0x)?[0-9a-fA-F]{8,}` containing a digit;
  * `[0-9]{4,}`;
  * `[A-Za-z]{2,10}[-_]-?[A-Za-z]?[0-9]{3,}` (for example `blk_-123`, `ERR-5001`);
  * a dotted-quad IPv4 with an optional port, or an IPv6 address checked by a parser;
  * an absolute path of two or more segments.
* **Lexical mode.** Lines containing the query as an exact, case-sensitive substring rank first, ordered by BM25. Other BM25 hits follow. With no lexical hit, nesso falls back to hybrid and says so on stderr.
* **Modes.** `--mode auto|hybrid|dense|lexical` on `grep` and `search`. `dense` reproduces today's ranking.
* **Ties.** Score descending, then ascending line ordinal, in every mode.
* **JSON.**
  * Rows gain `mode` (the effective mode after routing), plus `dense` and `lexical`: the raw dense dot product and the BM25 score, `null` when not computed.
  * `score` becomes the score of the effective mode. That changes its meaning, which ADR 0009 counts as breaking, so `schema_version` becomes 2 in the same minor release.
  * Text keeps its `{:.4f}` line shape and prints that score.
* **Storage.** Per-line postings live in a section of the still-unreleased corpus v3 (delta plus varint). `lexical.terms` and `bm25` are recorded in the policy block. A different term version exits 2 for lexical modes, while `dense` keeps working.
  * Preferred: the lexical section ships inside the first released v3, so there is one re-index and one breaking minor.
  * If a release with v3 ships first, the section becomes optional and needs a section directory added to ADR 0010 before that release. Then lexical modes on a corpus without it exit 2 with "re-run `nesso index`".

### Consequences

* Good, because identifiers rank first without the user choosing a mode, and `--mode dense` keeps today's behaviour.
* Bad, because `grep` keeps every line's terms and scores in memory, and corpora grow by the postings.
* Bad, because the default ranking and the JSON `score` meaning change once (a minor release with `schema_version` 2).

### Confirmation

* Unit tests cover the term rules, BM25 against hand-computed values, z-score edge cases, and every routing pattern, both matches and near-misses.
* `scripts/check-determinism` compares `grep` and `search` stdout for the nesso flags `--mode auto` and `--mode lexical` across thread counts. The script's own `--mode` stays `full` or `pr`.
* BDD covers each mode, an identifier ranked first, and the fallback message.

## Pros and Cons of the Options

* Dense only: good, unchanged; bad, identifiers rank poorly.
* BM25 only: good, exact identifiers; bad, paraphrases fail.
* RRF: good, no score scaling; bad, it ignores score gaps between ranks.
* z-score fusion: good, the best single blend; bad, identifiers are still diluted by dense scores.
* z-score fusion with routing: good, both strengths; bad, the patterns need care to avoid hijacking prose queries.
