import importlib.machinery
import importlib.util
import json
import math
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "eval-retrieval"


def load_eval():
    loader = importlib.machinery.SourceFileLoader("eval_retrieval", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[loader.name] = module
    loader.exec_module(module)
    return module


def idcg10() -> float:
    return sum(1.0 / math.log2(index + 1) for index in range(1, 11))


class EvalRetrievalMetricsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_eval()

    def test_perfect_ranking(self) -> None:
        ranked = ["E10"] * 10
        metrics = self.mod.score_ranking(ranked, frozenset({"E10"}), relevant_line_count=100)
        self.assertEqual(metrics.recall_at_10, 1.0)
        self.assertEqual(metrics.mrr, 1.0)
        self.assertEqual(metrics.ndcg_at_10, 1.0)

    def test_no_hits(self) -> None:
        metrics = self.mod.score_ranking([], frozenset({"E10"}), relevant_line_count=100)
        self.assertEqual(metrics.recall_at_10, 0.0)
        self.assertEqual(metrics.mrr, 0.0)
        self.assertEqual(metrics.ndcg_at_10, 0.0)

    def test_hit_at_rank_3(self) -> None:
        ranked = ["E1", "E1", "E10"] + ["E1"] * 7
        metrics = self.mod.score_ranking(ranked, frozenset({"E10"}), relevant_line_count=100)
        self.assertEqual(metrics.recall_at_10, 1.0)
        self.assertAlmostEqual(metrics.mrr, 1.0 / 3.0)
        expected_ndcg = (1.0 / math.log2(4)) / idcg10()
        self.assertAlmostEqual(metrics.ndcg_at_10, expected_ndcg)

    def test_duplicates(self) -> None:
        ranked = ["E10", "E10", "E1"]
        metrics = self.mod.score_ranking(ranked, frozenset({"E10"}), relevant_line_count=100)
        self.assertEqual(metrics.recall_at_10, 1.0)
        self.assertEqual(metrics.mrr, 1.0)
        dcg = 1.0 / math.log2(2) + 1.0 / math.log2(3)
        self.assertAlmostEqual(metrics.ndcg_at_10, dcg / idcg10())


class EvalRetrievalEndToEndTest(unittest.TestCase):
    def test_fake_nesso_report_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            structured = directory / "structured.csv"
            structured.write_text(
                "LineId,EventId\n1,E10\n2,E1\n3,E10\n4,E7\n5,E3\n",
                encoding="utf-8",
            )
            log_path = directory / "sample.log"
            log_path.write_text("a\nb\nc\nd\ne\n", encoding="utf-8")
            queries = directory / "queries.jsonl"
            queries.write_text(
                '{"id":"q1","class":"exact_id","query":"alpha","relevant":["E10"]}\n'
                '{"id":"q2","class":"paraphrase","query":"beta","relevant":["E7"]}\n',
                encoding="utf-8",
            )
            fake = directory / "fake-nesso"
            fake.write_text(
                textwrap.dedent(
                    """\
                    #!/usr/bin/env python3
                    import json
                    import sys
                    args = sys.argv[1:]
                    if args and args[0] == "index":
                        out = args[args.index("-o") + 1]
                        open(out, "w", encoding="utf-8").write("corpus\\n")
                        sys.exit(0)
                    if args and args[0] == "search":
                        query = args[1]
                        if query == "alpha":
                            lines = [1, 2, 3]
                        else:
                            lines = [4, 5, 2]
                        for rank, line in enumerate(lines, start=1):
                            print(
                                json.dumps(
                                    {
                                        "schema_version": 2,
                                        "rank": rank,
                                        "source": "sample.log",
                                        "kind": "line",
                                        "line": line,
                                        "score": 1.0 / rank,
                                        "text": "t",
                                        "occurrences": 1,
                                        "mode": "dense",
                                        "dense": 1.0 / rank,
                                        "lexical": None,
                                    },
                                    separators=(",", ":"),
                                )
                            )
                        sys.exit(0)
                    sys.stderr.write("unexpected\\n")
                    sys.exit(2)
                    """
                ),
                encoding="utf-8",
            )
            fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
            out = directory / "report.json"
            workdir = directory / "work"
            first = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--binary",
                    str(fake),
                    "--model-dir",
                    str(directory),
                    "--log",
                    str(log_path),
                    "--structured",
                    str(structured),
                    "--queries",
                    str(queries),
                    "--modes",
                    "dense",
                    "--workdir",
                    str(workdir),
                    "--out",
                    str(out),
                    "--skip-fetch",
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(first.returncode, 0, first.stderr + first.stdout)
            payload = out.read_bytes()
            second = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--binary",
                    str(fake),
                    "--model-dir",
                    str(directory),
                    "--log",
                    str(log_path),
                    "--structured",
                    str(structured),
                    "--queries",
                    str(queries),
                    "--modes",
                    "dense",
                    "--workdir",
                    str(workdir),
                    "--out",
                    str(out),
                    "--skip-fetch",
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(second.returncode, 0, second.stderr + second.stdout)
            self.assertEqual(out.read_bytes(), payload)
            report = json.loads(payload.decode())
            self.assertEqual(report["schema_version"], 1)
            self.assertEqual(report["k"], 10)
            self.assertEqual(report["query_count"], 2)
            mode = report["modes"][0]
            self.assertEqual(mode["mode"], "dense")
            self.assertEqual(mode["overall"]["n"], 2)
            q1 = mode["queries"][0]
            self.assertEqual(q1["id"], "q1")
            self.assertEqual(q1["ranked_event_ids"], ["E10", "E1", "E10"])
            self.assertEqual(q1["metrics"]["recall_at_10"], 1.0)
            self.assertEqual(q1["metrics"]["mrr"], 1.0)

    def test_missing_binary_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            out = directory / "out.json"
            env = os.environ.copy()
            env.pop("NESSO_BINARY", None)
            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--binary",
                    str(directory / "missing"),
                    "--out",
                    str(out),
                    "--skip-fetch",
                    "--log",
                    str(directory / "log"),
                    "--structured",
                    str(directory / "structured.csv"),
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertEqual(completed.returncode, 1)
            self.assertIn("nesso binary not found", completed.stderr)

    def test_bad_query_file_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            queries = directory / "bad.jsonl"
            queries.write_text('{"id":"x"}\n', encoding="utf-8")
            fake = directory / "fake-nesso"
            fake.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--binary",
                    str(fake),
                    "--queries",
                    str(queries),
                    "--out",
                    str(directory / "out.json"),
                    "--skip-fetch",
                    "--log",
                    str(directory / "log"),
                    "--structured",
                    str(directory / "s.csv"),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(completed.returncode, 1)
            self.assertIn("class must be one of", completed.stderr)


if __name__ == "__main__":
    unittest.main()
