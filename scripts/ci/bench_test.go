// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestExternalEntriesSkipsFailedTiers(t *testing.T) {
	wall := 0.5
	rss := 80_000_000.0
	failedWall := 9.0
	entries := externalEntries(benchFile{Cases: []benchCase{
		{Lines: 10000, OK: true, WallSeconds: &wall, PeakRssBytes: &rss},
		{Lines: 1000, OK: false, WallSeconds: &failedWall, PeakRssBytes: &rss},
		{Lines: 100000, OK: true, WallSeconds: &wall},
	}})
	if len(entries) != 3 {
		t.Fatalf("entries = %#v", entries)
	}
	if entries[0].Name != "grep-10000-wall" || entries[0].Unit != "seconds" || entries[0].Value != wall {
		t.Fatalf("first = %#v", entries[0])
	}
	if entries[1].Name != "grep-10000-rss" || entries[1].Unit != "bytes" {
		t.Fatalf("rss = %#v", entries[1])
	}
	if entries[2].Name != "grep-100000-wall" {
		t.Fatalf("partial tier = %#v", entries[2])
	}
}

func TestBenchExternalWritesJSON(t *testing.T) {
	dir := t.TempDir()
	result := filepath.Join(dir, "ci.json")
	output := filepath.Join(dir, "external.json")
	body := `{
	  "cases": [
	    {"lines": 1000, "ok": true, "wallSeconds": 0.5, "peakRssBytes": 80000000},
	    {"lines": 10000, "ok": false, "wallSeconds": 3, "peakRssBytes": 90000000, "error": "timed out"}
	  ]
	}`
	if err := os.WriteFile(result, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	if code := benchExternalMain([]string{"--result", result, "--output", output}); code != 0 {
		t.Fatalf("exit %d", code)
	}
	written, err := os.ReadFile(output)
	if err != nil {
		t.Fatal(err)
	}
	text := string(written)
	if !strings.Contains(text, `"name": "grep-1000-wall"`) || strings.Contains(text, "grep-10000") {
		t.Fatalf("output = %s", text)
	}
}

func TestHistoryFlagsMedianAndKeepsTheGateInformational(t *testing.T) {
	raw := []byte(`window.BENCHMARK_DATA = {
	  "entries": {
	    "nesso": [
	      {"commit": {"id": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}, "date": 1, "benches": [
	        {"name": "grep-1000-wall", "unit": "seconds", "value": 1},
	        {"name": "grep-1000-rss", "unit": "bytes", "value": 100}
	      ]},
	      {"commit": {"id": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}, "date": 2, "benches": [
	        {"name": "grep-1000-wall", "unit": "seconds", "value": 1},
	        {"name": "grep-1000-rss", "unit": "bytes", "value": 100}
	      ]},
	      {"commit": {"id": "cccccccccccccccccccccccccccccccccccccccc"}, "date": 3, "benches": [
	        {"name": "grep-1000-wall", "unit": "seconds", "value": 1},
	        {"name": "grep-1000-rss", "unit": "bytes", "value": 110}
	      ]}
	    ]
	  }
	};`)
	opts := historyOptions{wallTolerance: 4.5, rssTolerance: 0.20, window: 20, minSamples: 3}
	text, err := formatHistory([]externalBench{
		{Name: "grep-1000-wall", Unit: "seconds", Value: 5.5},
		{Name: "grep-1000-rss", Unit: "bytes", Value: 121},
		{Name: "grep-10000-wall", Unit: "seconds", Value: 4},
	}, raw, opts)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(text, "| `grep-1000-wall` | 5.500 | 1.000 (ccccccc) | +450.0% | 1.000 (n=3) | +450.0% |  |") {
		t.Fatalf("wall row missing in\n%s", text)
	}
	if !strings.Contains(text, "| `grep-1000-rss` | 121 | 110 (ccccccc) | +10.0% | 100 (n=3) | +21.0% | flag |") {
		t.Fatalf("rss flag missing in\n%s", text)
	}
	if !strings.Contains(text, "| `grep-10000-wall` | 4.000 | | | | | no history |") {
		t.Fatalf("missing tier row missing in\n%s", text)
	}
	if !strings.Contains(text, "does not fail the job") || !strings.Contains(text, "wall 5.50×, rss 1.20×") {
		t.Fatalf("gate note missing in\n%s", text)
	}
}

func TestHistoryFewSamplesDoesNotFlag(t *testing.T) {
	raw := []byte(`{"entries":{"nesso":[
	  {"commit":{"id":"abc1234"},"date":2,"benches":[{"name":"grep-1000-wall","unit":"seconds","value":1}]},
	  {"commit":{"id":"abc1235"},"date":1,"benches":[{"name":"grep-1000-wall","unit":"seconds","value":1}]}
	]}}`)
	opts := historyOptions{wallTolerance: 0.1, rssTolerance: 0.1, window: 20, minSamples: 3}
	text, err := formatHistory([]externalBench{
		{Name: "grep-1000-wall", Unit: "seconds", Value: 10},
	}, raw, opts)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(text, "| few samples |") || strings.Contains(text, "| flag |") {
		t.Fatalf("sample gate = %s", text)
	}
	if !strings.Contains(text, "1.000 (abc1234)") {
		t.Fatalf("newest point not last: %s", text)
	}
}

func TestHistoryWindowUsesNewestPoints(t *testing.T) {
	raw := []byte(`{"entries":{"nesso":[
	  {"date":1,"benches":[{"name":"grep-1000-rss","value":1}]},
	  {"date":2,"benches":[{"name":"grep-1000-rss","value":10}]},
	  {"date":3,"benches":[{"name":"grep-1000-rss","value":10}]},
	  {"date":4,"benches":[{"name":"grep-1000-rss","value":10}]}
	]}}`)
	opts := historyOptions{wallTolerance: 4.5, rssTolerance: 0.2, window: 3, minSamples: 3}
	text, err := formatHistory([]externalBench{
		{Name: "grep-1000-rss", Unit: "bytes", Value: 10},
	}, raw, opts)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(text, "10 (n=3)") || strings.Contains(text, "| flag |") {
		t.Fatalf("window = %s", text)
	}
}

func TestMissingHistoryFileIsNotAFailureMessage(t *testing.T) {
	text := missingHistory(historyOptions{pagesURL: "https://lorenzocaprari.github.io/nesso/dev/bench/"})
	if !strings.Contains(text, "No benchmark history") || !strings.Contains(text, "does not fail the job") {
		t.Fatalf("missing = %s", text)
	}
	if !strings.Contains(text, "https://lorenzocaprari.github.io/nesso/dev/bench/") {
		t.Fatalf("chart = %s", text)
	}
}

func TestBenchHistoryCommandAppendsSummary(t *testing.T) {
	dir := t.TempDir()
	result := filepath.Join(dir, "ci.json")
	summary := filepath.Join(dir, "summary.md")
	if err := os.WriteFile(result, []byte(`{"cases":[{"lines":1000,"ok":true,"wallSeconds":0.4,"peakRssBytes":10}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	code := benchHistoryMain([]string{"--result", result, "--summary", summary, "--wall-tolerance", "4.5", "--rss-tolerance", "0.2"})
	if code != 0 {
		t.Fatalf("exit %d", code)
	}
	text, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(text), "No benchmark history") {
		t.Fatalf("summary = %s", text)
	}
}

func TestFetchHistoryStatuses(t *testing.T) {
	tokenSeen := ""
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		tokenSeen = r.Header.Get("Authorization")
		if r.Header.Get("Accept") != "application/vnd.github.raw" {
			t.Errorf("accept = %s", r.Header.Get("Accept"))
		}
		switch r.URL.Path {
		case "/missing":
			http.NotFound(w, r)
		case "/down":
			http.Error(w, "nope", http.StatusBadGateway)
		default:
			_, _ = w.Write([]byte("window.BENCHMARK_DATA = {\"entries\":{}}"))
		}
	}))
	defer server.Close()
	client := server.Client()
	body, status, err := fetchHistory(context.Background(), server.URL+"/data.js", "secret-token", client)
	if err != nil || status != http.StatusOK || !strings.Contains(string(body), "BENCHMARK_DATA") {
		t.Fatalf("ok status=%d err=%v body=%s", status, err, body)
	}
	if tokenSeen != "Bearer secret-token" {
		t.Fatalf("authorization = %q", tokenSeen)
	}
	_, status, err = fetchHistory(context.Background(), server.URL+"/missing", "", client)
	if err != nil || status != http.StatusNotFound {
		t.Fatalf("missing status=%d err=%v", status, err)
	}
	_, status, err = fetchHistory(context.Background(), server.URL+"/down", "", client)
	if err != nil || status != http.StatusBadGateway {
		t.Fatalf("down status=%d err=%v", status, err)
	}
}

func TestGitHubRedirectRejectsOtherHosts(t *testing.T) {
	req, err := http.NewRequest(http.MethodGet, "https://evil.example/data.js", nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := githubRedirect(req, nil); err == nil {
		t.Fatal("expected refusal")
	}
	allowed, err := http.NewRequest(http.MethodGet, "https://raw.githubusercontent.com/o/r/data.js", nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := githubRedirect(allowed, nil); err != nil {
		t.Fatal(err)
	}
}

func TestHistoryURLRejectsOddRepos(t *testing.T) {
	if _, err := historyURL("lorenzocaprari/nesso"); err != nil {
		t.Fatal(err)
	}
	for _, repo := range []string{"", "nesso", "owner/name/extra", "owner/name?ref=main", "a/../b"} {
		if _, err := historyURL(repo); err == nil {
			t.Fatalf("accepted %q", repo)
		}
	}
}

func TestOverGateUsesTheSameLimitAsBenchCompare(t *testing.T) {
	opts := historyOptions{wallTolerance: 4.5, rssTolerance: 0.20}
	if overGate("grep-1000-wall", 5.5, 1, opts) {
		t.Fatal("wall equal to the limit must not flag")
	}
	if !overGate("grep-1000-wall", 5.51, 1, opts) {
		t.Fatal("wall above the limit must flag")
	}
	if overGate("grep-1000-rss", 120, 100, opts) {
		t.Fatal("rss equal to the limit must not flag")
	}
	if !overGate("grep-1000-rss", 120.1, 100, opts) {
		t.Fatal("rss above the limit must flag")
	}
}

func TestPagesURLLowercasesOwner(t *testing.T) {
	if got := pagesURL("LorenzoCaprari/nesso"); got != "https://lorenzocaprari.github.io/nesso/dev/bench/" {
		t.Fatalf("pages = %s", got)
	}
}
