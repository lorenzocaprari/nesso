import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check-runtime-licenses"


def run(nodes: dict) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory() as temp:
        path = pathlib.Path(temp) / "graph.json"
        path.write_text(json.dumps({"graph": {"nodes": nodes}}), encoding="utf-8")
        return subprocess.run(
            [sys.executable, str(SCRIPT), str(path)],
            capture_output=True,
            text=True,
            check=False,
        )


def host(ref: str, license, deps: dict | None = None, context: str = "host", test: bool = False) -> dict:
    node = {
        "ref": f"{ref}#rev",
        "context": context,
        "test": test,
        "dependencies": deps or {},
    }
    if license is not None:
        node["license"] = license
    return node


def permissive_graph() -> dict:
    return {
        "0": host("nesso/0.2.1", "Apache-2.0", {"1": {"ref": "cli11/2.6.2", "direct": True},
                                                "2": {"ref": "zlib/1.3.2", "direct": True},
                                                "3": {"ref": "mplonly/1.0", "direct": True}}),
        "1": host("cli11/2.6.2", "BSD-3-Clause"),
        "2": host("zlib/1.3.2", "Zlib"),
        "3": host("mplonly/1.0", "MPL-2.0"),
    }


def with_package(nodes: dict, key: str, ref: str, license, context: str = "host", test: bool = False,
                 direct: bool = True) -> dict:
    nodes = json.loads(json.dumps(nodes))
    nodes[key] = host(ref, license, context=context, test=test)
    nodes["0"]["dependencies"][key] = {"ref": ref, "direct": direct}
    return nodes


def eigen_graph(license="MPL-2.0 AND LGPL-3.0-or-later", eigen_ref="eigen/3.4.0",
                consumer_ref="onnxruntime/1.18.1", consumer_direct=True, root_direct=False) -> dict:
    nodes = {
        "0": host("nesso/0.2.1", "Apache-2.0", {
            "1": {"ref": consumer_ref, "direct": True},
            "2": {"ref": eigen_ref, "direct": False},
        }),
        "1": host(consumer_ref, "MIT", {}),
        "2": host(eigen_ref, license),
    }
    if consumer_direct:
        nodes["1"]["dependencies"]["2"] = {"ref": eigen_ref, "direct": True}
    if root_direct:
        nodes["0"]["dependencies"]["2"] = {"ref": eigen_ref, "direct": True}
    return nodes


class CheckRuntimeLicensesTest(unittest.TestCase):
    def test_clean_graph_passes(self) -> None:
        result = run(permissive_graph())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")

    def test_libiconv_lgpl_in_host_fails(self) -> None:
        nodes = with_package(permissive_graph(), "4", "libiconv/1.17", "LGPL-2.1-or-later")
        result = run(nodes)
        self.assertEqual(result.returncode, 1)
        self.assertIn("libiconv/1.17: copyleft license ['LGPL-2.1-or-later'] in the runtime graph", result.stderr)

    def test_gpl_tool_in_build_context_passes(self) -> None:
        nodes = with_package(permissive_graph(), "4", "autoconf/2.71", "GPL-3.0-or-later", context="build")
        nodes = with_package(nodes, "5", "catch2/3.14.0", "GPL-2.0-only", test=True)
        result = run(nodes)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_or_keeps_a_permissive_choice_and_rejects_copyleft_only(self) -> None:
        passed = run(with_package(permissive_graph(), "4", "dual/1.0", "GPL-2.0 OR MIT"))
        self.assertEqual(passed.returncode, 0, passed.stderr)
        failed = run(with_package(permissive_graph(), "4", "dual/1.0", "GPL-2.0 OR LGPL-2.1"))
        self.assertEqual(failed.returncode, 1)
        self.assertIn("dual/1.0: copyleft license ['GPL-2.0 OR LGPL-2.1']", failed.stderr)

    def test_bare_copyleft_names_fail(self) -> None:
        for license in ("GPL", "LGPL", "AGPL", "SSPL", "EUPL", "OSL", "CDDL", "EPL", "CPAL", "GPLv2", "LGPLv2.1"):
            result = run(with_package(permissive_graph(), "4", "legacy/1", license))
            self.assertEqual(result.returncode, 1, license)
            self.assertIn(f"legacy/1: copyleft license ['{license}']", result.stderr)

    def test_missing_license_fails(self) -> None:
        for license in (None, "", []):
            nodes = with_package(permissive_graph(), "4", "mystery/1.0", "MIT")
            if license is None:
                del nodes["4"]["license"]
            else:
                nodes["4"]["license"] = license
            result = run(nodes)
            self.assertEqual(result.returncode, 1, repr(license))
            self.assertIn("mystery/1.0: no license metadata", result.stderr)

    def test_eigen_allow_list_passes_only_for_the_right_consumer(self) -> None:
        for license in ("MPL-2.0 AND LGPL-3.0-or-later", ["MPL-2.0", "LGPL-3.0-or-later"]):
            passed = run(eigen_graph(license=license))
            self.assertEqual(passed.returncode, 0, passed.stderr)

        # onnxruntime is in the graph, but it does not directly depend on eigen.
        present = run(eigen_graph(consumer_direct=False, root_direct=True))
        self.assertEqual(present.returncode, 1)
        self.assertIn("eigen/3.4.0", present.stderr)
        self.assertIn("onnxruntime/1.18.1 is not the only direct dependent", present.stderr)
        self.assertIn("nesso/0.2.1", present.stderr)

        bumped_consumer = run(eigen_graph(consumer_ref="onnxruntime/1.19.0"))
        self.assertEqual(bumped_consumer.returncode, 1)
        self.assertIn("eigen/3.4.0", bumped_consumer.stderr)
        self.assertIn("saw ['onnxruntime/1.19.0']", bumped_consumer.stderr)

    def test_wrong_eigen_version_fails(self) -> None:
        result = run(eigen_graph(eigen_ref="eigen/3.4.1"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("eigen/3.4.1: copyleft license ['MPL-2.0 AND LGPL-3.0-or-later']", result.stderr)
        self.assertNotIn("allow-list", result.stderr)

    def test_usage(self) -> None:
        missing = subprocess.run([sys.executable, str(SCRIPT)], capture_output=True, text=True, check=False)
        self.assertEqual(missing.returncode, 2)
        self.assertIn("usage: check-runtime-licenses CONAN_GRAPH_JSON", missing.stderr)
        extra = subprocess.run(
            [sys.executable, str(SCRIPT), "one", "two"], capture_output=True, text=True, check=False
        )
        self.assertEqual(extra.returncode, 2)
        self.assertIn("usage:", extra.stderr)


if __name__ == "__main__":
    unittest.main()
