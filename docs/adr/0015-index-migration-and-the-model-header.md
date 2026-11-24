---
status: "accepted"
date: "2026-11-23"
decision-makers: Lorenzo Caprari
---


# Index migration and the model header

## Context and Problem Statement

`index` writes corpus format v3 ([ADR 0010](0010-corpus-format-v3.md)), and the version is `CORPUS_VERSION = 3` (`src/core/include/core/corpus_index.hpp:27`). The fixed header is 128 bytes (`HEADER_BYTES`, `src/core/corpus_detail.hpp:23`). It holds the magic, the version, the policy size, the dimension count, the counts and section offsets, and, at bytes 112 to 127, the file checksum (`src/core/corpus_writer.cpp:568-576`, `src/core/include/core/corpus_index.hpp:29-30`). No byte of it is free.

The model's identity is already recorded in the policy block, not in the fixed header. `MakeCorpusPolicy` writes `model.id`, `model.sha256`, `model.quantization`, `vocab.sha256` and `dimensions`, next to `batch`, `bm25`, `dedup`, `lexical.terms`, `mask`, `tokens.max` and `truncate` (`src/nesso/nesso.cpp:268-282`). `ValidatePolicy` requires ten of those keys and accepts others (`src/core/corpus_policy.cpp:53-89`). `search` refuses a corpus when one of the five identity keys differs (`RefuseModelMismatch`, `src/nesso/nesso_search.cpp:612-625`). It does that after the model loads (`src/nesso/nesso_search.cpp:836-857`), with one message for every key: "the corpus was built with a different model; re-run nesso index" (`src/nesso/nesso_errors.cpp:126-127`). The batch, truncation, dedup, mask and token policies only warn (`src/nesso/nesso_search.cpp:858-861`).

Three gaps remain:

* **No prompt-prefix version.** The embedder adds no prefix to a query or a passage today (`MINILM_MODEL_ID`, `src/embed/include/embed/embed_types.hpp:27`). A model that needs one, such as a `query:` and `passage:` pair, gives vectors that are comparable only when both sides use the same prefix rule. Nothing in the corpus records that rule.
* **No migration path.** A version other than 3 exits 2 with "corpus format vN is not supported; re-run nesso index" (`src/core/corpus_reader.cpp:164-170`, `src/nesso/nesso_errors.cpp:168-173`). The next change to how vectors are stored will force every user to re-index.
* **Caps written before index memory was bounded.** The reader and the writer share `MAX_SOURCES` 1,000,000, `MAX_LINES` 10,000,000, `MAX_DISTINCT` 1,000,000, `MAX_LEXICAL_TERMS` 4,000,000 and `MAX_LEXICAL_BYTES` 512 MiB (`src/core/corpus_detail.hpp:29-33`). A corpus over any of them is reported as "the file is corrupt" (`CorpusLimitsExceeded`, `src/core/corpus_reader.cpp:189-194`). The writer rejects the next source, distinct text, or line with the same error (`src/core/corpus_writer.cpp:310`, `323`, `488`). Header counts are 64-bit (`src/core/corpus_reader.cpp:140-147`), but per-line source and distinct ids are 32-bit (`src/core/corpus_writer.cpp:619-621`; `NO_DISTINCT`, `src/core/include/core/corpus_index.hpp:28`).

How should the corpus record the model contract, handle corpora written by an older nesso, and set its limits?

## Decision Drivers

* `search` refuses vectors that are not comparable with its query embedding, and says which field differs.
* The same binary, model, input, and policy give the same corpus bytes ([ADR 0003](0003-byte-identical-determinism.md)). A migrated corpus must equal a fresh `index` of the same input, or say that it does not.
* Users re-index as rarely as possible, and format changes that need a re-index ship together.
* `search` does not write the user's files as a side effect ([ADR 0005](0005-offline-local-data.md)). Exit codes and stdout stay stable ([ADR 0009](0009-command-line-contract-and-json-lines.md)).
* A cap is a safety limit against a hostile or broken file, not a promise of capacity. A raised cap must fit the memory `search` and `index` actually use.

## Considered Options

**A. Where the model contract lives**

* A1. Policy block keys: keep the existing ones and add one key for the prompt-prefix rule
* A2. A larger fixed header with typed model fields
* A3. A new model section between the header and the policy block

**B. When the version changes**

* B1. No bump now: add the key inside v3 and bump to v4 once, together with the next change to how vectors are stored
* B2. Bump to v4 now for the header, and again for the vector change
* B3. Bump to v4 now and also change how vectors are stored in the same release

**C. Corpora written by an older nesso**

