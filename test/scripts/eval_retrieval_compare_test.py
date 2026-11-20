import json
import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPARE = ROOT / "scripts" / "eval-retrieval-compare"


def metrics(recall: float, mrr: float, ndcg: float) -> dict[str, float]:
    return {
        "mrr": mrr,
        "ndcg_at_10": ndcg,
        "recall_at_10": recall,
    }


def mode_entry(
    mode: str,
    *,
    class_name: str = "paraphrase",
    class_metrics: dict[str, float] | None = None,
    overall_metrics: dict[str, float] | None = None,
) -> dict:
    class_metrics = class_metrics or metrics(1.0, 1.0, 1.0)
    overall_metrics = overall_metrics or class_metrics
    return {
        "by_class": [
            {
                "class": class_name,
                "metrics": class_metrics,
                "n": 1,
            }
        ],
        "mode": mode,
        "overall": {"metrics": overall_metrics, "n": 1},
    }


def report(
    *,
    schema_version: int = 3,
    dataset: str = "hdfs-2k",
    modes: list[dict] | None = None,
    include_dataset: bool = True,
    include_overall: bool = True,
) -> dict:
    modes = modes or [mode_entry("hybrid")]
    payload: dict = {
        "k": 10,
        "schema_version": schema_version,
    }
    if include_dataset:
        payload["datasets"] = [
            {
                "dataset": dataset,
                "modes": modes,
                "query_count": 1,
            }
        ]
    else:
        payload["datasets"] = []
    if include_overall:
        payload["overall"] = {
            "modes": modes,
            "query_count": 1,
        }
    else:
        payload["overall"] = {"modes": [], "query_count": 0}
    return payload


class EvalRetrievalCompareTest(unittest.TestCase):
    def run_compare(
        self,
        current: dict | None,
        previous: dict | None,
        extra: list[str] | None = None,
        env: dict[str, str] | None = None,
        *,
        write_current: bool = True,
        write_previous: bool = True,
        current_text: str | None = None,
        previous_text: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            current_path = directory / "current.json"
            previous_path = directory / "previous.json"
            if write_current:
                if current_text is not None:
                    current_path.write_text(current_text, encoding="utf-8")
                elif current is not None:
                    current_path.write_text(json.dumps(current), encoding="utf-8")
            if write_previous:
                if previous_text is not None:
                    previous_path.write_text(previous_text, encoding="utf-8")
                elif previous is not None:
                    previous_path.write_text(json.dumps(previous), encoding="utf-8")
            command = [
                "python3",
                str(COMPARE),
                "--current",
                str(current_path),
                "--previous",
                str(previous_path),
                *(extra or []),
            ]
            merged = os.environ.copy()
            merged.pop("NESSO_EVAL_TOLERANCE", None)
            if env:
                merged.update(env)
            return subprocess.run(command, capture_output=True, text=True, check=False, env=merged)

    def test_no_change(self) -> None:
        payload = report()
        result = self.run_compare(payload, payload)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("| ok |", result.stdout)
        self.assertNotIn("DROP ", result.stdout)
        self.assertNotIn("| drop |", result.stdout)

    def test_drop_just_over_five_percent(self) -> None:
        previous = report(
            modes=[mode_entry("hybrid", class_metrics=metrics(1.0, 1.0, 1.0))]
        )
        current = report(
            modes=[mode_entry("hybrid", class_metrics=metrics(0.949, 1.0, 1.0))]
        )
        result = self.run_compare(current, previous)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("| drop |", result.stdout)
        self.assertIn(
            "DROP hdfs-2k hybrid paraphrase recall_at_10: 1.000000 -> 0.949000",
            result.stdout,
        )

    def test_drop_just_under_five_percent(self) -> None:
        previous = report(
            modes=[mode_entry("hybrid", class_metrics=metrics(1.0, 1.0, 1.0))]
        )
        current = report(
            modes=[mode_entry("hybrid", class_metrics=metrics(0.951, 1.0, 1.0))]
        )
        result = self.run_compare(current, previous)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("| drop |", result.stdout)
        self.assertNotIn("DROP ", result.stdout)
        self.assertIn("| ok |", result.stdout)

    def test_previous_zero_is_not_a_drop(self) -> None:
        previous = report(
            modes=[mode_entry("hybrid", class_metrics=metrics(0.0, 0.0, 0.0))]
        )
        current = report(
            modes=[mode_entry("hybrid", class_metrics=metrics(0.0, 0.0, 0.0))]
        )
        result = self.run_compare(current, previous)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("| n/a |", result.stdout)
        self.assertNotIn("| drop |", result.stdout)
        self.assertNotIn("DROP ", result.stdout)

    def test_new_and_removed_system(self) -> None:
        previous = report(dataset="hdfs-2k", include_overall=False)
        current = report(dataset="linux-2k", include_overall=False)
        result = self.run_compare(current, previous)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("| linux-2k |", result.stdout)
        self.assertIn("| new |", result.stdout)
        self.assertIn("| hdfs-2k |", result.stdout)
        self.assertIn("| removed |", result.stdout)
        self.assertNotIn("DROP ", result.stdout)

    def test_schema_mismatch_skips_comparison(self) -> None:
        current = report(schema_version=3)
        previous = report(schema_version=2)
        result = self.run_compare(current, previous)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("schema_version mismatch", result.stdout)
        self.assertNotIn("| system |", result.stdout)
        self.assertNotIn("DROP ", result.stdout)

    def test_malformed_input_exits_nonzero(self) -> None:
        missing = self.run_compare(None, report(), write_current=False)
        self.assertNotEqual(missing.returncode, 0, missing.stdout)
        self.assertIn("cannot read", missing.stderr)

        bad_json = self.run_compare(None, report(), current_text="{not-json")
        self.assertNotEqual(bad_json.returncode, 0, bad_json.stdout)
        self.assertIn("malformed JSON", bad_json.stderr)

        no_schema = self.run_compare({"k": 10, "datasets": []}, report())
        self.assertNotEqual(no_schema.returncode, 0, no_schema.stdout)
        self.assertIn("schema_version", no_schema.stderr)

    def test_tolerance_env_override(self) -> None:
        previous = report(
            modes=[mode_entry("hybrid", class_metrics=metrics(1.0, 1.0, 1.0))]
        )
        current = report(
            modes=[mode_entry("hybrid", class_metrics=metrics(0.94, 1.0, 1.0))]
        )
        # 6% drop is inside a 10% tolerance.
        result = self.run_compare(current, previous, env={"NESSO_EVAL_TOLERANCE": "0.10"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("| drop |", result.stdout)


if __name__ == "__main__":
    unittest.main()
