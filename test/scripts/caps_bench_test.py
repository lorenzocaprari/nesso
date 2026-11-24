import importlib.machinery
import importlib.util
import json
import pathlib
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "caps-bench"


def load_caps_bench():
    loader = importlib.machinery.SourceFileLoader("caps_bench", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(SCRIPT), *args], capture_output=True, text=True, check=False)


class CapsBenchTest(unittest.TestCase):
    def test_distinct_lines_are_unique_and_letters_only(self) -> None:
        module = load_caps_bench()
        lines = {module.distinct_line(index) for index in range(5000)}
        self.assertEqual(len(lines), 5000)
        for line in lines:
            self.assertFalse(any(char.isdigit() for char in line))
        for word in module.WORDS:
            self.assertGreater(len(word), 2)
            self.assertTrue(set(word) - set("abcdef"))
        with self.assertRaises(ValueError):
            module.distinct_line(module.MAX_DISTINCT_TEXTS)

    def test_generate_directory_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            for out in (first, second):
                result = run("generate", "--distinct", "7", "--sources", "3", out)
                self.assertEqual(result.returncode, 0, result.stderr)
            files = sorted(path.relative_to(first) for path in pathlib.Path(first, "input").rglob("*.log"))
            self.assertEqual(len(files), 3)
            for name in files:
                self.assertEqual(pathlib.Path(first, name).read_bytes(), pathlib.Path(second, name).read_bytes())
            lines = [line for name in files for line in pathlib.Path(first, name).read_text().splitlines()]
            self.assertEqual(len(lines), 7)
            self.assertEqual(len(set(lines)), 7)
            meta = json.loads(pathlib.Path(first, "caps-input.json").read_text())
            self.assertEqual(meta, {"distinct": 7, "input": "input", "lines": 7, "sources": 3})

    def test_generate_tar_has_one_member_per_source(self) -> None:
        with tempfile.TemporaryDirectory() as out:
            result = run("generate", "--distinct", "1", "--sources", "12", "--tar", out)
            self.assertEqual(result.returncode, 0, result.stderr)
            with tarfile.open(pathlib.Path(out, "input.tar")) as archive:
                members = archive.getmembers()
            self.assertEqual(len(members), 12)
            self.assertEqual(members, sorted(members, key=lambda member: member.name))

    def test_generate_rejects_bad_counts(self) -> None:
        with tempfile.TemporaryDirectory() as out:
            result = run("generate", "--distinct", "0", "--sources", "1", out)
            self.assertEqual(result.returncode, 2)
            self.assertIn("at least 1", result.stderr)


if __name__ == "__main__":
    unittest.main()
