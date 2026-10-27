// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"fmt"
	"os"
)

const ciUsage = "usage: ci <patch-coverage|junit-summary|bench-external|bench-history|bench-scale|bench-summary|bench-baseline|trivy-summary|runtime-scan-dockerfile|ci-gate|ci-gate-needs|wait-build-env> [flags]"

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, ciUsage)
		os.Exit(1)
	}
	var code int
	switch os.Args[1] {
	case "patch-coverage":
		code = patchMain(os.Args[2:])
	case "junit-summary":
		code = junitMain(os.Args[2:])
	case "bench-external":
		code = benchExternalMain(os.Args[2:])
	case "bench-history":
		code = benchHistoryMain(os.Args[2:])
	case "bench-scale":
		code = benchScaleMain(os.Args[2:])
	case "bench-summary":
		code = benchSummaryMain(os.Args[2:])
	case "bench-baseline":
		code = benchBaselineMain(os.Args[2:])
	case "trivy-summary":
		code = trivyMain(os.Args[2:])
	case "runtime-scan-dockerfile":
		code = runtimeScanMain(os.Args[2:])
	case "ci-gate":
		code = ciGateMain(os.Args[2:])
	case "ci-gate-needs":
		code = ciGateNeedsMain(os.Args[2:])
	case "wait-build-env":
		code = waitBuildEnvMain(os.Args[2:])
	case "-h", "--help", "help":
		fmt.Fprintln(os.Stderr, ciUsage)
	default:
		fmt.Fprintf(os.Stderr, "unknown command %q\n", os.Args[1])
		code = 1
	}
	os.Exit(code)
}
