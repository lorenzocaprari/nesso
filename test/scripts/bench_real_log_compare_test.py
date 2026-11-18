import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "bench-real-log-compare"
BUDGET = 1536 * 1024 * 1024


def command(peak: int, wall: float, *, ok: bool = True) -> dict:
    return {
        "name": "index",
        "mode": None,
        "ok": ok,
        "wallSeconds": wall,
        "peakRssBytes": peak,
    }


def payload(peak: int, wall: float = 1.0) -> dict:
    return {
        "dataset": "real-log",
        "cases": [
            {
                "corpus": "hdfs-v1",
                "threads": "1",
                "commands": [
                    command(peak, wall),
                    {
                        "name": "search",
                        "mode": "dense",
                        "ok": True,
                        "wallSeconds": wall,
                        "peakRssBytes": peak,
                    },
                ],
            }
        ],
    }


def run_compare(*args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True,
        text=True,
        check=False,
        env=env,
    )


class BenchRealLogCompareTest(unittest.TestCase):
    def test_rss_over_the_budget_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            result = directory / "result.json"
            over = int(BUDGET * 1.15) + 1
            result.write_text(json.dumps(payload(over)), encoding="utf-8")
            completed = run_compare("--result", str(result), "--rss-tolerance", "0.15", "--wall-tolerance", "0.15")
            self.assertEqual(completed.returncode, 1, completed.stdout)
            self.assertIn("exceeds", completed.stderr)
            under = directory / "under.json"
            under.write_text(json.dumps(payload(int(BUDGET * 1.15))), encoding="utf-8")
            passed = run_compare("--result", str(under), "--rss-tolerance", "0.15")
            self.assertEqual(passed.returncode, 0, passed.stderr + passed.stdout)
            self.assertIn("speed comparison skipped", passed.stdout)

    def test_missing_history_warns_and_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            result = pathlib.Path(temp) / "result.json"
            result.write_text(json.dumps(payload(1024)), encoding="utf-8")
            completed = run_compare("--result", str(result), "--rss-tolerance", "0")
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertIn("no nightly history", completed.stdout)
            self.assertNotIn("exceeds", completed.stderr)

    def test_wall_over_the_median_warns_without_failing(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            history = directory / "history.json"
            history.write_text(
                json.dumps({"runs": [payload(1024, 1.0), payload(1024, 1.2), payload(1024, 1.1)]}),
                encoding="utf-8",
            )
            result = directory / "result.json"
            result.write_text(json.dumps(payload(1024, 10.0)), encoding="utf-8")
            completed = run_compare(
                "--result",
                str(result),
                "--history",
                str(history),
                "--rss-tolerance",
                "0",
                "--wall-tolerance",
                "0.15",
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertIn("above the median", completed.stdout)
            self.assertIn("search-dense", completed.stdout)

    def test_data_js_history_and_export(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            data = {
                "entries": {
                    "nesso real-log": [
                        {
                            "benches": [
                                {"name": "real-log/hdfs-v1/threads-1/index/wall", "unit": "s", "value": 2.0},
                            ]
                        }
                    ]
                }
            }
            history = directory / "data.js"
            history.write_text("window.BENCHMARK_DATA = " + json.dumps(data) + ";\n", encoding="utf-8")
            result = directory / "result.json"
            result.write_text(json.dumps(payload(1024, 2.0)), encoding="utf-8")
            external = directory / "external.json"
            completed = run_compare(
                "--result",
                str(result),
                "--history",
                str(history),
                "--rss-tolerance",
                "0",
                "--wall-tolerance",
                "0.15",
                "--export-external",
                str(external),
            )
            self.assertEqual(completed.returncode, 0, completed.stderr + completed.stdout)
            self.assertNotIn("above the median", completed.stdout)
            exported = json.loads(external.read_text(encoding="utf-8"))
            names = [entry["name"] for entry in exported]
            self.assertIn("real-log/hdfs-v1/threads-1/index/wall", names)
            self.assertIn("real-log/hdfs-v1/threads-1/search-dense/wall", names)
            self.assertTrue(all(entry["unit"] == "s" for entry in exported))


if __name__ == "__main__":
    unittest.main()
