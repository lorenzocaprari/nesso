import os
import pathlib
import re
import stat
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "scripts" / "package-deb"


def write_executable(path: pathlib.Path, text: str) -> None:
    path.write_text(text)
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


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
    (doc / "copyright").write_text("nesso\nCopyright (c) 2026 Lorenzo Caprari\n")
    (doc / "THIRD_PARTY_NOTICES").write_text("nesso third-party notices\n")
    return doc


def listing(deb: pathlib.Path) -> str:
    result = subprocess.run(["dpkg-deb", "-c", str(deb)], capture_output=True, text=True, check=True)
    return result.stdout


def depends(deb: pathlib.Path) -> str:
    result = subprocess.run(
        ["dpkg-deb", "-f", str(deb), "Depends"],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


class PackageDebTest(unittest.TestCase):
    def test_depends_uses_highest_glibc_symbol_version(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            bindir = directory / "bin"
            bindir.mkdir()
            write_executable(
                bindir / "objdump",
                textwrap.dedent(
                    """\
                    #!/bin/sh
                    printf '%s\\n' \\
                      '000000 DF *UND* 000000 GLIBC_2.2.5 old' \\
                      '000000 DF *UND* 000000 GLIBC_2.9 mid' \\
                      '000000 DF *UND* 000000 GLIBC_2.43 new'
                    """
                ),
            )
            binary = directory / "nesso"
            binary.write_bytes(elf64(62))
            doc_dir(directory)
            out = directory / "out"
            env = os.environ.copy()
            env["PATH"] = f"{bindir}{os.pathsep}{env.get('PATH', '')}"
            result = subprocess.run(
                [str(PACKAGE), "0.1.0", str(binary), str(model_dir(directory)), str(out)],
                capture_output=True,
                text=True,
                env=env,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                depends(out / "nesso_0.1.0_amd64.deb"),
                "libc6 (>= 2.43), libstdc++6, libgomp1",
            )

    def test_rejects_a_binary_with_no_glibc_symbols(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            bindir = directory / "bin"
            bindir.mkdir()
            write_executable(bindir / "objdump", "#!/bin/sh\nprintf '%s\\n' 'no versioned symbols'\n")
            binary = directory / "nesso"
            binary.write_bytes(elf64(62))
            doc_dir(directory)
            env = os.environ.copy()
            env["PATH"] = f"{bindir}{os.pathsep}{env.get('PATH', '')}"
            result = subprocess.run(
                [str(PACKAGE), "0.1.0", str(binary), str(model_dir(directory)), str(directory / "out")],
                capture_output=True,
                text=True,
                env=env,
                check=False,
            )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no GLIBC_ symbol versions", result.stderr)

    def test_depends_matches_objdump_of_a_real_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            binary = directory / "hello"
            compiled = subprocess.run(
                ["gcc", "-o", str(binary), "-x", "c", "-"],
                input=b"int main(void){return 0;}\n",
                capture_output=True,
                check=False,
            )
            if compiled.returncode != 0:
                self.skipTest(compiled.stderr.decode())
            doc_dir(directory)
            dump = subprocess.run(
                ["objdump", "-T", str(binary)],
                capture_output=True,
                text=True,
                check=True,
            )
            versions = re.findall(r"GLIBC_([0-9]+(?:\.[0-9]+)+)", dump.stdout)
            self.assertTrue(versions, dump.stdout)
            highest = max(versions, key=lambda version: tuple(int(part) for part in version.split(".")))
            out = directory / "out"
            result = subprocess.run(
                [str(PACKAGE), "0.1.0", str(binary), str(model_dir(directory)), str(out)],
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            debs = list(out.glob("nesso_0.1.0_*.deb"))
            self.assertEqual(len(debs), 1, result.stdout)
            self.assertIn(f"libc6 (>= {highest})", depends(debs[0]))

    def run_with_stub_objdump(self, directory: pathlib.Path, env_extra: dict[str, str] | None = None):
        bindir = directory / "bin"
        bindir.mkdir(exist_ok=True)
        write_executable(bindir / "objdump", "#!/bin/sh\nprintf '%s\\n' '000000 DF *UND* 000000 GLIBC_2.34 sym'\n")
        binary = directory / "nesso"
        binary.write_bytes(elf64(62))
        env = os.environ.copy()
        env["PATH"] = f"{bindir}{os.pathsep}{env.get('PATH', '')}"
        env["SOURCE_DATE_EPOCH"] = "1700000000"
        env.pop("DOC_DIR", None)
        env.update(env_extra or {})
        models = directory / "models" if (directory / "models").is_dir() else model_dir(directory)
        return subprocess.run(
            [str(PACKAGE), "0.1.0", str(binary), str(models), str(directory / "out")],
            capture_output=True,
            text=True,
            env=env,
            check=False,
        )

    def test_ships_copyright_and_third_party_notices(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            doc_dir(directory)
            result = self.run_with_stub_objdump(directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            files = listing(directory / "out" / "nesso_0.1.0_amd64.deb")
            for path in ("./usr/share/doc/nesso/copyright", "./usr/share/doc/nesso/THIRD_PARTY_NOTICES"):
                line = next((entry for entry in files.splitlines() if entry.endswith(" " + path)), "")
                self.assertTrue(line.startswith("-rw-r--r-- root/root"), files)

    def test_doc_dir_can_be_given_explicitly(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            other = directory / "elsewhere"
            other.mkdir()
            doc = doc_dir(other)
            result = self.run_with_stub_objdump(directory, {"DOC_DIR": str(doc)})
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("./usr/share/doc/nesso/THIRD_PARTY_NOTICES", listing(directory / "out" / "nesso_0.1.0_amd64.deb"))

    def test_rejects_a_missing_or_empty_doc_dir(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            result = self.run_with_stub_objdump(directory)
            self.assertEqual(result.returncode, 2)
            self.assertIn("THIRD_PARTY_NOTICES", result.stderr)
            doc = doc_dir(directory)
            (doc / "THIRD_PARTY_NOTICES").write_text("")
            result = self.run_with_stub_objdump(directory)
            self.assertEqual(result.returncode, 2)
            self.assertFalse((directory / "out").exists())


if __name__ == "__main__":
    unittest.main()
