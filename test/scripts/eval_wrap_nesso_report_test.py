#!/usr/bin/env python3
# Copyright (c) 2026 Lorenzo Caprari
# SPDX-License-Identifier: Apache-2.0

import importlib.machinery
import importlib.util
import json
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "eval-wrap-nesso-report"


def load_wrap():
    loader = importlib.machinery.SourceFileLoader("eval_wrap_nesso_report", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class EvalWrapNessoReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_wrap()

    def test_wraps_schema_3(self) -> None:
        payload = {
            "datasets": [
                {
                    "dataset": "hdfs-2k",
                    "modes": [{"mode": "auto", "queries": []}],
                    "query_count": 1,
                }
            ],
            "k": 10,
            "overall": {"modes": [{"mode": "auto"}], "query_count": 1},
            "schema_version": 3,
        }
        wrapped = self.mod.wrap(payload)
        self.assertEqual(wrapped["schema_version"], 4)
        self.assertEqual(wrapped["datasets"][0]["engines"][0]["engine"], "nesso")
        self.assertEqual(wrapped["datasets"][0]["engines"][0]["modes"][0]["mode"], "auto")
        self.assertEqual(wrapped["overall"]["engines"][0]["engine"], "nesso")

    def test_schema_4_and_needle_pass_through(self) -> None:
        schema4 = {
            "datasets": [{"dataset": "hdfs-2k", "engines": [], "query_count": 0}],
            "k": 10,
            "overall": {"engines": [], "query_count": 0},
            "schema_version": 4,
        }
        self.assertIs(self.mod.wrap(schema4), schema4)
        needle = {"kind": "needle", "datasets": [], "schema_version": 1}
        self.assertIs(self.mod.wrap(needle), needle)

    def test_cli_writes_sorted_json(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            inp = directory / "in.json"
            out = directory / "out.json"
            payload = {
                "datasets": [
                    {
                        "dataset": "hdfs-2k",
                        "modes": [{"mode": "dense"}],
                        "query_count": 0,
                    }
                ],
                "k": 10,
                "overall": {"modes": [{"mode": "dense"}], "query_count": 0},
                "schema_version": 3,
            }
            inp.write_text(json.dumps(payload), encoding="utf-8")
            self.mod.main(["--in", str(inp), "--out", str(out)])
            encoded = out.read_text(encoding="utf-8")
            self.assertTrue(encoded.endswith("\n"))
            self.assertEqual(
                encoded,
                json.dumps(json.loads(encoded), sort_keys=True, separators=(",", ":")) + "\n",
            )


if __name__ == "__main__":
    unittest.main()
