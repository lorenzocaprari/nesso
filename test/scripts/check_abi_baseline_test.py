import os
import pathlib
import stat
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check-abi-baseline"
INTERP_OFF = 0x200
X86_INTERP = "/lib64/ld-linux-x86-64.so.2"
AARCH64_INTERP = "/lib/ld-linux-aarch64.so.1"


def interp_program_header(interp: str) -> str:
    size = len(interp.encode("ascii")) + 1
    return (
        "  INTERP off    0x0000000000000200 vaddr 0x0000000000000200 "
        "paddr 0x0000000000000200 align 2**0\n"
        f"         filesz 0x{size:016x} memsz 0x{size:016x} flags r--\n"
    )


def binary_dump(needed: list[str], refs: list[tuple[str, str]], interp: str, file_format: str) -> str:
    lines = [
        f"fixture:     file format {file_format}",
        "",
        "Program Header:",
        interp_program_header(interp).rstrip("\n"),
        "Dynamic Section:",
    ]
    for soname in needed:
        lines.append(f"  NEEDED               {soname}")
    lines.append("")
    lines.append("Version References:")
    grouped: dict[str, list[str]] = {}
    for soname, version in refs:
        grouped.setdefault(soname, []).append(version)
    for soname, versions in grouped.items():
        lines.append(f"  required from {soname}:")
        for index, version in enumerate(versions, start=2):
            lines.append(f"    0x09691a75 0x00 {index:02d} {version}")
    lines.append("")
    return "\n".join(lines) + "\n"


def defs_dump(soname: str, versions: list[str]) -> str:
    lines = [
        f"{soname}:     file format elf64-x86-64",
        "",
        "Version definitions:",
        f"1 0x01 0x0865f4e6 {soname}",
    ]
    for index, version in enumerate(versions, start=2):
        lines.append(f"{index} 0x00 0x09691a75 {version}")
        if index > 2:
            lines.append(f"\t{versions[0]} ")
    return "\n".join(lines) + "\n"


def write_executable(path: pathlib.Path, body: str) -> None:
    path.write_text(body)
    path.chmod(path.stat().st_mode | stat.S_IEXEC)


def write_binary(path: pathlib.Path, interp: str) -> None:
    raw = interp.encode("ascii") + b"\0"
    blob = bytearray(INTERP_OFF + len(raw))
    blob[INTERP_OFF : INTERP_OFF + len(raw)] = raw
    path.write_bytes(blob)


