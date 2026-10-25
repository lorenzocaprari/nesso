import os
import pathlib
import shutil
import stat
import subprocess
import tarfile
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ARCHIVE = ROOT / "scripts" / "ci-build-archive"


def write_file(path: pathlib.Path, data: bytes, mode: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    path.chmod(mode)


def path_without_zstd(directory: pathlib.Path) -> str:
    bindir = directory / "bin"
    bindir.mkdir(parents=True)
    for name in ("bash", "tar", "find", "sort", "wc", "tr", "stat", "mktemp", "rm", "gzip"):
        source = shutil.which(name)
        if source is None:
            raise unittest.SkipTest(f"{name} is not on PATH")
        os.symlink(source, bindir / name)
    return str(bindir)


def archive_names(lines: list[str]) -> set[str]:
    names = set()
    for line in lines:
        parts = line.split()
        if len(parts) >= 6:
            names.add(parts[-1])
    return names


class CiBuildArchiveTest(unittest.TestCase):
    def pack(self, root: pathlib.Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        build = root / "build" / "Release"
        write_file(build / "src" / "nesso", b"elf-nesso", 0o755)
        write_file(
            build / "test" / "unit" / "core" / "distance_test",
            b"elf-test",
            0o755,
        )
        write_file(build / "test" / "fixtures" / "sample.log", b"log\n", 0o644)
        write_file(build / "libnesso.so", b"shared", 0o644)
        write_file(
            build / "test" / "unit" / "core" / "CTestTestfile.cmake",
            b"add_test\n",
            0o644,
        )
        write_file(build / "CTestTestfile.cmake", b"subdirs\n", 0o644)
        write_file(build / "src" / "nesso.cpp.o", b"object", 0o644)
        write_file(build / "libnesso.a", b"static", 0o644)
        write_file(build / "src" / "nesso.cpp.gcno", b"notes", 0o644)
        write_file(build / "src" / "nesso.cpp.gcda", b"data", 0o644)
        write_file(
            build / "CMakeFiles" / "nesso.dir" / "nesso.cpp.o",
            b"cmake-object",
            0o644,
        )
        write_file(
            build / "CMakeFiles" / "nesso.dir" / "nesso.cpp.o.ddi.i",
            b"scan",
            0o644,
        )
        write_file(build / "generators" / "conan_toolchain.cmake", b"cmake", 0o644)
        write_file(build / "build.ninja", b"ninja", 0o644)
        write_file(build / "compile_commands.json", b"[]", 0o644)
        return subprocess.run(
            [str(ARCHIVE), "pack", "build/Release", "release-build"],
            cwd=root,
            check=True,
            capture_output=True,
            text=True,
            env=env,
        )

    def assert_archive(self, root: pathlib.Path, result: subprocess.CompletedProcess[str], kind: str) -> None:
        suffix = "tar.zst" if kind == "zstd" else "tar.gz"
        other = "tar.gz" if kind == "zstd" else "tar.zst"
        archive = root / f"release-build.{suffix}"
        self.assertIn(f"compressor={kind}", result.stdout)
        self.assertTrue(archive.is_file())
        self.assertFalse((root / f"release-build.{other}").exists())

        if kind == "gzip":
            with tarfile.open(archive, "r:gz") as tar:
                names = set(tar.getnames())
                nesso = tar.getmember("build/Release/src/nesso")
                self.assertEqual(nesso.mode & 0o777, 0o755)
        else:
            listing = subprocess.run(
                ["tar", "-I", "zstd", "-tvf", str(archive)],
                check=True,
                capture_output=True,
                text=True,
            )
            names = archive_names(listing.stdout.splitlines())
            mode = next(
                line.split()[0]
                for line in listing.stdout.splitlines()
                if line.endswith("build/Release/src/nesso")
            )
            self.assertEqual(mode, "-rwxr-xr-x")

        self.assertIn("build/Release/src/nesso", names)
        self.assertIn("build/Release/test/unit/core/distance_test", names)
        self.assertIn("build/Release/test/fixtures/sample.log", names)
        self.assertIn("build/Release/libnesso.so", names)
        self.assertIn("build/Release/CTestTestfile.cmake", names)
        self.assertIn("build/Release/test/unit/core/CTestTestfile.cmake", names)
        self.assertNotIn("build/Release/src/nesso.cpp.o", names)
        self.assertNotIn("build/Release/libnesso.a", names)
        self.assertNotIn("build/Release/src/nesso.cpp.gcno", names)
        self.assertNotIn("build/Release/src/nesso.cpp.gcda", names)
        self.assertFalse(any("CMakeFiles" in name for name in names))
        self.assertFalse(any(name.endswith(".cmake") and "CTest" not in name for name in names))
        self.assertNotIn("build/Release/build.ninja", names)
        self.assertNotIn("build/Release/compile_commands.json", names)

        unpack_into = root / "unpacked"
        unpack_into.mkdir()
        os.link(archive, unpack_into / archive.name)
        subprocess.run(
            [str(ARCHIVE), "unpack", "release-build"],
            cwd=unpack_into,
            check=True,
        )
        restored = unpack_into / "build" / "Release" / "src" / "nesso"
        self.assertEqual(restored.read_bytes(), b"elf-nesso")
        self.assertTrue(restored.stat().st_mode & stat.S_IXUSR)
        self.assertEqual(restored.stat().st_mode & 0o777, 0o755)

    def test_pack_keeps_runtime_files_and_permissions(self) -> None:
        kind = "zstd" if shutil.which("zstd") else "gzip"
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            result = self.pack(root)
            self.assert_archive(root, result, kind)

    def test_pack_uses_gzip_when_zstd_is_absent(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            env = os.environ.copy()
            env["PATH"] = path_without_zstd(root / "path")
            result = self.pack(root, env)
            self.assert_archive(root, result, "gzip")


if __name__ == "__main__":
    unittest.main()
