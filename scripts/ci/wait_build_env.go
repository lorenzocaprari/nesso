// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"
	"time"
)

const buildEnvPublishedHint = "run CI on main so nesso-build-env is published first"

const buildEnvPullRequestHint = "a pull request's Pipeline prepare-image job publishes this tag"

type inspectKind int

const (
	inspectFound inspectKind = iota
	inspectMissing
	inspectFatal
)

type buildEnvWait struct {
	wait     time.Duration
	interval time.Duration
	now      func() time.Time
	sleep    func(time.Duration)
	inspect  func(image string) (int, string)
	stdout   io.Writer
}

func waitBuildEnvMain(args []string) int {
	fs := flag.NewFlagSet("wait-build-env", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	wait := fs.Duration("wait", 0, "how long to retry a missing tag; 0 fails on the first miss")
	interval := fs.Duration("interval", 30*time.Second, "time between retries")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci wait-build-env [--wait DURATION] [--interval DURATION] IMAGE...")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	if *wait < 0 || *interval <= 0 || fs.NArg() == 0 {
		fs.Usage()
		return 1
	}
	for _, image := range fs.Args() {
		if strings.TrimSpace(image) == "" || strings.ContainsAny(image, " \t\n") {
			fmt.Fprintf(os.Stderr, "wait-build-env: invalid image %q\n", image)
			fs.Usage()
			return 1
		}
	}
	err := (buildEnvWait{
		wait:     *wait,
		interval: *interval,
		inspect:  dockerBuildxInspect,
		stdout:   os.Stdout,
	}).run(fs.Args())
	if err != nil {
		fmt.Fprintf(os.Stderr, "wait-build-env: %v\n", err)
		return 1
	}
	return 0
}

func (w buildEnvWait) run(images []string) error {
	if len(images) == 0 {
		return fmt.Errorf("no image")
	}
	if w.interval <= 0 {
		return fmt.Errorf("interval must be positive")
	}
	now := w.now
	if now == nil {
		now = time.Now
	}
	sleep := w.sleep
	if sleep == nil {
		sleep = time.Sleep
	}
	stdout := w.stdout
	if stdout == nil {
		stdout = os.Stdout
	}
	inspect := w.inspect
	if inspect == nil {
		inspect = dockerBuildxInspect
	}
	deadline := now().Add(w.wait)
	announced := map[string]bool{}
	detail := map[string]string{}
	var nextHeartbeat time.Time
	for {
		var waiting []string
		for _, image := range images {
			code, output := inspect(image)
			switch classifyInspect(code, output) {
			case inspectFound:
				if !announced[image] {
					fmt.Fprintf(stdout, "present %s\n", image)
					announced[image] = true
				}
			case inspectFatal:
				return fmt.Errorf("%s: %s", image, oneLine(output))
			default:
				waiting = append(waiting, image)
				detail[image] = oneLine(output)
			}
		}
		if len(waiting) == 0 {
			return nil
		}
		if w.wait == 0 || !now().Before(deadline) {
			if w.wait == 0 {
				return fmt.Errorf("missing %s; %s", waiting[0], buildEnvPublishedHint)
			}
			return fmt.Errorf("timed out waiting for %s; %s", strings.Join(waiting, ", "), buildEnvPullRequestHint)
		}
		delay := w.interval
		if remaining := deadline.Sub(now()); delay > remaining {
			delay = remaining
		}
		if nextHeartbeat.IsZero() || !now().Before(nextHeartbeat) {
			for _, image := range waiting {
				fmt.Fprintf(stdout, "waiting %s for %s: %s\n", delay, image, detail[image])
			}
			nextHeartbeat = now().Add(5 * time.Minute)
		}
		sleep(delay)
	}
}

func classifyInspect(code int, output string) inspectKind {
	if code == 0 {
		return inspectFound
	}
	text := strings.ToLower(output)
	// A missing docker binary also says "not found". That is not a tag GHCR
	// has not published yet, and retrying it cannot succeed.
	if strings.Contains(text, "executable file not found") || strings.Contains(text, "no such file or directory") {
		return inspectFatal
	}
	if strings.Contains(text, "unauthorized") || strings.Contains(text, "authentication required") ||
		strings.Contains(text, "insufficient_scope") || strings.Contains(text, "denied") {
		return inspectFatal
	}
	if strings.Contains(text, "not found") || strings.Contains(text, "manifest unknown") ||
		strings.Contains(text, "name unknown") || strings.Contains(text, "no such manifest") {
		return inspectMissing
	}
	// A transport error names neither the tag nor an auth failure. Retry it
	// until --wait. An auth failure already returned above.
	return inspectMissing
}

func oneLine(output string) string {
	fields := strings.Fields(output)
	if len(fields) == 0 {
		return "inspect failed"
	}
	return strings.Join(fields, " ")
}

func dockerBuildxInspect(image string) (int, string) {
	cmd := exec.Command("docker", "buildx", "imagetools", "inspect", image)
	out, err := cmd.CombinedOutput()
	if err == nil {
		return 0, string(out)
	}
	var exitErr *exec.ExitError
	if errors.As(err, &exitErr) {
		return exitErr.ExitCode(), string(out)
	}
	return 1, err.Error()
}
