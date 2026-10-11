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


def model_dir(directory: pathlib.Path) -> pathlib.Path:
    models = directory / "models"
    models.mkdir()
    (models / "vocab.txt").write_text("token\n")
    (models / "model.onnx").write_bytes(b"onnx")
    return models


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
            binary.write_bytes(b"not-an-elf")
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
            binary.write_bytes(b"not-an-elf")
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
            self.assertIn(f"libc6 (>= {highest})", depends(out / "nesso_0.1.0_amd64.deb"))


if __name__ == "__main__":
    unittest.main()
