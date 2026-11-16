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
TRACE = (
    '{"peakRssBytes":1,"stages":['
    '{"name":"corpus-read","us":3},'
    '{"name":"model-load","us":11},'
    '{"name":"query-embed","us":5},'
    '{"name":"distance","us":7},'
    '{"name":"rank","us":20}]}'
)
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
        current.append({"args": args, "trace": os.environ.get("NESSO_TRACE")})
        path.write_text(json.dumps(current), encoding="utf-8")
    if "index" in args:
        if os.environ.get("NESSO_STUB_INDEX_CODE"):
            raise SystemExit(int(os.environ["NESSO_STUB_INDEX_CODE"]))
        Path(args[args.index("-o") + 1]).write_bytes(b"nesso-corpus")
        raise SystemExit(0)
    if "search" in args:
        if os.environ.get("NESSO_STUB_SEARCH_CODE"):
            raise SystemExit(int(os.environ["NESSO_STUB_SEARCH_CODE"]))
        traced = os.environ.get("NESSO_TRACE") == "1"
        if os.environ.get("NESSO_STUB_DIVERGE") == "1" and not traced:
            sys.stdout.write("other\\n")
        else:
            sys.stdout.write("match\\n")
        if traced:
            if os.environ.get("NESSO_STUB_TRACE") == "incomplete":
                sys.stderr.write(
                    '{"peakRssBytes":1,"stages":[{"name":"corpus-read","us":3}]}\\n'
                )
            else:
                sys.stderr.write(
                    '{"peakRssBytes":1,"stages":['
                    '{"name":"corpus-read","us":3},'
                    '{"name":"model-load","us":11},'
                    '{"name":"query-embed","us":5},'
                    '{"name":"distance","us":7},'
                    '{"name":"rank","us":20}]}\\n'
                )
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
    loader = importlib.machinery.SourceFileLoader("nesso_bench_search_profile", str(SCRIPT))
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


