// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestJUnitSummaryShowsFailingTestOutput(t *testing.T) {
	dir := t.TempDir()
	unit := filepath.Join(dir, "unit.xml")
	// Shape captured from ctest 3.28 --output-junit: the failure element is
	// message="Failed" and the assertion text is system-out.
	body := `<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="(empty)"
	tests="2"
	failures="1"
	disabled="1"
	skipped="0"
	hostname=""
	time="0"
	timestamp="2026-10-26T07:25:30"
	>
	<testcase name="pass_case" classname="pass_case" time="0.000962274" status="run">
		<properties/>
		<system-out>all-good
</system-out>
	</testcase>
	<testcase name="fail_case" classname="fail_case" time="0.000980922" status="fail">
		<failure message="Failed"/>
		<properties/>
		<system-out>CATCH: REQUIRE(x == 1) failed at src/foo.cpp:42
with expansion x := 0
</system-out>
	</testcase>
</testsuite>
`
	if err := os.WriteFile(unit, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	summary := filepath.Join(dir, "summary.md")
	if err := os.WriteFile(summary, []byte("### test-gcc-26-amd64\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	missing := filepath.Join(dir, "missing.xml")
	code := junitMain([]string{
		"--summary", summary,
		"unit=" + unit,
		"functional=" + missing,
	})
	if code != 0 {
		t.Fatalf("exit %d, want 0 (the test step already failed)", code)
	}
	text, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	got := string(text)
	for _, want := range []string{
		"### test-gcc-26-amd64",
		"| unit | 2 | 1 | 1 | 0.00 |",
		"| functional | — | — | — | not run |",
		"**unit / fail_case**",
		"CATCH: REQUIRE(x == 1) failed at src/foo.cpp:42",
		"with expansion x := 0",
	} {
		if !strings.Contains(got, want) {
			t.Fatalf("missing %q\n%s", want, got)
		}
	}
	if strings.Contains(got, "pass_case") {
		t.Fatalf("listed a passing test:\n%s", got)
	}
}

func TestJUnitSummaryAllPassed(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "unit.xml")
	body := `<?xml version="1.0" encoding="UTF-8"?>
<testsuites>
  <testsuite name="a" tests="1" failures="0" errors="0" skipped="0">
    <testcase name="distance_test" classname="distance_test" time="0.25" status="run"/>
  </testsuite>
  <testsuite name="b" tests="1" failures="0" skipped="1" disabled="0">
    <testcase name="top_k_test" classname="top_k_test" time="0.25" status="run"/>
  </testsuite>
</testsuites>
`
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	summary := filepath.Join(dir, "summary.md")
	if code := junitMain([]string{"--summary", summary, "unit=" + path}); code != 0 {
		t.Fatalf("exit %d", code)
	}
	text, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	got := string(text)
	if !strings.Contains(got, "| unit | 2 | 0 | 1 | 0.50 |") {
		t.Fatalf("summary:\n%s", got)
	}
	if !strings.Contains(got, "No failing tests.") {
		t.Fatalf("summary:\n%s", got)
	}
}

func TestJUnitSummaryRejectsMalformed(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "bad.xml")
	if err := os.WriteFile(path, []byte("<nope></nope>"), 0o644); err != nil {
		t.Fatal(err)
	}
	if code := junitMain([]string{"unit=" + path}); code != 1 {
		t.Fatalf("exit %d, want 1", code)
	}
	if code := junitMain(nil); code != 1 {
		t.Fatalf("usage exit %d", code)
	}
}

func TestJUnitSummaryClipsLongOutput(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "unit.xml")
	var lines strings.Builder
	for i := 0; i < 40; i++ {
		lines.WriteString("line\n")
	}
	body := `<?xml version="1.0" encoding="UTF-8"?>
<testsuite tests="1" failures="1">
  <testcase name="noisy" classname="noisy" time="0.1" status="fail">
    <failure message="Failed"/>
    <system-out>` + lines.String() + `</system-out>
  </testcase>
</testsuite>
`
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	summary := filepath.Join(dir, "summary.md")
	if code := junitMain([]string{"--summary", summary, "unit=" + path}); code != 0 {
		t.Fatalf("exit %d", code)
	}
	text, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(text), "\n...\n") {
		t.Fatalf("summary:\n%s", text)
	}
}
