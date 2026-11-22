// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// measuredReport is a trimmed --measure report: two datasets, nesso in two
// modes, grep with no index, and BM25.
const measuredReport = `{
  "datasets": [{"name": "HDFS_2k", "engines": []}],
  "measure": {
    "cpuModel": "Test CPU",
    "nproc": 4,
    "repeat": 3,
    "datasets": [
      {
        "name": "A",
        "logBytes": 1000,
        "engines": [
          {"engine": "nesso", "status": "ok", "indexWallSeconds": 4, "indexBytes": 3000,
           "modes": [
             {"mode": "auto", "queryLatencyMedianSeconds": 0.2},
             {"mode": "lexical", "queryLatencyMedianSeconds": 0.05}
           ]},
          {"engine": "grep", "status": "ok", "indexWallSeconds": null, "indexBytes": null,
           "queryLatencyMedianSeconds": 0.01},
          {"engine": "bm25", "status": "ok", "indexWallSeconds": 0.5, "indexBytes": 2000,
           "queryLatencyMedianSeconds": 0.1}
        ]
      },
      {
        "name": "B",
        "logBytes": 3000,
        "engines": [
          {"engine": "nesso", "status": "ok", "indexWallSeconds": 8, "indexBytes": 3000,
           "modes": [
             {"mode": "auto", "queryLatencyMedianSeconds": 0.4},
             {"mode": "lexical", "queryLatencyMedianSeconds": 0.07}
           ]},
          {"engine": "grep", "status": "ok", "indexWallSeconds": null, "indexBytes": null,
           "queryLatencyMedianSeconds": 0.03},
          {"engine": "bm25", "status": "ok", "indexWallSeconds": 1.5, "indexBytes": 1000,
           "queryLatencyMedianSeconds": 0.3}
        ]
      }
    ]
  }
}`

