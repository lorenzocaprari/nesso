import os
import pathlib
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


class CiBuildArchiveTest(unittest.TestCase):
    def test_pack_keeps_runtime_files_and_permissions(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
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

            result = subprocess.run(
                [str(ARCHIVE), "pack", "build/Release", "release-build"],
                cwd=root,
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertIn("compressor=gzip", result.stdout)
            self.assertTrue((root / "release-build.tar.gz").is_file())
            self.assertFalse((root / "release-build.tar.zst").exists())

            with tarfile.open(root / "release-build.tar.gz", "r:gz") as tar:
                names = set(tar.getnames())
                nesso = tar.getmember("build/Release/src/nesso")
                self.assertEqual(nesso.mode & 0o777, 0o755)

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
            os.link(root / "release-build.tar.gz", unpack_into / "release-build.tar.gz")
            subprocess.run(
                [str(ARCHIVE), "unpack", "release-build"],
                cwd=unpack_into,
                check=True,
            )
            restored = unpack_into / "build" / "Release" / "src" / "nesso"
            self.assertEqual(restored.read_bytes(), b"elf-nesso")
            self.assertTrue(restored.stat().st_mode & stat.S_IXUSR)
            self.assertEqual(restored.stat().st_mode & 0o777, 0o755)


if __name__ == "__main__":
    unittest.main()