class CheckAbiBaselineTest(unittest.TestCase):
    def run_case(
        self,
        needed: list[str],
        refs: list[tuple[str, str]],
        defs: dict[str, list[str]],
        interp: str = X86_INTERP,
        file_format: str = "elf64-x86-64",
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            lib_dir = root / "libs"
            dump_dir = root / "dumps"
            lib_dir.mkdir()
            dump_dir.mkdir()
            binary = root / "nesso"
            write_binary(binary, interp)
            (dump_dir / "nesso").write_text(binary_dump(needed, refs, interp, file_format))
            for soname, versions in defs.items():
                (lib_dir / soname).write_bytes(b"")
                (dump_dir / soname).write_text(defs_dump(soname, versions))
            objdump = root / "objdump"
            resolve = root / "resolve"
            write_executable(
                objdump,
                "#!/bin/sh\n"
                "set -eu\n"
                'file=""\n'
                'prev=""\n'
                'for arg in "$@"; do\n'
                '  if [ "${prev}" = "-p" ]; then file="${arg}"; fi\n'
                '  prev="${arg}"\n'
                "done\n"
                'base=$(basename "${file}")\n'
                f'cat "{dump_dir}/${{base}}"\n',
            )
            write_executable(
                resolve,
                "#!/bin/sh\n"
                "set -eu\n"
                'printf "%s\\n" "${NESSO_TEST_LIB_DIR}/$1"\n',
            )
            env = dict(os.environ)
            env["OBJDUMP"] = str(objdump)
            env["NESSO_LIB_RESOLVE"] = str(resolve)
            env["NESSO_TEST_LIB_DIR"] = str(lib_dir)
            return subprocess.run(
                ["bash", str(SCRIPT), str(binary)],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )

    def baseline(self) -> tuple[list[str], list[tuple[str, str]], dict[str, list[str]]]:
        needed = ["libc.so.6", "libm.so.6", "libstdc++.so.6", "libgcc_s.so.1", "libgomp.so.1"]
        refs = [
            ("libc.so.6", "GLIBC_2.2.5"),
            ("libc.so.6", "GLIBC_2.34"),
            ("libstdc++.so.6", "GLIBCXX_3.4.9"),
            ("libstdc++.so.6", "GLIBCXX_3.4.29"),
            ("libstdc++.so.6", "CXXABI_1.3.13"),
            ("libgcc_s.so.1", "GCC_3.0"),
            ("libgomp.so.1", "GOMP_1.0"),
            ("libgomp.so.1", "OMP_1.0"),
        ]
        defs = {
            "libc.so.6": ["GLIBC_2.2.5", "GLIBC_2.34"],
            "libstdc++.so.6": ["GLIBCXX_3.4.9", "GLIBCXX_3.4.29", "CXXABI_1.3.13"],
            "libgcc_s.so.1": ["GCC_3.0"],
            "libgomp.so.1": ["GOMP_1.0", "OMP_1.0"],
        }
        return needed, refs, defs

    def test_clean_pass(self) -> None:
        needed, refs, defs = self.baseline()
        result = self.run_case(needed, refs, defs)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(
            result.stdout,
            "check-abi-baseline: GLIBC_2.34 GLIBCXX_3.4.29 CXXABI_1.3.13\n",
        )

    def test_glibc_above_2_34(self) -> None:
        needed, refs, defs = self.baseline()
        refs.append(("libc.so.6", "GLIBC_2.35"))
        defs["libc.so.6"].append("GLIBC_2.35")
        result = self.run_case(needed, refs, defs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GLIBC_2.35 is newer than GLIBC_2.34", result.stderr)
        self.assertNotIn("check-abi-baseline:", result.stdout)

    def test_glibcxx_above_3_4_29(self) -> None:
        needed, refs, defs = self.baseline()
        refs.append(("libstdc++.so.6", "GLIBCXX_3.4.30"))
        defs["libstdc++.so.6"].append("GLIBCXX_3.4.30")
        result = self.run_case(needed, refs, defs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GLIBCXX_3.4.30 is newer than GLIBCXX_3.4.29", result.stderr)

    def test_cxxabi_above_1_3_13(self) -> None:
        needed, refs, defs = self.baseline()
        refs.append(("libstdc++.so.6", "CXXABI_1.3.14"))
        defs["libstdc++.so.6"].append("CXXABI_1.3.14")
        result = self.run_case(needed, refs, defs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CXXABI_1.3.14 is newer than CXXABI_1.3.13", result.stderr)

    def test_missing_version_definition(self) -> None:
        needed, refs, defs = self.baseline()
        defs["libc.so.6"] = ["GLIBC_2.2.5"]
        result = self.run_case(needed, refs, defs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GLIBC_2.34 is not defined by libc.so.6", result.stderr)
        self.assertNotIn("newer than", result.stderr)

    def test_extra_needed(self) -> None:
        needed, refs, defs = self.baseline()
        needed.append("libselinux.so.1")
        result = self.run_case(needed, refs, defs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NEEDED libselinux.so.1 is outside the baseline set", result.stderr)

    def test_wrong_interp(self) -> None:
        needed, refs, defs = self.baseline()
        result = self.run_case(needed, refs, defs, interp="/lib/ld-linux.so.2")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "PT_INTERP is /lib/ld-linux.so.2, want /lib64/ld-linux-x86-64.so.2",
            result.stderr,
        )

    def test_clean_pass_aarch64(self) -> None:
        needed, refs, defs = self.baseline()
        result = self.run_case(
            needed,
            refs,
            defs,
            interp=AARCH64_INTERP,
            file_format="elf64-littleaarch64",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "check-abi-baseline: GLIBC_2.34 GLIBCXX_3.4.29 CXXABI_1.3.13\n",
        )


if __name__ == "__main__":
    unittest.main()
