#!/usr/bin/env python3
# Copyright (c) 2026 Lorenzo Caprari
# SPDX-License-Identifier: Apache-2.0

import importlib.machinery
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "eval-host"


def load_module():
    loader = importlib.machinery.SourceFileLoader("eval_host", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise AssertionError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


HOST = load_module()

EPYC = """\
processor	: 0
vendor_id	: AuthenticAMD
model name	: AMD EPYC 7763 64-Core Processor
flags		: fpu avx avx2 fma
"""

XEON = """\
processor	: 0
model name	: INTEL(R) XEON(R) PLATINUM 8573C
flags		: avx2 fma avx512f avx512vnni amx_tile amx_int8
"""


class EvalHostTest(unittest.TestCase):
    def test_epyc_selects_avx2_for_both_kernels(self) -> None:
        got = HOST.host_from_cpuinfo(EPYC, None)
        self.assertEqual(got["cpuModel"], "AMD EPYC 7763 64-Core Processor")
        self.assertEqual(got["distanceKernel"], "avx2")
        self.assertEqual(got["int8Kernel"], "avx2")

    def test_xeon_keeps_avx2_distance_and_vnni_int8(self) -> None:
        # AMX is present and refused before the session, so the int8 kernel
        # stays the VNNI one. The distance kernel has no AVX-512 path.
        got = HOST.host_from_cpuinfo(XEON, None)
        self.assertEqual(got["cpuModel"], "INTEL(R) XEON(R) PLATINUM 8573C")
        self.assertEqual(got["distanceKernel"], "avx2")
        self.assertEqual(got["int8Kernel"], "avx512-vnni")

    def test_force_scalar_does_not_change_the_int8_kernel(self) -> None:
        got = HOST.host_from_cpuinfo(XEON, "scalar")
        self.assertEqual(got["distanceKernel"], "scalar")
        self.assertEqual(got["int8Kernel"], "avx512-vnni")

    def test_forced_avx2_without_fma_stays_on_scalar(self) -> None:
        text = "model name\t: old\nflags\t\t: avx2\n"
        got = HOST.host_from_cpuinfo(text, "avx2")
        self.assertEqual(got["distanceKernel"], "scalar")
        self.assertEqual(got["int8Kernel"], "avx2")

    def test_unknown_force_value_stays_automatic(self) -> None:
        self.assertEqual(HOST.host_from_cpuinfo(EPYC, "")["distanceKernel"], "avx2")
        self.assertEqual(HOST.host_from_cpuinfo(EPYC, "nope")["distanceKernel"], "avx2")

    def test_no_simd_is_scalar(self) -> None:
        text = "model name\t: soft\nflags\t\t: fpu sse sse2\n"
        got = HOST.host_from_cpuinfo(text, None)
        self.assertEqual(got["distanceKernel"], "scalar")
        self.assertEqual(got["int8Kernel"], "scalar")

    def test_missing_model_name_is_unknown(self) -> None:
        got = HOST.host_from_cpuinfo("flags\t\t: avx2 fma\n", None)
        self.assertEqual(got["cpuModel"], "unknown")

    def test_summary_names_cpu_and_both_kernels(self) -> None:
        line = HOST.summary_line(HOST.host_from_cpuinfo(XEON, None))
        self.assertEqual(
            line,
            "CPU INTEL(R) XEON(R) PLATINUM 8573C; distance kernel avx2; int8 kernel avx512-vnni.",
        )

    def test_cli_writes_the_record_and_reads_it_back(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            cpuinfo = pathlib.Path(tmp) / "cpuinfo"
            out = pathlib.Path(tmp) / "nested" / "host.json"
            cpuinfo.write_text(XEON, encoding="utf-8")
            wrote = subprocess.run(
                [sys.executable, str(SCRIPT), "--cpuinfo", str(cpuinfo), "--out", str(out)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(json.loads(wrote.stdout), json.loads(out.read_text(encoding="utf-8")))
            summary = subprocess.run(
                [sys.executable, str(SCRIPT), "--summary", "--host", str(out)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                summary.stdout.strip(),
                "CPU INTEL(R) XEON(R) PLATINUM 8573C; distance kernel avx2; int8 kernel avx512-vnni.",
            )


if __name__ == "__main__":
    unittest.main()
