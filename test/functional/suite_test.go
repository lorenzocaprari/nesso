// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package functional_test

import (
	"os"
	"runtime"
	"strings"
	"testing"

	"github.com/cucumber/godog"

	"github.com/lorenzocaprari/nesso/test/functional/internal/steps"
)

func TestFeatures(t *testing.T) {
	concurrency := runtime.NumCPU()
	if concurrency < 1 {
		concurrency = 1
	}
	paths := []string{"features"}
	if raw := strings.TrimSpace(os.Getenv("NESSO_GODOG_PATHS")); raw != "" {
		paths = strings.Fields(raw)
	}
	format := "pretty"
	if raw := strings.TrimSpace(os.Getenv("NESSO_GODOG_FORMAT")); raw != "" {
		format = raw
	}
	suite := godog.TestSuite{
		ScenarioInitializer: steps.InitializeScenario,
		Options: &godog.Options{
			Format:      format,
			NoColors:    true,
			Paths:       paths,
			Strict:      true,
			Concurrency: concurrency,
			TestingT:    t,
		},
	}
	if suite.Run() != 0 {
		t.Fatal("feature scenarios failed")
	}
}
