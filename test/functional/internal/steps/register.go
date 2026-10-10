// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

// Package steps binds Gherkin steps to the nesso binary. Scenarios observe only
// files, arguments, exit code, stdout and stderr.
package steps

import (
	"context"
	"os"

	"github.com/cucumber/godog"

	"github.com/lorenzocaprari/nesso/test/functional/internal/scenario"
)

type bindings struct {
	world *scenario.World
}

// InitializeScenario registers every step. Each scenario runs in its own
// temporary directory, which is also the working directory of nesso.
func InitializeScenario(sc *godog.ScenarioContext) {
	b := &bindings{world: &scenario.World{}}

	sc.Before(func(ctx context.Context, _ *godog.Scenario) (context.Context, error) {
		dir, err := os.MkdirTemp("", "nesso-bdd-")
		if err != nil {
			return ctx, err
		}
		*b.world = scenario.World{Dir: dir}
		return ctx, nil
	})
	sc.After(func(ctx context.Context, _ *godog.Scenario, _ error) (context.Context, error) {
		return ctx, os.RemoveAll(b.world.Dir)
	})

	sc.Step(`^a file "([^"]+)" with:$`, b.writeFile)
	sc.Step(`^an empty file "([^"]+)"$`, b.writeEmptyFile)
	sc.Step(`^a directory "([^"]+)"$`, b.makeDirectory)
	sc.Step(`^a line of (\d+) "(.)" in "([^"]+)"$`, b.appendRepeatedLine)
	sc.Step(`^a file "([^"]+)" with hex:$`, b.writeHexFile)
	sc.Step(`^I run nesso with arguments (.+)$`, b.runNesso)
	sc.Step(`^I run nesso without arguments$`, func() error { return b.runNesso("") })
	sc.Step(`^running nesso again prints the same stdout$`, b.runAgainSameStdout)
	sc.Step(`^the exit code is (\d+)$`, b.exitCodeIs)
	sc.Step(`^the exit code is not (\d+)$`, b.exitCodeIsNot)
	// (.*) so a phrase may contain quotes, as in: missing the "message" field.
	sc.Step(`^(stdout|stderr) contains "(.*)"$`, b.streamContains)
	sc.Step(`^(stdout|stderr) does not contain "(.*)"$`, b.streamOmits)
	sc.Step(`^(stdout|stderr) is empty$`, b.streamIsEmpty)
}
