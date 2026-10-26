import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "third-party-notices"

# Stands in for nm: prints the lines stored for a path in NM_FIXTURE.
FAKE_NM = """#!/usr/bin/env python3
import json, os, sys
table = json.load(open(os.environ["NM_FIXTURE"]))
print("\\n".join(table.get(sys.argv[-1], [])))
"""


class Fixture:
    def __init__(self, directory: pathlib.Path) -> None:
        self.directory = directory
        self.symbols: dict[str, list[str]] = {}
        self.nodes: dict[str, dict] = {"0": {"ref": "conanfile", "context": "host", "test": False, "dependencies": {}}}
        self.binary = directory / "nesso"
        self.binary.write_bytes(b"")
        self.nm = directory / "nm"
        self.nm.write_text(FAKE_NM)
        self.nm.chmod(0o755)

    def package(self, ref: str, kind: str = "static-library", licenses: dict[str, bytes] | None = None,
                archive: list[str] | None = None, folder: bool = True, context: str = "host",
                test: bool = False, license=None) -> str:
        key = str(len(self.nodes))
        name = ref.split("/")[0]
        path = None
        if folder:
            base = self.directory / "p" / name
            (base / "licenses").mkdir(parents=True, exist_ok=True)
            for relative, body in (licenses if licenses is not None else {"LICENSE": f"{name} text\n".encode()}).items():
                target = base / "licenses" / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(body)
            if archive is not None:
                (base / "lib").mkdir(exist_ok=True)
                lib = base / "lib" / f"lib{name}.a"
                lib.write_bytes(b"")
                self.symbols[str(lib)] = archive
            path = str(base)
        self.nodes[key] = {"ref": f"{ref}#c0ffee0123", "context": context, "test": test, "package_folder": path,
                           "package_type": kind, "binary": "Cache" if folder else "Skip", "license": license,
                           "dependencies": {}}
        return key

    def edge(self, parent: str, child: str, headers: bool = True, direct: bool = True) -> None:
        self.nodes[parent]["dependencies"][child] = {"ref": self.nodes[child]["ref"], "headers": headers,
                                                     "libs": True, "direct": direct}

    def run(self, out: str = "out", binary_symbols: list[str] | None = None, order=None) -> subprocess.CompletedProcess:
        self.symbols[str(self.binary)] = binary_symbols if binary_symbols is not None else ["0 T OrtRun", "0 W shared_tmpl"]
        fixture = self.directory / "nm.json"
        fixture.write_text(json.dumps(self.symbols))
        nodes = self.nodes if order is None else dict(order(list(self.nodes.items())))
        graph = self.directory / f"graph-{out}.json"
        graph.write_text(json.dumps({"graph": {"nodes": nodes}}))
        env = dict(os.environ, NM=str(self.nm), NM_FIXTURE=str(fixture))
        return subprocess.run([sys.executable, str(SCRIPT), str(graph), str(self.binary), str(self.directory / out)],
                              capture_output=True, text=True, check=False, env=env)


def shipped_graph(directory: pathlib.Path) -> Fixture:
    f = Fixture(directory)
    ort = f.package("onnxruntime/1.18.1", archive=["0 T OrtRun", "0 W shared_tmpl"], license="MIT",
                    licenses={"LICENSE": b"MIT text\r\nline two  \r\n"})
    cli = f.package("cli11/2.6.2", kind="header-library", license="BSD-3-Clause")
    eigen = f.package("eigen/3.4.0", kind="header-library", license=["MPL-2.0", "LGPL-3.0-or-later"],
                      licenses={"COPYING.MPL2": b"mpl\n", "COPYING.BSD": b"bsd\n", "sub/A": b"a\n"})
    date = f.package("date/3.0.1", archive=["0 T date_tz_only"], license="MIT")
    curl = f.package("libcurl/8.21.0", archive=["0 T curl_easy_init"], license="curl")
    zlib = f.package("zlib/1.3.2", archive=["0 T inflate", "0 W shared_tmpl"], license="Zlib")
    bzip2 = f.package("bzip2/1.0.8", folder=False, license="bzip2-1.0.6")
    cmake = f.package("cmake/3.31.12", folder=False, context="build")
    catch2 = f.package("catch2/3.14.0", archive=["0 T catch_main"], test=True)
    f.edge("0", ort)
    f.edge("0", cli)
    f.edge("0", bzip2, headers=False, direct=False)
    f.edge("0", cmake)
    f.edge("0", catch2)
    f.edge(ort, eigen)
    f.edge(ort, date)
    f.edge(ort, zlib)
    f.edge(ort, bzip2, headers=False, direct=False)
    f.edge(date, curl)
    return f


