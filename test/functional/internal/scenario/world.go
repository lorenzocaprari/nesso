// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

// Package scenario holds the state shared by the steps of one scenario.
package scenario

import "github.com/lorenzocaprari/nesso/test/functional/internal/runner"

// World is the per-scenario state: its directory and the last nesso run, with
// output already normalized.
type World struct {
	Dir         string
	Ran         bool
	CommandLine string
	Result      runner.Result
}
