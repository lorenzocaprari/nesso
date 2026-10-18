# Skill evals

10-20 prompts per skill, both `should_trigger` and should-not-trigger, 3-5 trials with the skill and without it, deterministic graders, pass rates. No model judge. No skill ships in this change. `evals/_example/` is fixture-only so the runner can be checked without a model.

Put a skill at `.agents/skills/<name>/SKILL.md`. The case directory name is the skill name. PyYAML is already in the build image (`.devcontainer/requirements.txt`).

## Cases

`evals/<skill>/cases.yaml`:

```yaml
schema_version: 1
skill: my-skill
cases:
  - id: happy-path
    prompt: "..."
    should_trigger: true
    trials: 3
    graders:
      - type: regex
        target: output
        pattern: "expected text"
      - type: regex
        target: transcript
        pattern: "SKILL\\.md"
      - type: files_changed
        paths: ["relative/file.txt"]
      - type: commands_run
        pattern: "ctest"
  - id: unrelated
    prompt: "..."
    should_trigger: false
    graders: []
```

`trials` is optional and defaults to 3 (allowed 3-5). `--trials` overrides every case on `--run`. Graders on one case are ANDed. An empty list only checks the trigger. A real skill needs 10-20 cases. `_example` is the only smaller set. When `.agents/skills/<name>/SKILL.md` exists, `evals/<name>/cases.yaml` must exist too.

| Type | Passes when |
| --- | --- |
| `regex` | `pattern` matches `output` (final `result` text) or `transcript` (assistant text plus each `tool_call` JSON). The user prompt is excluded. `re.search`; `.` does not match newlines unless the pattern sets `(?s)`. |
| `files_changed` | Every `paths` entry matches `args.path` or `result.success.path` on a completed `writeToolCall`, `editToolCall`, or `deleteToolCall`. Match is on path components, so `dir/file.txt` matches `/work/dir/file.txt`. |
| `commands_run` | `pattern` matches a completed `shellToolCall.args.command`, or `tool_call.function` whose `name` is `shell` or `bash`. `subtype: started` is ignored. A missing subtype counts as completed. |

`should_trigger: true` passes when a tool call reads `skills/<skill>/SKILL.md`. Outcomes are the graders alone. Combined (with_skill) needs both.

## Fixtures

`evals/<skill>/fixtures/<case-id>/` holds `pass*.jsonl` and `fail*.jsonl`, at least one of each. Each line is one JSON object. `pass` files must grade as a pass and `fail` files as a fail. `--dry-run` grades them and does not call a model.

## Run

```bash
python3 scripts/eval-skills --dry-run
python3 scripts/eval-skills --run
python3 scripts/eval-skills --run --skill my-skill --trials 5 --model <model>
```

`--dry-run` (alias `--self-test`) checks the schema, grades fixtures, and checks that each real skill has a case file. `--run` is local, needs `CURSOR_API_KEY`, and does not run in CI. `_example` is skipped. A missing `SKILL.md` for any other set is an error. Each trial is a detached worktree of `HEAD`. `without_skill` deletes that skill directory inside the worktree. Exit 0 when every trial returns a transcript, including grader failures. Exit 1 when the CLI is missing, the key is unset, a trial times out (default 900s, `--timeout`), or the output is not `stream-json`. Transcripts are left in `--save-dir`.

`--sandbox enabled` is the default, so graded shell and write calls stay inside the trial worktree. Warning: `--unsafe-no-sandbox` passes `--sandbox disabled` and turns the command sandbox off for every trial.

`--trust` only skips the headless workspace-trust prompt.

`--force` applies file edits in print mode and allows commands unless they are explicitly denied. It is separate from `--sandbox`.

Only `--backend cursor` is implemented. `.github/workflows/skill-evals.yml` runs `--dry-run` when `evals/**`, `.agents/skills/**`, `scripts/eval-skills`, or that workflow changes. The job has `contents: read` and no secrets. It is not part of the Pipeline merge gate.
