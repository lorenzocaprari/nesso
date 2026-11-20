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
PINNED_STRUCTURED_SHA256 = "729df59774e3dde934044028546d2a55d5e3d4370b9d12fcebbe4c087b2bf7b4"


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
        self.assertRegex(
            module.HDFS_STRUCTURED_URL,
            r"^https://raw\.githubusercontent\.com/logpai/loghub/[0-9a-f]{40}/HDFS/HDFS_2k\.log_structured\.csv$",
        )
        self.assertNotIn("/master/", module.HDFS_STRUCTURED_URL)
        self.assertNotIn("/main/", module.HDFS_STRUCTURED_URL)
        self.assertEqual(module.HDFS_STRUCTURED_SHA256, PINNED_STRUCTURED_SHA256)
        self.assertEqual(
            module.DEFAULT_STRUCTURED_DEST.relative_to(ROOT),
            pathlib.Path("build/bench/HDFS_2k.log_structured.csv"),
        )
        self.assertEqual(module.HDFS_COMMIT, module.HDFS_URL.split("/")[5])
        self.assertEqual(module.HDFS_COMMIT, module.HDFS_STRUCTURED_URL.split("/")[5])
        self.assertEqual(module.LOGHUB_COMMIT, module.HDFS_COMMIT)

    def test_samples_2k_pins_and_names(self) -> None:
        module = load_fetch()
        self.assertEqual(
            set(module.SAMPLES_2K),
            {"hdfs-2k", "bgl-2k", "thunderbird-2k", "openssh-2k", "linux-2k", "apache-2k"},
        )
        self.assertEqual(set(module.SAMPLES_2K) & set(module.FULL_SETS), set())
        expected = {
            "hdfs-2k": (
                "HDFS",
                "HDFS_2k.log",
                PINNED_SHA256,
                PINNED_STRUCTURED_SHA256,
            ),
            "bgl-2k": (
                "BGL",
                "BGL_2k.log",
                "2a819ea540909db682005c9cf948387a40729b5c2e9f19d430e29ce704825496",
                "3fe74103c0b02a28514534e2a47257a3f770135ca61afd425bbd3b9d6a31fe26",
            ),
            "thunderbird-2k": (
                "Thunderbird",
                "Thunderbird_2k.log",
                "903bbfa61c34d4803e4adcb0d726ff2eeb9a2e11971243269a2035fa6c3bbeb0",
                "c62196d8a3449de765501bbf58c2093452e9d1add439053301d29fdad8a06809",
            ),
            "openssh-2k": (
                "OpenSSH",
                "OpenSSH_2k.log",
                "1e4912727fa88245113d41b16a0cd25ceadba7f931e1c406542885b91254264f",
                "c0996a11545f4b94b435993760afa441a9e373f7bfc9e787afdb8e62f65acb4f",
            ),
            "linux-2k": (
                "Linux",
                "Linux_2k.log",
                "b3e20bc1afe732ab1bf3ed1de4bf9c809e4194e02f7dea911d918e5342e8e173",
                "7c86d7b0ecb961a25f00d9475a154df97613b9974f31ce142a146caa2017c71e",
            ),
            "apache-2k": (
                "Apache",
                "Apache_2k.log",
                "c7efa3eb686e3a96bd2f8f4457b2a7887e9cf2f3649327f1b4e87af841363ce8",
                "54331d12eedf513f2127f4d89f0284c8b15fddfa5471103abf9db2c73d737778",
            ),
        }
        for name, (folder, log_name, log_sha, structured_sha) in expected.items():
            spec = module.SAMPLES_2K[name]
            self.assertEqual(spec.folder, folder)
            self.assertEqual(spec.log_name, log_name)
            self.assertEqual(spec.log_sha256, log_sha)
            self.assertEqual(spec.structured_sha256, structured_sha)
            self.assertEqual(spec.log_url.split("/")[5], module.LOGHUB_COMMIT)
            self.assertEqual(spec.structured_url.split("/")[5], module.LOGHUB_COMMIT)
            self.assertIn(f"/{folder}/{log_name}", spec.log_url)
            self.assertIn(f"/{folder}/{log_name}_structured.csv", spec.structured_url)
            self.assertNotIn("/master/", spec.log_url)
            self.assertNotIn("/main/", spec.log_url)

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

    def test_bgl_2k_rejects_a_line_prefix(self) -> None:
        result = run_fetch("--dataset", "bgl-2k", "--lines", "10")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("bgl-2k", result.stderr)

    def test_all_2k_fetches_offline_fixtures(self) -> None:
        module = load_fetch()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            dest_dir = directory / "bench"
            dest_dir.mkdir()
            fixtures: dict[str, bytes] = {}
            samples: dict[str, object] = {}
            for name, old in module.SAMPLES_2K.items():
                log_payload = f"{name}-log\n".encode()
                csv_payload = f"{name}-csv\n".encode()
                log_src = directory / f"{name}.log"
                csv_src = directory / f"{name}.csv"
                log_src.write_bytes(log_payload)
                csv_src.write_bytes(csv_payload)
                spec = module.Sample2k(
                    name=name,
                    folder=old.folder,
                    log_name=old.log_name,
                    log_sha256=hashlib.sha256(log_payload).hexdigest(),
                    structured_sha256=hashlib.sha256(csv_payload).hexdigest(),
                )
                samples[name] = spec
                fixtures[spec.log_url] = log_payload
                fixtures[spec.structured_url] = csv_payload

            def fake_download(url: str, dest: pathlib.Path, timeout: int = 120) -> None:
                del timeout
                if url not in fixtures:
                    raise AssertionError(f"unexpected url {url}")
                dest.write_bytes(fixtures[url])

            original_samples = module.SAMPLES_2K
            original_download = module.download
            module.SAMPLES_2K = samples
            module.download = fake_download
            try:
                module.main(["--dataset", "all-2k", "--dest-dir", str(dest_dir)])
            finally:
                module.SAMPLES_2K = original_samples
                module.download = original_download
            for spec in samples.values():
                self.assertEqual((dest_dir / spec.log_name).read_bytes(), fixtures[spec.log_url])
                self.assertEqual(
                    (dest_dir / f"{spec.log_name}_structured.csv").read_bytes(),
                    fixtures[spec.structured_url],
                )

    def test_all_2k_rejects_dest_and_overrides(self) -> None:
        bad_dest = run_fetch("--dataset", "all-2k", "--dest", "/tmp/x.log")
        self.assertEqual(bad_dest.returncode, 1, bad_dest.stdout)
        self.assertIn("all-2k", bad_dest.stderr)
        bad_url = run_fetch("--dataset", "all-2k", "--url", "https://example.invalid/x")
        self.assertEqual(bad_url.returncode, 1, bad_url.stdout)
        self.assertIn("all-2k", bad_url.stderr)


