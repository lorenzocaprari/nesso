import os
import pathlib
import shutil
import stat
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "packaging" / "scripts" / "package-rpm"
SCRIPT_TEXT = PACKAGE.read_text(encoding="utf-8")
HAS_RPMBUILD = shutil.which("rpmbuild") is not None


def elf64(machine: int) -> bytes:
    buf = bytearray(64)
    buf[0:4] = b"\x7fELF"
    buf[4] = 2
    buf[5] = 1
    buf[6] = 1
    buf[18] = machine & 0xFF
    buf[19] = (machine >> 8) & 0xFF
    return bytes(buf)


def model_dir(directory: pathlib.Path) -> pathlib.Path:
    models = directory / "models"
    models.mkdir()
    (models / "vocab.txt").write_text("token\n")
    (models / "model.onnx").write_bytes(b"onnx")
    return models


def doc_dir(directory: pathlib.Path) -> pathlib.Path:
    doc = directory / "doc"
    doc.mkdir(exist_ok=True)
    (doc / "copyright").write_text("nesso\nCopyright (c) 2026 Lorenzo Caprari\nnesso third-party notices\n")
    (doc / "THIRD_PARTY_NOTICES").write_text("nesso third-party notices\n")
    return doc


class PackageRpmTest(unittest.TestCase):
    def test_script_declares_reproducibility_macros(self) -> None:
        for needle in (
            '_buildhost reproducible',
            "use_source_date_epoch_as_buildtime 1",
            "clamp_mtime_to_source_date_epoch 1",
            "source_date_epoch_from_changelog 0",
            "debug_package %{nil}",
            "_build_id_links none",
            "__brp_strip %{nil}",
            "dist %{nil}",
        ):
            self.assertIn(needle, SCRIPT_TEXT, needle)

    def test_script_pins_rpm_name_without_dist(self) -> None:
        self.assertIn('nesso-${VERSION}-1.${RPM_ARCH}.rpm', SCRIPT_TEXT)
        self.assertIn("%license", SCRIPT_TEXT)
        self.assertIn("%doc", SCRIPT_TEXT)
        self.assertIn("/usr/bin/nesso", SCRIPT_TEXT)
        self.assertIn("/usr/share/nesso/vocab.txt", SCRIPT_TEXT)
        self.assertIn("/usr/share/nesso/model.onnx", SCRIPT_TEXT)

    def test_rejects_bad_arity(self) -> None:
        result = subprocess.run([str(PACKAGE)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage: package-rpm", result.stderr)

    def test_rejects_bad_version(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = directory / "nesso"
            binary.write_bytes(elf64(62))
            binary.chmod(binary.stat().st_mode | stat.S_IXUSR)
            doc_dir(directory)
            result = subprocess.run(
                [str(PACKAGE), "v0.1.0", str(binary), str(model_dir(directory)), str(directory / "out")],
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 2)
        self.assertIn("VERSION must be X.Y.Z", result.stderr)

    def test_rejects_a_missing_or_empty_doc_dir(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = directory / "nesso"
            binary.write_bytes(elf64(62))
            binary.chmod(binary.stat().st_mode | stat.S_IXUSR)
            models = model_dir(directory)
            result = subprocess.run(
                [str(PACKAGE), "0.1.0", str(binary), str(models), str(directory / "out")],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("THIRD_PARTY_NOTICES", result.stderr)
            self.assertFalse((directory / "out").exists())
            doc = doc_dir(directory)
            (doc / "THIRD_PARTY_NOTICES").write_text("")
            result = subprocess.run(
                [str(PACKAGE), "0.1.0", str(binary), str(models), str(directory / "out")],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 2)

    @unittest.skipUnless(HAS_RPMBUILD, "rpmbuild is not installed")
    def test_builds_pinned_name_with_expected_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = directory / "nesso"
            binary.write_bytes(elf64(62))
            binary.chmod(binary.stat().st_mode | stat.S_IXUSR)
            doc_dir(directory)
            out = directory / "out"
            env = os.environ.copy()
            env["SOURCE_DATE_EPOCH"] = "1700000000"
            result = subprocess.run(
                [str(PACKAGE), "0.1.0", str(binary), str(model_dir(directory)), str(out)],
                capture_output=True,
                text=True,
                env=env,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            rpm = out / "nesso-0.1.0-1.x86_64.rpm"
            self.assertTrue(rpm.is_file(), result.stdout)
            self.assertEqual(result.stdout.strip(), str(rpm.resolve()))

            query = subprocess.run(
                [
                    "rpm",
                    "-qp",
                    "--qf",
                    "%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}\n",
                    str(rpm),
                ],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertEqual(query.stdout.strip(), "nesso-0.1.0-1.x86_64")

            files = subprocess.run(
                ["rpm", "-qlp", str(rpm)],
                capture_output=True,
                text=True,
                check=True,
            ).stdout.splitlines()
            for path in (
                "/usr/bin/nesso",
                "/usr/share/nesso/vocab.txt",
                "/usr/share/nesso/model.onnx",
                "/usr/share/doc/nesso/copyright",
                "/usr/share/doc/nesso/THIRD_PARTY_NOTICES",
            ):
                self.assertIn(path, files, files)

    @unittest.skipUnless(HAS_RPMBUILD, "rpmbuild is not installed")
    def test_spec_fields_from_built_rpm(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = directory / "nesso"
            binary.write_bytes(elf64(183))
            binary.chmod(binary.stat().st_mode | stat.S_IXUSR)
            doc_dir(directory)
            out = directory / "out"
            env = os.environ.copy()
            env["SOURCE_DATE_EPOCH"] = "1700000000"
            result = subprocess.run(
                [str(PACKAGE), "1.2.3", str(binary), str(model_dir(directory)), str(out)],
                capture_output=True,
                text=True,
                env=env,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            rpm = out / "nesso-1.2.3-1.aarch64.rpm"
            self.assertTrue(rpm.is_file(), result.stdout)
            fields = subprocess.run(
                [
                    "rpm",
                    "-qp",
                    "--qf",
                    "%{NAME}|%{VERSION}|%{RELEASE}|%{ARCH}|%{LICENSE}\n",
                    str(rpm),
                ],
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
            self.assertEqual(fields, "nesso|1.2.3|1|aarch64|Apache-2.0")


if __name__ == "__main__":
    unittest.main()
