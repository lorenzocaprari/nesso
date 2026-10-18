## Summary

## Tests

- [ ] `bash scripts/lint`
- [ ] ctest label that this change affects (`unit`, `integration`, or `functional`; `bdd` is the godog suite)
- [ ] `./scripts/fuzz-test` if parser or tokenizer input handling changed

## Checklist

- [ ] Title is `type(scope): subject` (`feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`; `!` before `:` for a breaking change)
- [ ] User-facing changes are under `## [Unreleased]` in `CHANGELOG.md`
- [ ] Updated AGENTS.md if commands, profiles or gates changed
