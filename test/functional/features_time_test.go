// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package functional_test

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"testing"
	"time"

	"github.com/cucumber/godog"

	"github.com/lorenzocaprari/nesso/test/functional/internal/steps"
)

// TestFeatureFileDurations times each features/*.feature file. Opt in with
// NESSO_MEASURE_FEATURES=1. Writes NESSO_FEATURE_TIMES (default
// feature-times.tsv) as "seconds\tpath" lines sorted by seconds descending.
func TestFeatureFileDurations(t *testing.T) {
	if os.Getenv("NESSO_MEASURE_FEATURES") != "1" {
		t.Skip("set NESSO_MEASURE_FEATURES=1")
	}
	entries, err := filepath.Glob("features/*.feature")
	if err != nil {
		t.Fatal(err)
	}
	sort.Strings(entries)
	concurrency := runtime.NumCPU()
	if concurrency < 1 {
		concurrency = 1
	}
	type row struct {
		path string
		sec  float64
	}
	var rows []row
	for _, path := range entries {
		start := time.Now()
		suite := godog.TestSuite{
			Name:                path,
			ScenarioInitializer: steps.InitializeScenario,
			Options: &godog.Options{
				Format:      "progress",
				NoColors:    true,
				Paths:       []string{path},
				Strict:      true,
				Concurrency: concurrency,
				TestingT:    t,
			},
		}
		if suite.Run() != 0 {
			t.Fatalf("%s failed", path)
		}
		rows = append(rows, row{path, time.Since(start).Seconds()})
	}
	sort.Slice(rows, func(i, j int) bool { return rows[i].sec > rows[j].sec })
	outPath := os.Getenv("NESSO_FEATURE_TIMES")
	if outPath == "" {
		outPath = "feature-times.tsv"
	}
	f, err := os.Create(outPath)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	for _, row := range rows {
		line := fmt.Sprintf("%.2f\t%s\n", row.sec, row.path)
		if _, err := f.WriteString(line); err != nil {
			t.Fatal(err)
		}
		t.Log(line)
	}
}
