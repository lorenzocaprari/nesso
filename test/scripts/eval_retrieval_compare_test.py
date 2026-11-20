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


def engine_report(engines: list[dict], *, dataset: str = "hdfs-2k") -> dict:
    """Schema 4: nesso keeps modes[], other engines carry by_class/overall."""
    return {
        "datasets": [{"dataset": dataset, "engines": engines, "query_count": 1}],
        "k": 10,
        "overall": {"engines": engines, "query_count": 1},
        "schema_version": 4,
    }


def baseline_entry(name: str, class_metrics: dict[str, float] | None = None) -> dict:
    class_metrics = class_metrics or metrics(1.0, 1.0, 1.0)
    return {
        "by_class": [{"class": "paraphrase", "metrics": class_metrics, "n": 1}],
        "engine": name,
        "overall": {"metrics": class_metrics, "n": 1},
        "status": "ok",
    }


def needle_report(engines: list[dict]) -> dict:
    return {
        "datasets": [{"dataset": "bgl-needle-100k", "engines": engines, "query_count": 1}],
        "kind": "needle",
        "overall": {"engines": engines, "query_count": 1},
        "schema_version": 1,
    }


def needle_entry(name: str, mrr: float, precision: float) -> dict:
    bucket = {
        "class": "symptom",
        "found_at_depth": 1.0,
        "median_first_hit_rank": 1,
        "metrics": {"mrr": mrr, "ndcg_at_10": mrr, "precision_at_10": precision, "recall_at_10": mrr},
        "n": 1,
    }
    return {
        "by_class": [bucket, {"class": "keyword", "n": 0}],
        "engine": name,
        "overall": {key: value for key, value in bucket.items() if key != "class"},
        "status": "ok",
    }


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


    def test_schema3_and_schema4_compare_as_one_shape(self) -> None:
        previous = report(modes=[mode_entry("auto")])
        current = engine_report(
            [
                {"engine": "nesso", "modes": [mode_entry("auto")], "status": "ok"},
                baseline_entry("bm25"),
                {"engine": "ripgrep", "reason": "rg executable not found", "status": "skipped"},
            ]
        )
        result = self.run_compare(current, previous)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("schema_version mismatch", result.stdout)
        self.assertIn("| hdfs-2k | auto | paraphrase | mrr | 1.000000 | 1.000000 | +0.0% | ok |", result.stdout)
        self.assertIn("| hdfs-2k | bm25 | paraphrase | mrr | 1.000000 | — | — | new |", result.stdout)
        self.assertNotIn("ripgrep", result.stdout)

    def test_schema4_baseline_drop_names_the_engine(self) -> None:
        previous = engine_report([baseline_entry("bm25")])
        current = engine_report([baseline_entry("bm25", metrics(1.0, 0.5, 1.0))])
        result = self.run_compare(current, previous)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("DROP hdfs-2k bm25 paraphrase mrr: 1.000000 -> 0.500000", result.stdout)

    def test_needle_reports_compare_including_precision(self) -> None:
        previous = needle_report([needle_entry("nesso/auto", 0.4, 0.2), needle_entry("grep", 0.0, 0.0)])
        current = needle_report([needle_entry("nesso/auto", 0.4, 0.1), needle_entry("grep", 0.0, 0.0)])
        result = self.run_compare(current, previous)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("DROP bgl-needle-100k auto symptom precision_at_10: 0.200000 -> 0.100000", result.stdout)
        self.assertIn("| bgl-needle-100k | grep | symptom | mrr |", result.stdout)
        # A class with n = 0 has no metrics and adds no row.
        self.assertNotIn("| keyword |", result.stdout)

    def test_needle_and_retrieval_reports_do_not_mix(self) -> None:
        result = self.run_compare(needle_report([needle_entry("grep", 0.0, 0.0)]), report())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("schema_version mismatch: current=needle 1 previous=retrieval 3", result.stdout)

    def run_margins(self, current: dict, references: list[dict], extra: list[str] | None = None) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            current_path = directory / "current.json"
            current_path.write_text(json.dumps(current), encoding="utf-8")
            command = ["python3", str(COMPARE), "--current", str(current_path), "--margins"]
            for index, payload in enumerate(references):
                path = directory / f"ref{index}.json"
                path.write_text(json.dumps(payload), encoding="utf-8")
                command += ["--reference", str(path)]
            return subprocess.run(command + (extra or []), capture_output=True, text=True, check=False)

    def test_margins_pick_the_best_other_engine(self) -> None:
        current = engine_report(
            [
                {"engine": "nesso", "modes": [mode_entry("auto", class_metrics=metrics(1.0, 0.6, 0.6)), mode_entry("dense")], "status": "ok"},
                baseline_entry("grep", metrics(0.2, 0.2, 0.2)),
                baseline_entry("bm25", metrics(0.5, 0.5, 0.5)),
            ]
        )
        result = self.run_margins(current, [])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("nesso `auto` against the best other engine by MRR", result.stdout)
        # nesso MRR 0.6 against bm25 0.5; grep and the nesso `dense` mode are not candidates.
        self.assertIn("| hdfs-2k | paraphrase | 1 | 0.600 | 0.600 | bm25 | 0.500 | 0.500 | +0.100 |", result.stdout)
        self.assertIn("References used: 0.", result.stdout)

    def test_margins_use_references_and_flag_small_classes(self) -> None:
        current = engine_report(
            [
                {"engine": "nesso", "modes": [mode_entry("auto", class_metrics=metrics(1.0, 0.6, 0.6))], "status": "ok"},
                baseline_entry("bm25", metrics(0.5, 0.5, 0.5)),
            ]
        )
        reference = engine_report([baseline_entry("diy-faiss", metrics(0.9, 0.9, 0.9)), baseline_entry("bm25", metrics(0.99, 0.99, 0.99))])
        result = self.run_margins(current, [reference, needle_report([needle_entry("ck", 1.0, 1.0)])])
        self.assertEqual(result.returncode, 0, result.stderr)
        # The reference's diy-faiss is the best other engine; its bm25 does not override this run's bm25.
        self.assertIn("| hdfs-2k | paraphrase | 1 | 0.600 | 0.600 | diy-faiss (reference) | 0.900 | 0.900 | -0.300 |", result.stdout)
        # The needle reference is ignored for an engine report.
        self.assertIn("References used: 1.", result.stdout)
        self.assertIn("fewer than 30 queries", result.stdout)

    def test_margins_for_needle_reports_and_missing_nesso(self) -> None:
        current = needle_report([needle_entry("nesso/auto", 0.4, 0.2), needle_entry("grep", 0.1, 0.1)])
        result = self.run_margins(current, [])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("| bgl-needle-100k | symptom | 1 | 0.400 | 0.400 | grep | 0.100 | 0.100 | +0.300 |", result.stdout)
        no_nesso = self.run_margins(engine_report([baseline_entry("bm25")]), [])
        self.assertEqual(no_nesso.returncode, 0, no_nesso.stderr)
        self.assertIn("no nesso rows", no_nesso.stdout)

    def test_margins_and_previous_can_be_combined_but_one_is_required(self) -> None:
        current = engine_report([{"engine": "nesso", "modes": [mode_entry("auto")], "status": "ok"}, baseline_entry("bm25")])
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "r.json"
            path.write_text(json.dumps(current), encoding="utf-8")
            neither = subprocess.run(["python3", str(COMPARE), "--current", str(path)], capture_output=True, text=True, check=False)
            both = subprocess.run(
                ["python3", str(COMPARE), "--current", str(path), "--previous", str(path), "--margins"],
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertNotEqual(neither.returncode, 0)
        self.assertIn("pass --previous, --margins, or both", neither.stderr)
        self.assertEqual(both.returncode, 0, both.stderr)
        self.assertIn("### nesso margin", both.stdout)
        self.assertIn("### Retrieval eval compare", both.stdout)


if __name__ == "__main__":
    unittest.main()
