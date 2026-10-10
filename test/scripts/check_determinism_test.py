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
        marker = pathlib.Path(sys.argv[0]).with_name("ranking-unstable")
        if marker.exists():
            counter = marker.with_name("ranking-counter")
            value = int(counter.read_text()) if counter.exists() else 0
            counter.write_text(str(value + 1))
            sys.stdout.write(f"PAY-E1042-{value}\\n")
        else:
            sys.stdout.write("PAY-E1042\\n")
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

    def run_gate(self, binary: pathlib.Path, directory: pathlib.Path) -> subprocess.CompletedProcess[str]:
        model_dir = directory / "models"
        model_dir.mkdir()
        input_file = directory / "events.log"
        input_file.write_text("PAY-E1042\n")
        stress = "python3 -c 'import time; time.sleep(60)'"
        return subprocess.run(
            [
                str(GATE),
                "--binary",
                str(binary),
                "--model-dir",
                str(model_dir),
                "--input",
                str(input_file),
                "--runs",
                "3",
                "--stress-command",
                stress,
            ],
            capture_output=True,
            text=True,
            check=False,
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


if __name__ == "__main__":
    unittest.main()
