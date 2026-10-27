// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeBench(t *testing.T, dir, name, body string) string {
	t.Helper()
	path := filepath.Join(dir, name)
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestScaleSpeedupPassesAtTheMinimumAndFailsBelowIt(t *testing.T) {
	one := benchFile{Threads: strPtr("1"), Cases: []benchCase{okCase(1000, 3, 100)}}
	four := benchFile{Threads: strPtr("4"), Cases: []benchCase{okCase(1000, 2, 120)}}
	got, err := scaleSpeedup(one, four, 1000)
	if err != nil {
		t.Fatal(err)
	}
	if got.ratio != 1.5 || got.linesPerSecond1 != 1000/3.0 || got.linesPerSecond4 != 500 {
		t.Fatalf("speedup = %#v", got)
	}
	if !got.meets(1.5) || got.meets(1.5+1e-9) {
		t.Fatalf("boundary = %#v", got)
	}
	below, err := scaleSpeedup(
		benchFile{Threads: strPtr("1"), Cases: []benchCase{okCase(1000, 2.9, 100)}},
		four,
		1000,
	)
	if err != nil {
		t.Fatal(err)
	}
	if below.meets(1.5) {
		t.Fatalf("below minimum passed: %#v", below)
	}
}

func TestScaleSpeedupRejectsABadSample(t *testing.T) {
	good := benchFile{Threads: strPtr("4"), Cases: []benchCase{okCase(1000, 2, 120)}}
	cases := []struct {
		name string
		one  benchFile
	}{
		{"wrong threads", benchFile{Threads: strPtr("2"), Cases: []benchCase{okCase(1000, 4, 100)}}},
		{"missing threads", benchFile{Cases: []benchCase{okCase(1000, 4, 100)}}},
		{"failed", benchFile{Threads: strPtr("1"), Cases: []benchCase{{Lines: 1000, OK: false}}}},
		{"two cases", benchFile{Threads: strPtr("1"), Cases: []benchCase{okCase(1000, 4, 100), okCase(2000, 8, 100)}}},
		{"wrong size", benchFile{Threads: strPtr("1"), Cases: []benchCase{okCase(2000, 4, 100)}}},
		{"zero wall", benchFile{Threads: strPtr("1"), Cases: []benchCase{okCase(1000, 0, 100)}}},
	}
	for _, tc := range cases {
		if _, err := scaleSpeedup(tc.one, good, 1000); err == nil {
			t.Fatalf("%s accepted", tc.name)
		}
	}
}

func TestBenchScaleCommand(t *testing.T) {
	dir := t.TempDir()
	one := writeBench(t, dir, "one.json", `{
	  "threads": "1",
	  "cases": [{"lines": 1000, "ok": true, "wallSeconds": 4, "peakRssBytes": 100}]
	}`)
	four := writeBench(t, dir, "four.json", `{
	  "threads": "4",
	  "cases": [{"lines": 1000, "ok": true, "wallSeconds": 2, "peakRssBytes": 120}]
	}`)
	if code := benchScaleMain([]string{"--one", one, "--four", four, "--lines", "1000", "--min-ratio", "1.5"}); code != 0 {
		t.Fatalf("pass exit %d", code)
	}
	if code := benchScaleMain([]string{"--one", one, "--four", four, "--lines", "1000", "--min-ratio", "2.1"}); code == 0 {
		t.Fatal("slow 4-thread run passed")
	}
}

func TestBenchSummaryTable(t *testing.T) {
	result := benchFile{Cases: []benchCase{
		okCase(10000, 10, 200),
		okCase(1000, 2.5, 150),
	}}
	one := benchFile{Threads: strPtr("1"), Cases: []benchCase{okCase(1000, 4, 100)}}
	four := benchFile{Threads: strPtr("4"), Cases: []benchCase{okCase(1000, 2, 120)}}
	text := formatBenchSummary(result, &one, &four, 1.5)
	if !strings.Contains(text, "| lines | threads | wall (s) | lines/s | peak RSS | 4 vs 1 |") {
		t.Fatalf("header missing\n%s", text)
	}
	if !strings.Contains(text, "| 1000 |  | 2.500 | 400.0 | 150 |  |") {
		t.Fatalf("default tier missing\n%s", text)
	}
	if !strings.Contains(text, "| 10000 |  | 10.000 | 1000.0 | 200 |  |") {
		t.Fatalf("10k tier missing\n%s", text)
	}
	if !strings.Contains(text, "| 1000 | 1 | 4.000 | 250.0 | 100 |  |") {
		t.Fatalf("1-thread row missing\n%s", text)
	}
	if !strings.Contains(text, "| 1000 | 4 | 2.000 | 500.0 | 120 | 2.00× |") {
		t.Fatalf("4-thread row missing\n%s", text)
	}
	if !strings.Contains(text, "minimum 1.50×") {
		t.Fatalf("minimum missing\n%s", text)
	}
	dir := t.TempDir()
	summary := filepath.Join(dir, "summary.md")
	code := benchSummaryMain([]string{
		"--result", writeBench(t, dir, "ci.json", `{"cases":[{"lines":1000,"ok":true,"wallSeconds":2.5,"peakRssBytes":150}]}`),
		"--one", writeBench(t, dir, "one.json", `{"threads":"1","cases":[{"lines":1000,"ok":true,"wallSeconds":4,"peakRssBytes":100}]}`),
		"--four", writeBench(t, dir, "four.json", `{"threads":"4","cases":[{"lines":1000,"ok":true,"wallSeconds":2,"peakRssBytes":120}]}`),
		"--lines", "1000",
		"--min-ratio", "1.5",
		"--summary", summary,
	})
	if code != 0 {
		t.Fatalf("exit %d", code)
	}
	written, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(written), "| 1000 | 4 | 2.000 | 500.0 | 120 | 2.00× |") {
		t.Fatalf("summary = %s", written)
	}
}

