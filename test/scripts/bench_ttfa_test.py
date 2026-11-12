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
    import json
    import os
    import sys
    from pathlib import Path
    args = sys.argv[1:]
    log = os.environ.get("NESSO_ARGV_LOG")
    if log:
        path = Path(log)
        current = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else []
        files = []
        skip_next = False
        for arg in args[1:]:
            if skip_next:
                skip_next = False
                continue
            if arg in {"-o", "--model-dir", "-i", "-k"}:
                skip_next = True
                continue
            if arg.startswith("-"):
                continue
            if args and args[0] == "search":
                continue
            candidate = Path(arg)
            files.append({"path": arg, "is_file": candidate.is_file(), "is_dir": candidate.is_dir()})
        current.append(
            {
                "args": args,
                "files": files,
                "xdg_cache_home": os.environ.get("XDG_CACHE_HOME"),
            }
        )
        path.write_text(json.dumps(current), encoding="utf-8")
    if "index" in args and os.environ.get("NESSO_STUB_INDEX_CODE"):
        raise SystemExit(int(os.environ["NESSO_STUB_INDEX_CODE"]))
    if "index" in args:
        Path(args[args.index("-o") + 1]).write_bytes(b"nesso-corpus")
        raise SystemExit(0)
    if "search" in args and os.environ.get("NESSO_STUB_SEARCH_CODE"):
        raise SystemExit(int(os.environ["NESSO_STUB_SEARCH_CODE"]))
    if "search" in args:
        sys.stdout.write("match\\n")
        raise SystemExit(0)
    raise SystemExit(2)
    """
)
FAKE_TIME = textwrap.dedent(
    """\
    #!/usr/bin/env python3
    import subprocess
    import sys
    from pathlib import Path
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
    counter = Path(__file__).with_suffix(".count")
    seen = int(counter.read_text(encoding="utf-8")) if counter.is_file() else 0
    counter.write_text(str(seen + 1), encoding="utf-8")
    if out is not None:
        with open(out, "w", encoding="utf-8") as handle:
            if seen == 0:
                handle.write("1.250000 4096\\n")
            else:
                handle.write("0.500000 1024\\n")
    raise SystemExit(completed.returncode)
    """
)


def load_bench():
    loader = importlib.machinery.SourceFileLoader("nesso_bench_ttfa", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def write_executable(path: pathlib.Path, source: str) -> None:
    path.write_text(source, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IEXEC)


def model_dir(directory: pathlib.Path) -> pathlib.Path:
    model = directory / "model"
    model.mkdir()
    (model / "model.onnx").write_bytes(b"x")
    (model / "vocab.txt").write_bytes(b"x")
    return model


def tree_files(root: pathlib.Path) -> list[str]:
    return sorted(path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file())


class BenchTtfaTest(unittest.TestCase):
    def test_tree_is_deterministic_and_has_required_shapes(self) -> None:
        bench = load_bench()
        self.assertEqual(bench.TTFA_DEFAULT_LINES, 256)
        self.assertLessEqual(bench.TTFA_DEFAULT_LINES, 1000)
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            first = directory / "first"
            second = directory / "second"
            other = directory / "other"
            stats = bench.generate_ttfa(first, lines=80)
            again = bench.generate_ttfa(second, lines=80)
            different = bench.generate_ttfa(other, lines=80, seed=bench.SEED + 1)
            files = tree_files(first)
            copied = tree_files(second)
            self.assertEqual(files, copied)
            for relative in files:
                self.assertEqual((first / relative).read_bytes(), (second / relative).read_bytes())
            self.assertNotEqual(
                [(relative, (first / relative).read_bytes()) for relative in files],
                [(relative, (other / relative).read_bytes()) for relative in tree_files(other)],
            )
            self.assertEqual(stats["lines"], 80)
            self.assertEqual(again["lines"], 80)
            self.assertEqual(different["lines"], 80)
            self.assertEqual(stats["filesRead"], len(files))
            self.assertEqual(stats["inputBytes"], sum((first / relative).stat().st_size for relative in files))
            self.assertIn("var/log/messages", files)
            self.assertIn("var/log/messages.1", files)
            self.assertIn("var/log/messages.2", files)
            self.assertIn("var/log/app/service.jsonl", files)
            self.assertIn("sos_commands/logs/journalctl_--no-pager", files)
            self.assertIn("var/log/messages.gz", files)
            self.assertIn(bench.TTFA_TAR_PATH, files)
            self.assertTrue(any(relative.endswith(".gz") for relative in files))
            self.assertTrue((first / "var/log/messages.gz").read_bytes().startswith(b"\x1f\x8b"))
            self.assertTrue((first / bench.TTFA_TAR_PATH).read_bytes().startswith(b"\x1f\x8b"))
            self.assertIn("disk write failed", (first / "var/log/messages").read_text(encoding="utf-8"))
            for line in (first / "var/log/app/service.jsonl").read_text(encoding="utf-8").splitlines():
                payload = json.loads(line)
                self.assertIsInstance(payload["message"], str)
            self.assertFalse((first / "sos_commands/logs/journalctl_--no-pager").suffix)

    def test_line_count_below_the_stream_set_is_rejected(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(SystemExit) as rejected:
                bench.generate_ttfa(pathlib.Path(temp) / "bundle", lines=len(bench.TTFA_STREAMS) - 1)
            self.assertIn("at least", str(rejected.exception))

    def test_mib_meets_the_requested_size(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            stats = bench.generate_ttfa(directory / "bundle", mib=0.05)
            size_bytes = round(0.05 * 1024 * 1024)
            self.assertGreaterEqual(stats["inputBytes"], size_bytes)
            self.assertGreater(stats["lines"], len(bench.TTFA_STREAMS))
            smaller = bench.generate_ttfa(directory / "smaller", lines=stats["lines"] - 1)
            self.assertLess(smaller["inputBytes"], size_bytes)
            with self.assertRaises(SystemExit) as rejected:
                bench.generate_ttfa(directory / "both", lines=80, mib=0.05)
            self.assertIn("--lines", str(rejected.exception))
            self.assertIn("--mib", str(rejected.exception))

    def test_mib_below_the_minimum_tree_is_rejected(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            minimum = bench.generate_ttfa(directory / "minimum", lines=len(bench.TTFA_STREAMS))
            size_bytes = minimum["inputBytes"] - 1
            mib = size_bytes / (1024 * 1024)
            self.assertEqual(round(mib * 1024 * 1024), size_bytes)
            with self.assertRaises(SystemExit) as rejected:
                bench.generate_ttfa(directory / "tiny", mib=mib)
            self.assertIn("minimum", str(rejected.exception))

    def test_cli_records_index_and_search_for_dataset_and_flag(self) -> None:
        bench = load_bench()
        for mode_args in (["--dataset", "ttfa"], ["--ttfa"]):
            with self.subTest(mode_args=mode_args):
                with tempfile.TemporaryDirectory() as temp:
                    directory = pathlib.Path(temp)
                    binary = directory / "nesso"
                    timer = directory / "time"
                    write_executable(binary, STUB)
                    write_executable(timer, FAKE_TIME)
                    output = directory / "out.json"
                    argv_log = directory / "argv.json"
                    env = os.environ.copy()
                    env.pop("GITHUB_SHA", None)
                    env["NESSO_THREADS"] = "3"
                    env["NESSO_BENCH_TIME"] = str(timer)
                    env["NESSO_ARGV_LOG"] = str(argv_log)
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(SCRIPT),
                            *mode_args,
                            "--lines",
                            "80",
                            "--binary",
                            str(binary),
                            "--model-dir",
                            str(model_dir(directory)),
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
                    self.assertEqual(
                        set(payload),
                        {"sha", "binary", "dataset", "query", "seed", "cpuModel", "nproc", "threads", "cases"},
                    )
                    self.assertEqual(payload["sha"], sha)
                    self.assertEqual(payload["binary"], str(binary))
                    self.assertEqual(payload["dataset"], "ttfa")
                    self.assertEqual(payload["query"], bench.LOGDIR_QUERY)
                    self.assertEqual(payload["seed"], bench.SEED)
                    self.assertEqual(payload["cpuModel"], cpu)
                    self.assertEqual(payload["nproc"], nproc)
                    self.assertEqual(payload["threads"], "3")
                    self.assertEqual(len(payload["cases"]), 1)
                    case = payload["cases"][0]
                    self.assertEqual(
                        set(case),
                        {
                            "lines",
                            "filesRead",
                            "ok",
                            "indexExitCode",
                            "searchExitCode",
                            "indexWallSeconds",
                            "searchWallSeconds",
                            "wallSeconds",
                            "indexPeakRssBytes",
                            "searchPeakRssBytes",
                            "peakRssBytes",
                            "inputBytes",
                            "corpusBytes",
                            "corpusToInputRatio",
                        },
                    )
                    self.assertEqual(case["lines"], 80)
                    self.assertEqual(case["filesRead"], 7)
                    self.assertTrue(case["ok"])
                    self.assertEqual(case["indexExitCode"], 0)
                    self.assertEqual(case["searchExitCode"], 0)
                    self.assertEqual(case["indexWallSeconds"], 1.25)
                    self.assertEqual(case["searchWallSeconds"], 0.5)
                    self.assertEqual(case["wallSeconds"], 1.75)
                    self.assertEqual(case["indexPeakRssBytes"], 4096 * 1024)
                    self.assertEqual(case["searchPeakRssBytes"], 1024 * 1024)
                    self.assertEqual(case["peakRssBytes"], 4096 * 1024)
                    self.assertEqual(case["corpusBytes"], len(b"nesso-corpus"))
                    self.assertGreater(case["inputBytes"], 0)
                    self.assertEqual(
                        case["corpusToInputRatio"],
                        round(case["corpusBytes"] / case["inputBytes"], 6),
                    )
                    for label in (
                        "index=",
                        "search=",
                        "wall=",
                        "peak=",
                        "corpusBytes=",
                        "inputBytes=",
                        "ratio=",
                        "nproc=",
                        "NESSO_THREADS=",
                        "files",
                    ):
                        self.assertIn(label, result.stdout)
                    self.assertIn(cpu, result.stdout)
                    invocations = json.loads(argv_log.read_text(encoding="utf-8"))
                    self.assertEqual(len(invocations), 2)
                    index_args = invocations[0]["args"]
                    search_args = invocations[1]["args"]
                    self.assertEqual(index_args[0], "index")
                    self.assertIn("--quiet", index_args)
                    self.assertNotIn("-i", index_args)
                    recorded = invocations[0]["files"]
                    self.assertEqual(len(recorded), 1)
                    self.assertTrue(recorded[0]["is_dir"])
                    self.assertFalse(recorded[0]["is_file"])
                    self.assertEqual(invocations[1]["files"], [])
                    self.assertEqual(search_args[0], "search")
                    self.assertEqual(search_args[1], bench.LOGDIR_QUERY)
                    self.assertIn("-i", search_args)
                    self.assertIsNotNone(invocations[0]["xdg_cache_home"])
                    self.assertIsNotNone(invocations[1]["xdg_cache_home"])
                    self.assertNotEqual(invocations[0]["xdg_cache_home"], invocations[1]["xdg_cache_home"])
                    self.assertIn("cache-index", invocations[0]["xdg_cache_home"])
                    self.assertIn("cache-search", invocations[1]["xdg_cache_home"])

    def test_index_failure_does_not_report_a_sum(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = directory / "nesso"
            timer = directory / "time"
            write_executable(binary, STUB)
            write_executable(timer, FAKE_TIME)
            output = directory / "out.json"
            env = os.environ.copy()
            env.pop("GITHUB_SHA", None)
            env["NESSO_BENCH_TIME"] = str(timer)
            env["NESSO_STUB_INDEX_CODE"] = "2"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--ttfa",
                    "--lines",
                    "40",
                    "--binary",
                    str(binary),
                    "--model-dir",
                    str(model_dir(directory)),
                    "--output",
                    str(output),
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertNotEqual(result.returncode, 0)
            case = json.loads(output.read_text(encoding="utf-8"))["cases"][0]
            self.assertFalse(case["ok"])
            self.assertEqual(case["indexExitCode"], 2)
            self.assertIsNone(case["searchExitCode"])
            self.assertEqual(case["indexWallSeconds"], 1.25)
            self.assertIsNone(case["searchWallSeconds"])
            self.assertIsNone(case["wallSeconds"])
            self.assertEqual(case["indexPeakRssBytes"], 4096 * 1024)
            self.assertIsNone(case["searchPeakRssBytes"])
            self.assertIn("nesso exited 2", case["error"])

    def test_dataset_flags_are_rejected_on_the_wrong_mode(self) -> None:
        cases = (
            (["--mib", "1"], "--dataset logdir"),
            (["--dataset", "hdfs", "--mib", "1"], "--dataset logdir"),
            (["--ttfa", "--sample", "x"], "--dataset hdfs"),
            (["--dataset", "ttfa", "--sizes", "10"], "synthetic grep"),
            (["--ttfa", "--lines", "40", "--mib", "1"], "--lines"),
            (["--ttfa", "--dataset", "logdir"], "--ttfa"),
            (["--lines", "10"], "--dataset hdfs"),
        )
        for args, needle in cases:
            with self.subTest(args=args):
                result = subprocess.run(
                    [sys.executable, str(SCRIPT), *args],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(needle, result.stderr)


if __name__ == "__main__":
    unittest.main()
