// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"math"
	"os"
	"sort"
	"strconv"
	"strings"
)

const (
	// threadScaleMinRatio is the minimum wall-time speedup of NESSO_THREADS=4
	// over NESSO_THREADS=1 for one grep of threadScaleLines, on the same runner.
	// The bench job runs that pair on ubuntu-latest (amd64) only.
	//
	// Those runners expose 2 physical cores and 4 logical CPUs, so four int8
	// sessions share two cores. At 4000 lines the measured speedup was 1.45×
	// to 2.13× (median 1.88×). 1.25× stays under the slowest sample. At 1000
	// lines the slowest runner was below 1×: loading four sessions cost more
	// than the shorter embed saved.
	threadScaleMinRatio = 1.25
	threadScaleLines    = 4000
)

type scaleSample struct {
	lines           int
	wall1           float64
	wall4           float64
	rss1            float64
	rss4            float64
	ratio           float64
	linesPerSecond1 float64
	linesPerSecond4 float64
}

func (s scaleSample) meets(min float64) bool {
	if min <= 0 || math.IsNaN(s.ratio) || math.IsInf(s.ratio, 0) {
		return false
	}
	return s.ratio >= min
}

func benchScaleMain(args []string) int {
	fs := flag.NewFlagSet("bench-scale", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	onePath := fs.String("one", "", "bench JSON from NESSO_THREADS=1")
	fourPath := fs.String("four", "", "bench JSON from NESSO_THREADS=4")
	lines := fs.Int("lines", threadScaleLines, "line count both runs must use")
	minRatio := fs.Float64("min-ratio", threadScaleMinRatio, "minimum wall speedup of 4 threads over 1")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci bench-scale --one FILE --four FILE")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	if *onePath == "" || *fourPath == "" || fs.NArg() != 0 {
		fs.Usage()
		return 1
	}
	sample, err := scaleFiles(*onePath, *fourPath, *lines)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-scale: %v\n", err)
		return 1
	}
	fmt.Printf("bench-scale: %s\n", sample.line(*minRatio))
	if !sample.meets(*minRatio) {
		fmt.Fprintf(os.Stderr, "bench-scale: speedup %.2f× is below minimum %.2f×\n", sample.ratio, *minRatio)
		return 1
	}
	return 0
}

func benchSummaryMain(args []string) int {
	fs := flag.NewFlagSet("bench-summary", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	resultPath := fs.String("result", "", "bench JSON for the sized tiers")
	onePath := fs.String("one", "", "bench JSON from NESSO_THREADS=1")
	fourPath := fs.String("four", "", "bench JSON from NESSO_THREADS=4")
	lines := fs.Int("lines", threadScaleLines, "line count the thread pair must use")
	minRatio := fs.Float64("min-ratio", threadScaleMinRatio, "minimum wall speedup printed in the summary")
	summary := fs.String("summary", "", "markdown file to append (default: GITHUB_STEP_SUMMARY)")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci bench-summary --result FILE --one FILE --four FILE")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	if *resultPath == "" || fs.NArg() != 0 {
		fs.Usage()
		return 1
	}
	result, err := loadBenchFile(*resultPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-summary: %v\n", err)
		return 1
	}
	var one, four *benchFile
	if *onePath != "" && *fourPath != "" {
		loadedOne, oneErr := loadBenchFile(*onePath)
		loadedFour, fourErr := loadBenchFile(*fourPath)
		if oneErr != nil || fourErr != nil {
			fmt.Fprintf(os.Stderr, "bench-summary: %v %v\n", oneErr, fourErr)
			return 1
		}
		if _, err := scaleSpeedup(loadedOne, loadedFour, *lines); err != nil {
			fmt.Fprintf(os.Stderr, "bench-summary: %v\n", err)
			return 1
		}
		one = &loadedOne
		four = &loadedFour
	}
	text := formatBenchSummary(result, one, four, *minRatio)
	fmt.Print(text)
	if err := appendSummary(summaryDestination(*summary), text); err != nil {
		fmt.Fprintf(os.Stderr, "bench-summary: %v\n", err)
		return 1
	}
	return 0
}

func scaleFiles(onePath, fourPath string, lines int) (scaleSample, error) {
	one, err := loadBenchFile(onePath)
	if err != nil {
		return scaleSample{}, err
	}
	four, err := loadBenchFile(fourPath)
	if err != nil {
		return scaleSample{}, err
	}
	return scaleSpeedup(one, four, lines)
}