* C1. Refuse and ask for `nesso index` (today)
* C2. Upgrade on open: `search` rewrites the file to the current version
* C3. An explicit `nesso migrate IN [OUT]` that writes a new file atomically
* C4. Read older versions in place, with no rewrite

**D. The mismatch error**

* D1. Keep today's single message
* D2. Name the first differing key and both values, exit 2 as today
* D3. As D2, and check the keys that do not need the model files before the model loads

**E. Caps**

* E1. Keep the caps
* E2. Raise them now
* E3. Raise them per cap, each with a measured peak RSS at the new limit; leave the ones that `search` memory cannot support yet
* E4. Drop the count caps; keep only overflow and file-size checks

## Decision Outcome

Chosen: A1, B1, C4 with C3 for the next version, D2 and E3.

* **A: chosen A1.** The five identity keys already work, and `ValidatePolicy` already accepts extra keys, the way `bm25` and `lexical.terms` were added (`src/core/corpus_policy.cpp:91-103`). Add `embed.prefix`, whose value names the prefix rule and its version, `none` for MiniLM. It becomes the sixth identity key in `RefuseModelMismatch`, so a different value exits 2. A corpus without the key reads as `none`, which is what every corpus written so far used.
* **B: chosen B1.** Version 3 has already grown without a bump: the lexical section was added later, and v3 files without it stay readable for dense search (`src/core/corpus_reader.cpp:492-494`). One bump, when the stored vector encoding changes, keeps users to one re-index or migration.
* **C: chosen C4 for v3 corpora that lack the new key, and C3 for the next version bump.** C1 stays for v2 and earlier ([ADR 0010](0010-corpus-format-v3.md)). `nesso migrate` reads a v3 corpus and writes the next version to a new file, then renames it into place. It refuses and names the reason when its output could not equal a fresh `index` of the same input. One example is an encoding that needs the model and not just the stored vectors.
* **D: chosen D2.** The exit code and the "re-run nesso index" advice stay. The stderr line names the key and both values, for example `model.id` with the stored and the loaded id. D3 saves the model load only on a mismatch, and splits one check in two.
* **E: chosen E3.** Index memory at `Finish` is bounded by `CORPUS_WRITER_MEMORY_BUDGET_BYTES` (1.5 GiB, `src/core/corpus_detail.hpp:34-35`). `index` still keeps every distinct text in memory (`distinct_texts`, `src/core/corpus_writer.cpp:221`, `323-335`), and `search` memory grows with the line count (see More Information). So raise `MAX_DISTINCT` and `MAX_SOURCES` only with a measured `index` peak at the new limit. Raise `MAX_LINES`, `MAX_LEXICAL_TERMS` and `MAX_LEXICAL_BYTES` only after `search` stops building per-line arrays at open. Keep the 32-bit id ceiling. Report a corpus over this build's caps with its own message ("the corpus exceeds this nesso's limits"), not as a corrupt file, so an older nesso does not call a newer, larger corpus corrupt.

### Consequences

* Good, because existing v3 corpora keep working, and only a model that changes the prefix rule forces a re-index.
* Good, because the one future format bump comes with a migration command instead of a forced re-index.
* Good, because a mismatch names the field, so a user can tell a model change from a quantization change.
* Bad, because the contract stays split between the typed header (dimensions) and text keys (everything else), and `ValidatePolicy` must keep the two in agreement (`src/core/corpus_reader.cpp:504-507`).
* Bad, because `nesso migrate` is a new command to document, test and keep within the ADR 0009 contract.
* Bad, because the higher limits wait for `search` memory work. Until then, `MAX_LINES` stays 10,000,000.

### Confirmation

* Unit tests: a corpus without `embed.prefix` opens as `none`. A different `embed.prefix` exits 2 and names the key. A corpus over a cap reports the limits message. Round-trip tests at each new cap value.
* Corpus fuzz seeds at and just past each cap, including 32-bit id overflow.
* BDD: the mismatch message for each identity key. `nesso migrate` output equals a fresh `index`, checked by `scripts/check-determinism`.
* A raised cap lands only with a peak-RSS measurement of `index` and `search` at the new limit, recorded in the pull request.

## Pros and Cons of the Options

### A1. Policy block keys

* Good, because it needs no layout change, and `search` already compares five of these keys.
* Good, because readers that do not know the key ignore it.
* Bad, because the values are text, so each new key needs its own validation.

### A2. Larger fixed header

* Good, because typed fields are checked before the policy is parsed.
* Bad, because the header has no free bytes, and `policy_offset` must equal `HEADER_BYTES` (`src/core/corpus_reader.cpp:198`), so it is a version bump.
* Bad, because it duplicates keys the policy block already holds.

