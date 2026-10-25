import os
import pathlib
import stat
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
GATE = ROOT / "scripts" / "check-determinism"


FAKE_BINARY = textwrap.dedent(
    """\
    #!/usr/bin/env python3
    import pathlib
    import sys

    args = sys.argv[1:]
    mode = args[0]
    output = pathlib.Path(args[args.index("-o") + 1]) if "-o" in args else None
    if mode == "index":
        marker = pathlib.Path(sys.argv[0]).with_name("unstable")
        if marker.exists():
            counter = marker.with_name("counter")
            value = int(counter.read_text()) if counter.exists() else 0
            counter.write_text(str(value + 1))
            output.write_bytes(f"unstable-{value}".encode())
        else:
            output.write_bytes(b"stable")
    elif mode == "search":
        query = args[1]
        log = pathlib.Path(sys.argv[0]).with_name("searches")
        with log.open("a") as handle:
            handle.write(query + "\\n")
        unstable_query_path = pathlib.Path(sys.argv[0]).with_name("unstable-query")
        unstable_query = unstable_query_path.read_text().strip() if unstable_query_path.exists() else None
        marker = pathlib.Path(sys.argv[0]).with_name("ranking-unstable")
        if marker.exists() or query == unstable_query:
            counter = marker.with_name("ranking-counter")
            value = int(counter.read_text()) if counter.exists() else 0
            counter.write_text(str(value + 1))
            sys.stdout.write(f"{query}-{value}\\n")
        else:
            sys.stdout.write(query + "\\n")
    else:
        raise SystemExit(2)
    """
)