func scaleSpeedup(one, four benchFile, lines int) (scaleSample, error) {
	if err := requireThreads(one, "1"); err != nil {
		return scaleSample{}, fmt.Errorf("1-thread run: %w", err)
	}
	if err := requireThreads(four, "4"); err != nil {
		return scaleSample{}, fmt.Errorf("4-thread run: %w", err)
	}
	left, err := singleCase(one, lines)
	if err != nil {
		return scaleSample{}, fmt.Errorf("1-thread run: %w", err)
	}
	right, err := singleCase(four, lines)
	if err != nil {
		return scaleSample{}, fmt.Errorf("4-thread run: %w", err)
	}
	wall1, rss1, err := completedMeasure(left)
	if err != nil {
		return scaleSample{}, fmt.Errorf("1-thread run: %w", err)
	}
	wall4, rss4, err := completedMeasure(right)
	if err != nil {
		return scaleSample{}, fmt.Errorf("4-thread run: %w", err)
	}
	return scaleSample{
		lines:           lines,
		wall1:           wall1,
		wall4:           wall4,
		rss1:            rss1,
		rss4:            rss4,
		ratio:           wall1 / wall4,
		linesPerSecond1: float64(lines) / wall1,
		linesPerSecond4: float64(lines) / wall4,
	}, nil
}

func requireThreads(file benchFile, want string) error {
	if file.Threads == nil || *file.Threads != want {
		got := "unset"
		if file.Threads != nil {
			got = *file.Threads
		}
		return fmt.Errorf("threads is %s, want %s", got, want)
	}
	return nil
}

func singleCase(file benchFile, lines int) (benchCase, error) {
	if len(file.Cases) != 1 {
		return benchCase{}, fmt.Errorf("want 1 case, have %d", len(file.Cases))
	}
	item := file.Cases[0]
	if item.Lines != lines {
		return benchCase{}, fmt.Errorf("lines %d, want %d", item.Lines, lines)
	}
	return item, nil
}

func completedMeasure(item benchCase) (float64, float64, error) {
	if !item.OK {
		if item.Error != "" {
			return 0, 0, fmt.Errorf("%s", item.Error)
		}
		return 0, 0, fmt.Errorf("did not complete")
	}
	wall, ok := positive(item.WallSeconds)
	if !ok {
		return 0, 0, fmt.Errorf("invalid wall time")
	}
	rss, ok := positive(item.PeakRssBytes)
	if !ok {
		return 0, 0, fmt.Errorf("invalid peak RSS")
	}
	return wall, rss, nil
}

func (s scaleSample) line(min float64) string {
	return fmt.Sprintf(
		"%d lines, 1 thread %.3fs (%.1f lines/s), 4 threads %.3fs (%.1f lines/s), speedup %.2f×, minimum %.2f×",
		s.lines, s.wall1, s.linesPerSecond1, s.wall4, s.linesPerSecond4, s.ratio, min,
	)
}

func formatBenchSummary(result benchFile, one, four *benchFile, minRatio float64) string {
	var b strings.Builder
	b.WriteString("### Benchmark\n\n")
	b.WriteString("| lines | threads | wall (s) | lines/s | peak RSS | 4 vs 1 |\n")
	b.WriteString("| --- | --- | --- | --- | --- | --- |\n")
	cases := append([]benchCase(nil), result.Cases...)
	sort.SliceStable(cases, func(i, j int) bool { return cases[i].Lines < cases[j].Lines })
	for _, item := range cases {
		b.WriteString(summaryRow(item, "", ""))
	}
	var sample scaleSample
	var scaleErr error
	if one != nil && four != nil && len(one.Cases) == 1 {
		sample, scaleErr = scaleSpeedup(*one, *four, one.Cases[0].Lines)
	}
	if one != nil && len(one.Cases) == 1 {
		b.WriteString(summaryRow(one.Cases[0], "1", ""))
	}
	if four != nil && len(four.Cases) == 1 {
		ratio := ""
		if scaleErr == nil {
			ratio = fmt.Sprintf("%.2f×", sample.ratio)
		}
		b.WriteString(summaryRow(four.Cases[0], "4", ratio))
	}
	b.WriteByte('\n')
	switch {
	case one == nil || four == nil:
		b.WriteString("Thread sample was not recorded.\n")
	case scaleErr != nil:
		fmt.Fprintf(&b, "Thread sample: %s.\n", scaleErr)
	default:
		fmt.Fprintf(&b, "4-thread / 1-thread wall speedup %.2f× (minimum %.2f×).\n", sample.ratio, minRatio)
	}
	return b.String()
}

func summaryRow(item benchCase, threads, ratio string) string {
	wall, wallOK := positive(item.WallSeconds)
	rss, rssOK := positive(item.PeakRssBytes)
	if !item.OK || !wallOK || !rssOK || item.Lines <= 0 {
		return fmt.Sprintf("| %d | %s |  |  |  | %s |\n", item.Lines, threads, ratio)
	}
	return fmt.Sprintf(
		"| %d | %s | %.3f | %.1f | %.0f | %s |\n",
		item.Lines,
		threads,
		wall,
		float64(item.Lines)/wall,
		rss,
		ratio,
	)
}

