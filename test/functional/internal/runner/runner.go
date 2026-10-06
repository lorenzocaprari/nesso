// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

// Package runner executes the nesso binary and captures its observable
// behaviour.
package runner

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"os/exec"
	"time"
)

// Timeout bounds a single nesso invocation.
const Timeout = 120 * time.Second

// Result is the raw outcome of one invocation.
type Result struct {
	ExitCode int
	Stdout   string
	Stderr   string
}

// Run executes binary with args in dir. A non-zero exit status is a Result, not
// an error. Failing to start the process or exceeding Timeout is an error.
func Run(binary string, args []string, dir string) (Result, error) {
	ctx, cancel := context.WithTimeout(context.Background(), Timeout)
	defer cancel()

	var stdout, stderr bytes.Buffer
	cmd := exec.CommandContext(ctx, binary, args...)
	cmd.Dir = dir
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	runErr := cmd.Run()

	result := Result{Stdout: stdout.String(), Stderr: stderr.String()}
	var exitErr *exec.ExitError
	switch {
	case runErr == nil:
		return result, nil
	case errors.As(runErr, &exitErr) && ctx.Err() == nil:
		result.ExitCode = exitErr.ExitCode()
		return result, nil
	default:
		return result, fmt.Errorf("running %s: %w", binary, runErr)
	}
}
