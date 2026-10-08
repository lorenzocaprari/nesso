// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package steps

import (
	"fmt"
	"os"
	"strings"

	"github.com/lorenzocaprari/nesso/test/functional/internal/normalize"
	"github.com/lorenzocaprari/nesso/test/functional/internal/runner"
)

const binaryEnv = "NESSO_BINARY"
const fixtureEnv = "NESSO_FIXTURE_DIR"

func (b *bindings) runNesso(commandLine string) error {
	binary := os.Getenv(binaryEnv)
	if binary == "" {
		return fmt.Errorf("%s must name the nesso binary", binaryEnv)
	}
	args, err := runner.SplitArguments(commandLine)
	if err != nil {
		return err
	}
	dir := b.world.Dir
	fixture := os.Getenv(fixtureEnv)
	for i := range args {
		if strings.Contains(args[i], normalize.FixturePlaceholder) && fixture == "" {
			return fmt.Errorf("%s must name the fixture directory", fixtureEnv)
		}
		args[i] = normalize.ExpandDir(args[i], dir, fixture)
	}

	result, err := runner.Run(binary, args, dir)
	if err != nil {
		return err
	}
	result.Stdout = normalize.Output(result.Stdout, dir, fixture)
	result.Stderr = normalize.Output(result.Stderr, dir, fixture)
	b.world.Result = result
	b.world.CommandLine = commandLine
	b.world.Ran = true
	return nil
}

func (b *bindings) runAgainSameStdout() error {
	if !b.world.Ran {
		return fmt.Errorf("nesso has not been run")
	}
	previous := b.world.Result.Stdout
	if err := b.runNesso(b.world.CommandLine); err != nil {
		return err
	}
	if b.world.Result.Stdout != previous {
		return fmt.Errorf("stdout changed\nfirst:\n%s\nsecond:\n%s", previous, b.world.Result.Stdout)
	}
	return nil
}
