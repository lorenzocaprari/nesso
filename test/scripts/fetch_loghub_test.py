import hashlib
import importlib.machinery
import importlib.util
import io
import pathlib
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile


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

    def test_full_set_urls_are_pinned_to_the_zenodo_record(self) -> None:
        module = load_fetch()
        self.assertEqual(module.ZENODO_RECORD, "8196385")
        self.assertEqual(set(module.FULL_SETS), {"hdfs-v1", "bgl", "thunderbird"})
        for spec in module.FULL_SETS.values():
            self.assertIn(f"/records/{module.ZENODO_RECORD}/", spec.url)
            self.assertNotIn("/latest", spec.url)
            self.assertEqual(len(spec.archive_md5), 32)
            self.assertEqual(len(spec.archive_sha256), 64)
            self.assertEqual(len(spec.prefix_sha256), 64)
            self.assertNotIn("0" * 64, (spec.archive_sha256, spec.prefix_sha256))

    def test_zip_prefix_keeps_the_first_lines_and_rejects_a_bad_archive_pin(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            archive = directory / "sample.zip"
            payload = b"one\ntwo\nthree\nfour\n"
            with zipfile.ZipFile(archive, "w") as handle:
                handle.writestr("HDFS.log", payload)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            dest = directory / "HDFS_1m.log"
            bad = run_fetch(
                "--dataset",
                "hdfs-v1",
                "--url",
                archive.as_uri(),
                "--sha256",
                "0" * 64,
                "--lines",
                "2",
                "--dest",
                str(dest),
            )
            self.assertEqual(bad.returncode, 1, bad.stdout)
            self.assertIn("sha256 mismatch", bad.stderr)
            self.assertFalse(dest.exists())
            self.assertFalse(list(directory.glob("*.partial")))
            self.assertFalse(list(directory.glob("*.archive")))
            good = run_fetch(
                "--dataset",
                "hdfs-v1",
                "--url",
                archive.as_uri(),
                "--sha256",
                digest,
                "--lines",
                "2",
                "--dest",
                str(dest),
            )
            self.assertEqual(good.returncode, 0, good.stderr)
            self.assertEqual(dest.read_bytes(), b"one\ntwo\n")
            short = run_fetch(
                "--dataset",
                "hdfs-v1",
                "--url",
                archive.as_uri(),
                "--sha256",
                digest,
                "--lines",
                "9",
                "--dest",
                str(directory / "short.log"),
            )
            self.assertEqual(short.returncode, 1, short.stdout)
            self.assertIn("need 9", short.stderr)

    def test_tar_prefix_and_prefix_pin_mismatch_and_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            archive = directory / "sample.tar.gz"
            payload = b"alpha\nbeta\ngamma\n"
            with tarfile.open(archive, "w:gz") as handle:
                info = tarfile.TarInfo("nested/Thunderbird.log")
                info.size = len(payload)
                handle.addfile(info, io.BytesIO(payload))
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            dest = directory / "Thunderbird_1m.log"
            prefix = hashlib.sha256(b"alpha\nbeta\n").hexdigest()
            mismatch = run_fetch(
                "--dataset",
                "thunderbird",
                "--url",
                archive.as_uri(),
                "--sha256",
                digest,
                "--lines",
                "2",
                "--prefix-sha256",
                "a" * 64,
                "--dest",
                str(dest),
            )
            self.assertEqual(mismatch.returncode, 1, mismatch.stdout)
            self.assertIn("prefix sha256 mismatch", mismatch.stderr)
            self.assertFalse(dest.exists())
            first = run_fetch(
                "--dataset",
                "thunderbird",
                "--url",
                archive.as_uri(),
                "--sha256",
                digest,
                "--lines",
                "2",
                "--prefix-sha256",
                prefix,
                "--dest",
                str(dest),
            )
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(dest.read_bytes(), b"alpha\nbeta\n")
            archive.unlink()
            second = run_fetch(
                "--dataset",
                "thunderbird",
                "--url",
                archive.as_uri(),
                "--sha256",
                digest,
                "--lines",
                "2",
                "--prefix-sha256",
                prefix,
                "--dest",
                str(dest),
            )
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertIn("cached", second.stdout)
            self.assertNotIn("wrote", second.stdout)

    def test_hdfs_2k_rejects_a_line_prefix(self) -> None:
        result = run_fetch("--dataset", "hdfs-2k", "--lines", "10")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("hdfs-2k", result.stderr)


if __name__ == "__main__":
    unittest.main()
