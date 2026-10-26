import importlib.machinery
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "demo-log"
ENOSPC = "No space left on device"


def load_demo_log():
    loader = importlib.machinery.SourceFileLoader("demo_log", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def run_log(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True,
        text=True,
        check=False,
    )


class DemoLogTest(unittest.TestCase):
    def test_query_word_check_ignores_unrelated_letters(self) -> None:
        module = load_demo_log()
        self.assertEqual(module.query_words_in("No space left on device"), set())
        self.assertEqual(module.query_words_in("this finished"), set())
        self.assertEqual(module.query_words_in("the disk is full"), {"the", "disk", "is", "full"})

    def test_default_log_is_stable_and_omits_the_query(self) -> None:
        module = load_demo_log()
        first = run_log()
        second = run_log()
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout, second.stdout)
        self.assertEqual(first.stderr, "")
        self.assertEqual(first.stdout, module.render_text())

        lines = first.stdout.splitlines()
        self.assertEqual(len(lines), module.LINE_COUNT)
        self.assertEqual(module.SEED, 7)
        self.assertTrue(all(lines))
        self.assertGreaterEqual(sum(ENOSPC in line for line in lines), 3)
        self.assertNotIn("the disk is full", first.stdout.lower())
        for line in lines:
            self.assertEqual(module.query_words_in(line), set(), line)

    def test_seed_changes_the_log_and_lines_flag_is_honored(self) -> None:
        baseline = run_log("--lines", "20")
        other = run_log("--seed", "8", "--lines", "20")
        self.assertEqual(baseline.returncode, 0, baseline.stderr)
        self.assertEqual(other.returncode, 0, other.stderr)
        self.assertEqual(len(baseline.stdout.splitlines()), 20)
        self.assertNotEqual(baseline.stdout, other.stdout)
        self.assertGreaterEqual(sum(ENOSPC in line for line in baseline.stdout.splitlines()), 3)

    def test_output_flag_writes_lf_and_leaves_stdout_empty(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "app.log"
            result = run_log("-o", str(path), "--lines", "5")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            data = path.read_bytes()
            self.assertNotIn(b"\r", data)
            text = data.decode("utf-8")
            self.assertEqual(text, run_log("--lines", "5").stdout)
            self.assertTrue(text.endswith("\n"))
            self.assertGreaterEqual(text.count(ENOSPC), 3)

    def test_rejects_a_non_positive_line_count_and_a_missing_directory(self) -> None:
        rejected = run_log("--lines", "0")
        self.assertEqual(rejected.returncode, 2)
        self.assertIn("positive", rejected.stderr)
        self.assertEqual(rejected.stdout, "")

        with tempfile.TemporaryDirectory() as temp:
            missing = pathlib.Path(temp) / "missing" / "app.log"
            result = run_log("-o", str(missing), "--lines", "1")
        self.assertEqual(result.returncode, 2)
        self.assertIn("directory does not exist", result.stderr)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