func TestBenchBaselineKeepsTheSlowerWall(t *testing.T) {
	dir := t.TempDir()
	fast := writeBench(t, dir, "fast.json", `{
	  "sha": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
	  "query": "database connection refused",
	  "cases": [
	    {"lines": 1000, "ok": true, "exitCode": 0, "wallSeconds": 0.9, "peakRssBytes": 10, "trace": {"peakRssBytes": 1}},
	    {"lines": 10000, "ok": true, "exitCode": 0, "wallSeconds": 7, "peakRssBytes": 99},
	    {"lines": 100000, "ok": true, "exitCode": 0, "wallSeconds": 70, "peakRssBytes": 50}
	  ]
	}`)
	slow := writeBench(t, dir, "slow.json", `{
	  "sha": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
	  "query": "database connection refused",
	  "cases": [
	    {"lines": 1000, "ok": true, "exitCode": 0, "wallSeconds": 2.5, "peakRssBytes": 30},
	    {"lines": 10000, "ok": true, "exitCode": 0, "wallSeconds": 14, "peakRssBytes": 40},
	    {"lines": 100000, "ok": false, "exitCode": 1, "wallSeconds": 200, "peakRssBytes": 80, "error": "timed out"}
	  ]
	}`)
	out := filepath.Join(dir, "baseline.json")
	if code := benchBaselineMain([]string{"--output", out, "--tiers", "1000,10000,100000", slow}); code == 0 {
		t.Fatal("missing completed 100k passed")
	}
	slow100k := writeBench(t, dir, "slow100k.json", `{
	  "sha": "cccccccccccccccccccccccccccccccccccccccc",
	  "cases": [
	    {"lines": 100000, "ok": true, "exitCode": 0, "wallSeconds": 130, "peakRssBytes": 60}
	  ]
	}`)
	if code := benchBaselineMain([]string{"--output", out, "--tiers", "1000,10000,100000", fast, slow, slow100k}); code != 0 {
		t.Fatalf("exit %d", code)
	}
	text, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	body := string(text)
	if strings.Contains(body, "trace") || strings.Contains(body, `"sha":`) {
		t.Fatalf("mixed sources should not keep one sha or a trace\n%s", body)
	}
	if !strings.Contains(body, `"wallSeconds": 2.5`) || !strings.Contains(body, `"peakRssBytes": 30`) {
		t.Fatalf("1k should be the slow sample\n%s", body)
	}
	if !strings.Contains(body, `"wallSeconds": 14`) || strings.Contains(body, `"peakRssBytes": 99`) {
		t.Fatalf("10k rss must come from the slower run\n%s", body)
	}
	if !strings.Contains(body, `"wallSeconds": 130`) || !strings.Contains(body, `"sourceSha": "cccccccccccccccccccccccccccccccccccccccc"`) {
		t.Fatalf("100k source missing\n%s", body)
	}
	if !strings.Contains(body, `"query": "database connection refused"`) {
		t.Fatalf("query missing\n%s", body)
	}
	single := filepath.Join(dir, "single.json")
	if code := benchBaselineMain([]string{"--output", single, "--tiers", "1000,10000,100000", fast}); code != 0 {
		t.Fatalf("single exit %d", code)
	}
	singleBody, err := os.ReadFile(single)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(singleBody), `"sha": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"`) {
		t.Fatalf("single source sha missing\n%s", singleBody)
	}
	if strings.Contains(string(singleBody), "sourceSha") {
		t.Fatalf("single source should not repeat sourceSha\n%s", singleBody)
	}
}

func strPtr(value string) *string { return &value }

func okCase(lines int, wall, rss float64) benchCase {
	return benchCase{Lines: lines, OK: true, ExitCode: 0, WallSeconds: &wall, PeakRssBytes: &rss}
}
