// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package steps

import (
	"fmt"
	"os"

	"github.com/lorenzocaprari/nesso/test/functional/internal/normalize"
	"github.com/lorenzocaprari/nesso/test/functional/internal/runner"
)

const binaryEnv = "NESSO_BINARY"

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
	for i := range args {
		args[i] = normalize.ExpandDir(args[i], dir)
	}

	result, err := runner.Run(binary, args, dir)
	if err != nil {
		return err
	}
	result.Stdout = normalize.Output(result.Stdout, dir)
	result.Stderr = normalize.Output(result.Stderr, dir)
	b.world.Result = result
	b.world.Ran = true
	return nil
}
