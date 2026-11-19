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


if __name__ == "__main__":
    unittest.main()
