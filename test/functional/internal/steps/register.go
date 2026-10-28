// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

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

type scenarioDirKey struct{}

func scenarioDir(ctx context.Context) (string, bool) {
	dir, ok := ctx.Value(scenarioDirKey{}).(string)
	return dir, ok
}

// InitializeScenario registers every step. Godog calls it once per scenario,
// including when scenarios run concurrently, so this bindings value stays
// local to the call. Each scenario runs in its own temporary directory, which
// is also the working directory of nesso. Index paths, environment, and the
// pty live on that scenario's world; the model and fixture directories are
// only read.
func InitializeScenario(sc *godog.ScenarioContext) {
	b := &bindings{world: &scenario.World{}}

	sc.Before(func(ctx context.Context, _ *godog.Scenario) (context.Context, error) {
		dir, err := os.MkdirTemp("", "nesso-bdd-")
		if err != nil {
			return ctx, err
		}
		*b.world = scenario.World{Dir: dir}
		return context.WithValue(ctx, scenarioDirKey{}, dir), nil
	})
	sc.After(func(ctx context.Context, _ *godog.Scenario, _ error) (context.Context, error) {
		return ctx, os.RemoveAll(b.world.Dir)
	})

	sc.Step(`^a file "([^"]+)" with:$`, b.writeFile)
	sc.Step(`^stdin is:$`, b.setStdin)
	sc.Step(`^stdin is a line of (\d+) "(.)"$`, b.setStdinLine)
	sc.Step(`^stdin is hex:$`, b.setStdinHex)
	sc.Step(`^the environment variable "([^"]+)" is "([^"]*)"$`, b.setEnv)
	sc.Step(`^stderr is a terminal$`, b.setTerminal)
	sc.Step(`^an empty file "([^"]+)"$`, b.writeEmptyFile)
	sc.Step(`^the file "([^"]+)" is removed$`, b.removeFile)
	sc.Step(`^the file "([^"]+)" is touched$`, b.touchFile)
	sc.Step(`^a directory "([^"]+)"$`, b.makeDirectory)
	sc.Step(`^a line of (\d+) "(.)" in "([^"]+)"$`, b.appendRepeatedLine)
	sc.Step(`^(\d+) lines "(.*)" in "([^"]+)"$`, b.appendNumberedLines)
	sc.Step(`^a file "([^"]+)" with hex:$`, b.writeHexFile)
	sc.Step(`^I run nesso with arguments (.+)$`, b.runNesso)
	sc.Step(`^I run nesso without arguments$`, func() error { return b.runNesso("") })
	sc.Step(`^running nesso again prints the same stdout$`, b.runAgainSameStdout)
	sc.Step(`^the exit code is (\d+)$`, b.exitCodeIs)
	sc.Step(`^the exit code is not (\d+)$`, b.exitCodeIsNot)
	// (.*) so a phrase may contain quotes, as in: missing the "message" field.
	sc.Step(`^(stdout|stderr) contains "(.*)"$`, b.streamContains)
	sc.Step(`^stdout shows the host intra-op floor for one thread$`, b.hostIntraOpFloor)
	sc.Step(`^stdout shows the host int8 policy for a budget of two$`, b.hostBudgetTwo)
	sc.Step(`^(stdout|stderr) does not contain "(.*)"$`, b.streamOmits)
	sc.Step(`^the "(--[^"]+)" help is at most (\d+) lines$`, b.optionHelpAtMost)
	sc.Step(`^lines that share text share a score$`, b.sharedTextSharesScore)
	sc.Step(`^every stdout line is a JSON match$`, b.stdoutLinesAreJsonMatches)
	sc.Step(`^(stdout|stderr) is empty$`, b.streamIsEmpty)
}
