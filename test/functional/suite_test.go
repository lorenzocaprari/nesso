// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package functional_test

import (
	"runtime"
	"testing"

	"github.com/cucumber/godog"

	"github.com/lorenzocaprari/nesso/test/functional/internal/steps"
)

func TestFeatures(t *testing.T) {
	concurrency := runtime.NumCPU()
	if concurrency < 1 {
		concurrency = 1
	}
	suite := godog.TestSuite{
		ScenarioInitializer: steps.InitializeScenario,
		Options: &godog.Options{
			Format:      "pretty",
			NoColors:    true,
			Paths:       []string{"features"},
			Strict:      true,
			Concurrency: concurrency,
			TestingT:    t,
		},
	}
	if suite.Run() != 0 {
		t.Fatal("feature scenarios failed")
	}
}