### A3. Model section

* Good, because the model contract is kept apart from the batch and text policy.
* Bad, because it is a new section to validate and fuzz, and a version bump, for data that already exists.

### B1. Bump once, later

* Good, because users rebuild or migrate once.
* Neutral, because the prefix key is optional until then.
* Bad, because v3 readers must handle files with and without the key until v4.

### B2. Bump now and again later

* Good, because each version has one meaning.
* Bad, because users rebuild or migrate twice, for a key that can be optional.

### B3. Bump now with the vector change

* Good, because there is one bump.
* Bad, because the header work then waits for the vector work, and it changes the size and quality of `search` results in the same release.

### C1. Refuse and re-index

* Good, because there is one reader and nothing new to test.
* Bad, because a large corpus costs its full embedding time again, and stdin or archive sources may no longer exist.

### C2. Upgrade on open

* Good, because it is invisible to the user.
* Bad, because `search` would write the user's file, which breaks a read-only command and a read-only mount, and it races with other readers of the mapped file.
* Bad, because a failure mid-write must not lose the old corpus.

### C3. `nesso migrate`

* Good, because the rewrite is explicit, atomic, and testable against a fresh `index`.
* Good, because it works for stdin and archive corpora whose sources are gone.
* Bad, because it is a new command and exit-code surface.

### C4. Read in place

* Good, because there is no rewrite.
* Bad, because each extra reader version must be tested and fuzzed. It is cheap only when the layout is unchanged, as with an optional key.

### D1. Single message

* Good, because nothing changes.
* Bad, because "a different model" also covers a quantization, vocabulary, dimension or prefix difference.

### D2. Name the key and values

* Good, because the user sees what to fix, with the same exit code.
* Neutral, because only stderr text changes.

### D3. Check before the model loads

* Good, because it fails faster.
* Bad, because the checksums need the model files hashed, so the check splits in two.

### E1. Keep caps

* Good, because there is no risk.
* Bad, because the limits stay at 10,000,000 lines and 1,000,000 distinct texts.

### E2. Raise now

* Good, because larger corpora work at once.
* Bad, because `search` memory grows with lines and postings, so a raised cap can promise what the RSS cannot deliver.

### E3. Raise per cap, measured

* Good, because each limit matches measured memory.
* Bad, because the line and lexical caps wait for `search` memory work.

### E4. Drop count caps

* Good, because the format, not a constant, sets the limits.
* Bad, because a hostile header could ask for huge allocations before its sections are checked.

## More Information

Does raising the caps need `search` memory work? The code says yes for the line and lexical caps:

* `MapCorpusFile` maps the file and the vector matrix stays in the mapping. It also builds a `std::vector<CorpusLine>` with one entry per line (`src/core/corpus_reader.cpp:81`, `403`). On x86-64 that is about 64 bytes per line [estimated, from the struct layout in `src/core/include/core/corpus_index.hpp`], or about 640 MB at 10,000,000 lines.
* It decodes the lexical section into one `LexicalLinePostings` per line, each with its own heap vector, plus a copy of each term (`src/core/corpus_reader.cpp:84-86`, `526-532`).
* It checks distinct texts through an `unordered_set` of views (`LoadMappedDistinctTexts`, `src/core/corpus_reader.cpp:303-335`). `search` also keeps a per-distinct counter (`TallySearchLineMeta`, `src/nesso/nesso_search.cpp:634-657`).
* `grep` builds no corpus, so the corpus caps do not apply to it. In hybrid and lexical mode it keeps every line's text in memory until it ranks (`all_lines`, `src/nesso/nesso_grep.cpp:127`, `266-268`).

`index` at `Finish` spills its terms with bounded runs, and its peak RSS no longer grows with input size (CHANGELOG, `CORPUS_WRITER_MEMORY_BUDGET_BYTES`). The input measured there has 2,000 distinct texts, so it does not test the distinct-text set.

ADR 0010 says the checksum is checked "before any other field is read". The reader checks the magic and the version first, then the checksum (`src/core/corpus_reader.cpp:162-180`). That order is what lets a v2 or v4 file get the version message rather than "corrupt". This record keeps that order.

[ADR 0017](0017-embed-the-log-message-without-its-header.md) adds a second optional key the same way, `embed.text`. A corpus without it reads as `line`. It is not an identity key: a different value warns and `search` continues.

This record amends [ADR 0010](0010-corpus-format-v3.md). Revisit it when the stored vector encoding changes, because that change carries the version bump and the first use of `nesso migrate`.
