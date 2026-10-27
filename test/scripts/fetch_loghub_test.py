import hashlib
import importlib.machinery
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "fetch-loghub"
PINNED_SHA256 = "7c967000980c086ed55fa6544ba4f05fe66d44622795e890c68caf8bbb635035"


def load_fetch():
    loader = importlib.machinery.SourceFileLoader("fetch_loghub", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def run_fetch(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True,
        text=True,
        check=False,
    )


class FetchLoghubTest(unittest.TestCase):
    def test_pin_is_a_commit_url(self) -> None:
        module = load_fetch()
        self.assertRegex(
            module.HDFS_URL,
            r"^https://raw\.githubusercontent\.com/logpai/loghub/[0-9a-f]{40}/HDFS/HDFS_2k\.log$",
        )
        self.assertNotIn("/master/", module.HDFS_URL)
        self.assertNotIn("/main/", module.HDFS_URL)
        self.assertEqual(module.HDFS_SHA256, PINNED_SHA256)
        self.assertEqual(module.DEFAULT_DEST.relative_to(ROOT), pathlib.Path("build/bench/HDFS_2k.log"))

    def test_hash_mismatch_fails_and_keeps_no_file(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            source = directory / "sample.log"
            source.write_text("alpha\n", encoding="utf-8")
            dest = directory / "HDFS_2k.log"
            result = run_fetch("--url", source.as_uri(), "--sha256", "0" * 64, "--dest", str(dest))
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("sha256 mismatch", result.stderr)
            self.assertIn("expected", result.stderr)
            self.assertFalse(dest.exists())
            self.assertFalse(dest.with_name(dest.name + ".partial").exists())

    def test_cached_hash_skips_the_download(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            source = directory / "sample.log"
            payload = "alpha\nbeta\n"
            source.write_text(payload, encoding="utf-8")
            digest = hashlib.sha256(payload.encode()).hexdigest()
            dest = directory / "cached.log"
            first = run_fetch("--url", source.as_uri(), "--sha256", digest, "--dest", str(dest))
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertIn("wrote", first.stdout)
            self.assertEqual(dest.read_text(encoding="utf-8"), payload)
            source.unlink()
            second = run_fetch("--url", source.as_uri(), "--sha256", digest, "--dest", str(dest))
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertIn("cached", second.stdout)
            self.assertNotIn("wrote", second.stdout)
            self.assertEqual(dest.read_text(encoding="utf-8"), payload)

    def test_wrong_cache_is_replaced_when_the_download_matches(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            source = directory / "sample.log"
            payload = "kept\n"
            source.write_text(payload, encoding="utf-8")
            digest = hashlib.sha256(payload.encode()).hexdigest()
            dest = directory / "cached.log"
            dest.write_text("stale\n", encoding="utf-8")
            result = run_fetch("--url", source.as_uri(), "--sha256", digest, "--dest", str(dest))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("wrote", result.stdout)
            self.assertEqual(dest.read_text(encoding="utf-8"), payload)


if __name__ == "__main__":
    unittest.main()
