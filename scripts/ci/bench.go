// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"math"
	"net/http"
	"os"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

const (
	benchmarkDataPath = "dev/bench/data.js"
	historyPrefix     = "window.BENCHMARK_DATA ="
	historyWindow     = 20
	historyMinSamples = 3
	maxHistoryBytes   = 8 << 20
	defaultTolerance  = 0.15
)

var repoPattern = regexp.MustCompile(`^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$`)

type benchFile struct {
	Cases []benchCase `json:"cases"`
}

type benchCase struct {
	Lines        int      `json:"lines"`
	OK           bool     `json:"ok"`
	WallSeconds  *float64 `json:"wallSeconds"`
	PeakRssBytes *float64 `json:"peakRssBytes"`
}

type externalBench struct {
	Name  string  `json:"name"`
	Unit  string  `json:"unit"`
	Value float64 `json:"value"`
}

type historyFile struct {
	Entries map[string][]historyRun `json:"entries"`
}

type historyRun struct {
	Commit struct {
		ID string `json:"id"`
	} `json:"commit"`
	Date    int64           `json:"date"`
	Benches []externalBench `json:"benches"`
}

type historyPoint struct {
	date   int64
	commit string
	value  float64
}

type historyOptions struct {
	wallTolerance float64
	rssTolerance  float64
	window        int
	minSamples    int
	pagesURL      string
}

func benchExternalMain(args []string) int {
	fs := flag.NewFlagSet("bench-external", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	result := fs.String("result", "", "bench result JSON")
	output := fs.String("output", "", "customSmallerIsBetter JSON to write")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci bench-external --result FILE --output FILE")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	if *result == "" || *output == "" {
		fs.Usage()
		return 1
	}
	entries, err := externalFromFile(*result)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-external: %v\n", err)
		return 1
	}
	if len(entries) == 0 {
		fmt.Fprintln(os.Stderr, "bench-external: no completed tiers")
		return 0
	}
	payload, err := json.MarshalIndent(entries, "", "  ")
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-external: %v\n", err)
		return 1
	}
	if err := os.WriteFile(*output, append(payload, '\n'), 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "bench-external: %v\n", err)
		return 1
	}
	return 0
}

func benchHistoryMain(args []string) int {
	fs := flag.NewFlagSet("bench-history", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	result := fs.String("result", "", "bench result JSON")
	historyFileFlag := fs.String("history-file", "", "data.js or history JSON (skips the network)")
	repo := fs.String("repo", "", "owner/name whose gh-pages history is read")
	summary := fs.String("summary", "", "markdown file to append (default: GITHUB_STEP_SUMMARY)")
	wallRaw := fs.String("wall-tolerance", "", "fractional wall increase allowed before a flag")
	rssRaw := fs.String("rss-tolerance", "", "fractional RSS increase allowed before a flag")
	window := fs.Int("window", historyWindow, "recent main points in the median")
	minSamples := fs.Int("min-samples", historyMinSamples, "points required before a flag")
	pages := fs.String("pages-url", "", "chart URL (default: derived from --repo)")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci bench-history --result FILE [--history-file FILE | --repo OWNER/NAME]")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	if *result == "" {
		fs.Usage()
		return 1
	}
	opts, err := resolveHistoryOptions(*wallRaw, *rssRaw, *window, *minSamples)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-history: %v\n", err)
		return 1
	}
	if *pages != "" {
		opts.pagesURL = *pages
	} else if *repo != "" {
		opts.pagesURL = pagesURL(*repo)
	}
	current, err := externalFromFile(*result)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-history: %v\n", err)
		return 1
	}
	text, err := historyReport(current, *historyFileFlag, *repo, opts)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-history: %v\n", err)
		return 1
	}
	fmt.Print(text)
	if err := appendSummary(summaryDestination(*summary), text); err != nil {
		fmt.Fprintf(os.Stderr, "bench-history: %v\n", err)
		return 1
	}
	return 0
}

