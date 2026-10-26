// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"fmt"
	"os"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: ci <patch-coverage|junit-summary|bench-external|bench-history> [flags]")
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
	case "-h", "--help", "help":
		fmt.Fprintln(os.Stderr, "usage: ci <patch-coverage|junit-summary|bench-external|bench-history> [flags]")
	default:
		fmt.Fprintf(os.Stderr, "unknown command %q\n", os.Args[1])
		code = 1
	}
	os.Exit(code)
}
