import importlib.machinery
import importlib.util
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "bench"
STUB = textwrap.dedent(
    """\
    #!/usr/bin/env python3
    import sys
    from pathlib import Path
    args = sys.argv[1:]
    if "-o" in args:
        Path(args[args.index("-o") + 1]).write_bytes(b"nesso-corpus")
    sys.stderr.write('{"peakRssBytes": 1, "stages": [{"name": "parse", "us": 3}]}\\n')
    sys.stdout.write("match\\n")
    """
)
FAKE_TIME = textwrap.dedent(
    """\
    #!/usr/bin/env python3
    import subprocess
    import sys
    args = sys.argv[1:]
    out = None
    index = 0
    while index < len(args):
        if args[index] == "-o":
            out = args[index + 1]
            index += 2
            continue
        if args[index] == "-f":
            index += 2
            continue
        break
    completed = subprocess.run(args[index:], check=False)
    if out is not None:
        with open(out, "w", encoding="utf-8") as handle:
            handle.write("0.05 2048\\n")
    raise SystemExit(completed.returncode)
    """
)


def load_bench():
    loader = importlib.machinery.SourceFileLoader("nesso_bench_real_log", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class BenchRealLogTest(unittest.TestCase):
    def test_prefix_does_not_repeat_a_short_file(self) -> None:
        bench = load_bench()
        self.assertEqual(set(bench.REAL_LOG_DEFAULT_LINES), set(bench.REAL_LOG_FILES))
        self.assertEqual(bench.REAL_LOG_DEFAULT_LINES["hdfs-v1"], bench.REAL_LOG_FETCH_LINES)
        for count in bench.REAL_LOG_DEFAULT_LINES.values():
            self.assertGreater(count, 0)
            self.assertLessEqual(count, bench.REAL_LOG_FETCH_LINES)
        self.assertEqual(bench.REAL_LOG_THREAD_COUNTS, (1, 8))
        self.assertEqual(bench.REAL_LOG_DEFAULT_CORPORA, ("hdfs-v1", "thunderbird"))
        self.assertEqual(bench.parse_real_log_corpora(None), ("hdfs-v1", "thunderbird"))
        self.assertEqual(bench.parse_real_log_corpora("all"), tuple(bench.REAL_LOG_FILES))
        self.assertEqual(bench.parse_real_log_corpora("thunderbird"), ("thunderbird",))
        with self.assertRaises(SystemExit):
            bench.parse_real_log_corpora("nope")

        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            source = directory / "HDFS_1m.log"
            source.write_bytes(b"one\ntwo\nthree\nfour\n")
            dest_dir = directory / "work"
            dest_dir.mkdir()
            path, size = bench.prepare_real_log(source, dest_dir, 2)
            self.assertEqual(path.read_bytes(), b"one\ntwo\n")
            self.assertEqual(size, len(b"one\ntwo\n"))
            self.assertNotEqual(path, source)
            exact, exact_size = bench.prepare_real_log(source, dest_dir, 4)
            self.assertEqual(exact, source)
            self.assertEqual(exact_size, source.stat().st_size)
            with self.assertRaises(SystemExit) as rejected:
                bench.prepare_real_log(source, dest_dir, 5)
            self.assertIn("need 5", str(rejected.exception))

    def test_real_log_json_records_each_command(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            root = directory / "logs"
            root.mkdir()
            for name in bench.REAL_LOG_FILES.values():
                (root / name).write_text("alpha\nbeta\n", encoding="utf-8")
            model = directory / "model"
            model.mkdir()
            (model / "model.onnx").write_bytes(b"x")
            (model / "vocab.txt").write_bytes(b"x")
            binary = directory / "nesso"
            binary.write_text(STUB, encoding="utf-8")
            binary.chmod(binary.stat().st_mode | stat.S_IEXEC)
            timer = directory / "time"
            timer.write_text(FAKE_TIME, encoding="utf-8")
            timer.chmod(timer.stat().st_mode | stat.S_IEXEC)
            output = directory / "out.json"
            env = os.environ.copy()
            env.pop("GITHUB_SHA", None)
            env["NESSO_BENCH_TIME"] = str(timer)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--dataset",
                    "real-log",
                    "--lines",
                    "2",
                    "--loghub-dir",
                    str(root),
                    "--binary",
                    str(binary),
                    "--model-dir",
                    str(model),
                    "--output",
                    str(output),
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            payload = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(payload["dataset"], "real-log")
            self.assertEqual(payload["lines"], {name: 2 for name in bench.REAL_LOG_DEFAULT_CORPORA})
            default = bench.REAL_LOG_DEFAULT_CORPORA
            self.assertEqual(len(payload["cases"]), len(default) * len(bench.REAL_LOG_THREAD_COUNTS))
            names = [case["corpus"] for case in payload["cases"]]
            self.assertEqual(names, [name for name in default for _ in bench.REAL_LOG_THREAD_COUNTS])
            threads = {case["threads"] for case in payload["cases"]}
            self.assertEqual(threads, {"1", "8"})
            case = payload["cases"][0]
            self.assertEqual(case["query"], bench.REAL_LOG_QUERIES[case["corpus"]])
            self.assertEqual(case["inputBytes"], len(b"alpha\nbeta\n"))
            commands = [(command["name"], command["mode"]) for command in case["commands"]]
            self.assertEqual(
                commands,
                [("index", None), ("grep", None), ("search", "auto"), ("search", "dense")],
            )
            for command in case["commands"]:
                self.assertTrue(command["ok"])
                self.assertEqual(command["wallSeconds"], 0.05)
                self.assertEqual(command["peakRssBytes"], 2048 * 1024)
                self.assertEqual(command["lines"], 2)
                self.assertEqual(command["linesPerSecond"], round(2 / 0.05, 6))
                self.assertEqual(command["corpusBytes"], len(b"nesso-corpus"))
                self.assertEqual(command["stages"], [{"name": "parse", "us": 3}])
            self.assertIn("lines/s=", result.stdout)
            self.assertIn("mode=dense", result.stdout)

            all_out = directory / "all.json"
            all_result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--dataset",
                    "real-log",
                    "--corpora",
                    "all",
                    "--lines",
                    "2",
                    "--loghub-dir",
                    str(root),
                    "--binary",
                    str(binary),
                    "--model-dir",
                    str(model),
                    "--output",
                    str(all_out),
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertEqual(all_result.returncode, 0, all_result.stderr + all_result.stdout)
            all_payload = json.loads(all_out.read_text(encoding="utf-8"))
            self.assertEqual(
                len(all_payload["cases"]),
                len(bench.REAL_LOG_FILES) * len(bench.REAL_LOG_THREAD_COUNTS),
            )
    def test_real_log_uses_each_corpus_default_without_lines(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            root = directory / "logs"
            root.mkdir()
            longest = max(bench.REAL_LOG_DEFAULT_LINES.values())
            for name in bench.REAL_LOG_FILES.values():
                (root / name).write_bytes(b"a\n" * (longest + 1))
            model = directory / "model"
            model.mkdir()
            (model / "model.onnx").write_bytes(b"x")
            (model / "vocab.txt").write_bytes(b"x")
            binary = directory / "nesso"
            binary.write_text(STUB, encoding="utf-8")
            binary.chmod(binary.stat().st_mode | stat.S_IEXEC)
            timer = directory / "time"
            timer.write_text(FAKE_TIME, encoding="utf-8")
            timer.chmod(timer.stat().st_mode | stat.S_IEXEC)
            output = directory / "out.json"
            env = os.environ.copy()
            env.pop("GITHUB_SHA", None)
            env["NESSO_BENCH_TIME"] = str(timer)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--dataset",
                    "real-log",
                    "--corpora",
                    "all",
                    "--loghub-dir",
                    str(root),
                    "--binary",
                    str(binary),
                    "--model-dir",
                    str(model),
                    "--output",
                    str(output),
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            payload = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(payload["lines"], bench.REAL_LOG_DEFAULT_LINES)
            for case in payload["cases"]:
                want = bench.REAL_LOG_DEFAULT_LINES[case["corpus"]]
                self.assertEqual(case["lines"], want)
                self.assertEqual(case["inputBytes"], 2 * want)
                for command in case["commands"]:
                    self.assertEqual(command["lines"], want)

    def test_missing_log_names_the_nesso_bench_command(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            (root / bench.REAL_LOG_FILES["hdfs-v1"]).write_text("alpha\n", encoding="utf-8")
            self.assertEqual(bench.ensure_real_log_root(root, ("hdfs-v1",)), root)
            with self.assertRaises(SystemExit) as missing:
                bench.ensure_real_log_root(root, ("hdfs-v1", "thunderbird"))
            message = str(missing.exception)
            self.assertIn(bench.REAL_LOG_FILES["thunderbird"], message)
            self.assertNotIn(bench.REAL_LOG_FILES["hdfs-v1"], message)
            self.assertIn(f"scripts/fetch-loghub --dataset thunderbird --dest-dir {root}", message)

    def test_workflows_pin_the_same_nesso_bench_commit(self) -> None:
        workflows = ROOT / ".github" / "workflows"
        pins = {}
        for name in ("bench-real-log.yml", "eval-retrieval.yml", "ci.yml"):
            text = (workflows / name).read_text(encoding="utf-8")
            found = [line.split(":", 1)[1].strip() for line in text.splitlines() if line.strip().startswith("NESSO_BENCH_REF:")]
            self.assertEqual(len(found), 1, name)
            pins[name] = found[0]
        self.assertEqual(len(set(pins.values())), 1, pins)
        self.assertRegex(pins["bench-real-log.yml"], r"^[0-9a-f]{40}$")

    def test_released_nesso_input_is_manual_only_and_not_inlined(self) -> None:
        workflows = ROOT / ".github" / "workflows"
        for name in ("bench-real-log.yml", "eval-retrieval.yml"):
            text = (workflows / name).read_text(encoding="utf-8")
            self.assertIn("nesso_version:", text)
            self.assertIn("scripts/fetch-released-nesso", text)
            self.assertIn("inputs.nesso_version || ''", text)
            for line in text.splitlines():
                if "inputs.nesso_version" in line:
                    self.assertIn("NESSO_VERSION:", line)
                    self.assertNotRegex(line, r"\brun:")
        ci = (workflows / "ci.yml").read_text(encoding="utf-8")
        self.assertNotIn("nesso_version", ci)
        self.assertNotIn("fetch-released-nesso", ci)


if __name__ == "__main__":
    unittest.main()
