import http.server
import importlib.machinery
import importlib.util
import json
import math
import os
import pathlib
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
import threading
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "eval-retrieval"

# Frozen schema_version 3 report from the fake-nesso e2e fixture on origin/main.
NESSO_ONLY_GOLDEN = (
    b'{"datasets":[{"dataset":"hdfs-2k","modes":[{"by_class":[{"class":"cause","metrics":'
    b'{"mrr":0.0,"ndcg_at_10":0.0,"recall_at_10":0.0},"n":0},{"class":"exact_id","metrics":'
    b'{"mrr":1.0,"ndcg_at_10":1.0,"recall_at_10":1.0},"n":1},{"class":"paraphrase","metrics":'
    b'{"mrr":1.0,"ndcg_at_10":1.0,"recall_at_10":1.0},"n":1}],"mode":"dense","overall":'
    b'{"metrics":{"mrr":1.0,"ndcg_at_10":1.0,"recall_at_10":1.0},"n":2},"queries":'
    b'[{"class":"exact_id","id":"q1","metrics":{"mrr":1.0,"ndcg_at_10":1.0,"recall_at_10":1.0},'
    b'"ranked_lines":[1,2,3]},{"class":"paraphrase","id":"q2","metrics":'
    b'{"mrr":1.0,"ndcg_at_10":1.0,"recall_at_10":1.0},"ranked_lines":[4,5,2]}]}],'
    b'"query_count":2}],"k":10,"overall":{"modes":[{"by_class":[{"class":"cause","metrics":'
    b'{"mrr":0.0,"ndcg_at_10":0.0,"recall_at_10":0.0},"n":0},{"class":"exact_id","metrics":'
    b'{"mrr":1.0,"ndcg_at_10":1.0,"recall_at_10":1.0},"n":1},{"class":"paraphrase","metrics":'
    b'{"mrr":1.0,"ndcg_at_10":1.0,"recall_at_10":1.0},"n":1}],"mode":"dense","overall":'
    b'{"metrics":{"mrr":1.0,"ndcg_at_10":1.0,"recall_at_10":1.0},"n":2}}],"query_count":2},'
    b'"schema_version":3}\n'
)


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

    def test_single_line_hit(self) -> None:
        metrics = self.mod.score_ranking([7] + [8] * 9, frozenset({7}))
        self.assertEqual(metrics.recall_at_10, 1.0)
        self.assertEqual(metrics.mrr, 1.0)
        self.assertEqual(metrics.ndcg_at_10, 1.0)

    def test_single_line_miss(self) -> None:
        metrics = self.mod.score_ranking([2, 3, 4], frozenset({1}))
        self.assertEqual(metrics.recall_at_10, 0.0)
        self.assertEqual(metrics.mrr, 0.0)
        self.assertEqual(metrics.ndcg_at_10, 0.0)

    def test_template_311_all_in_top_10(self) -> None:
        relevant = frozenset(range(1, 312))
        self.assertEqual(len(relevant), 311)
        metrics = self.mod.score_ranking(list(range(1, 11)), relevant)
        self.assertEqual(metrics.recall_at_10, 1.0)
        self.assertEqual(metrics.mrr, 1.0)
        self.assertEqual(metrics.ndcg_at_10, 1.0)

    def test_template_311_three_of_ten(self) -> None:
        relevant = frozenset(range(1, 312))
        ranked = [1, 2, 3] + [1000] * 7
        metrics = self.mod.score_ranking(ranked, relevant)
        self.assertAlmostEqual(metrics.recall_at_10, 3.0 / 10.0)
        self.assertEqual(metrics.mrr, 1.0)
        dcg = 1.0 / math.log2(2) + 1.0 / math.log2(3) + 1.0 / math.log2(4)
        self.assertAlmostEqual(metrics.ndcg_at_10, dcg / idcg10())

    def test_hit_at_rank_3(self) -> None:
        ranked = [9, 8, 1] + [7] * 7
        metrics = self.mod.score_ranking(ranked, frozenset({1}))
        self.assertEqual(metrics.recall_at_10, 1.0 / min(10, 1))
        self.assertAlmostEqual(metrics.mrr, 1.0 / 3.0)
        ideal = 1.0 / math.log2(2)
        self.assertAlmostEqual(metrics.ndcg_at_10, (1.0 / math.log2(4)) / ideal)


class EvalRetrievalLabelsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_eval()

    def _queries(self, directory: pathlib.Path, body: str) -> pathlib.Path:
        path = directory / "queries.jsonl"
        path.write_text(body, encoding="utf-8")
        return path

    def test_line_id_alignment_requires_matching_row_count(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            log = directory / "sample.log"
            # Last line has no trailing newline; still counts as a line.
            log.write_bytes(b"one\ntwo")
            structured = directory / "structured.csv"
            structured.write_text("LineId,EventId\n1,E1\n", encoding="utf-8")
            mapping = self.mod.load_event_ids(structured)
            with self.assertRaises(SystemExit) as ctx:
                self.mod.assert_line_id_alignment(log, mapping)
            self.assertIn("does not match", str(ctx.exception))

    def test_line_id_alignment_accepts_last_line_without_newline(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            log = directory / "sample.log"
            log.write_bytes(b"one\ntwo")
            structured = directory / "structured.csv"
            structured.write_text("LineId,EventId\n1,E1\n2,E2\n", encoding="utf-8")
            mapping = self.mod.load_event_ids(structured)
            self.mod.assert_line_id_alignment(log, mapping)

    def test_dataset_mapping_resolves_all_2k(self) -> None:
        specs = self.mod.parse_datasets("all-2k")
        self.assertIsNotNone(specs)
        assert specs is not None
        self.assertEqual([spec.name for spec in specs], list(self.mod.DATASETS_2K))
        single = self.mod.parse_datasets("bgl-2k,openssh-2k,bgl-2k")
        assert single is not None
        self.assertEqual([spec.name for spec in single], ["bgl-2k", "openssh-2k"])
        with self.assertRaises(SystemExit):
            self.mod.parse_datasets("bgl")

    def test_wrong_line_label_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            log = directory / "sample.log"
            log.write_text("nothing about the query\n", encoding="utf-8")
            loaded = self.mod.load_queries(
                self._queries(
                    directory,
                    '{"id":"exact_01","class":"exact_id","query":"blk_123","relevant_lines":[1]}\n',
                )
            )
            with self.assertRaises(SystemExit) as ctx:
                self.mod.validate_relevant_lines(loaded, log)
            message = str(ctx.exception)
            self.assertIn("exact_01", message)
            self.assertIn("whole token", message)
            self.assertIn("blk_123", message)

    def test_token_boundary_miss_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            log = directory / "sample.log"
            log.write_text("saw blk_1234 here\n", encoding="utf-8")
            loaded = self.mod.load_queries(
                self._queries(
                    directory,
                    '{"id":"exact_01","class":"exact_id","query":"blk_123","relevant_lines":[1]}\n',
                )
            )
            with self.assertRaises(SystemExit) as ctx:
                self.mod.validate_relevant_lines(loaded, log)
            message = str(ctx.exception)
            self.assertIn("exact_01", message)
            self.assertIn("line 1", message)
            self.assertIn("blk_123", message)
            self.assertIn("whole token", message)

    def test_rejects_both_neither_and_legacy_relevant(self) -> None:
        cases = (
            '{"id":"q","class":"exact_id","query":"a","relevant_lines":[1],"relevant_events":["E1"]}\n',
            '{"id":"q","class":"exact_id","query":"a"}\n',
            '{"id":"q","class":"exact_id","query":"a","relevant":["E1"]}\n',
        )
        for body in cases:
            with self.subTest(body=body):
                with tempfile.TemporaryDirectory() as temp:
                    path = self._queries(pathlib.Path(temp), body)
                    with self.assertRaises(SystemExit) as ctx:
                        self.mod.load_queries(path)
                    message = str(ctx.exception)
                    self.assertTrue(
                        "exactly one" in message or "unknown key 'relevant'" in message,
                        message,
                    )


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
            log_path.write_text("alpha\nb\nc\nd\ne\n", encoding="utf-8")
            queries = directory / "queries.jsonl"
            queries.write_text(
                '{"id":"q1","class":"exact_id","query":"alpha","relevant_lines":[1]}\n'
                '{"id":"q2","class":"paraphrase","query":"beta","relevant_events":["E7"]}\n',
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
            command = [
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
            ]
            first = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(first.returncode, 0, first.stderr + first.stdout)
            payload = out.read_bytes()
            self.assertEqual(payload, NESSO_ONLY_GOLDEN)
            second = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(second.returncode, 0, second.stderr + second.stdout)
            self.assertEqual(out.read_bytes(), payload)
            with_engine = subprocess.run(
                [*command, "--engine", "nesso"],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(with_engine.returncode, 0, with_engine.stderr + with_engine.stdout)
            self.assertEqual(out.read_bytes(), NESSO_ONLY_GOLDEN)
            report = json.loads(payload.decode())
            self.assertEqual(report["schema_version"], 3)
            self.assertEqual(report["k"], 10)
            self.assertEqual(len(report["datasets"]), 1)
            dataset = report["datasets"][0]
            self.assertEqual(dataset["dataset"], "hdfs-2k")
            self.assertEqual(dataset["query_count"], 2)
            self.assertEqual(report["overall"]["query_count"], 2)
            mode = dataset["modes"][0]
            self.assertEqual(mode["mode"], "dense")
            self.assertEqual(mode["overall"]["n"], 2)
            q1 = mode["queries"][0]
            self.assertEqual(q1["id"], "q1")
            self.assertEqual(q1["ranked_lines"], [1, 2, 3])
            self.assertNotIn("ranked_event_ids", q1)
            self.assertEqual(q1["metrics"]["recall_at_10"], 1.0)
            self.assertEqual(q1["metrics"]["mrr"], 1.0)
            q2 = mode["queries"][1]
            self.assertEqual(q2["ranked_lines"], [4, 5, 2])
            self.assertEqual(q2["metrics"]["recall_at_10"], 1.0)
            self.assertEqual(q2["metrics"]["mrr"], 1.0)
            overall_mode = report["overall"]["modes"][0]
            self.assertEqual(overall_mode["mode"], "dense")
            self.assertEqual(overall_mode["overall"]["n"], 2)

    def test_multi_dataset_report_has_overall(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
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
                        print(
                            json.dumps(
                                {
                                    "schema_version": 2,
                                    "rank": 1,
                                    "source": "sample.log",
                                    "kind": "line",
                                    "line": 1,
                                    "score": 1.0,
                                    "text": "t",
                                    "occurrences": 1,
                                    "mode": "dense",
                                    "dense": 1.0,
                                    "lexical": None,
                                },
                                separators=(",", ":"),
                            )
                        )
                        sys.exit(0)
                    sys.exit(2)
                    """
                ),
                encoding="utf-8",
            )
            fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
            mod = load_eval()
            dataset_results = []
            for name in ("hdfs-2k", "bgl-2k"):
                log_path = directory / f"{name}.log"
                structured = directory / f"{name}.csv"
                queries_path = directory / f"{name}.jsonl"
                log_path.write_bytes(b"token_a\ntoken_b")
                structured.write_text("LineId,EventId\n1,E1\n2,E2\n", encoding="utf-8")
                queries_path.write_text(
                    '{"id":"exact_01","class":"exact_id","query":"token_a","relevant_lines":[1]}\n',
                    encoding="utf-8",
                )
                queries = mod.load_queries(queries_path)
                lines = mod.read_log_lines(log_path)
                event_by_line = mod.load_event_ids(structured)
                mod.assert_line_id_alignment(log_path, event_by_line, lines)
                mod.validate_relevant_lines(queries, log_path, lines)
                dataset_results.append(
                    mod.evaluate_dataset(
                        binary=fake,
                        model_dir=directory,
                        queries=queries,
                        event_by_line=event_by_line,
                        log_path=log_path,
                        lines=lines,
                        modes=["dense"],
                        engines=["nesso"],
                        workdir=directory / "work",
                        corpus_name=f"{name.replace('-', '_')}.nesso",
                        dataset_name=name,
                    )
                )
            overall = mod.aggregate_overall(dataset_results, ["dense"])
            payload = {
                "datasets": dataset_results,
                "k": 10,
                "overall": overall,
                "schema_version": 3,
            }
            self.assertEqual(len(payload["datasets"]), 2)
            self.assertEqual(payload["overall"]["query_count"], 2)
            self.assertEqual(payload["overall"]["modes"][0]["overall"]["n"], 2)
            text = mod.format_report(payload)
            self.assertIn("=== hdfs-2k ===", text)
            self.assertIn("=== bgl-2k ===", text)
            self.assertIn("=== overall ===", text)
            encoded = json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n"
            self.assertEqual(
                json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n",
                encoded,
            )

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


class EvalRetrievalEnginesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_eval()

    def test_fake_engines_feed_score_queries(self) -> None:
        mod = self.mod

        class FakeA:
            name = "fake_a"

            def availability(self) -> tuple[bool, str]:
                return True, ""

            def prepare(self, log_path, lines, workdir) -> None:
                return None

            def rank(self, query: str, *, k: int = 10) -> list[int]:
                del k
                return [1, 2] if query == "hit" else [9]

        class FakeB:
            name = "fake_b"

            def availability(self) -> tuple[bool, str]:
                return True, ""

            def prepare(self, log_path, lines, workdir) -> None:
                return None

            def rank(self, query: str, *, k: int = 10) -> list[int]:
                del k
                return [3]

        queries = [
            mod.Query("q1", "exact_id", "hit", frozenset({1}), None),
            mod.Query("q2", "paraphrase", "miss", frozenset({3}), None),
        ]
        event_by_line = {1: "E1", 2: "E2", 3: "E3"}
        scored_a = mod.score_queries(
            queries=queries,
            event_by_line=event_by_line,
            rank_fn=lambda q: FakeA().rank(q),
        )
        self.assertEqual(scored_a["queries"][0]["ranked_lines"], [1, 2])
        self.assertEqual(scored_a["queries"][0]["metrics"]["mrr"], 1.0)
        self.assertEqual(scored_a["queries"][1]["ranked_lines"], [9])
        self.assertEqual(scored_a["queries"][1]["metrics"]["recall_at_10"], 0.0)
        scored_b = mod.score_queries(
            queries=queries,
            event_by_line=event_by_line,
            rank_fn=lambda q: FakeB().rank(q),
        )
        self.assertEqual(scored_b["queries"][1]["ranked_lines"], [3])
        self.assertEqual(scored_b["queries"][1]["metrics"]["mrr"], 1.0)

    def test_fixed_pattern_rank_rules(self) -> None:
        mod = self.mod
        with tempfile.TemporaryDirectory() as temp:
            log_path = pathlib.Path(temp) / "fixture.log"
            log_path.write_text(
                "alpha only\n"
                "bravo charlie\n"
                "FOO marker\n"
                "nothing useful\n"
                "charlie alone\n",
                encoding="utf-8",
            )
            prefixes = [["grep", "-n", "-F", "-i"]]
            if shutil.which("rg") is not None:
                prefixes.append(["rg", "-n", "-F", "-i"])
            for prefix in prefixes:
                with self.subTest(prefix=prefix[0]):
                    exact = mod.fixed_pattern_rank(
                        prefix, log_path, "alpha only", k=10, label=prefix[0]
                    )
                    self.assertEqual(exact, [1])
                    fallback = mod.fixed_pattern_rank(
                        prefix, log_path, "bravo nowhere", k=10, label=prefix[0]
                    )
                    self.assertEqual(fallback, [2])
                    empty = mod.fixed_pattern_rank(
                        prefix, log_path, "zzzz nohit", k=10, label=prefix[0]
                    )
                    self.assertEqual(empty, [])
                    case = mod.fixed_pattern_rank(
                        prefix, log_path, "Foo", k=10, label=prefix[0]
                    )
                    self.assertEqual(case, [3])
                    multi = mod.fixed_pattern_rank(
                        prefix, log_path, "missing charlie", k=10, label=prefix[0]
                    )
                    self.assertEqual(multi, [2, 5])

    def test_bm25_known_order(self) -> None:
        mod = self.mod
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            lines = [
                "alpha alpha alpha",
                "beta rare",
                "rare only here",
                "gamma",
            ]
            engine = mod.Bm25Engine()
            engine.prepare(directory / "log", lines, directory)
            self.assertEqual(engine.rank("rare"), [2, 3])
            self.assertEqual(engine.rank("alpha"), [1])
            self.assertEqual(engine.rank(""), [])

    def test_require_fts5_ok(self) -> None:
        self.mod.require_fts5()

    def test_ripgrep_skipped_when_rg_missing(self) -> None:
        mod = self.mod
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            log_path = directory / "sample.log"
            log_path.write_text("alpha\nbeta\n", encoding="utf-8")
            structured = directory / "structured.csv"
            structured.write_text("LineId,EventId\n1,E1\n2,E2\n", encoding="utf-8")
            queries = directory / "queries.jsonl"
            queries.write_text(
                '{"id":"q1","class":"exact_id","query":"alpha","relevant_lines":[1]}\n',
                encoding="utf-8",
            )
            out = directory / "out.json"
            env = os.environ.copy()
            env["PATH"] = "/usr/bin:/bin"
            # Ensure a PATH without rg even if /usr/bin has it.
            bin_dir = directory / "bin"
            bin_dir.mkdir()
            grep_src = pathlib.Path("/usr/bin/grep")
            if not grep_src.is_file():
                grep_src = pathlib.Path("/bin/grep")
            (bin_dir / "grep").symlink_to(grep_src)
            env["PATH"] = str(bin_dir)
            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--engine",
                    "ripgrep",
                    "--engine",
                    "grep",
                    "--log",
                    str(log_path),
                    "--structured",
                    str(structured),
                    "--queries",
                    str(queries),
                    "--out",
                    str(out),
                    "--workdir",
                    str(directory / "work"),
                    "--skip-fetch",
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr + completed.stdout)
            report = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(report["schema_version"], 4)
            engines = {item["engine"]: item for item in report["datasets"][0]["engines"]}
            self.assertEqual(engines["ripgrep"]["status"], "skipped")
            self.assertIn("rg", engines["ripgrep"]["reason"])
            self.assertNotIn("metrics", engines["ripgrep"])
            self.assertEqual(engines["grep"]["status"], "ok")
            self.assertEqual(engines["grep"]["queries"][0]["ranked_lines"], [1])

    def test_schema4_grep_only_report(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            log_path = directory / "sample.log"
            log_path.write_text("alpha\nbeta\n", encoding="utf-8")
            structured = directory / "structured.csv"
            structured.write_text("LineId,EventId\n1,E1\n2,E2\n", encoding="utf-8")
            queries = directory / "queries.jsonl"
            queries.write_text(
                '{"id":"q1","class":"exact_id","query":"alpha","relevant_lines":[1]}\n',
                encoding="utf-8",
            )
            out = directory / "out.json"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--engine",
                    "grep",
                    "--log",
                    str(log_path),
                    "--structured",
                    str(structured),
                    "--queries",
                    str(queries),
                    "--out",
                    str(out),
                    "--workdir",
                    str(directory / "work"),
                    "--skip-fetch",
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr + completed.stdout)
            report = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(report["schema_version"], 4)
            dataset = report["datasets"][0]
            self.assertIn("engines", dataset)
            self.assertNotIn("modes", dataset)
            self.assertEqual(dataset["engines"][0]["engine"], "grep")
            self.assertIn("grep", completed.stdout)


class EvalRetrievalDiyFaissTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_eval()
        diy_path = ROOT / "bench" / "competitors" / "diy_faiss.py"
        loader = importlib.machinery.SourceFileLoader("nesso_diy_faiss_test", str(diy_path))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        if spec is None:
            raise AssertionError(f"cannot load {diy_path}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[loader.name] = module
        loader.exec_module(module)
        cls.diy = module

    def test_core_import_skips_heavy_deps(self) -> None:
        for name in ("sentence_transformers", "torch", "faiss"):
            self.assertNotIn(name, sys.modules)

    def test_top_k_line_ids_tie_break_and_truncate(self) -> None:
        diy = self.diy
        # Equal top scores: lower line id wins. Then lower scores fill the rest.
        scores = [0.1, 0.9, 0.9, 0.5, 0.2, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.8]
        self.assertEqual(diy.top_k_line_ids(scores, k=3), [2, 3, 12])
        self.assertEqual(diy.top_k_line_ids(scores, k=1), [2])
        self.assertEqual(diy.top_k_line_ids([], k=10), [])

    def test_fake_encoder_index_prepare_and_rank(self) -> None:
        diy = self.diy

        class FakeIndex:
            def __init__(self, matrix: list[list[float]]) -> None:
                self.matrix = matrix

            def search(self, query_vec: list[list[float]], n: int):
                query = query_vec[0]
                scores = [
                    sum(a * b for a, b in zip(row, query, strict=True)) for row in self.matrix
                ]
                order = sorted(range(len(scores)), key=lambda i: (-scores[i], i))[:n]
                return [[scores[i] for i in order]], [[i for i in order]]

        # Lines 0 and 1 are identical embeddings → tie broken by line id.
        line_vecs = {
            "alpha": [1.0, 0.0],
            "alpha twin": [1.0, 0.0],
            "beta": [0.0, 1.0],
            "noise": [0.2, 0.2],
        }

        def encode_fn(texts):
            return [line_vecs.get(text, [0.0, 0.0]) for text in texts]

        engine = diy.DiyFaissEngine(encode_fn=encode_fn, make_index_fn=FakeIndex)
        lines = ["alpha", "alpha twin", "beta", "noise"]
        with tempfile.TemporaryDirectory() as temp:
            engine.prepare(pathlib.Path(temp) / "log", lines, pathlib.Path(temp))
        self.assertEqual(engine.rank("alpha", k=3), [1, 2, 4])
        self.assertEqual(engine.rank("beta", k=2), [3, 4])
        versions = engine.versions()
        self.assertEqual(versions["model_revision"], diy.MODEL_REVISION)
        self.assertEqual(versions["faiss"], "fake")

    def test_diy_faiss_missing_deps_fails_clearly(self) -> None:
        diy = self.diy
        engine = diy.DiyFaissEngine()

        def boom() -> None:
            raise SystemExit(f"error: {diy.INSTALL_HINT}")

        engine._ensure_stack = boom  # type: ignore[method-assign]
        ok, reason = engine.availability()
        self.assertFalse(ok)
        self.assertIn("diy-faiss.hashes.txt", reason)
        self.assertFalse(reason.startswith("error:"))

        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            log_path = directory / "sample.log"
            log_path.write_text("alpha\nbeta\n", encoding="utf-8")
            structured = directory / "structured.csv"
            structured.write_text("LineId,EventId\n1,E1\n2,E2\n", encoding="utf-8")
            queries = directory / "queries.jsonl"
            queries.write_text(
                '{"id":"q1","class":"exact_id","query":"alpha","relevant_lines":[1]}\n',
                encoding="utf-8",
            )
            out = directory / "out.json"
            # Force the loaded diy module's import to fail.
            env = os.environ.copy()
            env["PYTHONPATH"] = str(directory)
            blocker = directory / "sentence_transformers.py"
            blocker.write_text(
                "raise ImportError('blocked for diy-faiss missing-deps test')\n",
                encoding="utf-8",
            )
            # Also block torch/faiss so the stack cannot partially import.
            (directory / "torch.py").write_text(
                "raise ImportError('blocked')\n", encoding="utf-8"
            )
            (directory / "faiss.py").write_text(
                "raise ImportError('blocked')\n", encoding="utf-8"
            )
            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--engine",
                    "diy-faiss",
                    "--log",
                    str(log_path),
                    "--structured",
                    str(structured),
                    "--queries",
                    str(queries),
                    "--out",
                    str(out),
                    "--workdir",
                    str(directory / "work"),
                    "--skip-fetch",
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertNotEqual(completed.returncode, 0, completed.stdout)
            self.assertIn("diy-faiss.hashes.txt", completed.stderr)

    def test_evaluate_keyword_engine_attaches_versions(self) -> None:
        mod = self.mod
        diy = self.diy

        class FakeIndex:
            def __init__(self, matrix) -> None:
                self.matrix = matrix

            def search(self, query_vec, n: int):
                del query_vec
                scores = [1.0] + [0.0] * (len(self.matrix) - 1)
                indices = list(range(len(self.matrix)))
                return [scores[:n]], [indices[:n]]

        engine = diy.DiyFaissEngine(
            encode_fn=lambda texts: [[1.0] for _ in texts],
            make_index_fn=FakeIndex,
        )
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            lines = ["alpha", "beta"]
            payload = mod.evaluate_keyword_engine(
                engine,
                queries=[mod.Query("q1", "exact_id", "alpha", frozenset({1}), None)],
                event_by_line={1: "E1", 2: "E2"},
                log_path=directory / "log",
                lines=lines,
                workdir=directory,
            )
        self.assertEqual(payload["status"], "ok")
        self.assertEqual(payload["engine"], "diy-faiss")
        self.assertIn("versions", payload)
        self.assertEqual(
            payload["versions"]["model_revision"],
            "1110a243fdf4706b3f48f1d95db1a4f5529b4d41",
        )
        self.assertEqual(payload["queries"][0]["ranked_lines"], [1, 2])


class _FakeEsHandler(http.server.BaseHTTPRequestHandler):
    """Minimal Elasticsearch-compatible surface for EsCompatEngine unit tests."""

    server_version = "FakeES/1.0"

    def log_message(self, format: str, *args) -> None:  # noqa: A003
        del format, args

    def _read_json(self) -> object:
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b""
        if not raw:
            return {}
        return json.loads(raw.decode("utf-8"))

    def _send(self, code: int, payload: object | None = None) -> None:
        body = b""
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        state = self.server.state  # type: ignore[attr-defined]
        if path == "/":
            self._send(200, {"version": {"number": state["version"]}})
            return
        if path == "/_cat/plugins":
            self._send(200, [{"component": name} for name in state["plugins"]])
            return
        if path == "/_cluster/health":
            self._send(200, {"status": "green"})
            return
        self._send(404, {"error": "not_found"})

    def do_PUT(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        body = self._read_json()
        self.server.state["indices"].add(path.strip("/"))  # type: ignore[attr-defined]
        self.server.state["put_bodies"][path.strip("/")] = body  # type: ignore[attr-defined]
        self._send(200, {"acknowledged": True})

    def do_DELETE(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        name = path.strip("/")
        self.server.state["indices"].discard(name)  # type: ignore[attr-defined]
        self.server.state["docs"].pop(name, None)  # type: ignore[attr-defined]
        self._send(200, {"acknowledged": True})

    def do_POST(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        parts = [part for part in path.split("/") if part]
        if len(parts) == 2 and parts[1] == "_bulk":
            index = parts[0]
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length).decode("utf-8")
            lines = [line for line in raw.splitlines() if line.strip()]
            docs = self.server.state["docs"].setdefault(index, {})  # type: ignore[attr-defined]
            items = []
            force_error = self.server.state.get("bulk_error")  # type: ignore[attr-defined]
            for index_line, doc_line in zip(lines[0::2], lines[1::2], strict=True):
                meta = json.loads(index_line)["index"]
                doc_id = str(meta["_id"])
                if force_error and doc_id == str(force_error):
                    items.append({"index": {"_id": doc_id, "status": 400, "error": {"type": "bulk", "reason": "forced"}}})
                    continue
                docs[doc_id] = json.loads(doc_line)
                items.append({"index": {"_id": doc_id, "status": 201}})
            errors = any("error" in item["index"] for item in items)
            self._send(200, {"errors": errors, "items": items})
            return
        if len(parts) == 2 and parts[1] in ("_refresh", "_flush"):
            self._read_json()
            self.server.state["calls"].append(parts[1])  # type: ignore[attr-defined]
            self._send(200, {"_shards": {"successful": 1}})
            return
        if len(parts) == 2 and parts[1] == "_search":
            index = parts[0]
            body = self._read_json()
            assert isinstance(body, dict)
            self.server.state["search_bodies"].append(body)  # type: ignore[attr-defined]
            size = int(body.get("size", 10))
            canned = self.server.state.get("search_hits")  # type: ignore[attr-defined]
            if canned is not None:
                hits = canned[:size]
            else:
                docs = self.server.state["docs"].get(index, {})  # type: ignore[attr-defined]
                hits = [
                    {"_id": doc_id, "_score": 1.0, "_source": doc}
                    for doc_id, doc in docs.items()
                ][:size]
            self._send(200, {"hits": {"hits": hits}})
            return
        if path == "/_cluster/health":
            self._send(200, {"status": "green"})
            return
        self._send(404, {"error": "not_found"})


class _FakeEsServer(http.server.ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], handler) -> None:
        super().__init__(address, handler)
        self.state: dict = {
            "indices": set(),
            "docs": {},
            "bulk_error": None,
            "search_hits": None,
            "put_bodies": {},
            "search_bodies": [],
            "calls": [],
            "version": "9.9.9-fake",
            "plugins": [],
        }


class EvalRetrievalEsCompatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_eval()

    def _start_server(self) -> tuple[_FakeEsServer, str, threading.Thread]:
        server = _FakeEsServer(("127.0.0.1", 0), _FakeEsHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address[:2]
        return server, f"http://{host}:{port}", thread

    def tearDown(self) -> None:
        server = getattr(self, "_server", None)
        if server is not None:
            server.shutdown()
            server.server_close()

    def test_line_id_mapping_and_tie_break(self) -> None:
        mod = self.mod
        server, base_url, _thread = self._start_server()
        self._server = server
        # Equal scores: client must order by ascending line id.
        server.state["search_hits"] = [
            {"_id": "5", "_score": 1.0},
            {"_id": "2", "_score": 2.0},
            {"_id": "3", "_score": 1.0},
            {"_id": "1", "_score": 2.0},
        ]
        engine = mod.EsCompatEngine(
            name="xerj",
            base_url=base_url,
            index_name="nesso-eval-xerj",
            query_fn=mod.xerj_match_query,
            mappings=mod.xerj_lexical_mappings(),
            xerj_version="fake-1",
            url_mode="external",
        )
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            lines = ["a", "b", "c", "d", "e"]
            engine.prepare(directory / "log", lines, directory)
            ranked = engine.rank("anything", k=10)
            engine.cleanup()
        self.assertEqual(ranked, [1, 2, 3, 5])

    def test_bulk_item_error_fails(self) -> None:
        mod = self.mod
        server, base_url, _thread = self._start_server()
        self._server = server
        server.state["bulk_error"] = "2"
        engine = mod.EsCompatEngine(
            name="xerj",
            base_url=base_url,
            index_name="nesso-eval-xerj",
            query_fn=mod.xerj_match_query,
            mappings=mod.xerj_lexical_mappings(),
        )
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            with self.assertRaises(SystemExit) as raised:
                engine.prepare(directory / "log", ["one", "two"], directory)
        self.assertIn("bulk", str(raised.exception).lower())

    def test_skipped_without_url_or_binary(self) -> None:
        mod = self.mod
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            log_path = directory / "sample.log"
            log_path.write_text("alpha\nbeta\n", encoding="utf-8")
            structured = directory / "structured.csv"
            structured.write_text("LineId,EventId\n1,E1\n2,E2\n", encoding="utf-8")
            queries = directory / "queries.jsonl"
            queries.write_text(
                '{"id":"q1","class":"exact_id","query":"alpha","relevant_lines":[1]}\n',
                encoding="utf-8",
            )
            out = directory / "out.json"
            env = os.environ.copy()
            env["PATH"] = str(directory / "empty-bin")
            (directory / "empty-bin").mkdir()
            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--engine",
                    "xerj",
                    "--engine",
                    "xerj-neural",
                    "--log",
                    str(log_path),
                    "--structured",
                    str(structured),
                    "--queries",
                    str(queries),
                    "--out",
                    str(out),
                    "--workdir",
                    str(directory / "work"),
                    "--skip-fetch",
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr + completed.stdout)
            report = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(report["schema_version"], 4)
            engines = {item["engine"]: item for item in report["datasets"][0]["engines"]}
            self.assertEqual(engines["xerj"]["status"], "skipped")
            self.assertIn("xerj", engines["xerj"]["reason"])
            self.assertNotIn("metrics", engines["xerj"])
            self.assertEqual(engines["xerj-neural"]["status"], "skipped")
            self.assertNotIn("metrics", engines["xerj-neural"])

    def test_neural_skipped_when_model_absent(self) -> None:
        mod = self.mod
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            fake_bin = directory / "xerj"
            fake_bin.write_text(
                "#!/bin/sh\necho 'xerj fake' >&2\nexit 1\n",
                encoding="utf-8",
            )
            fake_bin.chmod(fake_bin.stat().st_mode | stat.S_IXUSR)
            ctx = mod.open_xerj_context(
                engines=["xerj-neural"],
                xerj_url=None,
                xerj_bin=str(fake_bin),
                xerj_model_dir=str(directory / "missing-model"),
            )
            try:
                engine = mod.make_xerj_engine(ctx, neural=True)
                ok, reason = engine.availability()
                self.assertFalse(ok)
                self.assertIn("model", reason.lower())
            finally:
                ctx.shutdown()

    def test_schema4_xerj_versions_shape(self) -> None:
        mod = self.mod
        server, base_url, _thread = self._start_server()
        self._server = server
        server.state["search_hits"] = [{"_id": "1", "_score": 1.0}]
        ctx = mod.XerjContext(
            url=base_url,
            url_mode="external",
            version="fake-xerj",
        )
        engine = mod.make_xerj_engine(ctx, neural=False)
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            payload = mod.evaluate_keyword_engine(
                engine,
                queries=[mod.Query("q1", "exact_id", "alpha", frozenset({1}), None)],
                event_by_line={1: "E1"},
                log_path=directory / "log",
                lines=["alpha"],
                workdir=directory,
            )
        self.assertEqual(payload["status"], "ok")
        self.assertEqual(payload["engine"], "xerj")
        self.assertIn("versions", payload)
        self.assertEqual(payload["versions"]["url_mode"], "external")
        self.assertEqual(payload["versions"]["xerj_version"], "fake-xerj")
        self.assertIn("index_seconds", payload["versions"])
        # OpenSearch generalization must not add keys to the xerj report.
        self.assertEqual(
            set(payload["versions"]),
            {"index_seconds", "url_mode", "xerj_version"},
        )
        self.assertEqual(payload["queries"][0]["ranked_lines"], [1])
        report = {
            "datasets": [
                {
                    "dataset": "hdfs-2k",
                    "engines": [payload],
                    "query_count": 1,
                }
            ],
            "k": 10,
            "overall": {"engines": [payload], "query_count": 1},
            "schema_version": 4,
        }
        text = mod.format_report(report)
        self.assertIn("xerj_version=fake-xerj", text)
        self.assertIn("url_mode=external", text)

    def test_nesso_only_golden_unchanged(self) -> None:
        # Re-assert the frozen schema-3 bytes still match after xerj wiring.
        self.assertTrue(NESSO_ONLY_GOLDEN.endswith(b'"schema_version":3}\n'))


def _run_eval_cli(directory: pathlib.Path, extra: list[str], env: dict[str, str]) -> tuple[
    subprocess.CompletedProcess[str], pathlib.Path
]:
    log_path = directory / "sample.log"
    log_path.write_text("alpha\nbeta\n", encoding="utf-8")
    structured = directory / "structured.csv"
    structured.write_text("LineId,EventId\n1,E1\n2,E2\n", encoding="utf-8")
    queries = directory / "queries.jsonl"
    queries.write_text(
        '{"id":"q1","class":"exact_id","query":"alpha","relevant_lines":[1]}\n',
        encoding="utf-8",
    )
    out = directory / "out.json"
    completed = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            *extra,
            "--log",
            str(log_path),
            "--structured",
            str(structured),
            "--queries",
            str(queries),
            "--out",
            str(out),
            "--workdir",
            str(directory / "work"),
            "--skip-fetch",
        ],
        capture_output=True,
        text=True,
        check=False,
        env=env,
    )
    return completed, out


def _env_without_opensearch() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("OPENSEARCH_HOME", None)
    return env


class EvalRetrievalOpenSearchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_eval()

    def _start_server(self) -> tuple[_FakeEsServer, str]:
        server = _FakeEsServer(("127.0.0.1", 0), _FakeEsHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self._server = server
        host, port = server.server_address[:2]
        return server, f"http://{host}:{port}"

    def tearDown(self) -> None:
        server = getattr(self, "_server", None)
        if server is not None:
            server.shutdown()
            server.server_close()

    def test_external_probe_and_lexical_engine(self) -> None:
        mod = self.mod
        server, base_url = self._start_server()
        server.state["search_hits"] = [
            {"_id": "2", "_score": 1.0},
            {"_id": "1", "_score": 1.0},
        ]
        ctx = mod.open_opensearch_context(
            engines=["opensearch", "opensearch-knn"],
            opensearch_url=base_url + "/",
            opensearch_home=None,
        )
        self.assertEqual(ctx.version, "9.9.9-fake")
        self.assertEqual(ctx.url_mode, "external")
        # No k-NN plugin on the fake node: the k-NN engine is skipped, not zeroed.
        skipped = mod.make_opensearch_engine(ctx, knn=True, encoder=lambda texts: [])
        ok, reason = skipped.availability()
        self.assertFalse(ok)
        self.assertIn("opensearch-knn", reason)

        engine = mod.make_opensearch_engine(ctx, knn=False)
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            payload = mod.evaluate_keyword_engine(
                engine,
                queries=[mod.Query("q1", "exact_id", "alpha", frozenset({1}), None)],
                event_by_line={1: "E1", 2: "E2"},
                log_path=directory / "log",
                lines=["alpha", "beta"],
                workdir=directory,
            )
        put = server.state["put_bodies"][mod.OPENSEARCH_INDEX_LEXICAL]
        self.assertEqual(put["settings"]["index"]["number_of_shards"], 1)
        self.assertEqual(put["settings"]["index"]["number_of_replicas"], 0)
        self.assertEqual(
            server.state["search_bodies"][0],
            {"size": 10, "_source": False, "query": {"match": {"text": "alpha"}}},
        )
        self.assertEqual(payload["status"], "ok")
        self.assertEqual(payload["queries"][0]["ranked_lines"], [1, 2])
        versions = payload["versions"]
        self.assertEqual(versions["opensearch_version"], "9.9.9-fake")
        self.assertEqual(versions["java_opts"], "external")
        self.assertNotIn("xerj_version", versions)
        report = {
            "datasets": [{"dataset": "hdfs-2k", "engines": [payload], "query_count": 1}],
            "k": 10,
            "overall": {"engines": [payload], "query_count": 1},
            "schema_version": 4,
        }
        text = mod.format_report(report)
        self.assertIn("opensearch_version=9.9.9-fake", text)
        self.assertNotIn("xerj_version", text)

    def test_knn_engine_sends_vectors(self) -> None:
        mod = self.mod
        server, base_url = self._start_server()
        server.state["plugins"] = ["opensearch-knn"]
        server.state["search_hits"] = [{"_id": "2", "_score": 0.9}]
        ctx = mod.open_opensearch_context(
            engines=["opensearch-knn"],
            opensearch_url=base_url,
            opensearch_home=None,
        )
        self.assertIsNone(ctx.knn_skip_reason)

        def encoder(texts: list[str]) -> list[list[float]]:
            return [[float(len(text)), 0.5] for text in texts]

        engine = mod.make_opensearch_engine(ctx, knn=True, encoder=encoder)
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            engine.prepare(directory / "log", ["a", "bbb"], directory)
            docs = dict(server.state["docs"][mod.OPENSEARCH_INDEX_KNN])
            ranked = engine.rank("cc", k=5)
            engine.flush()
            versions = engine.versions()
            engine.cleanup()
        self.assertEqual(ranked, [2])
        put = server.state["put_bodies"][mod.OPENSEARCH_INDEX_KNN]
        self.assertTrue(put["settings"]["index"]["knn"])
        vec_mapping = put["mappings"]["properties"]["vec"]
        self.assertEqual(vec_mapping["type"], "knn_vector")
        self.assertEqual(vec_mapping["dimension"], 384)
        self.assertEqual(vec_mapping["method"]["name"], "hnsw")
        self.assertEqual(docs["2"], {"line": 2, "text": "bbb", "vec": [3.0, 0.5]})
        # cleanup() drops the index.
        self.assertNotIn(mod.OPENSEARCH_INDEX_KNN, server.state["docs"])
        self.assertEqual(
            server.state["search_bodies"][0],
            {
                "size": 5,
                "_source": False,
                "query": {"knn": {"vec": {"vector": [2.0, 0.5], "k": 5}}},
            },
        )
        self.assertIn("_flush", server.state["calls"])
        self.assertEqual(versions["knn_method"], "hnsw/lucene/cosinesimil")
        self.assertEqual(versions["model_revision"], "1110a243fdf4706b3f48f1d95db1a4f5529b4d41")

    def test_knn_bulk_documents_carry_vectors(self) -> None:
        mod = self.mod
        ctx = mod.OpenSearchContext(url="http://127.0.0.1:1", version="x")
        engine = mod.make_opensearch_engine(
            ctx,
            knn=True,
            encoder=lambda texts: [[1.0] for _ in texts],
        )
        docs = engine._bulk_docs(7, ["x", "y"])
        self.assertEqual(
            docs,
            [
                {"line": 7, "text": "x", "vec": [1.0]},
                {"line": 8, "text": "y", "vec": [1.0]},
            ],
        )
        short = mod.make_opensearch_engine(ctx, knn=True, encoder=lambda texts: [])
        with self.assertRaises(SystemExit) as raised:
            short._bulk_docs(1, ["x"])
        self.assertIn("0 vectors", str(raised.exception))

    def test_skipped_without_home_or_url(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            completed, out = _run_eval_cli(
                directory,
                ["--engine", "opensearch", "--engine", "opensearch-knn"],
                _env_without_opensearch(),
            )
            self.assertEqual(completed.returncode, 0, completed.stderr + completed.stdout)
            report = json.loads(out.read_text(encoding="utf-8"))
        engines = {item["engine"]: item for item in report["datasets"][0]["engines"]}
        for name in ("opensearch", "opensearch-knn"):
            self.assertEqual(engines[name]["status"], "skipped")
            self.assertIn("OpenSearch not configured", engines[name]["reason"])
            self.assertNotIn("metrics", engines[name])

    def test_spawn_failure_reports_log_tail(self) -> None:
        mod = self.mod
        with tempfile.TemporaryDirectory() as temp:
            home = pathlib.Path(temp) / "opensearch"
            (home / "bin").mkdir(parents=True)
            launcher = home / "bin" / "opensearch"
            launcher.write_text(
                "#!/bin/sh\necho \"fake opensearch: $OPENSEARCH_JAVA_OPTS\"\nexit 3\n",
                encoding="utf-8",
            )
            launcher.chmod(launcher.stat().st_mode | stat.S_IXUSR)
            with self.assertRaises(SystemExit) as raised:
                mod.open_opensearch_context(
                    engines=["opensearch"],
                    opensearch_url=None,
                    opensearch_home=str(home),
                )
        message = str(raised.exception)
        self.assertIn("OpenSearch exited with code 3", message)
        self.assertIn(f"fake opensearch: {mod.opensearch_java_opts()}", message)

    def test_missing_launcher_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(SystemExit) as raised:
                self.mod.resolve_opensearch_home(temp)
        self.assertIn("launcher not found", str(raised.exception))

    def test_version_from_home_reads_jar_name(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            home = pathlib.Path(temp)
            (home / "lib").mkdir()
            (home / "lib" / "opensearch-core-3.1.0.jar").write_bytes(b"")
            self.assertEqual(self.mod.opensearch_version_from_home(home), "unknown")
            (home / "lib" / "opensearch-3.1.0.jar").write_bytes(b"")
            self.assertEqual(self.mod.opensearch_version_from_home(home), "3.1.0")

    def test_url_must_be_loopback(self) -> None:
        with self.assertRaises(SystemExit) as raised:
            self.mod.assert_loopback_url("http://example.com:9200", flag="--opensearch-url")
        self.assertIn("--opensearch-url", str(raised.exception))

    def test_measure_refuses_external_url(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            completed, _out = _run_eval_cli(
                pathlib.Path(temp),
                [
                    "--engine",
                    "opensearch",
                    "--measure",
                    "--opensearch-url",
                    "http://127.0.0.1:9200",
                ],
                _env_without_opensearch(),
            )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("not --opensearch-url", completed.stderr)


class EvalRetrievalMeasureStatsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_eval()

    def test_median_odd_and_even(self) -> None:
        self.assertEqual(self.mod.median_value([1.0, 2.0, 3.0]), 2.0)
        self.assertEqual(self.mod.median_value([1.0, 2.0, 3.0, 4.0]), 2.5)
        self.assertIsNone(self.mod.median_value([]))

    def test_percentile_p95_nearest_rank(self) -> None:
        # ceil(0.95 * 20) - 1 == 18 → value 19 for 1..20
        self.assertEqual(self.mod.percentile_p95([float(x) for x in range(1, 21)]), 19.0)
        self.assertEqual(self.mod.percentile_p95([1.0]), 1.0)
        self.assertIsNone(self.mod.percentile_p95([]))

    def test_index_to_log_ratio_and_none(self) -> None:
        self.assertEqual(self.mod.index_to_log_ratio(200, 100), 2.0)
        self.assertIsNone(self.mod.index_to_log_ratio(None, 100))
        self.assertEqual(self.mod.format_measure_none(None), "none")
        self.assertEqual(self.mod.format_measure_none(1.5), "1.500000")

    def test_no_index_fields_are_null(self) -> None:
        fields = self.mod.no_index_fields(100)
        self.assertIsNone(fields["indexWallSeconds"])
        self.assertIsNone(fields["indexPeakRssBytes"])
        self.assertIsNone(fields["indexBytes"])
        self.assertIsNone(fields["indexToLogRatio"])
        table = self.mod.format_measure_table(
            {
                "name": "hdfs-2k",
                "engines": [
                    {
                        "engine": "grep",
                        "status": "ok",
                        **fields,
                        "queryWallSeconds": 0.1,
                        "queryPeakRssBytes": 1024,
                        "queryLatencyMedianSeconds": 0.01,
                        "queryLatencyP95Seconds": 0.02,
                        "timeToFirstResultSeconds": 0.05,
                    }
                ],
            }
        )
        self.assertIn("none", table)


@unittest.skipUnless(
    pathlib.Path(os.environ.get("NESSO_BENCH_TIME", "/usr/bin/time")).is_file(),
    "GNU time required",
)
class EvalRetrievalMeasureRssWrapperTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_eval()

    def test_execute_timed_fake_child(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            time_path = directory / "time.txt"
            result = self.mod.execute_timed(
                [sys.executable, "-c", "print('worker-ok')"],
                time_path,
            )
        self.assertEqual(result["exitCode"], 0)
        self.assertIn("worker-ok", result["stdout"])
        self.assertIsInstance(result["wallSeconds"], float)
        self.assertGreaterEqual(result["wallSeconds"], 0.0)
        self.assertIsInstance(result["peakRssBytes"], int)
        self.assertGreater(result["peakRssBytes"], 0)

    def test_measure_worker_bm25_index_json(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            log_path = directory / "log.txt"
            log_path.write_text("alpha one\nbeta two\ngamma three\n", encoding="utf-8")
            workdir = directory / "work"
            payload = self.mod.run_measure_worker(
                engine="bm25",
                phase="index",
                log_path=log_path,
                workdir=workdir,
                queries=[],
            )
        self.assertIsInstance(payload["wallSeconds"], float)
        self.assertIsInstance(payload["peakRssBytes"], int)
        self.assertGreater(payload["indexBytes"], 0)
        self.assertEqual(payload["latencySeconds"], [])


@unittest.skipUnless(os.environ.get("NESSO_EVAL_MEASURE") == "1", "set NESSO_EVAL_MEASURE=1")
class EvalRetrievalMeasureLiveTest(unittest.TestCase):
    def test_measure_hdfs_2k_smoke(self) -> None:
        mod = load_eval()
        out = pathlib.Path(tempfile.mkdtemp()) / "measure.json"
        # Prefer a real binary when present; otherwise skip.
        try:
            binary = mod.resolve_binary(None)
        except SystemExit as exc:
            self.skipTest(str(exc))
        log_path = ROOT / "build" / "bench" / "HDFS_2k.log"
        structured = ROOT / "build" / "bench" / "HDFS_2k.log_structured.csv"
        if not log_path.is_file() or not structured.is_file():
            self.skipTest("HDFS_2k fixtures missing; run scripts/fetch-loghub --dataset hdfs-2k")
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--dataset",
                "hdfs-2k",
                "--engine",
                "nesso",
                "--engine",
                "grep",
                "--modes",
                "dense",
                "--binary",
                str(binary),
                "--measure",
                "--repeat",
                "1",
                "--skip-fetch",
                "--out",
                str(out),
                "--workdir",
                str(out.parent / "work"),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        payload = json.loads(out.read_text(encoding="utf-8"))
        self.assertIn("measure", payload)
        measure = payload["measure"]
        self.assertEqual(measure["repeat"], 1)
        self.assertIn("cpuModel", measure)
        self.assertIn("tool", measure)
        engines = {row["engine"]: row for row in measure["datasets"][0]["engines"]}
        self.assertIn("nesso", engines)
        self.assertIn("grep", engines)
        self.assertIsNone(engines["grep"]["indexBytes"])
        self.assertIsNotNone(engines["nesso"]["indexBytes"])


@unittest.skipUnless(os.environ.get("NESSO_EVAL_DIY") == "1", "set NESSO_EVAL_DIY=1")
class EvalRetrievalDiyFaissLiveTest(unittest.TestCase):
    def test_real_stack_tiny_fixture(self) -> None:
        diy_path = ROOT / "bench" / "competitors" / "diy_faiss.py"
        loader = importlib.machinery.SourceFileLoader("nesso_diy_faiss_live", str(diy_path))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        if spec is None:
            raise AssertionError(f"cannot load {diy_path}")
        diy = importlib.util.module_from_spec(spec)
        sys.modules[loader.name] = diy
        loader.exec_module(diy)

        engine = diy.DiyFaissEngine()
        ok, reason = engine.availability()
        self.assertTrue(ok, reason)
        lines = [
            "connection refused on port 22",
            "disk space low on /var",
            "ssh authentication failure for user root",
            "unrelated kernel message",
        ]
        with tempfile.TemporaryDirectory() as temp:
            engine.prepare(pathlib.Path(temp) / "log", lines, pathlib.Path(temp))
            ranked = engine.rank("ssh login failed", k=2)
            versions = engine.versions()
        self.assertEqual(len(ranked), 2)
        self.assertTrue(all(1 <= line_id <= len(lines) for line_id in ranked))
        # Semantic neighbor for the ssh line should beat the disk-space line.
        self.assertIn(3, ranked)
        self.assertEqual(versions["model_revision"], diy.MODEL_REVISION)
        self.assertTrue(versions["sentence_transformers"])
        self.assertTrue(versions["torch"])
        self.assertTrue(versions["faiss"])


if __name__ == "__main__":
    unittest.main()
