// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestMain(m *testing.M) {
	os.Unsetenv("GITHUB_STEP_SUMMARY")
	os.Exit(m.Run())
}

func TestGateFailsOnUncoveredChangedLines(t *testing.T) {
	dir := t.TempDir()
	lcov := filepath.Join(dir, "coverage.cleaned")
	diff := filepath.Join(dir, "pr.diff")
	summary := filepath.Join(dir, "summary.md")
	// Absolute SF paths are what lcov writes inside the coverage job.
	trace := "" +
		"TN:\n" +
		"SF:/__w/nesso/nesso/src/cli.cpp\n" +
		"FN:2,covered\n" +
		"FNDA:1,covered\n" +
		"DA:2,1,abc123\n" +
		"DA:3,0\n" +
		"DA:4,3\n" +
		"DA:5,1\n" +
		"BRDA:2,0,0,1\n" +
		"end_of_record\n" +
		"SF:/__w/nesso/nesso/src/cli.cpp\n" +
		"DA:3,0\n" +
		"DA:4,8\n" +
		"end_of_record\n"
	// Line 1 is a comment. It is not executable, so it must not dilute the ratio.
	// A workflow file in the same diff is not product code.
	body := "" +
		"diff --git a/.github/workflows/ci.yml b/.github/workflows/ci.yml\n" +
		"--- a/.github/workflows/ci.yml\n" +
		"+++ b/.github/workflows/ci.yml\n" +
		"@@ -1,0 +1,1 @@\n" +
		"+name: Pipeline\n" +
		"diff --git a/src/cli.cpp b/src/cli.cpp\n" +
		"--- a/src/cli.cpp\n" +
		"+++ b/src/cli.cpp\n" +
		"@@ -0,0 +1,5 @@\n" +
		"+// comment only\n" +
		"+int covered() { return 1; }\n" +
		"+int missed() { return 2; }\n" +
		"+int coveredAgain() { return 3; }\n" +
		"+int coveredToo() { return 4; }\n"
	if err := os.WriteFile(lcov, []byte(trace), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(diff, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	code := patchMain([]string{
		"--lcov", lcov,
		"--diff", diff,
		"--repo", "/__w/nesso/nesso",
		"--prefix", "src",
		"--threshold", "80",
		"--summary", summary,
	})
	if code != 2 {
		t.Fatalf("exit %d, want 2", code)
	}
	got, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	text := string(got)
	if !strings.Contains(text, "75.0% of changed executable lines under `src/` are covered (3/4). Required >= 80%.") {
		t.Fatalf("summary:\n%s", text)
	}
	if !strings.Contains(text, "src/cli.cpp:3\n") {
		t.Fatalf("missing uncovered line:\n%s", text)
	}
	if strings.Contains(text, "src/cli.cpp:1") || strings.Contains(text, "ci.yml") {
		t.Fatalf("counted a non-executable or non-product line:\n%s", text)
	}
}

func TestGatePassesAtEightyPercent(t *testing.T) {
	dir := t.TempDir()
	lcov := filepath.Join(dir, "coverage.cleaned")
	diff := filepath.Join(dir, "pr.diff")
	summary := filepath.Join(dir, "summary.md")
	trace := "SF:src/cli.cpp\nDA:1,1\nDA:2,1\nDA:3,0\nDA:4,4\nDA:5,1\nend_of_record\n"
	body := "" +
		"diff --git a/src/cli.cpp b/src/cli.cpp\n" +
		"@@ -0,0 +1,5 @@\n" +
		"+a\n+b\n+c\n+d\n+e\n"
	if err := os.WriteFile(lcov, []byte(trace), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(diff, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	code := patchMain([]string{
		"--lcov", lcov,
		"--diff", diff,
		"--threshold", "80",
		"--summary", summary,
	})
	if code != 0 {
		t.Fatalf("exit %d, want 0", code)
	}
	text, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(text), "80.0% of changed executable lines under `src/` are covered (4/5). Required >= 80%.") {
		t.Fatalf("summary:\n%s", text)
	}
	if !strings.Contains(string(text), "src/cli.cpp:3\n") {
		t.Fatalf("passing gate should still list the miss:\n%s", text)
	}
}

func TestGatePassesWhenDiffHasNoExecutableProductLines(t *testing.T) {
	dir := t.TempDir()
	lcov := filepath.Join(dir, "coverage.cleaned")
	diff := filepath.Join(dir, "pr.diff")
	summary := filepath.Join(dir, "summary.md")
	trace := "SF:src/cli.cpp\nDA:10,1\nend_of_record\n"
	body := "" +
		"diff --git a/.github/workflows/ci.yml b/.github/workflows/ci.yml\n" +
		"@@ -4,0 +5,2 @@\n" +
		"+  # ci only\n" +
		"+  run: true\n" +
		"diff --git a/src/cli.cpp b/src/cli.cpp\n" +
		"@@ -1,0 +1,2 @@\n" +
		"+// docs comment\n" +
		"+// still not executable\n" +
		"diff --git \"a/src/note file.cpp\" \"b/src/note file.cpp\"\n" +
		"@@ -1 +1 @@\n" +
		"-old\n" +
		"+// renamed comment only\n"
	if err := os.WriteFile(lcov, []byte(trace), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(diff, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	code := patchMain([]string{"--lcov", lcov, "--diff", diff, "--summary", summary})
	if code != 0 {
		t.Fatalf("exit %d, want 0", code)
	}
	text, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(text), "No changed executable lines under `src/`. Patch coverage passed.") {
		t.Fatalf("summary:\n%s", text)
	}
}

func TestGateGitDiffFailsUncoveredLine(t *testing.T) {
	if _, err := exec.LookPath("git"); err != nil {
		t.Skip("git is required")
	}
	dir := t.TempDir()
	git := func(args ...string) {
		t.Helper()
		cmd := exec.Command("git", args...)
		cmd.Dir = dir
		cmd.Env = append(os.Environ(),
			"GIT_AUTHOR_NAME=Nesso Test",
			"GIT_AUTHOR_EMAIL=test@example.com",
			"GIT_COMMITTER_NAME=Nesso Test",
			"GIT_COMMITTER_EMAIL=test@example.com",
		)
		if out, err := cmd.CombinedOutput(); err != nil {
			t.Fatalf("git %s: %v\n%s", strings.Join(args, " "), err, out)
		}
	}
	git("init", "-b", "main")
	git("config", "commit.gpgsign", "false")
	git("config", "core.autocrlf", "false")
	if err := os.WriteFile(filepath.Join(dir, "README.md"), []byte("base\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	git("add", "README.md")
	git("commit", "-m", "base")
	base := strings.TrimSpace(gitOutput(t, dir, "rev-parse", "HEAD"))
	if err := os.Mkdir(filepath.Join(dir, "src"), 0o755); err != nil {
		t.Fatal(err)
	}
	source := "int covered() { return 1; }\nint missed() { return 2; }\n"
	if err := os.WriteFile(filepath.Join(dir, "src", "cli.cpp"), []byte(source), 0o644); err != nil {
		t.Fatal(err)
	}
	git("add", "src/cli.cpp")
	git("commit", "-m", "add cli")
	head := strings.TrimSpace(gitOutput(t, dir, "rev-parse", "HEAD"))
	lcov := filepath.Join(dir, "coverage.cleaned")
	trace := "SF:" + filepath.Join(dir, "src", "cli.cpp") + "\nDA:1,4\nDA:2,0\nend_of_record\n"
	if err := os.WriteFile(lcov, []byte(trace), 0o644); err != nil {
		t.Fatal(err)
	}
	summary := filepath.Join(dir, "summary.md")
	code := patchMain([]string{
		"--lcov", lcov,
		"--repo", dir,
		"--base", base,
		"--head", head,
		"--prefix", "src",
		"--threshold", "80",
		"--summary", summary,
	})
	if code != 2 {
		t.Fatalf("exit %d, want 2", code)
	}
	text, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(text), "50.0% of changed executable lines under `src/` are covered (1/2). Required >= 80%.") {
		t.Fatalf("summary:\n%s", text)
	}
	if !strings.Contains(string(text), "src/cli.cpp:2\n") {
		t.Fatalf("summary:\n%s", text)
	}
}

func TestPatchCoverageRejectsBadArgs(t *testing.T) {
	dir := t.TempDir()
	lcov := filepath.Join(dir, "coverage.cleaned")
	if err := os.WriteFile(lcov, []byte("SF:src/a.cpp\nDA:1,1\nend_of_record\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if code := patchMain(nil); code != 1 {
		t.Fatalf("missing lcov exit %d", code)
	}
	if code := patchMain([]string{"--lcov", lcov, "--diff", lcov, "--base", "abc"}); code != 1 {
		t.Fatalf("both sources exit %d", code)
	}
	if code := patchMain([]string{"--lcov", lcov, "--base", "HEAD;touch", "--repo", dir}); code != 1 {
		t.Fatalf("bad rev exit %d", code)
	}
	if code := patchMain([]string{"--lcov", lcov, "--diff", lcov, "--threshold", "101"}); code != 1 {
		t.Fatalf("bad threshold exit %d", code)
	}
}

func TestFormatPatchCapsUncoveredList(t *testing.T) {
	uncovered := make([]string, maxUncoveredListed+3)
	for i := range uncovered {
		uncovered[i] = "src/cli.cpp:1"
	}
	text := formatPatch(patchResult{
		covered:   0,
		total:     len(uncovered),
		uncovered: uncovered,
		prefixes:  []string{"src"},
		threshold: 80,
	})
	if !strings.Contains(text, "... and 3 more\n") {
		t.Fatalf("summary:\n%s", text)
	}
	if strings.Count(text, "src/cli.cpp:1\n") != maxUncoveredListed {
		t.Fatalf("listed %d lines", strings.Count(text, "src/cli.cpp:1\n"))
	}
}

func gitOutput(t *testing.T, dir string, args ...string) string {
	t.Helper()
	cmd := exec.Command("git", args...)
	cmd.Dir = dir
	out, err := cmd.Output()
	if err != nil {
		t.Fatalf("git %s: %v", strings.Join(args, " "), err)
	}
	return string(out)
}