func resolveHistoryOptions(wallRaw, rssRaw string, window, minSamples int) (historyOptions, error) {
	wall, err := resolveOneTolerance(wallRaw, "NESSO_BENCH_WALL_TOLERANCE")
	if err != nil {
		return historyOptions{}, err
	}
	rss, err := resolveOneTolerance(rssRaw, "NESSO_BENCH_RSS_TOLERANCE")
	if err != nil {
		return historyOptions{}, err
	}
	if window < 1 {
		return historyOptions{}, fmt.Errorf("window must be >= 1")
	}
	if minSamples < 1 {
		return historyOptions{}, fmt.Errorf("min-samples must be >= 1")
	}
	return historyOptions{
		wallTolerance: wall,
		rssTolerance:  rss,
		window:        window,
		minSamples:    minSamples,
	}, nil
}

func resolveOneTolerance(raw, envName string) (float64, error) {
	if raw == "" {
		raw = os.Getenv(envName)
	}
	if raw == "" {
		return defaultTolerance, nil
	}
	value, err := strconv.ParseFloat(raw, 64)
	if err != nil {
		return 0, fmt.Errorf("invalid %s: %w", envName, err)
	}
	if value < 0 || math.IsNaN(value) || math.IsInf(value, 0) {
		return 0, fmt.Errorf("invalid %s: %s", envName, raw)
	}
	return value, nil
}

func externalFromFile(path string) ([]externalBench, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var payload benchFile
	if err := json.Unmarshal(raw, &payload); err != nil {
		return nil, fmt.Errorf("cannot read %s: %w", path, err)
	}
	return externalEntries(payload), nil
}

func externalEntries(payload benchFile) []externalBench {
	cases := append([]benchCase(nil), payload.Cases...)
	sort.SliceStable(cases, func(i, j int) bool {
		return cases[i].Lines < cases[j].Lines
	})
	entries := make([]externalBench, 0, len(cases)*2)
	for _, item := range cases {
		if !item.OK || item.Lines <= 0 {
			continue
		}
		if value, ok := positive(item.WallSeconds); ok {
			entries = append(entries, externalBench{
				Name:  fmt.Sprintf("grep-%d-wall", item.Lines),
				Unit:  "seconds",
				Value: value,
			})
		}
		if value, ok := positive(item.PeakRssBytes); ok {
			entries = append(entries, externalBench{
				Name:  fmt.Sprintf("grep-%d-rss", item.Lines),
				Unit:  "bytes",
				Value: value,
			})
		}
	}
	return entries
}

func positive(value *float64) (float64, bool) {
	if value == nil || math.IsNaN(*value) || math.IsInf(*value, 0) || *value <= 0 {
		return 0, false
	}
	return *value, true
}

func historyReport(current []externalBench, historyPath, repo string, opts historyOptions) (string, error) {
	if historyPath != "" {
		raw, err := os.ReadFile(historyPath)
		if err != nil {
			return "", err
		}
		return formatHistory(current, raw, opts)
	}
	if repo == "" {
		return missingHistory(opts), nil
	}
	url, err := historyURL(repo)
	if err != nil {
		return "", err
	}
	body, status, err := fetchHistory(context.Background(), url, os.Getenv("GITHUB_TOKEN"), historyClient())
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-history: %v\n", err)
		return unavailableHistory(opts, "request failed"), nil
	}
	if status == http.StatusNotFound {
		return missingHistory(opts), nil
	}
	if status != http.StatusOK {
		fmt.Fprintf(os.Stderr, "bench-history: history HTTP %d\n", status)
		return unavailableHistory(opts, fmt.Sprintf("HTTP %d", status)), nil
	}
	text, err := formatHistory(current, body, opts)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-history: %v\n", err)
		return unavailableHistory(opts, "unreadable history"), nil
	}
	return text, nil
}

func missingHistory(opts historyOptions) string {
	return historyPreamble(
		"No benchmark history on the `gh-pages` branch yet. A push to `main` records it.",
		opts,
	)
}

func unavailableHistory(opts historyOptions, reason string) string {
	return historyPreamble(
		fmt.Sprintf("Benchmark history could not be read (%s).", reason),
		opts,
	)
}

