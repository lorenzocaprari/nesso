import json
import os
import pathlib
import subprocess
import tempfile
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPARE = ROOT / "scripts" / "bench-compare"
BENCH = ROOT / "scripts" / "bench"


def load_bench():
    module = types.ModuleType("nesso_bench_script")
    module.__file__ = str(BENCH)
    exec(BENCH.read_text(encoding="utf-8"), module.__dict__)
    return module


def case(lines: int, wall: float, rss: int, ok: bool = True, error: str | None = None) -> dict:
    payload = {
        "lines": lines,
        "ok": ok,
        "exitCode": 0 if ok else 1,
        "wallSeconds": wall,
        "peakRssBytes": rss,
    }
    if error is not None:
        payload["error"] = error
    return payload


def payload(*cases: dict) -> dict:
    return {"sha": "test", "cases": list(cases)}


class BenchCompareTest(unittest.TestCase):
    def run_compare(
        self,
        baseline: dict | None,
        result: dict,
        extra: list[str] | None = None,
        env: dict[str, str] | None = None,
        baseline_path: pathlib.Path | None = None,
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            result_path = directory / "result.json"
            result_path.write_text(json.dumps(result), encoding="utf-8")
            if baseline_path is None:
                baseline_path = directory / "baseline.json"
                if baseline is not None:
                    baseline_path.write_text(json.dumps(baseline), encoding="utf-8")
            command = [
                "python3",
                str(COMPARE),
                "--baseline",
                str(baseline_path),
                "--result",
                str(result_path),
                *(extra or []),
            ]
            merged = os.environ.copy()
            merged.pop("NESSO_BENCH_TOLERANCE", None)
            merged.pop("NESSO_BENCH_WALL_TOLERANCE", None)
            merged.pop("NESSO_BENCH_RSS_TOLERANCE", None)
            if env:
                merged.update(env)
            return subprocess.run(command, capture_output=True, text=True, check=False, env=merged)

    def test_missing_baseline_warns_and_passes(self) -> None:
        missing = pathlib.Path("/tmp/nesso-bench-baseline-does-not-exist.json")
        result = self.run_compare(None, payload(case(1000, 1.0, 1000)), baseline_path=missing)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("warning: no baseline", result.stdout)

    def test_injected_slowdown_fails(self) -> None:
        baseline = payload(case(1000, 1.0, 100_000), case(10_000, 8.0, 100_000))
        slowed = payload(case(1000, 1.2, 100_000), case(10_000, 8.0, 100_000))
        result = self.run_compare(baseline, slowed)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("1000 lines: wallSeconds", result.stdout)
        self.assertIn("REGRESSION", result.stdout)
        self.assertIn("10000 lines: wallSeconds", result.stdout)
        self.assertIn("ok", result.stdout)

    def test_rss_regression_fails_when_wall_is_inside_tolerance(self) -> None:
        baseline = payload(case(1000, 1.0, 100_000))
        result = self.run_compare(baseline, payload(case(1000, 1.0, 130_000)))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("peakRssBytes", result.stdout)
        self.assertIn("REGRESSION", result.stdout)

    def test_within_default_tolerance_passes(self) -> None:
        baseline = payload(case(1000, 1.0, 100_000))
        result = self.run_compare(baseline, payload(case(1000, 1.14, 114_000)))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("bench-compare: ok", result.stdout)

    def test_omitted_large_tier_is_not_a_failure(self) -> None:
        baseline = payload(case(1000, 1.0, 100_000), case(100_000, 200.0, 100_000))
        result = self.run_compare(baseline, payload(case(1000, 1.0, 100_000)))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("100000 lines: skipped (not in this run)", result.stdout)
        self.assertIn("bench-compare: ok", result.stdout)

    def test_tier_missing_from_baseline_warns(self) -> None:
        baseline = payload(case(1000, 1.0, 100_000))
        result = self.run_compare(baseline, payload(case(1000, 1.0, 100_000), case(10_000, 8.0, 100_000)))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("10000 lines: warning: no baseline for this tier", result.stdout)

    def test_failed_run_against_completed_baseline_fails(self) -> None:
        baseline = payload(case(1000, 1.0, 100_000))
        result = self.run_compare(baseline, payload(case(1000, 1.0, 100_000, ok=False, error="timed out")))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("FAILED", result.stdout)

    def test_ci_tolerances_pass_at_the_limit_and_fail_above_it(self) -> None:
        baseline = payload(case(1000, 2.74, 156_700_672), case(10_000, 14.81, 154_976_256))
        env = {"NESSO_BENCH_WALL_TOLERANCE": "0.35", "NESSO_BENCH_RSS_TOLERANCE": "0.15"}
        at_limit = payload(
            case(1000, 2.74 * 1.35, int(156_700_672 * 1.15)),
            case(10_000, 14.81 * 1.35, 154_976_256),
        )
        held = self.run_compare(baseline, at_limit, env=env)
        self.assertEqual(held.returncode, 0, held.stdout + held.stderr)
        slowed = payload(case(1000, 2.74 * 1.35 + 0.01, 156_700_672), case(10_000, 14.81, 154_976_256))
        failed = self.run_compare(baseline, slowed, env=env)
        self.assertEqual(failed.returncode, 1, failed.stdout)
        self.assertIn("REGRESSION", failed.stdout)

    def test_grep_payload_records_threads(self) -> None:
        bench = load_bench()
        body = bench.grep_payload(
            sha="abc",
            binary=bench.Path("build/Release/src/nesso"),
            query="database connection refused",
            cpu="test",
            nproc=4,
            threads="4",
            cases=[],
        )
        self.assertEqual(body["threads"], "4")
        self.assertEqual(body["nproc"], 4)
        self.assertEqual(body["cpuModel"], "test")
        self.assertIsNone(
            bench.grep_payload(
                sha="abc",
                binary=bench.Path("nesso"),
                query="q",
                cpu="test",
                nproc=1,
                threads=None,
                cases=[],
            )["threads"]
        )

    def test_trace_stages_match_grep(self) -> None:
        bench = load_bench()
        self.assertEqual(bench.TRACE_STAGES, ("parse", "query-embed", "embed"))
        trace = {
            "peakRssBytes": 1,
            "stages": [{"name": name, "us": 1} for name in bench.TRACE_STAGES],
        }
        self.assertTrue(bench.trace_ok(trace))
        old = {
            "peakRssBytes": 1,
            "stages": [
                {"name": "parse", "us": 1},
                {"name": "embed", "us": 1},
                {"name": "rank", "us": 1},
            ],
        }
        self.assertFalse(bench.trace_ok(old))


if __name__ == "__main__":
    unittest.main()
