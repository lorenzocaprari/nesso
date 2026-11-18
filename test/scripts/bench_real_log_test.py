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
        self.assertEqual(bench.REAL_LOG_DEFAULT_LINES, 1_000_000)
        self.assertEqual(bench.REAL_LOG_THREAD_COUNTS, (1, 8))
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
                    "--real-log-root",
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
            self.assertEqual(payload["lines"], 2)
            self.assertEqual(len(payload["cases"]), len(bench.REAL_LOG_FILES) * len(bench.REAL_LOG_THREAD_COUNTS))
            names = [case["corpus"] for case in payload["cases"]]
            self.assertEqual(names, [name for name in bench.REAL_LOG_FILES for _ in bench.REAL_LOG_THREAD_COUNTS])
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


if __name__ == "__main__":
    unittest.main()