class CheckDeterminismTest(unittest.TestCase):
    def write_binary(self, directory: pathlib.Path, unstable: str | None) -> pathlib.Path:
        binary = directory / "fake-nesso"
        binary.write_text(FAKE_BINARY)
        binary.chmod(binary.stat().st_mode | stat.S_IXUSR)
        if unstable is not None:
            (directory / unstable).touch()
        return binary

    def run_gate(
        self,
        binary: pathlib.Path,
        directory: pathlib.Path,
        stress: str = "python3 -c 'import time; time.sleep(60)'",
        runs: str | None = "3",
        index_runs: str | None = None,
        mode: str | None = None,
        env: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        model_dir = directory / "models"
        model_dir.mkdir()
        input_file = directory / "events.log"
        input_file.write_text("PAY-E1042\n")
        command = [
            str(GATE),
            "--binary",
            str(binary),
            "--model-dir",
            str(model_dir),
            "--input",
            str(input_file),
            "--stress-command",
            stress,
        ]
        if runs is not None:
            command.extend(["--runs", runs])
        if index_runs is not None:
            command.extend(["--index-runs", index_runs])
        if mode is not None:
            command.extend(["--mode", mode])
        return subprocess.run(
            command,
            capture_output=True,
            text=True,
            check=False,
            env=env,
        )

    def test_accepts_identical_corpora_and_rankings(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            result = self.run_gate(self.write_binary(directory, unstable=None), directory)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("3 runs", result.stdout)

    def test_reports_a_changed_corpus(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            result = self.run_gate(self.write_binary(directory, unstable="unstable"), directory)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("corpus differs", result.stderr)

    def test_reports_changed_rankings(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            result = self.run_gate(self.write_binary(directory, unstable="ranking-unstable"), directory)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ranking differs", result.stderr)

    def test_fails_when_stress_exits_before_a_comparison(self) -> None:
        stress = """python3 -c 'import sys; sys.stderr.write("stress-died-now\\n"); raise SystemExit(9)'"""
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            result = self.run_gate(self.write_binary(directory, unstable=None), directory, stress)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("stress command exited before the gate completed", result.stderr)
        self.assertIn("exit 9", result.stderr)
        self.assertIn("stress-died-now", result.stderr)
        self.assertNotIn("determinism OK", result.stdout)

    def test_restarts_stress_after_an_early_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            marker = directory / "stress-marker"
            launcher = directory / "flaky-stress.py"
            launcher.write_text(
                textwrap.dedent(
                    f"""\
                    import pathlib
                    import sys
                    import time

                    marker = pathlib.Path({str(marker)!r})
                    if not marker.exists():
                        marker.write_text("1")
                        sys.stderr.write("first-start-died\\n")
                        raise SystemExit(3)
                    time.sleep(60)
                    """
                )
            )
            result = self.run_gate(
                self.write_binary(directory, unstable=None),
                directory,
                f"python3 {launcher}",
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("3 runs", result.stdout)
        self.assertNotIn("first-start-died", result.stderr)

    def test_index_runs_search_four_queries_including_pay_e1042(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = self.write_binary(directory, unstable=None)
            result = self.run_gate(binary, directory, runs="2", index_runs="2")
            searches = (directory / "searches").read_text().splitlines()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("index-runs 2", result.stdout)
        for query in (
            "PAY-E1042",
            "connection refused",
            "database connection error",
            "payment timeout",
        ):
            self.assertGreaterEqual(searches.count(query), 2, searches)

    def test_index_runs_names_the_query_whose_ranking_changed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = self.write_binary(directory, unstable=None)
            (directory / "unstable-query").write_text("payment timeout\n")
            result = self.run_gate(binary, directory, runs="2", index_runs="2")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("payment timeout", result.stderr)
        self.assertIn("ranking", result.stderr)

    def test_full_mode_defaults_to_ten_runs_without_index_runs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            result = self.run_gate(self.write_binary(directory, unstable=None), directory, runs=None)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("determinism OK (full): 10 runs", result.stdout)
        self.assertNotIn("index-runs", result.stdout)

    def test_pr_mode_defaults_to_two_runs_and_two_index_runs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = self.write_binary(directory, unstable=None)
            result = self.run_gate(binary, directory, runs=None, mode="pr")
            searches = (directory / "searches").read_text().splitlines()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("determinism OK (pr): 2 runs", result.stdout)
        self.assertIn("index-runs 2", result.stdout)
        for query in (
            "PAY-E1042",
            "connection refused",
            "database connection error",
            "payment timeout",
        ):
            self.assertGreaterEqual(searches.count(query), 2, searches)

    def test_explicit_runs_override_pr_mode_and_env(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            env = dict(**os.environ)
            env["NESSO_DETERMINISM_MODE"] = "pr"
            result = self.run_gate(
                self.write_binary(directory, unstable=None),
                directory,
                runs="4",
                index_runs="3",
                mode="full",
                env=env,
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("determinism OK (full): 4 runs", result.stdout)
        self.assertIn("index-runs 3", result.stdout)

    def test_pr_mode_still_reports_a_changed_corpus(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            result = self.run_gate(
                self.write_binary(directory, unstable="unstable"),
                directory,
                runs=None,
                mode="pr",
            )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("corpus differs", result.stderr)
        self.assertNotIn("determinism OK", result.stdout)

    def test_rejects_an_unknown_mode_from_the_environment(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "determinism-input.log"
            env = dict(**os.environ)
            env["NESSO_DETERMINISM_MODE"] = "weekly"
            result = subprocess.run(
                [str(GATE), "--emit-input", str(path)],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("mode", result.stderr)
        self.assertFalse(path.exists())

    def test_emit_input_writes_at_least_3500_lines(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "determinism-input.log"
            result = subprocess.run(
                [str(GATE), "--emit-input", str(path)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            lines = path.read_text().splitlines()

        self.assertGreaterEqual(len(lines), 3500)
        self.assertTrue(any("PAY-E1042" in line for line in lines))

    def test_pr_mode_emit_input_is_eight_batches(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "determinism-input.log"
            result = subprocess.run(
                [str(GATE), "--mode", "pr", "--emit-input", str(path)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            lines = path.read_text().splitlines()

        self.assertEqual(len(lines), 256)
        self.assertEqual(len(lines) % 32, 0)
        self.assertTrue(any("PAY-E1042" in line for line in lines))

    def test_full_flag_overrides_pr_environment_for_emit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "determinism-input.log"
            env = dict(**os.environ)
            env["NESSO_DETERMINISM_MODE"] = "pr"
            result = subprocess.run(
                [str(GATE), "--mode", "full", "--emit-input", str(path)],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            lines = path.read_text().splitlines()

        self.assertGreaterEqual(len(lines), 3500)

    def test_default_stress_command_pins_a_cpu_loop(self) -> None:
        result = subprocess.run([str(GATE), "--help"], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "stress-ng --cpu 1 --cpu-method loop --timeout 0 --cache-size 2M",
            " ".join(result.stdout.split()),
        )


if __name__ == "__main__":
    unittest.main()
