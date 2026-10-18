# Architecture Decision Records

An architecture decision record (ADR) documents one significant design
choice for Nesso: the problem, the options considered, and the outcome.
Write one when the choice is hard to reverse or hard to recover from the
code, such as storage layout, ranking, the embedding pipeline, the public
command-line interface, or the build. Skip one for a bug fix, a local
refactor, or a dependency bump that leaves those choices unchanged.

Records in this directory use [MADR](https://adr.github.io/madr/) 4.0.0,
the latest MADR release (published 2024-09-17). Copy
[`template.md`](template.md) to a new file named `NNNN-kebab-title.md`:

* `NNNN` is a four-digit, zero-padded number. `0000` is the first record.
* The rest is a lowercase title with hyphens between words.
* Do not renumber a record or reuse a number.

`template.md` is the MADR 4.0.0 full template and is not a record. Remove a
section from a record only when the template marks that section optional.
The status placeholder is closed so a copy is valid YAML; the comment in
`template.md` notes how that differs from upstream.

Set `status` in the YAML front matter to one of:

* `proposed`: under discussion
* `accepted`: the current decision
* `rejected`: considered and declined
* `deprecated`: no longer followed, with no replacement
* `superseded by NNNN`: replaced by the record with that number

Quote a status that contains spaces. Add a row to the index when you add a
record.

## Index

| Number | Title | Status | Date |
| ------ | ----- | ------ | ---- |
| 0000 | Use MADR for architecture decisions | accepted | 2026-10-18 |
| 0001 | Exact brute-force search without an ANN index | accepted | 2026-10-07 |
| 0002 | Run embeddings on CPU only | accepted | 2026-10-11 |
| 0003 | Byte-identical determinism | accepted | 2026-10-18 |
| 0004 | Stream parse, embed, and score in fixed-size batches | accepted | 2026-10-13 |
