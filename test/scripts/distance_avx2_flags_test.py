import json
import os
import pathlib
import platform
import shlex
import unittest


SOURCE = "distance_avx2.cpp"
FORBIDDEN = ("-mavx2", "-mfma")
X86_64 = {"x86_64", "amd64"}


def command_arguments(entry: dict) -> list[str]:
    arguments = entry.get("arguments")
    if isinstance(arguments, list):
        return [str(argument) for argument in arguments]
    command = entry.get("command")
    if not isinstance(command, str):
        file_name = entry.get("file", "<unknown>")
        raise ValueError(f"{file_name} has neither a command string nor an arguments list")
    return shlex.split(command)


def is_avx2_translation_unit(entry: dict) -> bool:
    file_name = str(entry.get("file", "")).replace("\\", "/")
    return pathlib.PurePosixPath(file_name).name == SOURCE


def forbidden_flags(entries: list[dict]) -> list[str]:
    found: list[str] = []
    for entry in entries:
        if not is_avx2_translation_unit(entry):
            continue
        for argument in command_arguments(entry):
            if argument in FORBIDDEN and argument not in found:
                found.append(argument)
    return found


def check_entries(entries: list[dict], machine: str) -> list[str]:
    errors: list[str] = []
    flags = forbidden_flags(entries)
    if flags:
        errors.append(f"{SOURCE} compiled with {' '.join(flags)}")
    present = any(is_avx2_translation_unit(entry) for entry in entries)
    if machine.lower() in X86_64:
        if not present:
            errors.append(f"{SOURCE} is missing from compile_commands.json")
    elif present:
        errors.append(f"{SOURCE} is compiled on {machine}")
    return errors


def load_entries(path: pathlib.Path) -> list[dict]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, list):
        raise ValueError(f"{path} is not a compile commands list")
    return payload


def resolve_compile_commands(raw: str | None, require: bool) -> pathlib.Path | None:
    """Return the database path. Test jobs unpack an archive that omits it."""
    if not raw:
        if require:
            raise FileNotFoundError("NESSO_COMPILE_COMMANDS is unset")
        return None
    path = pathlib.Path(raw)
    if path.is_file():
        return path
    if require:
        raise FileNotFoundError(path)
    return None


class DistanceAvx2FlagsTest(unittest.TestCase):
    def test_rejects_file_wide_avx2_and_fma_flags(self) -> None:
        entries = [
            {
                "file": "/src/core/distance_avx2.cpp",
                "command": "c++ -O0 -mavx2 -mfma -c /src/core/distance_avx2.cpp",
            }
        ]
        self.assertEqual(check_entries(entries, "x86_64"), [f"{SOURCE} compiled with -mavx2 -mfma"])

    def test_accepts_a_baseline_x86_64_command(self) -> None:
        entries = [
            {
                "file": "src/core/distance_avx2.cpp",
                "arguments": ["c++", "-march=x86-64-v2", "-mno-avx2", "-mfma4", "-c", "src/core/distance_avx2.cpp"],
            }
        ]
        self.assertEqual(check_entries(entries, "x86_64"), [])

    def test_ignores_the_same_flags_on_another_translation_unit(self) -> None:
        entries = [
            {
                "file": "/src/core/distance.cpp",
                "command": "c++ -mavx2 -mfma -c /src/core/distance.cpp",
            },
            {
                "file": "/src/core/distance_avx2.cpp",
                "command": "c++ -march=x86-64-v2 -c /src/core/distance_avx2.cpp",
            },
        ]
        self.assertEqual(check_entries(entries, "amd64"), [])

    def test_requires_the_translation_unit_on_x86_64(self) -> None:
        self.assertEqual(
            check_entries([], "x86_64"),
            [f"{SOURCE} is missing from compile_commands.json"],
        )

    def test_rejects_the_translation_unit_off_x86_64(self) -> None:
        entries = [{"file": "distance_avx2.cpp", "command": "c++ -c distance_avx2.cpp"}]
        self.assertEqual(
            check_entries(entries, "aarch64"),
            [f"{SOURCE} is compiled on aarch64"],
        )

    def test_accepts_an_arm64_database_without_the_translation_unit(self) -> None:
        entries = [{"file": "src/core/distance.cpp", "command": "c++ -c src/core/distance.cpp"}]
        self.assertEqual(check_entries(entries, "aarch64"), [])

    def test_quoted_command_splits_into_flags(self) -> None:
        entries = [
            {
                "file": r"C:\src\distance_avx2.cpp",
                "command": 'c++ -DNAME="a b" -mavx2 -c distance_avx2.cpp',
            }
        ]
        self.assertEqual(forbidden_flags(entries), ["-mavx2"])

    def test_rejects_an_entry_with_no_command(self) -> None:
        with self.assertRaises(ValueError):
            forbidden_flags([{"file": "distance_avx2.cpp"}])

    def test_missing_database_is_optional_until_the_build_requires_it(self) -> None:
        missing = pathlib.Path("/no/such/compile_commands.json")
        self.assertIsNone(resolve_compile_commands(None, require=False))
        self.assertIsNone(resolve_compile_commands(str(missing), require=False))
        with self.assertRaises(FileNotFoundError):
            resolve_compile_commands(None, require=True)
        with self.assertRaises(FileNotFoundError):
            resolve_compile_commands(str(missing), require=True)

    def test_this_build_omits_file_wide_avx2_flags(self) -> None:
        require = os.environ.get("NESSO_REQUIRE_COMPILE_COMMANDS") == "1"
        try:
            path = resolve_compile_commands(os.environ.get("NESSO_COMPILE_COMMANDS"), require)
        except FileNotFoundError as exc:
            self.fail(str(exc))
        if path is None:
            self.skipTest("compile_commands.json is not in this tree")
        errors = check_entries(load_entries(path), platform.machine())
        self.assertEqual(errors, [], errors)


if __name__ == "__main__":
    unittest.main()
