// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package steps

import (
	"errors"
	"fmt"
	"runtime"
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

func phrase(fragment string) string {
	// Feature files write \" for a quote inside a step.
	return strings.ReplaceAll(fragment, `\"`, `"`)
}

func (b *bindings) hostIntraOpFloor() error {
	text, err := b.stream("stdout")
	if err != nil {
		return err
	}
	int8Line := "int8: 1 single-thread session"
	fp32Line := "fp32: 1 session, 1 intra-op thread"
	if runtime.GOARCH == "arm64" {
		int8Line = "int8: 1 session, 2 intra-op threads"
		fp32Line = "fp32: 1 session, 2 intra-op threads"
	}
	if !strings.Contains(text, int8Line) || !strings.Contains(text, fp32Line) {
		return fmt.Errorf("stdout does not contain %q and %q:\n%s", int8Line, fp32Line, text)
	}
	return nil
}

func (b *bindings) hostBudgetTwo() error {
	text, err := b.stream("stdout")
	if err != nil {
		return err
	}
	int8Line := "int8: 2 single-thread"
	if runtime.GOARCH == "arm64" {
		int8Line = "int8: 1 session, 2 intra-op threads"
	}
	if !strings.Contains(text, int8Line) {
		return fmt.Errorf("stdout does not contain %q:\n%s", int8Line, text)
	}
	return nil
}

func (b *bindings) streamContains(name, fragment string) error {
	text, err := b.stream(name)
	if err != nil {
		return err
	}
	fragment = phrase(fragment)
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
	fragment = phrase(fragment)
	if strings.Contains(text, fragment) {
		return fmt.Errorf("%s contains %q:\n%s", name, fragment, text)
	}
	return nil
}

func (b *bindings) optionHelpAtMost(flag, limit string) error {
	text, err := b.stream("stdout")
	if err != nil {
		return err
	}
	maxLines, convErr := strconv.Atoi(limit)
	if convErr != nil {
		return convErr
	}
	count, err := optionHelpLines(text, flag)
	if err != nil {
		return err
	}
	if count > maxLines {
		return fmt.Errorf("%s help is %d lines, want at most %d\n%s", flag, count, maxLines, text)
	}
	return nil
}

func optionHelpLines(text, flag string) (int, error) {
	lines := strings.Split(strings.TrimRight(text, "\n"), "\n")
	start := -1
	for i, line := range lines {
		if strings.Contains(line, flag) {
			start = i
			break
		}
	}
	if start < 0 {
		return 0, fmt.Errorf("stdout has no %s help:\n%s", flag, text)
	}
	count := 1
	for i := start + 1; i < len(lines); i++ {
		trimmed := strings.TrimLeft(lines[i], " ")
		if trimmed == "" || strings.HasPrefix(trimmed, "-") || strings.HasSuffix(strings.TrimRight(trimmed, " "), ":") {
			break
		}
		count++
	}
	return count, nil
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