type pinnedBaseline struct {
	SHA   string       `json:"sha,omitempty"`
	Query string       `json:"query,omitempty"`
	Cases []pinnedCase `json:"cases"`
}

type pinnedCase struct {
	Lines        int     `json:"lines"`
	OK           bool    `json:"ok"`
	ExitCode     int     `json:"exitCode"`
	WallSeconds  float64 `json:"wallSeconds"`
	PeakRssBytes float64 `json:"peakRssBytes"`
	SourceSHA    string  `json:"sourceSha,omitempty"`
}

func benchBaselineMain(args []string) int {
	fs := flag.NewFlagSet("bench-baseline", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	output := fs.String("output", "", "baseline JSON to write")
	tiersRaw := fs.String("tiers", "1000,10000,100000", "comma-separated line counts the baseline must contain")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci bench-baseline --output FILE [--tiers 1000,10000,100000] RESULT.json...")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	tiers, err := parseTiers(*tiersRaw)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-baseline: %v\n", err)
		return 1
	}
	if *output == "" || fs.NArg() == 0 {
		fs.Usage()
		return 1
	}
	var files []benchFile
	for _, path := range fs.Args() {
		file, err := loadBenchFile(path)
		if err != nil {
			fmt.Fprintf(os.Stderr, "bench-baseline: %v\n", err)
			return 1
		}
		files = append(files, file)
	}
	pinned, err := slowerBaseline(files, tiers)
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-baseline: %v\n", err)
		return 1
	}
	payload, err := json.MarshalIndent(pinned, "", "  ")
	if err != nil {
		fmt.Fprintf(os.Stderr, "bench-baseline: %v\n", err)
		return 1
	}
	if err := os.WriteFile(*output, append(payload, '\n'), 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "bench-baseline: %v\n", err)
		return 1
	}
	fmt.Printf("bench-baseline: wrote %s (%d tiers)\n", *output, len(pinned.Cases))
	return 0
}

func parseTiers(text string) ([]int, error) {
	var tiers []int
	for _, part := range strings.Split(text, ",") {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		value, err := strconv.Atoi(part)
		if err != nil || value <= 0 {
			return nil, fmt.Errorf("invalid tier %q", part)
		}
		tiers = append(tiers, value)
	}
	if len(tiers) == 0 {
		return nil, fmt.Errorf("no tiers")
	}
	return tiers, nil
}

func slowerBaseline(files []benchFile, tiers []int) (pinnedBaseline, error) {
	type chosen struct {
		item benchCase
		sha  string
	}
	best := map[int]chosen{}
	query := ""
	for _, file := range files {
		if query == "" {
			query = file.Query
		}
		for _, item := range file.Cases {
			wall, rss, err := completedMeasure(item)
			if err != nil {
				continue
			}
			item.WallSeconds = &wall
			item.PeakRssBytes = &rss
			prev, ok := best[item.Lines]
			if ok {
				prevWall, _ := positive(prev.item.WallSeconds)
				if wall <= prevWall {
					continue
				}
			}
			best[item.Lines] = chosen{item: item, sha: file.SHA}
		}
	}
	var missing []int
	for _, lines := range tiers {
		if _, ok := best[lines]; !ok {
			missing = append(missing, lines)
		}
	}
	if len(missing) > 0 {
		return pinnedBaseline{}, fmt.Errorf("no completed case for lines %v", missing)
	}
	shared := true
	sha := ""
	for _, lines := range tiers {
		itemSHA := best[lines].sha
		if sha == "" {
			sha = itemSHA
		}
		if itemSHA == "" || itemSHA != sha {
			shared = false
		}
	}
	pinned := pinnedBaseline{Query: query}
	if shared {
		pinned.SHA = sha
	}
	for _, lines := range tiers {
		item := best[lines]
		wall, _ := positive(item.item.WallSeconds)
		rss, _ := positive(item.item.PeakRssBytes)
		row := pinnedCase{
			Lines:        lines,
			OK:           true,
			ExitCode:     item.item.ExitCode,
			WallSeconds:  math.Round(wall*1e6) / 1e6,
			PeakRssBytes: rss,
		}
		if !shared && item.sha != "" {
			row.SourceSHA = item.sha
		}
		pinned.Cases = append(pinned.Cases, row)
	}
	return pinned, nil
}

func loadBenchFile(path string) (benchFile, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return benchFile{}, err
	}
	var payload benchFile
	if err := json.Unmarshal(raw, &payload); err != nil {
		return benchFile{}, fmt.Errorf("cannot read %s: %w", path, err)
	}
	return payload, nil
}
