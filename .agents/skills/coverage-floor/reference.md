# Coverage floor reference

`scripts/code-coverage` is the procedure. Read the floors from that file. This note only names outputs and how a floor miss differs from an lcov capture failure.

## Outputs

The script `cd`s to the repository root derived from its own path. The first argument is the Conan profile; the default is `conan/profiles/code-coverage`.

| Path | Written by |
| --- | --- |
| `build/coverage/coverage.info` | `lcov --capture` |
| `build/coverage/coverage.cleaned` | `lcov --remove` |
| `build/coverage/report/` | `genhtml` |
| `build/coverage/report/summary.txt` | the three `check` rows |

Each summary row is `<metric>: <measured>% (required >= <floor>%)`, where metric is `lines`, `functions`, or `branches`. The script writes `summary.txt` after `check`, including when a floor fails, then exits 2. It does not write that file if `lcov` or `genhtml` exits first. When `GITHUB_STEP_SUMMARY` is set, the same rows are appended there.

The Pipeline job is `coverage` in `.github/workflows/ci.yml`. It runs `./scripts/code-coverage conan/profiles/code-coverage` and uploads `build/coverage/report` as `coverage-report`. When `CI=true`, the script adds `--no-remote` and exports `NESSO_REQUIRE_MODEL=1`. That job also sets `NESSO_REQUIRE_BDD=1`.

## Floor miss

`check` compares each measured percent to the matching `REQUIRED_*` value. A miss prints `ERROR: <metric> <measured>% < <required>%` on stderr and the script exits 2. `Coverage OK` is printed only after all three pass.

## `negative taken count`

That lcov text is a capture problem. The script's `lcov --capture` and the following `genhtml` both run before `check`. If either exits, the floor comparison has not run. The script's `--ignore-errors` lists are in `scripts/code-coverage` (`inconsistent`, `mismatch`, `unused` on capture; the summary call also ignores `corrupt`). Do not add this error to that list and do not lower `REQUIRED_*`.

## Tests

Add tests under `test/unit`, `test/integration`, or `test/functional`. Copy the `LABELS` property from the `CMakeLists.txt` next to the test: `unit`, `integration`, or `functional`. The godog test is named `bdd`; its label is `functional`.

The coverage script runs this, with no `-L` filter:

```bash
ctest --test-dir build/coverage/build/Debug --output-on-failure
```

Paths on the script's `lcov --remove` list are outside the percentage. Some of those tests are also compiled with `-fno-profile-arcs` (see the comments in `test/`). Read the remove list and those comments before extending one of those files. Library objects they call are still measured unless that `.cpp` is removed too.
