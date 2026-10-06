// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

// Package steps binds Gherkin steps to the nesso binary. Scenarios observe only
// files, arguments, exit code, stdout and stderr.
package steps

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/cucumber/godog"
)

const (
	binaryEnv      = "NESSO_BINARY"
	commandTimeout = 120 * time.Second
	dirPlaceholder = "{dir}"
)

type world struct {
	dir      string
	ran      bool
	exitCode int
	stdout   string
	stderr   string
}

// InitializeScenario registers every step. Each scenario runs in its own
// temporary directory, which is also the working directory of nesso.
func InitializeScenario(sc *godog.ScenarioContext) {
	w := &world{}

	sc.Before(func(ctx context.Context, _ *godog.Scenario) (context.Context, error) {
		dir, err := os.MkdirTemp("", "nesso-bdd-")
		if err != nil {
			return ctx, err
		}
		*w = world{dir: dir}
		return ctx, nil
	})
	sc.After(func(ctx context.Context, _ *godog.Scenario, _ error) (context.Context, error) {
		return ctx, os.RemoveAll(w.dir)
	})

	sc.Step(`^a file "([^"]+)" with:$`, w.writeFile)
	sc.Step(`^I run nesso with arguments (.+)$`, w.runNesso)
	sc.Step(`^I run nesso without arguments$`, func() error { return w.runNesso("") })
	sc.Step(`^the exit code is (\d+)$`, w.exitCodeIs)
	sc.Step(`^the exit code is not (\d+)$`, w.exitCodeIsNot)
	sc.Step(`^(stdout|stderr) contains "([^"]*)"$`, w.streamContains)
	sc.Step(`^(stdout|stderr) does not contain "([^"]*)"$`, w.streamOmits)
	sc.Step(`^(stdout|stderr) is empty$`, w.streamIsEmpty)
}

func (w *world) writeFile(name string, content *godog.DocString) error {
	relative := filepath.FromSlash(name)
	if !filepath.IsLocal(relative) {
		return fmt.Errorf("fixture path %q escapes the scenario directory", name)
	}
	target := filepath.Join(w.dir, relative)
	if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
		return err
	}
	return os.WriteFile(target, []byte(content.Content+"\n"), 0o644)
}

func (w *world) runNesso(commandLine string) error {
	binary := os.Getenv(binaryEnv)
	if binary == "" {
		return fmt.Errorf("%s must name the nesso binary", binaryEnv)
	}
	args, err := splitArguments(commandLine)
	if err != nil {
		return err
	}
	for i := range args {
		args[i] = strings.ReplaceAll(args[i], dirPlaceholder, w.dir)
	}

	ctx, cancel := context.WithTimeout(context.Background(), commandTimeout)
	defer cancel()

	var stdout, stderr bytes.Buffer
	cmd := exec.CommandContext(ctx, binary, args...)
	cmd.Dir = w.dir
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	runErr := cmd.Run()

	var exitErr *exec.ExitError
	switch {
	case runErr == nil:
		w.exitCode = 0
	case errors.As(runErr, &exitErr) && ctx.Err() == nil:
		w.exitCode = exitErr.ExitCode()
	default:
		return fmt.Errorf("running %s: %w", binary, runErr)
	}
	w.ran = true
	w.stdout = w.normalize(stdout.String())
	w.stderr = w.normalize(stderr.String())
	return nil
}

// normalize makes captured output identical across platforms: LF line endings
// and the scenario directory replaced by a placeholder.
func (w *world) normalize(text string) string {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	text = strings.ReplaceAll(text, w.dir, dirPlaceholder)
	text = strings.ReplaceAll(text, filepath.ToSlash(w.dir), dirPlaceholder)
	return strings.ReplaceAll(text, `{dir}\`, "{dir}/")
}

func (w *world) requireRun() error {
	if !w.ran {
		return errors.New("no nesso command has run in this scenario")
	}
	return nil
}

func (w *world) exitCodeIs(want string) error {
	if err := w.requireRun(); err != nil {
		return err
	}
	expected, _ := strconv.Atoi(want)
	if w.exitCode != expected {
		return fmt.Errorf("exit code %d, want %d\nstdout:\n%s\nstderr:\n%s", w.exitCode, expected, w.stdout, w.stderr)
	}
	return nil
}

func (w *world) exitCodeIsNot(unwanted string) error {
	if err := w.requireRun(); err != nil {
		return err
	}
	rejected, _ := strconv.Atoi(unwanted)
	if w.exitCode == rejected {
		return fmt.Errorf("exit code is %d\nstdout:\n%s\nstderr:\n%s", w.exitCode, w.stdout, w.stderr)
	}
	return nil
}

func (w *world) stream(name string) (string, error) {
	if err := w.requireRun(); err != nil {
		return "", err
	}
	if name == "stdout" {
		return w.stdout, nil
	}
	return w.stderr, nil
}

func (w *world) streamContains(name, fragment string) error {
	text, err := w.stream(name)
	if err != nil {
		return err
	}
	if !strings.Contains(text, fragment) {
		return fmt.Errorf("%s does not contain %q:\n%s", name, fragment, text)
	}
	return nil
}

func (w *world) streamOmits(name, fragment string) error {
	text, err := w.stream(name)
	if err != nil {
		return err
	}
	if strings.Contains(text, fragment) {
		return fmt.Errorf("%s contains %q:\n%s", name, fragment, text)
	}
	return nil
}

func (w *world) streamIsEmpty(name string) error {
	text, err := w.stream(name)
	if err != nil {
		return err
	}
	if text != "" {
		return fmt.Errorf("%s is not empty:\n%s", name, text)
	}
	return nil
}

// splitArguments splits on whitespace and keeps double-quoted spans together.
func splitArguments(line string) ([]string, error) {
	var (
		args    []string
		current strings.Builder
		quoted  bool
		started bool
	)
	for _, r := range line {
		switch {
		case r == '"':
			quoted = !quoted
			started = true
		case (r == ' ' || r == '\t') && !quoted:
			if started {
				args = append(args, current.String())
				current.Reset()
				started = false
			}
		default:
			current.WriteRune(r)
			started = true
		}
	}
	if quoted {
		return nil, fmt.Errorf("unterminated quote in %q", line)
	}
	if started {
		args = append(args, current.String())
	}
	return args, nil
}