func historyPreamble(lead string, opts historyOptions) string {
	var b strings.Builder
	b.WriteString("### Benchmark history\n\n")
	b.WriteString(lead)
	b.WriteString(" This does not fail the job. The merge gate is the baseline comparison above.\n")
	writeChart(&b, opts.pagesURL)
	return b.String()
}

func formatHistory(current []externalBench, raw []byte, opts historyOptions) (string, error) {
	series, err := seriesByName(raw)
	if err != nil {
		return "", err
	}
	var b strings.Builder
	b.WriteString("### Benchmark history\n\n")
	b.WriteString("Informational. This does not fail the job. The merge gate is the baseline comparison above.\n\n")
	if len(current) == 0 {
		b.WriteString("No completed tiers in this run.\n")
		writeChart(&b, opts.pagesURL)
		return b.String(), nil
	}
	b.WriteString("| metric | this run | last main | vs last | median | vs median | note |\n")
	b.WriteString("| --- | --- | --- | --- | --- | --- | --- |\n")
	for _, metric := range current {
		b.WriteString(historyRow(metric, series[metric.Name], opts))
	}
	fmt.Fprintf(
		&b,
		"\nA `flag` means this run is above the median of the last %d recorded points by more than the bench gate (wall %.2f×, rss %.2f×). Fewer than %d points does not flag. `scripts/bench-compare` remains the gate against the latest baseline.\n",
		opts.window,
		1+opts.wallTolerance,
		1+opts.rssTolerance,
		opts.minSamples,
	)
	writeChart(&b, opts.pagesURL)
	return b.String(), nil
}

func historyRow(metric externalBench, points []historyPoint, opts historyOptions) string {
	window := recentPoints(points, opts.window)
	thisRun := formatMeasure(metric.Name, metric.Value)
	if len(window) == 0 {
		return fmt.Sprintf("| `%s` | %s | | | | | no history |\n", metric.Name, thisRun)
	}
	last := window[len(window)-1]
	mid := median(valuesOf(window))
	note := ""
	if len(window) < opts.minSamples {
		note = "few samples"
	} else if overGate(metric.Name, metric.Value, mid, opts) {
		note = "flag"
	}
	return fmt.Sprintf(
		"| `%s` | %s | %s | %s | %s | %s | %s |\n",
		metric.Name,
		thisRun,
		formatSample(metric.Name, last),
		formatDelta(metric.Value, last.value),
		fmt.Sprintf("%s (n=%d)", formatMeasure(metric.Name, mid), len(window)),
		formatDelta(metric.Value, mid),
		note,
	)
}

func writeChart(b *strings.Builder, pages string) {
	if pages == "" {
		return
	}
	fmt.Fprintf(b, "\nChart on the `gh-pages` branch: %s\n", pages)
}

func recentPoints(points []historyPoint, window int) []historyPoint {
	if len(points) == 0 {
		return nil
	}
	sorted := append([]historyPoint(nil), points...)
	sort.SliceStable(sorted, func(i, j int) bool {
		return sorted[i].date < sorted[j].date
	})
	if len(sorted) > window {
		sorted = sorted[len(sorted)-window:]
	}
	return sorted
}

func valuesOf(points []historyPoint) []float64 {
	values := make([]float64, len(points))
	for i, point := range points {
		values[i] = point.value
	}
	return values
}

func median(values []float64) float64 {
	sorted := append([]float64(nil), values...)
	sort.Float64s(sorted)
	mid := len(sorted) / 2
	if len(sorted)%2 == 1 {
		return sorted[mid]
	}
	return (sorted[mid-1] + sorted[mid]) / 2
}

func overGate(name string, current, baseline float64, opts historyOptions) bool {
	if baseline <= 0 {
		return false
	}
	tolerance := opts.wallTolerance
	switch {
	case strings.HasSuffix(name, "-rss"):
		tolerance = opts.rssTolerance
	case strings.HasSuffix(name, "-wall"):
	default:
		return false
	}
	return current > baseline*(1+tolerance)
}

func formatSample(name string, point historyPoint) string {
	text := formatMeasure(name, point.value)
	if sha := shortSHA(point.commit); sha != "" {
		return text + " (" + sha + ")"
	}
	return text
}

