// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import "math"

// benchFile is one scripts/bench result file (bench/results/*.json).
type benchFile struct {
	SHA     string      `json:"sha,omitempty"`
	Query   string      `json:"query,omitempty"`
	Threads *string     `json:"threads"`
	CPU     string      `json:"cpuModel,omitempty"`
	Cases   []benchCase `json:"cases"`
}

type benchCase struct {
	Lines        int      `json:"lines"`
	OK           bool     `json:"ok"`
	ExitCode     int      `json:"exitCode,omitempty"`
	WallSeconds  *float64 `json:"wallSeconds"`
	PeakRssBytes *float64 `json:"peakRssBytes"`
	Error        string   `json:"error,omitempty"`
}

func positive(value *float64) (float64, bool) {
	if value == nil || math.IsNaN(*value) || math.IsInf(*value, 0) || *value <= 0 {
		return 0, false
	}
	return *value, true
}