class ThirdPartyNoticesTest(unittest.TestCase):
    def test_lists_only_components_shipped_in_the_binary(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            f = shipped_graph(pathlib.Path(temp))
            first = f.run("first")
            self.assertEqual(first.returncode, 0, first.stderr)
            second = f.run("second", order=lambda items: [items[0]] + list(reversed(items[1:])))
            self.assertEqual(second.returncode, 0, second.stderr)
            notices = (f.directory / "first" / "THIRD_PARTY_NOTICES").read_text()
            self.assertEqual(notices, (f.directory / "second" / "THIRD_PARTY_NOTICES").read_text())

            listed = [line for line in notices.splitlines() if line.startswith("- ")]
            self.assertEqual(listed[:4], [
                "- cli11 2.6.2 (BSD-3-Clause)",
                "- date 3.0.1 (MIT)",
                "- eigen 3.4.0 (LGPL-3.0-or-later, MPL-2.0)",
                "- onnxruntime 1.18.1 (MIT)",
            ])
            self.assertTrue(any("all-MiniLM-L6-v2" in line for line in listed))
            self.assertTrue(any("Unicode-3.0" in line for line in listed))
            for absent in ("libcurl", "zlib", "bzip2", "cmake", "catch2"):
                self.assertNotIn(f"- {absent} ", notices)

            self.assertLess(notices.index("--- licenses/COPYING.BSD ---"), notices.index("--- licenses/COPYING.MPL2 ---"))
            self.assertLess(notices.index("--- licenses/COPYING.MPL2 ---"), notices.index("--- licenses/sub/A ---"))
            self.assertIn("MIT text\nline two\n", notices)
            self.assertNotIn("\r", notices)
            self.assertIn("--- ThirdPartyNotices.txt ---", notices)
            self.assertIn("Apache License", notices)
            self.assertIn("UNICODE LICENSE V3", notices)
            self.assertNotIn(temp, notices)
            self.assertNotIn("c0ffee0123", notices)
            self.assertTrue(notices.endswith("\n") and not notices.endswith("\n\n"))

            copyright_text = (f.directory / "first" / "copyright").read_text()
            self.assertTrue(copyright_text.startswith("nesso\nCopyright (c) 2026 Lorenzo Caprari\n"))
            self.assertTrue(copyright_text.endswith(notices))

    def test_a_skipped_package_that_ships_needs_its_folder(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            f = shipped_graph(pathlib.Path(temp))
            gsl = f.package("ms-gsl/4.0.0", kind="header-library", folder=False)
            f.edge("1", gsl)
            result = f.run()
            self.assertEqual(result.returncode, 1)
            self.assertIn("ms-gsl/4.0.0 has no package_folder", result.stderr)
            self.assertIn("skip_binaries=False", result.stderr)

    def test_weak_symbols_do_not_count_as_linked(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            f = shipped_graph(pathlib.Path(temp))
            other = f.package("re2/20231101", archive=["0 W shared_tmpl", "0 T re2_only"])
            f.edge("1", other)
            result = f.run()
            self.assertEqual(result.returncode, 1)
            self.assertIn("re2/20231101 is a header requirement of shipped code but is not linked", result.stderr)

            linked = f.run("linked", binary_symbols=["0 T OrtRun", "0 t re2_only"])
            self.assertEqual(linked.returncode, 0, linked.stderr)
            self.assertIn("- re2 20231101", (f.directory / "linked" / "THIRD_PARTY_NOTICES").read_text())

    def test_fails_without_license_files(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            f = shipped_graph(pathlib.Path(temp))
            gsl = f.package("ms-gsl/4.0.0", kind="header-library", licenses={})
            f.edge("1", gsl)
            result = f.run()
            self.assertEqual(result.returncode, 1)
            self.assertIn("ms-gsl/4.0.0 has no files in its licenses/ folder", result.stderr)
            self.assertFalse((f.directory / "out").exists())

    def test_fails_for_onnxruntime_problems(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            f = Fixture(pathlib.Path(temp))
            ort = f.package("onnxruntime/9.9.9", archive=["0 T OrtRun"])
            f.edge("0", ort)
            result = f.run()
            self.assertEqual(result.returncode, 1)
            self.assertIn("onnxruntime 9.9.9", result.stderr)
            unlinked = f.run("unlinked", binary_symbols=["0 T something_else"])
            self.assertEqual(unlinked.returncode, 1)
            self.assertIn("onnxruntime is not linked", unlinked.stderr)

    def test_rejects_bad_input(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            f = shipped_graph(pathlib.Path(temp))
            stripped = f.run("stripped", binary_symbols=[])
            self.assertEqual(stripped.returncode, 1)
            self.assertIn("has no symbol table", stripped.stderr)
            f.nodes = {}
            graph = f.directory / "bad.json"
            graph.write_text(json.dumps({"nodes": {}}))
            env = dict(os.environ, NM=str(f.nm), NM_FIXTURE=str(f.directory / "nm.json"))
            bad = subprocess.run([sys.executable, str(SCRIPT), str(graph), str(f.binary), str(f.directory / "o")],
                                 capture_output=True, text=True, check=False, env=env)
            self.assertEqual(bad.returncode, 1)
            self.assertIn("not a Conan graph", bad.stderr)
            usage = subprocess.run([sys.executable, str(SCRIPT)], capture_output=True, text=True, check=False)
            self.assertEqual(usage.returncode, 2)


if __name__ == "__main__":
    unittest.main()
