// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package steps

import (
	"errors"
	"fmt"
	"strconv"
	"strings"
)

func (b *bindings) requireRun() error {
	if !b.world.Ran {
		return errors.New("no nesso command has run in this scenario")
	}
	return nil
}

func (b *bindings) exitCodeIs(want string) error {
	if err := b.requireRun(); err != nil {
		return err
	}
	expected, _ := strconv.Atoi(want)
	result := b.world.Result
	if result.ExitCode != expected {
		return fmt.Errorf("exit code %d, want %d\nstdout:\n%s\nstderr:\n%s",
			result.ExitCode, expected, result.Stdout, result.Stderr)
	}
	return nil
}

func (b *bindings) exitCodeIsNot(unwanted string) error {
	if err := b.requireRun(); err != nil {
		return err
	}
	rejected, _ := strconv.Atoi(unwanted)
	result := b.world.Result
	if result.ExitCode == rejected {
		return fmt.Errorf("exit code is %d\nstdout:\n%s\nstderr:\n%s", result.ExitCode, result.Stdout, result.Stderr)
	}
	return nil
}

func (b *bindings) stream(name string) (string, error) {
	if err := b.requireRun(); err != nil {
		return "", err
	}
	if name == "stdout" {
		return b.world.Result.Stdout, nil
	}
	return b.world.Result.Stderr, nil
}

func (b *bindings) streamContains(name, fragment string) error {
	text, err := b.stream(name)
	if err != nil {
		return err
	}
	if !strings.Contains(text, fragment) {
		return fmt.Errorf("%s does not contain %q:\n%s", name, fragment, text)
	}
	return nil
}

func (b *bindings) streamOmits(name, fragment string) error {
	text, err := b.stream(name)
	if err != nil {
		return err
	}
	if strings.Contains(text, fragment) {
		return fmt.Errorf("%s contains %q:\n%s", name, fragment, text)
	}
	return nil
}

func (b *bindings) streamIsEmpty(name string) error {
	text, err := b.stream(name)
	if err != nil {
		return err
	}
	if text != "" {
		return fmt.Errorf("%s is not empty:\n%s", name, text)
	}
	return nil
}
