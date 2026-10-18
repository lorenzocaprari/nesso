import pathlib
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
CHECK = ROOT / "scripts" / "check-agent-docs"


def write(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text).lstrip("\n"))


class CheckAgentDocsTest(unittest.TestCase):
    def run_check(self, root: pathlib.Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(CHECK), "--root", str(root)],
            capture_output=True,
            text=True,
            check=False,
        )

    def make_repo(self, root: pathlib.Path, agents: str, contributing: str | None = None) -> None:
        write(root / "AGENTS.md", agents)
        write(
            root / "CONTRIBUTING.md",
            contributing
            if contributing is not None
            else """\
            # Contributing

            See [the readme](README.md).
            """,
        )
        write(root / "README.md", "# nesso\n")
        write(root / "conan" / "profiles" / "gcc-26", "profile\n")
        write(root / "scripts" / "lint", "#!/usr/bin/env bash\n")
        write(
            root / "docs" / "adr" / "0001-example.md",
            """\
            ---
            status: "accepted"
            date: "2026-10-18"
            ---

            # Example
            """,
        )
        write(
            root / "docs" / "adr" / "README.md",
            """\
            # Architecture Decision Records

            ## Index

            | Number | Title | Status | Date |
            | ------ | ----- | ------ | ---- |
            | 0001 | Example | accepted | 2026-10-18 |
            """,
        )

    def test_accepts_a_repo_whose_docs_match_the_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(
                root,
                """\
                # AGENTS

                Profiles: `gcc-26`.

                ```bash
                conan install . -pr:h ./conan/profiles/gcc-26-debug
                ```

                See `scripts/lint` and `conan/profiles/gcc-26`.
                Generated: `build/not-real`, `CMakeUserPresets.json`, `models/`, `models-fp32/`,
                `determinism-input.log`, and `ghcr.io/<owner>/nesso-build-env:deps-<hash>`.
                There is no `devcontainer.json`.
                """,
            )
            write(root / "conan" / "profiles" / "gcc-26-debug", "profile\n")
            result = self.run_check(root)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("check-agent-docs: ok", result.stdout)

    def test_fails_when_a_profile_file_is_not_named(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26` and `gcc-26-debug`.\n")
            write(root / "conan" / "profiles" / "gcc-26-debug", "profile\n")
            write(root / "conan" / "profiles" / "clang-26", "profile\n")
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("conan profile clang-26 is not named in AGENTS.md", result.stderr)
        self.assertNotIn("gcc-26-debug", result.stderr)

    def test_fails_when_agents_names_a_missing_profile(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`, `gcc-99`.\n")
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("conan profile gcc-99 is named in AGENTS.md but does not exist", result.stderr)

    def test_names_a_profile_from_a_conan_profiles_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(
                root,
                """\
                Profiles: `gcc-26`.

                ```bash
                conan install . -pr:h ./conan/profiles/gcc-26-debug
                ```
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "conan profile gcc-26-debug is named in AGENTS.md but does not exist",
            result.stderr,
        )

    def test_does_not_treat_a_longer_profile_name_as_a_shorter_one(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "See `conan/profiles/gcc-26-debug`.\n")
            write(root / "conan" / "profiles" / "gcc-26-debug", "profile\n")
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("conan profile gcc-26 is not named in AGENTS.md", result.stderr)
        self.assertNotIn("gcc-26-debug is not named", result.stderr)

    def test_fails_when_a_fenced_command_references_a_missing_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(
                root,
                """\
                Profiles: `gcc-26`.

                ```bash
                ./scripts/missing-from-fence --input determinism-input.log
                ctest --test-dir build/Debug
                ```
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("AGENTS.md references missing path scripts/missing-from-fence", result.stderr)
        self.assertNotIn("determinism-input.log", result.stderr)
        self.assertNotIn("build/Debug", result.stderr)

    def test_fails_when_a_backtick_path_or_link_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(
                root,
                "Profiles: `gcc-26`. See `scripts/missing-script`.\n",
                """\
                # Contributing

                See [missing](docs/nope.md) and `conan/profiles/gcc-26*`.
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("AGENTS.md references missing path scripts/missing-script", result.stderr)
        self.assertIn("CONTRIBUTING.md references missing path docs/nope.md", result.stderr)
        self.assertNotIn("gcc-26*", result.stderr)

    def test_fails_when_an_adr_is_missing_from_the_index(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / "docs" / "adr" / "0002-other.md",
                """\
                ---
                status: "proposed"
                ---

                # Other
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "docs/adr/0002-other.md is missing from the docs/adr/README.md index",
            result.stderr,
        )

    def test_fails_when_adr_status_differs_from_the_index(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / "docs" / "adr" / "README.md",
                """\
                | Number | Title | Status | Date |
                | ------ | ----- | ------ | ---- |
                | 0001 | Example | proposed | 2026-10-18 |
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ADR 0001 status differs", result.stderr)
        self.assertIn("accepted", result.stderr)
        self.assertIn("proposed", result.stderr)

    def test_accepts_a_quoted_superseded_status(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / "docs" / "adr" / "0001-example.md",
                """\
                ---
                status: "superseded by 0002"
                date: "2026-10-18"
                ---

                # Example
                """,
            )
            write(
                root / "docs" / "adr" / "README.md",
                """\
                | Number | Title | Status | Date |
                | ------ | ----- | ------ | ---- |
                | 0001 | Example | superseded by 0002 | 2026-10-18 |
                """,
            )
            result = self.run_check(root)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("check-agent-docs: ok", result.stdout)

    def test_fails_when_adr_numbers_are_duplicated(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / "docs" / "adr" / "0001-copy.md",
                """\
                ---
                status: "accepted"
                ---

                # Copy
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate ADR number 0001", result.stderr)
        self.assertIn("docs/adr/0001-example.md", result.stderr)
        self.assertIn("docs/adr/0001-copy.md", result.stderr)

    def test_fails_when_the_index_repeats_a_number(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / "docs" / "adr" / "README.md",
                """\
                | Number | Title | Status | Date |
                | ------ | ----- | ------ | ---- |
                | 0001 | Example | accepted | 2026-10-18 |
                | 0001 | Example again | accepted | 2026-10-18 |
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate ADR number 0001 in the docs/adr/README.md index", result.stderr)

    def test_future_skill_and_rule_files_are_checked_when_present(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / ".agents" / "skills" / "demo" / "SKILL.md",
                """\
                ---
                name: "demo"
                description: "Does a thing"
                ---

                # Demo
                """,
            )
            write(
                root / ".cursor" / "rules" / "style.mdc",
                """\
                ---
                description: style
                alwaysApply: false
                ---

                Use the repo style.
                """,
            )
            write(
                root / ".github" / "instructions" / "style.instructions.md",
                """\
                ---
                applyTo: "**"
                ---

                Use the repo style.
                """,
            )
            result = self.run_check(root)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("check-agent-docs: ok", result.stdout)

    def test_fails_when_a_skill_front_matter_is_wrong(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(root / ".agents" / "skills" / "demo" / "SKILL.md", "# Demo\n")
            write(
                root / ".agents" / "skills" / "other" / "SKILL.md",
                """\
                ---
                name: demo
                ---

                # Other
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(".agents/skills/demo/SKILL.md is missing YAML front matter", result.stderr)
        self.assertIn(
            '.agents/skills/other/SKILL.md front matter name is "demo", expected "other"',
            result.stderr,
        )
        self.assertIn(
            ".agents/skills/other/SKILL.md front matter is missing description",
            result.stderr,
        )

    def test_fails_when_an_mdc_has_neither_globs_nor_always_apply(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / ".cursor" / "rules" / "style.mdc",
                """\
                ---
                description: style
                ---

                Use the repo style.
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            ".cursor/rules/style.mdc has neither globs nor alwaysApply",
            result.stderr,
        )

    def test_fails_when_instructions_lack_apply_to(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / ".github" / "instructions" / "style.instructions.md",
                """\
                ---
                description: style
                ---

                Use the repo style.
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            ".github/instructions/style.instructions.md is missing applyTo",
            result.stderr,
        )

    def test_fails_when_paired_rule_bodies_differ(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / ".cursor" / "rules" / "style.mdc",
                """\
                ---
                globs: src/**
                alwaysApply: false
                ---

                Cursor body.
                """,
            )
            write(
                root / ".github" / "instructions" / "style.instructions.md",
                """\
                ---
                applyTo: "src/**"
                ---

                Instructions body.
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            ".cursor/rules/style.mdc and .github/instructions/style.instructions.md "
            "differ below the front matter",
            result.stderr,
        )

    def test_fails_when_an_index_row_has_no_file(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / "docs" / "adr" / "README.md",
                """\
                | Number | Title | Status | Date |
                | ------ | ----- | ------ | ---- |
                | 0001 | Example | accepted | 2026-10-18 |
                | 0009 | Missing record | accepted | 2026-10-18 |
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("docs/adr/README.md index lists 0009 but no matching ADR file exists", result.stderr)

    def test_fails_when_adr_title_differs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / "docs" / "adr" / "README.md",
                """\
                | Number | Title | Status | Date |
                | ------ | ----- | ------ | ---- |
                | 0001 | Other title | accepted | 2026-10-18 |
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ADR 0001 title differs", result.stderr)
        self.assertIn("Example", result.stderr)
        self.assertIn("Other title", result.stderr)

    def test_fails_when_adr_date_differs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(
                root / "docs" / "adr" / "README.md",
                """\
                | Number | Title | Status | Date |
                | ------ | ----- | ------ | ---- |
                | 0001 | Example | accepted | 2026-01-01 |
                """,
            )
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ADR 0001 date differs", result.stderr)
        self.assertIn("2026-10-18", result.stderr)
        self.assertIn("2026-01-01", result.stderr)

    def test_fails_when_a_cursor_rule_has_no_front_matter(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            self.make_repo(root, "Profiles: `gcc-26`.\n")
            write(root / ".cursor" / "rules" / "style.mdc", "No front matter.\n")
            result = self.run_check(root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(".cursor/rules/style.mdc is missing YAML front matter", result.stderr)

    def test_current_repository_passes(self) -> None:
        result = self.run_check(ROOT)
        lint = (ROOT / "scripts" / "lint").read_text()
        agents = (ROOT / "AGENTS.md").read_text()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("check-agent-docs: ok", result.stdout)
        self.assertIn("scripts/check-agent-docs", lint)
        self.assertNotIn("./scripts/check-agent-docs", agents)


if __name__ == "__main__":
    unittest.main()
