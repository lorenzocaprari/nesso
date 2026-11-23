// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

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

	result, err := runner.Run(binary, args, dir, b.world.Stdin, b.world.Env, b.world.Terminal, b.world.StdoutPath)
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
	return b.runPrintsSameStdout(b.world.CommandLine)
}

// finalRecordsMatch drops the `"final":false` records from the current stdout
// and compares the rest with the stdout of another run.
func (b *bindings) finalRecordsMatch(commandLine string) error {
	if !b.world.Ran {
		return fmt.Errorf("nesso has not been run")
	}
	var kept strings.Builder
	provisional := 0
	for _, line := range strings.SplitAfter(b.world.Result.Stdout, "\n") {
		if strings.Contains(line, `,"final":false,"refresh":`) {
			provisional++
			continue
		}
		kept.WriteString(line)
	}
	if provisional == 0 {
		return fmt.Errorf("stdout has no provisional records:\n%s", b.world.Result.Stdout)
	}
	if err := b.runNesso(commandLine); err != nil {
		return err
	}
	if kept.String() != b.world.Result.Stdout {
		return fmt.Errorf("final records differ\nwith provisional records:\n%s\nplain:\n%s", kept.String(), b.world.Result.Stdout)
	}
	return nil
}

func (b *bindings) runPrintsSameStdout(commandLine string) error {
	if !b.world.Ran {
		return fmt.Errorf("nesso has not been run")
	}
	previous := b.world.Result.Stdout
	if err := b.runNesso(commandLine); err != nil {
		return err
	}
	if b.world.Result.Stdout != previous {
		return fmt.Errorf("stdout changed\nfirst:\n%s\nsecond:\n%s", previous, b.world.Result.Stdout)
	}
	return nil
}
