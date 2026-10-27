// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"bytes"
	"os"
	"strings"
	"testing"
	"time"
)

func TestClassifyInspectMissingTag(t *testing.T) {
	output := "ERROR: ghcr.io/lorenzocaprari/nesso-build-env:deps-96d82fd30b7d2f8b: not found\n"
	if classifyInspect(1, output) != inspectMissing {
		t.Fatalf("kind = %v", classifyInspect(1, output))
	}
	if classifyInspect(1, "manifest unknown") != inspectMissing {
		t.Fatal("manifest unknown should retry")
	}
	if classifyInspect(0, output) != inspectFound {
		t.Fatal("exit 0 is present")
	}
}

func TestClassifyInspectFatal(t *testing.T) {
	for _, output := range []string{
		"ERROR: denied: requested access to the resource is denied",
		"unauthorized: authentication required",
		"exec: \"docker\": executable file not found in $PATH",
		"docker: no such file or directory",
	} {
		if classifyInspect(1, output) != inspectFatal {
			t.Fatalf("kind for %q = missing or found", output)
		}
	}
}

func TestWaitBuildEnvAlreadyPresent(t *testing.T) {
	var sleeps int
	stdout := &bytes.Buffer{}
	w := fakeWait(t, stdout, &sleeps, func(image string) (int, string) {
		return 0, "Name: " + image
	})
	w.wait = 350 * time.Minute
	if err := w.run([]string{"ghcr.io/example/amd64", "ghcr.io/example/arm64"}); err != nil {
		t.Fatal(err)
	}
	if sleeps != 0 {
		t.Fatalf("sleeps = %d", sleeps)
	}
	if !strings.Contains(stdout.String(), "present ghcr.io/example/amd64\n") ||
		!strings.Contains(stdout.String(), "present ghcr.io/example/arm64\n") {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestWaitBuildEnvPollsUntilBothTagsExist(t *testing.T) {
	var sleeps int
	seen := map[string]int{}
	stdout := &bytes.Buffer{}
	w := fakeWait(t, stdout, &sleeps, func(image string) (int, string) {
		seen[image]++
		if strings.HasSuffix(image, "arm64") || seen[image] >= 3 {
			return 0, ""
		}
		return 1, "ERROR: " + image + ": not found"
	})
	w.wait = 2 * time.Minute
	if err := w.run([]string{"ghcr.io/example/amd64", "ghcr.io/example/arm64"}); err != nil {
		t.Fatal(err)
	}
	if sleeps != 2 {
		t.Fatalf("sleeps = %d", sleeps)
	}
	if !strings.Contains(stdout.String(), "waiting 30s for ghcr.io/example/amd64: ERROR: ghcr.io/example/amd64: not found\n") {
		t.Fatalf("stdout = %q", stdout.String())
	}
	if strings.Contains(stdout.String(), "waiting 30s for ghcr.io/example/arm64") {
		t.Fatalf("arm64 was present: %q", stdout.String())
	}
}

func TestWaitBuildEnvMissingOnMainFailsImmediately(t *testing.T) {
	var sleeps int
	w := fakeWait(t, &bytes.Buffer{}, &sleeps, func(image string) (int, string) {
		return 1, "ERROR: " + image + ": not found"
	})
	err := w.run([]string{"ghcr.io/example/amd64", "ghcr.io/example/arm64"})
	if err == nil {
		t.Fatal("expected an error")
	}
	if sleeps != 0 {
		t.Fatalf("sleeps = %d", sleeps)
	}
	if err.Error() != "missing ghcr.io/example/amd64; "+buildEnvPublishedHint {
		t.Fatalf("error = %q", err.Error())
	}
}

func TestWaitBuildEnvTimesOut(t *testing.T) {
	var sleeps int
	w := fakeWait(t, &bytes.Buffer{}, &sleeps, func(image string) (int, string) {
		return 1, "ERROR: " + image + ": not found"
	})
	w.wait = 45 * time.Second
	err := w.run([]string{"ghcr.io/example/amd64"})
	if err == nil {
		t.Fatal("expected a timeout")
	}
	if sleeps != 2 {
		t.Fatalf("sleeps = %d, want 2 (30s then the remaining 15s)", sleeps)
	}
	if !strings.Contains(err.Error(), "timed out waiting for ghcr.io/example/amd64") ||
		!strings.Contains(err.Error(), buildEnvPullRequestHint) {
		t.Fatalf("error = %q", err.Error())
	}
}

func TestWaitBuildEnvDeniedDoesNotPoll(t *testing.T) {
	var sleeps int
	w := fakeWait(t, &bytes.Buffer{}, &sleeps, func(string) (int, string) {
		return 1, "ERROR: denied: requested access to the resource is denied"
	})
	w.wait = 350 * time.Minute
	err := w.run([]string{"ghcr.io/example/amd64"})
	if err == nil {
		t.Fatal("expected a fatal inspect error")
	}
	if sleeps != 0 {
		t.Fatalf("sleeps = %d", sleeps)
	}
	if !strings.Contains(err.Error(), "denied") {
		t.Fatalf("error = %q", err.Error())
	}
}

func TestWaitBuildEnvRetriesTransientInspect(t *testing.T) {
	var sleeps int
	calls := 0
	w := fakeWait(t, &bytes.Buffer{}, &sleeps, func(image string) (int, string) {
		calls++
		if calls == 1 {
			return 1, "net/http: TLS handshake timeout"
		}
		return 0, image
	})
	w.wait = time.Minute
	if err := w.run([]string{"ghcr.io/example/amd64"}); err != nil {
		t.Fatal(err)
	}
	if sleeps != 1 {
		t.Fatalf("sleeps = %d", sleeps)
	}
}

func TestWaitBuildEnvMainUsage(t *testing.T) {
	if waitBuildEnvMain(nil) == 0 {
		t.Fatal("no images should fail")
	}
	if waitBuildEnvMain([]string{"--wait", "-1s", "ghcr.io/example/amd64"}) == 0 {
		t.Fatal("negative wait should fail")
	}
	if waitBuildEnvMain([]string{"--interval", "0s", "ghcr.io/example/amd64"}) == 0 {
		t.Fatal("zero interval should fail")
	}
	if waitBuildEnvMain([]string{"ghcr.io/example/bad image"}) == 0 {
		t.Fatal("image with a space should fail")
	}
}

func TestPackageWorkflowWaitsOnlyOnPullRequest(t *testing.T) {
	body, err := os.ReadFile("../../.github/workflows/package.yml")
	if err != nil {
		t.Fatal(err)
	}
	text := string(body)
	start := strings.Index(text, "  resolve-image:\n")
	end := strings.Index(text, "  repro-build:\n")
	if start < 0 || end < start {
		t.Fatal("resolve-image job not found")
	}
	job := text[start:end]
	for _, needle := range []string{
		"timeout-minutes: 360",
		"contents: read",
		"packages: read",
		"go run -C scripts/ci . wait-build-env",
		`[[ "${EVENT_NAME}" == "pull_request" ]]`,
		"--wait 350m",
		"--interval 30s",
		`GOPROXY: "off"`,
		"GOTOOLCHAIN: local",
	} {
		if !strings.Contains(job, needle) {
			t.Fatalf("resolve-image missing %q", needle)
		}
	}
	if strings.Contains(job, "packages: write") {
		t.Fatal("resolve-image must not push the build-env image")
	}
	if strings.Contains(job, "imagetools inspect") {
		t.Fatal("inspect belongs in wait-build-env")
	}
	waitAt := strings.Index(job, "--wait 350m")
	guardAt := strings.Index(job, `[[ "${EVENT_NAME}" == "pull_request" ]]`)
	if waitAt < guardAt {
		t.Fatal("--wait 350m is not inside the pull_request branch")
	}
}

func fakeWait(t *testing.T, stdout *bytes.Buffer, sleeps *int, inspect func(string) (int, string)) buildEnvWait {
	t.Helper()
	var clock time.Time
	return buildEnvWait{
		interval: 30 * time.Second,
		now:      func() time.Time { return clock },
		sleep: func(d time.Duration) {
			*sleeps++
			clock = clock.Add(d)
		},
		inspect: inspect,
		stdout:  stdout,
	}
}