func formatMeasure(name string, value float64) string {
	if strings.HasSuffix(name, "-rss") {
		return strconv.FormatFloat(value, 'f', 0, 64)
	}
	return strconv.FormatFloat(value, 'f', 3, 64)
}

func formatDelta(current, baseline float64) string {
	if baseline <= 0 || math.IsNaN(current) || math.IsNaN(baseline) {
		return ""
	}
	return fmt.Sprintf("%+.1f%%", (current-baseline)/baseline*100)
}

func shortSHA(id string) string {
	id = strings.TrimSpace(id)
	if id == "" {
		return ""
	}
	if len(id) > 7 {
		return id[:7]
	}
	return id
}

func seriesByName(raw []byte) (map[string][]historyPoint, error) {
	parsed, err := parseHistory(raw)
	if err != nil {
		return nil, err
	}
	series := make(map[string][]historyPoint)
	for _, runs := range parsed.Entries {
		for _, run := range runs {
			seen := make(map[string]struct{}, len(run.Benches))
			for _, bench := range run.Benches {
				if _, ok := seen[bench.Name]; ok {
					continue
				}
				if bench.Name == "" || math.IsNaN(bench.Value) || math.IsInf(bench.Value, 0) || bench.Value <= 0 {
					continue
				}
				seen[bench.Name] = struct{}{}
				series[bench.Name] = append(series[bench.Name], historyPoint{
					date:   run.Date,
					commit: run.Commit.ID,
					value:  bench.Value,
				})
			}
		}
	}
	return series, nil
}

func parseHistory(raw []byte) (historyFile, error) {
	text := bytes.TrimSpace(raw)
	if bytes.HasPrefix(text, []byte(historyPrefix)) {
		text = bytes.TrimSpace(text[len(historyPrefix):])
	}
	text = bytes.TrimSuffix(text, []byte(";"))
	text = bytes.TrimSpace(text)
	var parsed historyFile
	if err := json.Unmarshal(text, &parsed); err != nil {
		return historyFile{}, fmt.Errorf("cannot parse benchmark history: %w", err)
	}
	if parsed.Entries == nil {
		parsed.Entries = map[string][]historyRun{}
	}
	return parsed, nil
}

func pagesURL(repo string) string {
	owner, name, ok := strings.Cut(repo, "/")
	if !ok || owner == "" || name == "" {
		return ""
	}
	return "https://github.com/" + strings.ToLower(owner) + "/" + name + "/blob/gh-pages/dev/bench/index.html"
}

func historyURL(repo string) (string, error) {
	if !repoPattern.MatchString(repo) {
		return "", fmt.Errorf("invalid repository %q", repo)
	}
	return "https://api.github.com/repos/" + repo + "/contents/" + benchmarkDataPath + "?ref=gh-pages", nil
}

func historyClient() *http.Client {
	return &http.Client{
		Timeout:       20 * time.Second,
		CheckRedirect: githubRedirect,
	}
}

// Refuse redirects off GitHub so a read token is not sent to another host.
func githubRedirect(req *http.Request, via []*http.Request) error {
	switch req.URL.Host {
	case "api.github.com", "github.com", "raw.githubusercontent.com", "objects.githubusercontent.com":
	default:
		return fmt.Errorf("refusing redirect to %s", req.URL.Host)
	}
	if len(via) >= 5 {
		return errors.New("too many redirects")
	}
	return nil
}

func fetchHistory(ctx context.Context, url, token string, client *http.Client) ([]byte, int, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, 0, err
	}
	req.Header.Set("Accept", "application/vnd.github.raw")
	req.Header.Set("X-GitHub-Api-Version", "2022-11-28")
	req.Header.Set("User-Agent", "nesso-ci")
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, 0, err
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(io.LimitReader(resp.Body, maxHistoryBytes+1))
	if err != nil {
		return nil, resp.StatusCode, err
	}
	if len(body) > maxHistoryBytes {
		return nil, resp.StatusCode, fmt.Errorf("history response exceeds %d bytes", maxHistoryBytes)
	}
	return body, resp.StatusCode, nil
}
