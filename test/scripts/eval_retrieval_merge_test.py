import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MERGE = ROOT / "scripts" / "eval-retrieval-merge"


def metrics(value: float) -> dict[str, float]:
    return {"mrr": value, "ndcg_at_10": value, "precision_at_10": value, "recall_at_10": value}


def engine(name: str, value: float) -> dict:
    bucket = {"found_at_depth": 1.0, "median_first_hit_rank": 1, "metrics": metrics(value), "n": 2}
    return {
        "by_class": [{"class": "symptom", **bucket}, {"class": "keyword", "n": 0}],
        "engine": name,
        "overall": bucket,
        "status": "ok",
    }


def needle_report(*engines: dict, dataset: str = "bgl-needle-100k", lines: int = 100) -> dict:
    return {
        "datasets": [{"dataset": dataset, "engines": list(engines), "lines": lines, "query_count": 2}],
        "depth": 100,
        "k": 10,
        "kind": "needle",
        "overall": {"engines": list(engines), "query_count": 2},
        "schema_version": 1,
    }


def retrieval_report(*engines: dict) -> dict:
    return {
        "datasets": [{"dataset": "hdfs-2k", "engines": list(engines), "query_count": 2}],
        "k": 10,
        "overall": {"engines": list(engines), "query_count": 2},
        "schema_version": 4,
    }


class EvalRetrievalMergeTest(unittest.TestCase):
    def run_merge(self, *reports: dict, markdown: bool = False) -> tuple[subprocess.CompletedProcess[str], dict | None, str]:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            paths = []
            for index, payload in enumerate(reports):
                path = directory / f"r{index}.json"
                path.write_text(json.dumps(payload), encoding="utf-8")
                paths.append(str(path))
            out = directory / "merged.json"
            md = directory / "merged.md"
            command = [sys.executable, str(MERGE), *paths, "--out", str(out)]
            if markdown:
                command += ["--markdown", str(md)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            merged = json.loads(out.read_text(encoding="utf-8")) if out.exists() else None
            text = md.read_text(encoding="utf-8") if md.exists() else ""
        return result, merged, text

    def test_merges_engines_in_input_order(self) -> None:
        result, merged, text = self.run_merge(
            needle_report(engine("diy-faiss", 0.4)),
            needle_report(engine("ck", 0.1), {"engine": "vecgrep", "reason": "missing", "status": "skipped"}),
            markdown=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        assert merged is not None
        self.assertEqual([row["engine"] for row in merged["overall"]["engines"]], ["diy-faiss", "ck", "vecgrep"])
        self.assertEqual([row["engine"] for row in merged["datasets"][0]["engines"]], ["diy-faiss", "ck", "vecgrep"])
        self.assertEqual(merged["kind"], "needle")
        self.assertIn("| diy-faiss | symptom | 2 | 0.400 | 0.400 | 0.400 | 1.000 |", text)
        self.assertIn("| vecgrep | skipped: missing |", text)
        # Empty classes add no row.
        self.assertNotIn("| keyword |", text)

    def test_retrieval_markdown_uses_recall(self) -> None:
        result, _merged, text = self.run_merge(retrieval_report(engine("bm25", 1.0)), markdown=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("| engine | class | n | MRR | nDCG@10 | recall@10 |", text)

    def test_rejects_mismatches(self) -> None:
        cases = [
            ((needle_report(engine("a", 1.0)), needle_report(engine("a", 0.5))), "is in both"),
            ((needle_report(engine("a", 1.0)), retrieval_report(engine("b", 1.0))), "report;"),
            ((needle_report(engine("a", 1.0)), needle_report(engine("b", 1.0), dataset="other")), "has datasets"),
            ((needle_report(engine("a", 1.0)), needle_report(engine("b", 1.0), lines=5)), "differs outside engines"),
            (({"datasets": [], "schema_version": 3, "overall": {"engines": []}},), "schema_version 4"),
        ]
        for reports, message in cases:
            result, _merged, _text = self.run_merge(*reports)
            self.assertNotEqual(result.returncode, 0, message)
            self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
