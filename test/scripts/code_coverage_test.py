#!/usr/bin/env python3
"""Unit tests for scripts/code-coverage mode parsing and lcov merge floors."""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "code-coverage"


def write_trace(path: pathlib.Path, name: str) -> None:
    # Minimal hit records so lcov --summary and -a accept the file. Counts come
    # from DA/FN/BRDA lines, not the LF/LH headers alone.
    source = path.with_suffix(".cpp")
    source.write_text(f"int {name};\n", encoding="utf-8")
    path.write_text(
        "\n".join(
            [
                "TN:",
                f"SF:{source}",
                f"FN:1,{name}",
                f"FNDA:1,{name}",
                "FNF:1",
                "FNH:1",
                "BRDA:1,0,0,1",
                "BRF:1",
                "BRH:1",
                "DA:1,1",
                "LF:1",
                "LH:1",
                "end_of_record",
                "",
            ]
        ),
        encoding="utf-8",
    )


class CodeCoverageScriptTest(unittest.TestCase):
    def run_script(self, *args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        merged = os.environ.copy()
        if env:
            merged.update(env)
        return subprocess.run(
            ["bash", str(SCRIPT), *args],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
            env=merged,
        )

    def test_help_lists_modes(self) -> None:
        result = self.run_script("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--mode", result.stdout)
        self.assertIn("merge", result.stdout)

    def test_merge_requires_traces(self) -> None:
        result = self.run_script("--mode", "merge")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("--merge", result.stderr)

    def test_merge_applies_floors_on_combined_trace(self) -> None:
        if shutil.which("lcov") is None or shutil.which("genhtml") is None:
            self.skipTest("lcov/genhtml required")
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            a = directory / "a.info"
            b = directory / "b.info"
            write_trace(a, "a")
            write_trace(b, "b")
            out = directory / "out"
            cleaned = out / "coverage.cleaned"
            result = self.run_script(
                "--mode",
                "merge",
                "--out-dir",
                str(out),
                "--info",
                str(out / "coverage.info"),
                "--cleaned",
                str(cleaned),
                "--report-dir",
                str(out / "report"),
                "--merge",
                str(a),
                "--merge",
                str(b),
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(cleaned.is_file())
            summary = (out / "report" / "summary.txt").read_text(encoding="utf-8")
            self.assertIn("lines:", summary)
            self.assertIn("functions:", summary)
            self.assertIn("branches:", summary)
            self.assertIn("Coverage OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