func writeEval(t *testing.T, dir, name, body string) string {
	t.Helper()
	path := filepath.Join(dir, name)
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func tableRow(t *testing.T, text, name string) []string {
	t.Helper()
	for _, line := range strings.Split(text, "\n") {
		if strings.HasPrefix(line, "| "+name+" |") {
			cells := strings.Split(strings.Trim(line, "|"), "|")
			for i := range cells {
				cells[i] = strings.TrimSpace(cells[i])
			}
			return cells[1:]
		}
	}
	t.Fatalf("no row %q in:\n%s", name, text)
	return nil
}

func TestEvalRatiosPerDatasetAndAggregate(t *testing.T) {
	path := writeEval(t, t.TempDir(), "all_2k.json", measuredReport)
	measure, err := loadEvalMeasure(path)
	if err != nil || measure == nil {
		t.Fatalf("load: %v %v", measure, err)
	}
	text := formatEvalRatios("2k", measure, nil, "")
	if !strings.Contains(text, evalRatiosLabel) {
		t.Fatalf("missing label:\n%s", text)
	}
	if !strings.Contains(text, "| dataset | nesso index wall ÷ BM25 | nesso/auto p50 ÷ grep | nesso/auto p50 ÷ BM25 | nesso/lexical p50 ÷ grep | nesso/lexical p50 ÷ BM25 | nesso index ÷ log | BM25 index ÷ log |") {
		t.Fatalf("header:\n%s", text)
	}
	// A: 4/0.5, 0.2/0.01, 0.2/0.1, 0.05/0.01, 0.05/0.1, 3000/1000, 2000/1000.
	want := []string{"8.00", "20.0", "2.00", "5.00", "0.50", "3.00", "2.00"}
	if got := tableRow(t, text, "A"); strings.Join(got, ",") != strings.Join(want, ",") {
		t.Fatalf("A = %v, want %v", got, want)
	}
	// Aggregate: (4+8)/(0.5+1.5); median(0.2,0.4)/median(0.01,0.03) = 0.3/0.02;
	// 0.3/0.2; 0.06/0.02; 0.06/0.2; 6000/4000; 3000/4000.
	want = []string{"6.00", "15.0", "1.50", "3.00", "0.30", "1.50", "0.75"}
	if got := tableRow(t, text, "**aggregate**"); strings.Join(got, ",") != strings.Join(want, ",") {
		t.Fatalf("aggregate = %v, want %v", got, want)
	}
	if !strings.Contains(text, "Test CPU, nproc 4, median of 3 repeats") {
		t.Fatalf("runner line:\n%s", text)
	}
}

func TestEvalRatiosShowsPreviousInParentheses(t *testing.T) {
	dir := t.TempDir()
	current := writeEval(t, dir, "current.json", measuredReport)
	previousBody := strings.Replace(measuredReport, `"indexWallSeconds": 4,`, `"indexWallSeconds": 2,`, 1)
	previous := writeEval(t, dir, "previous.json", previousBody)
	summary := filepath.Join(dir, "summary.md")
	if code := evalRatiosMain([]string{"--current", current, "--previous", previous, "--title", "2k", "--summary", summary}); code != 0 {
		t.Fatalf("exit %d", code)
	}
	data, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	text := string(data)
	if got := tableRow(t, text, "A"); got[0] != "8.00 (4.00)" || got[1] != "20.0 (20.0)" {
		t.Fatalf("A = %v", got)
	}
	if !strings.Contains(text, "Previous nightly, in parentheses:") {
		t.Fatalf("previous line:\n%s", text)
	}
}

func TestEvalRatiosWithoutMeasureExitsZero(t *testing.T) {
	dir := t.TempDir()
	quality := writeEval(t, dir, "quality.json", `{"datasets": [], "overall": {}}`)
	measured := writeEval(t, dir, "measured.json", measuredReport)
	summary := filepath.Join(dir, "summary.md")

	// Current without measure: a note, exit 0.
	if code := evalRatiosMain([]string{"--current", quality, "--summary", summary}); code != 0 {
		t.Fatalf("quality-only current exit %d", code)
	}
	// Current with measure, previous without it (the first nightly after
	// --measure lands): the table without previous values, and a note.
	if code := evalRatiosMain([]string{"--current", measured, "--previous", quality, "--summary", summary}); code != 0 {
		t.Fatalf("quality-only previous exit %d", code)
	}
	// Missing previous file: a note, exit 0.
	if code := evalRatiosMain([]string{"--current", measured, "--previous", filepath.Join(dir, "absent.json"), "--summary", summary}); code != 0 {
		t.Fatalf("absent previous exit %d", code)
	}
	data, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	text := string(data)
	for _, want := range []string{
		"no `measure` section (pull request runs",
		"The previous nightly report has no `measure` section.",
		"No previous nightly report to compare.",
	} {
		if !strings.Contains(text, want) {
			t.Fatalf("missing %q in:\n%s", want, text)
		}
	}
	if got := tableRow(t, text, "A"); got[0] != "8.00" {
		t.Fatalf("A without previous = %v", got)
	}

	nullMeasure := writeEval(t, dir, "null.json", `{"measure": null}`)
	if m, err := loadEvalMeasure(nullMeasure); err != nil || m != nil {
		t.Fatalf("null measure = %v, %v", m, err)
	}
}

func TestEvalRatiosMissingValuesPrintNA(t *testing.T) {
	body := `{"measure": {"datasets": [{"name": "solo", "logBytes": 0, "engines": [
	  {"engine": "nesso", "status": "ok", "indexWallSeconds": 3, "modes": [{"mode": "dense", "queryLatencyMedianSeconds": 0.1}]},
	  {"engine": "grep", "status": "ok", "queryLatencyMedianSeconds": 0},
	  {"engine": "bm25", "status": "skipped", "reason": "no fts5"}
	]}]}}`
	path := writeEval(t, t.TempDir(), "solo.json", body)
	measure, err := loadEvalMeasure(path)
	if err != nil {
		t.Fatal(err)
	}
	text := formatEvalRatios("solo", measure, nil, "")
	got := tableRow(t, text, "solo")
	for i, cell := range got {
		if cell != "n/a" {
			t.Fatalf("cell %d = %q, want n/a (row %v)", i, cell, got)
		}
	}
	if strings.Contains(text, "**aggregate**") {
		t.Fatalf("aggregate row for one dataset:\n%s", text)
	}
}

func TestEvalRatiosRejectsMalformedInput(t *testing.T) {
	dir := t.TempDir()
	good := writeEval(t, dir, "good.json", measuredReport)
	cases := map[string]string{
		"not json":          `{"measure":`,
		"measure not obj":   `{"measure": [1, 2]}`,
		"datasets not list": `{"measure": {"datasets": {"name": "A"}}}`,
		"wall not number":   `{"measure": {"datasets": [{"name": "A", "engines": [{"engine": "bm25", "indexWallSeconds": "fast"}]}]}}`,
		"dataset no name":   `{"measure": {"datasets": [{"engines": []}]}}`,
		"duplicate dataset": `{"measure": {"datasets": [{"name": "A"}, {"name": "A"}]}}`,
		"engine no name":    `{"measure": {"datasets": [{"name": "A", "engines": [{"status": "ok"}]}]}}`,
	}
	summary := filepath.Join(dir, "summary.md")
	for name, body := range cases {
		bad := writeEval(t, dir, "bad.json", body)
		if code := evalRatiosMain([]string{"--current", bad, "--summary", summary}); code == 0 {
			t.Fatalf("%s: current accepted", name)
		}
		if code := evalRatiosMain([]string{"--current", good, "--previous", bad, "--summary", summary}); code == 0 {
			t.Fatalf("%s: previous accepted", name)
		}
	}
	if code := evalRatiosMain([]string{"--current", filepath.Join(dir, "absent.json"), "--summary", summary}); code == 0 {
		t.Fatal("missing current accepted")
	}
	if code := evalRatiosMain([]string{"--summary", summary}); code == 0 {
		t.Fatal("no --current accepted")
	}
}
