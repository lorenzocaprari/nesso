---
status: "proposed"
date: "2026-11-09"
decision-makers: Lorenzo Caprari
---

# Hybrid dense and BM25 search with query-class routing

## Context and Problem Statement

`grep` and `search` embed the query and rank by dot product ([ADR 0001](0001-exact-brute-force-search.md)).
Since [#185](https://github.com/lorenzocaprari/nesso/pull/185), each distinct text is embedded once, so lines that share a text share a score.
`index` writes corpus format v3 ([ADR 0010](0010-corpus-format-v3.md)): one vector per distinct text, line records, a policy block, and (since [#212](https://github.com/lorenzocaprari/nesso/pull/212)) per-line lexical postings.
The lexical term rules and Okapi BM25 scorer are on `main` ([#191](https://github.com/lorenzocaprari/nesso/pull/191)); there is still no `--mode`, no hybrid fusion, and no query routing.
JSON `schema_version` is 1, with `score` and `occurrences`, and the order is score descending, then ascending input ordinal ([ADR 0009](0009-command-line-contract-and-json-lines.md)).
A query such as `blk_-1608999687919862906` or `0a000086` is embedded as WordPiece fragments, so the line that contains it is often not ranked first.
Measured on an internal evaluation set, hybrid improved ranking quality over dense-only, and on exact-identifier queries BM25 clearly outperformed both dense and hybrid.
A later internal evaluation showed that a fixed fusion weight that helps keyword-style natural-language queries can hurt paraphrase-style ones, and that those two kinds of query cannot be told apart from the query string alone.
Numbers on a public set will be added once the public evaluation harness exists.
How should lexical scoring be added, combined, routed, and stored?

## Decision Drivers

* Exact identifiers are found first. Natural-language queries keep dense quality.
* Byte-identical results for any `--threads` value ([ADR 0003](0003-byte-identical-determinism.md)).
* Users re-index once, and the change follows the CLI contract.
* Routing uses deterministic rules only: no model, no randomness, and no locale dependence.

## Considered Options

* Dense only (today)
* BM25 only
* Reciprocal rank fusion (RRF)
* z-score weighted fusion
* z-score weighted fusion with identifier-token routing
* z-score weighted fusion with query-class routing

## Decision Outcome

Chosen option: "z-score weighted fusion with query-class routing", because fusion keeps dense quality on prose, and routing sends pure identifiers to the scorer that finds them.

* **Lexical terms.** ASCII is lowercased. Runs of `[a-z0-9]` and bytes ≥ 0x80 form terms, and camelCase boundaries split them. Each run joined by `-`, `_`, `.`, or `:` is also kept as one compound term, so `blk_-123` and `10.0.0.1` match whole. The term rules have a version.
* **BM25.** Okapi BM25 per line, not per distinct text. Identifiers can differ between lines that share one embedding, so a future dedup that masks variable tokens relies on the lexical scorer.
  * `k1 = 1.5`, `b = 0.75`, and `idf = ln(1 + (N − df + 0.5) / (df + 0.5))`.
  * The sum runs in double, over the distinct query terms in byte order.
* **Fusion.** For hybrid scoring, `score = 0.8·z(dense) + 0.2·z(bm25)`.
  * `z` uses the mean and population standard deviation over all candidate lines, and a zero deviation gives 0.
  * The sums are taken in double, in fixed blocks of 4096 lines in ordinal order, so they never depend on the thread count.
  * Those weights are fixed constants. Forced `--mode hybrid` always uses them.
* **Query classes.** In the default mode `auto`, the query is classified with deterministic ASCII rules, then scored by class.
  * If the whole query is wrapped in one pair of ASCII `"…"`, that pair is stripped once. The remainder is split on ASCII whitespace (`0x09`, `0x0A`, `0x0D`, `0x20`), empty tokens are dropped, and each token may strip one surrounding `"…"` pair.
  * An **identifier token** is a token that fully matches one of these patterns: a UUID (8-4-4-4-12 hex); `(0x)?[0-9a-fA-F]{8,}` containing a digit; `[0-9]{4,}`; `[A-Za-z]{2,10}[-_]-?[A-Za-z]?[0-9]{3,}` (for example `blk_-123`, `ERR-5001`); a dotted-quad IPv4 with an optional port, or an IPv6 address checked by a parser; an absolute path of two or more segments.
  * **identifier:** exactly one token, and it is an identifier token → lexical mode.
  * **mixed:** two or more tokens, and at least one is an identifier token → hybrid with the fusion weights above.
  * **natural language:** every other non-empty query → hybrid with the same fusion weights.
  * Examples: `blk_-1608999687919862906` and `"0a000086"` are identifier; `timeout` and `auth failure` are natural language; `connection failed with ERR-5001` and `error "ERR-5001" today` are mixed.
* **Lexical mode.** Lines containing the query as an exact, case-sensitive substring rank first, ordered by BM25. Other BM25 hits follow. With no lexical hit, nesso falls back to hybrid and says so on stderr.
* **Modes.** `--mode auto|hybrid|dense|lexical` on `grep` and `search`. `dense` reproduces today's ranking. Class routing applies only under `auto`.
* **Ties.** Score descending, then ascending line ordinal, in every mode.
* **JSON.**
  * Rows gain `mode` (the effective mode after routing), plus `dense` and `lexical`: the raw dense dot product and the BM25 score, `null` when not computed.
  * `score` becomes the score of the effective mode. That changes its meaning, which ADR 0009 counts as breaking, so `schema_version` becomes 2 in the same minor release.
  * Text keeps its `{:.4f}` line shape and prints that score.
* **Storage.** Per-line postings live in the corpus v3 lexical section (delta plus varint). New writers always emit that section and record `lexical.terms` and `bm25` in the policy block ([#212](https://github.com/lorenzocaprari/nesso/pull/212)). A different term version exits 2 for lexical modes, while `dense` keeps working. Pre-postings v3 files with an empty lexical section stay readable for dense search; lexical modes on a corpus without postings exit 2 with "re-run `nesso index`".

### Consequences

* Good, because identifiers rank first without the user choosing a mode, and `--mode dense` keeps today's behaviour.
* Bad, because `grep` keeps every line's terms and scores in memory, and corpora grow by the postings.
* Bad, because the default ranking and the JSON `score` meaning change once (a minor release with `schema_version` 2).
* Bad, because natural-language keyword and paraphrase queries share one class and one fusion weight; paraphrase-specific weighting is deferred until a search-time signal is validated on a larger internal evaluation.

### Confirmation

* Unit tests cover the term rules, BM25 against hand-computed values, z-score edge cases, every identifier pattern, and every query class, both matches and near-misses.
* `scripts/check-determinism` compares `grep` and `search` stdout for the nesso flags `--mode auto` and `--mode lexical` across thread counts. The script's own `--mode` stays `full` or `pr`.
* BDD covers each mode, each query class, an identifier ranked first, and the fallback message.
* The C2c paraphrase check keeps validating the 0.8/0.2 hybrid weights.

## Pros and Cons of the Options

* Dense only: good, unchanged; bad, identifiers rank poorly.
* BM25 only: good, exact identifiers; bad, paraphrases fail.
* RRF: good, no score scaling; bad, it ignores score gaps between ranks.
* z-score fusion: good, the best single blend; bad, identifiers are still diluted by dense scores.
* z-score fusion with identifier-token routing: good, pure identifiers go to lexical; bad, a single fusion weight still covers every other query.
* z-score fusion with query-class routing: good, mixed queries stay hybrid while pure identifiers stay lexical; bad, keyword and paraphrase natural-language queries still share one weight until a search-time signal exists.

## More Information

Amended 2026-11-09: routing classifies the query as identifier, mixed, or natural language. Pure identifiers still go to lexical mode. Mixed and natural-language queries, and forced `--mode hybrid`, keep `0.8·z(dense) + 0.2·z(bm25)`. Storage facts were refreshed for corpus v3 postings on `main`.
