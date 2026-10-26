import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check-no-runpath"

HEADER = """
Dynamic section at offset 0x1234 contains 4 entries:
  Tag        Type                         Name/Value
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
"""


def run(output: str, code: int = 0, args: list[str] | None = None) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory() as temp:
        fake = pathlib.Path(temp) / "readelf"
        fake.write_text(
            "#!/bin/sh\n"
            f"cat <<'OUT'\n{output}\nOUT\n"
            f"echo 'readelf: stub error' >&2\nexit {code}\n"
        )
        fake.chmod(0o755)
        env = dict(os.environ, READELF=str(fake))
        return subprocess.run(
            [sys.executable, str(SCRIPT), *(args if args is not None else ["nesso"])],
            capture_output=True,
            text=True,
            check=False,
            env=env,
        )


class CheckNoRunpathTest(unittest.TestCase):
    def test_passes_without_a_search_path(self) -> None:
        result = run(HEADER + " 0x000000000000000c (INIT)               0x4000\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_fails_on_runpath_with_a_trailing_empty_entry(self) -> None:
        result = run(HEADER + " 0x000000000000001d (RUNPATH)            Library runpath: [/opt/conan2/p/b/onnx/p/lib:]\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("has RUNPATH [/opt/conan2/p/b/onnx/p/lib:]", result.stderr)
        self.assertIn("1 empty entry, searched as the current directory", result.stderr)

    def test_fails_on_an_empty_runpath(self) -> None:
        result = run(HEADER + " 0x000000000000001d (RUNPATH)            Library runpath: []\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("has RUNPATH []", result.stderr)
        self.assertIn("empty entry", result.stderr)

    def test_fails_on_rpath_without_empty_entries(self) -> None:
        result = run(HEADER + " 0x000000000000000f (RPATH)              Library rpath: [/usr/local/lib:/opt/lib]\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("has RPATH [/usr/local/lib:/opt/lib]", result.stderr)
        self.assertNotIn("empty entry", result.stderr)

    def test_fails_on_a_tag_line_it_cannot_parse(self) -> None:
        result = run(HEADER + " 0x000000000000001d (RUNPATH)            something unexpected\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("has RUNPATH", result.stderr)

    def test_reports_readelf_failure_and_usage(self) -> None:
        failed = run("", code=1)
        self.assertEqual(failed.returncode, 2)
        self.assertIn("readelf: stub error", failed.stderr)
        usage = run("", args=[])
        self.assertEqual(usage.returncode, 2)
        self.assertIn("usage:", usage.stderr)


if __name__ == "__main__":
    unittest.main()
