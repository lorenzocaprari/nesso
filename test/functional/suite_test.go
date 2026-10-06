// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package functional_test

import (
	"testing"

	"github.com/cucumber/godog"

	"github.com/lorenzocaprari/nesso/test/functional/internal/steps"
)

func TestFeatures(t *testing.T) {
	suite := godog.TestSuite{
		ScenarioInitializer: steps.InitializeScenario,
		Options: &godog.Options{
			Format:   "pretty",
			NoColors: true,
			Paths:    []string{"features"},
			Strict:   true,
			TestingT: t,
		},
	}
	if suite.Run() != 0 {
		t.Fatal("feature scenarios failed")
	}
}
