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
    Path(args[args.index("-o") + 1]).write_bytes(b"nesso-corpus")
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
    loader = importlib.machinery.SourceFileLoader("nesso_bench", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class BenchHdfsTest(unittest.TestCase):
    def test_repeat_cycles_the_sample_to_n_lines(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            source = directory / "sample.log"
            source.write_bytes(b"one\ntwo")
            dest = directory / "out.log"
            written, nbytes = bench.repeat_lines(source, dest, 5)
            text = "one\ntwo\none\ntwo\none\n"
            self.assertEqual(written, 5)
            self.assertEqual(dest.read_text(encoding="utf-8"), text)
            self.assertEqual(nbytes, len(text.encode()))
            prefix = directory / "prefix.log"
            self.assertEqual(bench.repeat_lines(source, prefix, 1), (1, len(b"one\n")))
            self.assertEqual(prefix.read_text(encoding="utf-8"), "one\n")

    def test_repeat_rejects_non_positive_and_empty_samples(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            source = directory / "sample.log"
            source.write_text("one\n", encoding="utf-8")
            dest = directory / "out.log"
            with self.assertRaises(SystemExit) as rejected:
                bench.repeat_lines(source, dest, 0)
            self.assertIn("positive", str(rejected.exception))
            source.write_text("", encoding="utf-8")
            with self.assertRaises(SystemExit) as empty:
                bench.repeat_lines(source, dest, 3)
            self.assertIn("no lines", str(empty.exception))

    def test_hdfs_output_fields(self) -> None:
        bench = load_bench()
        self.assertEqual(bench.DEFAULT_SIZES, (1000, 10_000, 100_000))
        self.assertEqual(bench.HDFS_DEFAULT_LINES, 100_000)
        self.assertEqual(bench.gnu_time_binary(), "/usr/bin/time")
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            sample = directory / "sample.log"
            sample.write_text("alpha\nbeta\n", encoding="utf-8")
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
            env["NESSO_THREADS"] = "3"
            env["NESSO_BENCH_TIME"] = str(timer)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--dataset",
                    "hdfs",
                    "--lines",
                    "5",
                    "--sample",
                    str(sample),
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
            text = output.read_text(encoding="utf-8")
            self.assertTrue(text.endswith("\n"))
            payload = json.loads(text)
            self.assertEqual(text, json.dumps(payload, indent=2) + "\n")
            sha = subprocess.check_output(
                ["git", "-c", "safe.directory=*", "-C", str(ROOT), "rev-parse", "HEAD"],
                text=True,
            ).strip()
            cpu = bench.parse_cpu_model(pathlib.Path("/proc/cpuinfo").read_text(encoding="utf-8"))
            nproc = int(subprocess.check_output(["nproc"], text=True).strip())
            expected_input = "".join(("alpha\n" if index % 2 == 0 else "beta\n") for index in range(5))
            case = payload["cases"][0]
            self.assertEqual(payload["sha"], sha)
            self.assertEqual(payload["binary"], str(binary))
            self.assertEqual(payload["dataset"], "hdfs")
            self.assertEqual(payload["cpuModel"], cpu)
            self.assertEqual(payload["nproc"], nproc)
            self.assertEqual(payload["threads"], "3")
            self.assertEqual(case["lines"], 5)
            self.assertTrue(case["ok"])
            self.assertEqual(case["exitCode"], 0)
            self.assertEqual(case["wallSeconds"], 0.05)
            self.assertEqual(case["peakRssBytes"], 2048 * 1024)
            self.assertEqual(case["linesPerSecond"], round(5 / 0.05, 6))
            self.assertEqual(case["inputBytes"], len(expected_input.encode()))
            self.assertEqual(case["corpusBytes"], len(b"nesso-corpus"))
            self.assertEqual(
                case["corpusToInputRatio"],
                round(case["corpusBytes"] / case["inputBytes"], 6),
            )
            for label in ("lines/s=", "peak=", "corpusBytes=", "inputBytes=", "ratio=", "nproc=", "NESSO_THREADS="):
                self.assertIn(label, result.stdout)
            self.assertIn(cpu, result.stdout)

    def test_lines_without_hdfs_is_rejected(self) -> None:
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--lines", "10"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--dataset hdfs", result.stderr)


if __name__ == "__main__":
    unittest.main()