_LABELLED_LINES = [
    b"- 1 2005.06.03 R00 normal one\n",
    b"KERNDTLB 2 2005.06.03 R01 data TLB error interrupt\n",
    b"- 3 2005.06.03 R02 normal three\n",
    b"APPSEV 4 2005.06.03 R03 ciod: failed to read message\n",
    b"- 5 2005.06.03 R04 normal five\n",
    b"KERN_2 6 2005.06.03 R05 tagged with digits and underscore\n",
]


def _labelled_zip(directory: pathlib.Path, lines: list[bytes], member: str = "BGL/BGL_full.log") -> tuple[pathlib.Path, str]:
    archive = directory / "BGL.zip"
    with zipfile.ZipFile(archive, "w") as handle:
        handle.writestr(member, b"".join(lines))
    return archive, hashlib.sha256(archive.read_bytes()).hexdigest()


class FetchLoghubLabelledWindowTest(unittest.TestCase):
    def test_sources_are_pinned_to_the_loghub2_record(self) -> None:
        module = load_fetch()
        self.assertEqual(set(module.LABELLED_SETS), {"bgl-loghub2", "thunderbird-loghub2"})
        for spec in module.LABELLED_SETS.values():
            self.assertTrue(spec.url.startswith("https://zenodo.org/records/8275861/files/"))
            self.assertEqual(len(spec.archive_sha256), 64)
            self.assertTrue(spec.member.endswith("_full.log"))
        self.assertEqual(
            set(module.WINDOW_PINS),
            {
                ("bgl-loghub2", 4_100_001, 100_000),
                ("bgl-loghub2", 3_200_001, 1_000_000),
                ("thunderbird-loghub2", 8_550_001, 1_000_000),
            },
        )

    def test_windows_strip_the_label_and_write_a_sidecar(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            archive, digest = _labelled_zip(directory, _LABELLED_LINES)
            out = directory / "out"
            result = run_fetch(
                "--dataset",
                "bgl-loghub2",
                "--url",
                archive.as_uri(),
                "--sha256",
                digest,
                "--window",
                "2:3",
                "--window",
                "5:2",
                "--dest-dir",
                str(out),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            first_log = (out / "BGL_loghub2_2_3.log").read_bytes()
            first_labels = (out / "BGL_loghub2_2_3.labels").read_text(encoding="utf-8")
            second_log = (out / "BGL_loghub2_5_2.log").read_bytes()
            second_labels = (out / "BGL_loghub2_5_2.labels").read_text(encoding="utf-8")
            leftovers = sorted(path.name for path in out.iterdir())
        self.assertEqual(
            first_log,
            b"2 2005.06.03 R01 data TLB error interrupt\n"
            b"3 2005.06.03 R02 normal three\n"
            b"4 2005.06.03 R03 ciod: failed to read message\n",
        )
        self.assertEqual(first_labels, "KERNDTLB\n-\nAPPSEV\n")
        self.assertEqual(second_log, b"5 2005.06.03 R04 normal five\n6 2005.06.03 R05 tagged with digits and underscore\n")
        self.assertEqual(second_labels, "-\nKERN_2\n")
        # The downloaded archive copy and partial files are removed.
        self.assertEqual(
            leftovers,
            ["BGL_loghub2_2_3.labels", "BGL_loghub2_2_3.log", "BGL_loghub2_5_2.labels", "BGL_loghub2_5_2.log"],
        )

    def test_short_window_and_bad_label_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            archive, digest = _labelled_zip(directory, _LABELLED_LINES)
            short = run_fetch(
                "--dataset", "bgl-loghub2", "--url", archive.as_uri(), "--sha256", digest,
                "--window", "5:10", "--dest-dir", str(directory / "short"),
            )
            self.assertNotEqual(short.returncode, 0)
            self.assertIn("has 2 lines, need 10", short.stderr)
            self.assertFalse((directory / "short" / "BGL_loghub2_5_10.log").exists())

            (directory / "bad").mkdir()
            bad_archive, bad_digest = _labelled_zip(directory / "bad", [b"lowercase 1 x\n"])
            bad = run_fetch(
                "--dataset", "bgl-loghub2", "--url", bad_archive.as_uri(), "--sha256", bad_digest,
                "--window", "1:1", "--dest-dir", str(directory / "bad-out"),
            )
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("is not '-' or an alert category", bad.stderr)

            wrong_pin = run_fetch(
                "--dataset", "bgl-loghub2", "--url", archive.as_uri(), "--sha256", "0" * 64,
                "--window", "1:2", "--dest-dir", str(directory / "pin"),
            )
            self.assertNotEqual(wrong_pin.returncode, 0)
            self.assertIn("sha256 mismatch", wrong_pin.stderr)

    def test_window_flag_rules(self) -> None:
        no_window = run_fetch("--dataset", "bgl-loghub2")
        self.assertNotEqual(no_window.returncode, 0)
        self.assertIn("needs at least one --window", no_window.stderr)
        wrong_dataset = run_fetch("--dataset", "bgl", "--window", "1:5")
        self.assertNotEqual(wrong_dataset.returncode, 0)
        self.assertIn("--window applies to", wrong_dataset.stderr)
        bad_window = run_fetch("--dataset", "bgl-loghub2", "--window", "abc")
        self.assertNotEqual(bad_window.returncode, 0)
        self.assertIn("START:COUNT", bad_window.stderr)
        with_dest = run_fetch("--dataset", "bgl-loghub2", "--window", "1:5", "--dest", "x.log")
        self.assertNotEqual(with_dest.returncode, 0)
        self.assertIn("--dest-dir", with_dest.stderr)

    def test_pinned_window_cache_skips_the_download(self) -> None:
        module = load_fetch()
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            archive, digest = _labelled_zip(directory, _LABELLED_LINES)
            spec = module.LABELLED_SETS["bgl-loghub2"]
            window = module.Window(start=1, count=3)
            out = directory / "out"
            module.fetch_labelled(spec, [window], out, url=archive.as_uri(), expected=digest)
            log_path, labels_path = module.window_paths(spec, window, out)
            module.WINDOW_PINS[("bgl-loghub2", 1, 3)] = (
                module.sha256_file(log_path),
                module.sha256_file(labels_path),
            )
            try:
                # A cached, pinned window never touches the (now missing) URL.
                paths = module.fetch_labelled(
                    spec, [window], out, url=(directory / "missing.zip").as_uri(), expected=digest
                )
                self.assertEqual(paths, [(log_path, labels_path)])
                # A pin mismatch on a fresh write fails and keeps no file.
                log_path.unlink()
                module.WINDOW_PINS[("bgl-loghub2", 1, 3)] = ("0" * 64, "0" * 64)
                with self.assertRaises(SystemExit) as raised:
                    module.fetch_labelled(spec, [window], out, url=archive.as_uri(), expected=digest)
                self.assertIn("window sha256 mismatch", str(raised.exception))
                self.assertFalse(log_path.exists())
            finally:
                del module.WINDOW_PINS[("bgl-loghub2", 1, 3)]


if __name__ == "__main__":
    unittest.main()
