#!/usr/bin/env python3
# Copyright (c) 2026 Lorenzo Caprari
# SPDX-License-Identifier: Apache-2.0

import importlib.machinery
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "eval-measure-summary"


def load_module():
    loader = importlib.machinery.SourceFileLoader("eval_measure_summary", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


MIB = 1024 * 1024


def payload() -> dict:
    return {
        "measure": {
            "cpuModel": "cpu",
            "nproc": 4,
            "repeat": 3,
            "threads": "1",
            "datasets": [
                {
                    "name": "bgl-needle-100k",
                    "logLines": 100000,
                    "logBytes": 1,
                    "engines": [
                        {
                            "engine": "nesso",
                            "status": "ok",
                            "indexWallSeconds": 100.0,
                            "indexPeakRssBytes": 400 * MIB,
                            "timeToFirstResultSeconds": 2.0,
                            "range": {"indexWallSeconds": [90.0, 110.0]},
                            "modes": [
                                {
                                    "mode": "auto",
                                    "queryWallSeconds": 60.0,
                                    "queryPeakRssBytes": 500 * MIB,
                                    "queryLatencyMedianSeconds": 1.0,
                                    "range": {"queryWallSeconds": [58.0, 61.0]},
                                }
                            ],
                        },
                        {
                            "engine": "bm25",
                            "status": "ok",
                            "indexWallSeconds": 4.0,
                            "indexPeakRssBytes": 100 * MIB,
                            "timeToFirstResultSeconds": 4.5,
                            "queryWallSeconds": 12.0,
                            "queryPeakRssBytes": 50 * MIB,
                            "queryLatencyMedianSeconds": 0.2,
                            "range": {},
                        },
                        {
                            "engine": "grep",
                            "status": "ok",
                            "indexWallSeconds": None,
                            "indexPeakRssBytes": None,
                            "timeToFirstResultSeconds": 0.1,
                            "queryWallSeconds": 6.0,
                            "queryPeakRssBytes": 4 * MIB,
                            "queryLatencyMedianSeconds": 0.1,
                            "range": {},
                        },
                    ],
                }
            ],
        }
    }


class EvalMeasureSummaryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_module()

    def test_ratios_and_ranges(self) -> None:
        text = self.mod.render(payload(), "auto")
        self.assertIn("| Index wall time | 100.00 (90.00-110.00) s | 4.00 s | n/a | 25.00x | n/a |", text)
        self.assertIn("| Queries total wall time | 60.00 (58.00-61.00) s | 12.00 s | 6.00 s | 5.00x | 10.00x |", text)
        self.assertIn("| Query peak RSS | 500.00 MiB | 50.00 MiB | 4.00 MiB | 10.00x | 125.00x |", text)
        self.assertIn("development only", text.lower())

    def test_skipped_engine_is_n_a(self) -> None:
        data = payload()
        data["measure"]["datasets"][0]["engines"][2] = {"engine": "grep", "status": "skipped", "reason": "x"}
        text = self.mod.render(data, "auto")
        self.assertIn("Skipped: grep: x", text)
        self.assertIn("| Queries total wall time | 60.00 (58.00-61.00) s | 12.00 s | n/a | 5.00x | n/a |", text)

    def test_missing_measure_block(self) -> None:
        with self.assertRaises(SystemExit):
            self.mod.render({}, "auto")


if __name__ == "__main__":
    unittest.main()