class BenchSearchProfileTest(unittest.TestCase):
    def test_profile_log_keeps_letter_ids(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "corpus.log"
            bench.write_profile_log(path, 40)
            lines = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(lines), 40)
            distinct = [line for line in lines if line != bench.QUERY]
            self.assertGreater(len(distinct), 20)
            self.assertEqual(len(distinct), len(set(distinct)))
            self.assertTrue(all("id-w" in line for line in distinct))
            self.assertIn(bench.QUERY, lines)

    def test_stage_times_sum_and_reject_bad_values(self) -> None:
        bench = load_bench()
        self.assertEqual(bench.SEARCH_PROFILE_DEFAULT_LINES, 1000)
        trace = {
            "stages": [
                {"name": "corpus-read", "us": 3},
                {"name": "model-load", "us": 11},
                {"name": "query-embed", "us": 5},
                {"name": "distance", "us": 7},
                {"name": "distance", "us": 1},
                {"name": "rank", "us": 20},
            ]
        }
        self.assertEqual(bench.stage_microseconds(trace, "distance"), 8)
        self.assertEqual(
            bench.search_profile_times(trace),
            {"ioUs": 3, "embedUs": 5, "distanceUs": 8, "modelLoadUs": 11},
        )
        self.assertIsNone(bench.stage_microseconds(trace, "missing"))
        self.assertIsNone(bench.search_profile_times({"stages": [{"name": "distance", "us": True}]}))
        self.assertIsNone(bench.search_profile_times({"stages": [{"name": "distance", "us": 1.5}]}))
        self.assertIsNone(bench.search_profile_times(None))
        parsed = bench.parse_trace("note\n" + TRACE + "\n")
        self.assertEqual(bench.search_profile_times(parsed)["distanceUs"], 7)

    def test_cli_records_the_split_and_matching_stdout(self) -> None:
        bench = load_bench()
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
            env.pop("NESSO_TRACE", None)
            env["NESSO_THREADS"] = "3"
            env["NESSO_BENCH_TIME"] = str(timer)
            env["NESSO_ARGV_LOG"] = str(argv_log)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--dataset",
                    "search-profile",
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
                {
                    "sha",
                    "binary",
                    "dataset",
                    "query",
                    "seed",
                    "cpuModel",
                    "nproc",
                    "threads",
                    "lines",
                    "ok",
                    "indexExitCode",
                    "searchExitCode",
                    "indexWallSeconds",
                    "searchWallSeconds",
                    "peakRssBytes",
                    "ioUs",
                    "embedUs",
                    "distanceUs",
                    "modelLoadUs",
                    "stdoutIdentical",
                    "stages",
                },
            )
            self.assertEqual(payload["sha"], sha)
            self.assertEqual(payload["binary"], str(binary))
            self.assertEqual(payload["dataset"], "search-profile")
            self.assertEqual(payload["query"], bench.QUERY)
            self.assertEqual(payload["seed"], bench.SEED)
            self.assertEqual(payload["cpuModel"], cpu)
            self.assertEqual(payload["nproc"], nproc)
            self.assertEqual(payload["threads"], "3")
            self.assertEqual(payload["lines"], 40)
            self.assertTrue(payload["ok"])
            self.assertEqual(payload["indexExitCode"], 0)
            self.assertEqual(payload["searchExitCode"], 0)
            self.assertEqual(payload["indexWallSeconds"], 1.25)
            self.assertEqual(payload["searchWallSeconds"], 0.5)
            self.assertEqual(payload["peakRssBytes"], 1024 * 1024)
            self.assertEqual(payload["ioUs"], 3)
            self.assertEqual(payload["embedUs"], 5)
            self.assertEqual(payload["distanceUs"], 7)
            self.assertEqual(payload["modelLoadUs"], 11)
            self.assertTrue(payload["stdoutIdentical"])
            self.assertEqual([stage["name"] for stage in payload["stages"]],
                             ["corpus-read", "model-load", "query-embed", "distance", "rank"])
            for label in ("ioUs=", "embedUs=", "distanceUs=", "modelLoadUs=", "wall=", "nproc=", "NESSO_THREADS="):
                self.assertIn(label, result.stdout)
            invocations = json.loads(argv_log.read_text(encoding="utf-8"))
            self.assertEqual([item["args"][0] for item in invocations], ["index", "search", "search"])
            self.assertEqual([item["trace"] for item in invocations], [None, "1", ""])
            self.assertEqual(invocations[1]["args"][1], bench.QUERY)
            self.assertIn("-k", invocations[1]["args"])
            self.assertEqual(invocations[1]["args"][invocations[1]["args"].index("-k") + 1], "5")

    def test_missing_distance_stage_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = directory / "nesso"
            timer = directory / "time"
            write_executable(binary, STUB)
            write_executable(timer, FAKE_TIME)
            env = os.environ.copy()
            env.pop("GITHUB_SHA", None)
            env["NESSO_BENCH_TIME"] = str(timer)
            env["NESSO_STUB_TRACE"] = "incomplete"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--dataset",
                    "search-profile",
                    "--lines",
                    "40",
                    "--binary",
                    str(binary),
                    "--model-dir",
                    str(model_dir(directory)),
                    "--output",
                    str(directory / "out.json"),
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("missing corpus-read/model-load/query-embed/distance trace", result.stdout)
            payload = json.loads((directory / "out.json").read_text(encoding="utf-8"))
            self.assertFalse(payload["ok"])
            self.assertIsNone(payload["distanceUs"])
            self.assertFalse(payload["stdoutIdentical"])

    def test_stdout_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = directory / "nesso"
            timer = directory / "time"
            write_executable(binary, STUB)
            write_executable(timer, FAKE_TIME)
            env = os.environ.copy()
            env.pop("GITHUB_SHA", None)
            env["NESSO_BENCH_TIME"] = str(timer)
            env["NESSO_STUB_DIVERGE"] = "1"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--dataset",
                    "search-profile",
                    "--lines",
                    "40",
                    "--binary",
                    str(binary),
                    "--model-dir",
                    str(model_dir(directory)),
                    "--output",
                    str(directory / "out.json"),
                ],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("search stdout changed with NESSO_TRACE=1", result.stdout)
            payload = json.loads((directory / "out.json").read_text(encoding="utf-8"))
            self.assertFalse(payload["stdoutIdentical"])
            self.assertEqual(payload["distanceUs"], 7)

    def test_rejected_flags(self) -> None:
        bench = load_bench()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = directory / "nesso"
            write_executable(binary, STUB)
            common = [
                sys.executable,
                str(SCRIPT),
                "--dataset",
                "search-profile",
                "--binary",
                str(binary),
                "--model-dir",
                str(model_dir(directory)),
            ]
            mib = subprocess.run([*common, "--mib", "1"], capture_output=True, text=True, check=False)
            self.assertEqual(mib.returncode, 1)
            self.assertIn("--mib", mib.stderr)
            lines = subprocess.run([*common, "--lines", "0"], capture_output=True, text=True, check=False)
            self.assertEqual(lines.returncode, 1)
            self.assertIn("positive", lines.stderr)
            self.assertEqual(bench.SEARCH_PROFILE_DEFAULT_LINES, 1000)


if __name__ == "__main__":
    unittest.main()


if __name__ == "__main__":
    unittest.main()
