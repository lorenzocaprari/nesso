---
status: "accepted"
date: "2026-10-18"
decision-makers: Lorenzo Caprari
---

# Use MADR for architecture decisions

## Context and Problem Statement

Nesso has no recorded architecture decisions. Choices such as corpus
layout, exact ranking, and the embedding pipeline are visible in the code,
but not why they were taken. Which format should new records use?

## Decision Drivers

* A record is a Markdown file in the repository, with no extra tooling.
* The format names the options that were rejected.
* The template is a published, versioned document.
* A record covers one decision and stays short.

## Considered Options

* MADR
* Nygard-style
* No ADRs

## Decision Outcome

Chosen option: "MADR", because the 4.0.0 template already separates
context, options, and consequences, and 4.0.0 is the current release.

### Consequences

* Good, because a later change can see the rejected options in the same file.
* Good, because each record is plain Markdown under `docs/adr/`.
* Bad, because MADR suggests `docs/decisions`, and this repository uses
  `docs/adr/` instead. MADR allows another directory pattern.
* Bad, because an existing Nygard-style draft has to be rewritten before
  it can be merged.

### Confirmation

A pull request that adds a decision copies `docs/adr/template.md`, sets
`status`, uses the filename `NNNN-kebab-title.md`, and adds a row to the
index in `docs/adr/README.md`.

## Pros and Cons of the Options

### MADR

[MADR](https://adr.github.io/madr/) 4.0.0 (Markdown Architectural Decision
Records). The full template is
<https://github.com/adr/madr/blob/4.0.0/template/adr-template.md>.

* Good, because context, drivers, options, outcome, consequences, and
  pros and cons are separate sections.
* Good, because 4.0.0 is the latest release (2024-09-17). No later release
  is published.
* Neutral, because several sections are marked optional and can be removed
  when they add nothing.
* Bad, because the headings are stricter than a short free-form note.

### Nygard-style

Michael Nygard's record: title, status, context, decision, and
consequences. See
<http://thinkrelevance.com/blog/2011/11/15/documenting-architecture-decisions>.

* Good, because the template is short and widely known.
* Bad, because considered options are not a section, so rejected
  alternatives are easy to leave out.
* Bad, because this repository would be choosing its own heading names.

### No ADRs

* Good, because there is no extra document to update.
* Bad, because the reason for a design choice stays in commit messages and
  review threads.
* Bad, because a later reader cannot tell a deliberate choice from an
  accident of the first implementation.

## More Information

Template version: MADR 4.0.0.
Homepage: <https://adr.github.io/madr/>.
The template is dual-licensed MIT OR CC0-1.0.

An earlier Nygard-style draft ADR exists on the unmerged branch
`chore/repo-hygiene-adr` at `docs/adr/0001-exact-search-no-ann.md` (exact
brute-force ranking, no approximate nearest-neighbor index). That draft
will be ported to MADR in a separate pull request. It is not included here.
