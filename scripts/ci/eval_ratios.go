// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io/fs"
	"math"
	"os"
	"sort"
	"strings"
)

// evalRatiosLabel heads every table eval-ratios writes. Hosted-runner timings
// are noisy and the runner hardware changes between nights, so only ratios
// between engines timed on the same runner in the same job are shown, and
// they are never published or used as a gate.
const evalRatiosLabel = "development-only: same-runner ratios, not published results"

// evalReport is the part of a nesso-bench eval-retrieval report eval-ratios
// reads: the optional top-level measure object written by --measure.
type evalReport struct {
	Measure json.RawMessage `json:"measure"`
}

type evalMeasure struct {
	CPUModel string               `json:"cpuModel"`
	Nproc    *int                 `json:"nproc"`
	Repeat   *int                 `json:"repeat"`
	Datasets []evalMeasureDataset `json:"datasets"`
}

type evalMeasureDataset struct {
	Name     string              `json:"name"`
	LogBytes *float64            `json:"logBytes"`
	Engines  []evalMeasureEngine `json:"engines"`
}

// evalMeasureEngine is one engine row. nesso keeps its query timings per mode
// under modes; grep and BM25 keep them on the row. grep has no index, so its
// index fields are null.
type evalMeasureEngine struct {
	Engine          string            `json:"engine"`
	Status          string            `json:"status"`
	IndexWall       *float64          `json:"indexWallSeconds"`
	IndexBytes      *float64          `json:"indexBytes"`
	QueryLatencyP50 *float64          `json:"queryLatencyMedianSeconds"`
	Modes           []evalMeasureMode `json:"modes"`
}

type evalMeasureMode struct {
	Mode            string   `json:"mode"`
	QueryLatencyP50 *float64 `json:"queryLatencyMedianSeconds"`
}

// ratioColumn is one ratio of the table, computed per dataset and for the
// aggregate row.
type ratioColumn struct {
	header    string
	dataset   func(evalMeasureDataset) (float64, bool)
	aggregate func([]evalMeasureDataset) (float64, bool)
}

func evalRatiosMain(args []string) int {
	fset := flag.NewFlagSet("eval-ratios", flag.ContinueOnError)
	fset.SetOutput(os.Stderr)
	currentPath := fset.String("current", "", "eval-retrieval report from this run")
	previousPath := fset.String("previous", "", "eval-retrieval report from the previous nightly run (optional; a missing file is noted)")
	title := fset.String("title", "", "heading for the table (default: the current report's file name)")
	summary := fset.String("summary", "", "markdown file to append (default: GITHUB_STEP_SUMMARY)")
	fset.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci eval-ratios --current FILE [--previous FILE] [--title TEXT]")
		fset.PrintDefaults()
	}
	if err := fset.Parse(args); err != nil {
		return 1
	}
	if *currentPath == "" || fset.NArg() != 0 {
		fset.Usage()
		return 1
	}
	heading := *title
	if heading == "" {
		heading = *currentPath
	}
	current, err := loadEvalMeasure(*currentPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "eval-ratios: %v\n", err)
		return 1
	}
	var previous *evalMeasure
	previousNote := ""
	if *previousPath != "" {
		previous, err = loadEvalMeasure(*previousPath)
		switch {
		case errors.Is(err, fs.ErrNotExist):
			previousNote = "No previous nightly report to compare."
		case err != nil:
			fmt.Fprintf(os.Stderr, "eval-ratios: %v\n", err)
			return 1
		case previous == nil:
			previousNote = "The previous nightly report has no `measure` section."
		}
	}
	text := formatEvalRatios(heading, current, previous, previousNote)
	fmt.Print(text)
	if err := appendSummary(summaryDestination(*summary), text); err != nil {
		fmt.Fprintf(os.Stderr, "eval-ratios: %v\n", err)
		return 1
	}
	return 0
}

// loadEvalMeasure returns the report's measure section, or nil when the report
// has none. Unreadable files and malformed JSON are errors.
func loadEvalMeasure(path string) (*evalMeasure, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var report evalReport
	if err := json.Unmarshal(data, &report); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	raw := bytes.TrimSpace(report.Measure)
	if len(raw) == 0 || bytes.Equal(raw, []byte("null")) {
		return nil, nil
	}
	var measure evalMeasure
	if err := json.Unmarshal(raw, &measure); err != nil {
		return nil, fmt.Errorf("%s: measure: %w", path, err)
	}
	seen := map[string]bool{}
	for i, dataset := range measure.Datasets {
		if dataset.Name == "" {
			return nil, fmt.Errorf("%s: measure.datasets[%d] has no name", path, i)
		}
		if seen[dataset.Name] {
			return nil, fmt.Errorf("%s: measure dataset %q appears twice", path, dataset.Name)
		}
		seen[dataset.Name] = true
		for j, engine := range dataset.Engines {
			if engine.Engine == "" {
				return nil, fmt.Errorf("%s: measure dataset %q engines[%d] has no engine name", path, dataset.Name, j)
			}
		}
	}
	return &measure, nil
}

func (d evalMeasureDataset) engine(name string) (evalMeasureEngine, bool) {
	for _, engine := range d.Engines {
		if engine.Engine == name && engine.Status != "skipped" {
			return engine, true
		}
	}
	return evalMeasureEngine{}, false
}

// nessoModeP50 is nesso's median query latency in one mode.
func (d evalMeasureDataset) nessoModeP50(mode string) (float64, bool) {
	nesso, ok := d.engine("nesso")
	if !ok {
		return 0, false
	}
	for _, m := range nesso.Modes {
		if m.Mode == mode {
			return positive(m.QueryLatencyP50)
		}
	}
	return 0, false
}

func (d evalMeasureDataset) engineP50(name string) (float64, bool) {
	engine, ok := d.engine(name)
	if !ok {
		return 0, false
	}
	return positive(engine.QueryLatencyP50)
}

func (d evalMeasureDataset) indexWall(name string) (float64, bool) {
	engine, ok := d.engine(name)
	if !ok {
		return 0, false
	}
	return positive(engine.IndexWall)
}

func (d evalMeasureDataset) indexBytes(name string) (float64, bool) {
	engine, ok := d.engine(name)
	if !ok {
		return 0, false
	}
	return positive(engine.IndexBytes)
}

func ratio(num float64, numOK bool, den float64, denOK bool) (float64, bool) {
	if !numOK || !denOK || den <= 0 {
		return 0, false
	}
	return num / den, true
}

// sumRatio sums num and den over the datasets where both are present.
func sumRatio(datasets []evalMeasureDataset, num, den func(evalMeasureDataset) (float64, bool)) (float64, bool) {
	var numSum, denSum float64
	count := 0
	for _, dataset := range datasets {
		n, nOK := num(dataset)
		d, dOK := den(dataset)
		if nOK && dOK {
			numSum += n
			denSum += d
			count++
		}
	}
	if count == 0 {
		return 0, false
	}
	return ratio(numSum, true, denSum, true)
}

// medianRatio divides the median of num by the median of den, each taken over
// the datasets where both are present.
func medianRatio(datasets []evalMeasureDataset, num, den func(evalMeasureDataset) (float64, bool)) (float64, bool) {
	var nums, dens []float64
	for _, dataset := range datasets {
		n, nOK := num(dataset)
		d, dOK := den(dataset)
		if nOK && dOK {
			nums = append(nums, n)
			dens = append(dens, d)
		}
	}
	if len(nums) == 0 {
		return 0, false
	}
	return ratio(medianOf(nums), true, medianOf(dens), true)
}

func medianOf(values []float64) float64 {
	sorted := append([]float64(nil), values...)
	sort.Float64s(sorted)
	mid := len(sorted) / 2
	if len(sorted)%2 == 1 {
		return sorted[mid]
	}
	return (sorted[mid-1] + sorted[mid]) / 2
}

// nessoModes lists nesso's modes in the order the current report gives them.
func nessoModes(measure *evalMeasure) []string {
	var modes []string
	seen := map[string]bool{}
	for _, dataset := range measure.Datasets {
		nesso, ok := dataset.engine("nesso")
		if !ok {
			continue
		}
		for _, mode := range nesso.Modes {
			if mode.Mode != "" && !seen[mode.Mode] {
				seen[mode.Mode] = true
				modes = append(modes, mode.Mode)
			}
		}
	}
	return modes
}

func ratioColumns(modes []string) []ratioColumn {
	nessoIndex := func(d evalMeasureDataset) (float64, bool) { return d.indexWall("nesso") }
	bm25Index := func(d evalMeasureDataset) (float64, bool) { return d.indexWall("bm25") }
	columns := []ratioColumn{{
		header: "nesso index wall ÷ BM25",
		dataset: func(d evalMeasureDataset) (float64, bool) {
			n, nOK := nessoIndex(d)
			b, bOK := bm25Index(d)
			return ratio(n, nOK, b, bOK)
		},
		aggregate: func(ds []evalMeasureDataset) (float64, bool) { return sumRatio(ds, nessoIndex, bm25Index) },
	}}
	for _, mode := range modes {
		nessoP50 := func(d evalMeasureDataset) (float64, bool) { return d.nessoModeP50(mode) }
		for _, other := range []struct{ engine, label string }{{"grep", "grep"}, {"bm25", "BM25"}} {
			otherP50 := func(d evalMeasureDataset) (float64, bool) { return d.engineP50(other.engine) }
			columns = append(columns, ratioColumn{
				header: fmt.Sprintf("nesso/%s p50 ÷ %s", mode, other.label),
				dataset: func(d evalMeasureDataset) (float64, bool) {
					n, nOK := nessoP50(d)
					o, oOK := otherP50(d)
					return ratio(n, nOK, o, oOK)
				},
				aggregate: func(ds []evalMeasureDataset) (float64, bool) { return medianRatio(ds, nessoP50, otherP50) },
			})
		}
	}
	logBytes := func(d evalMeasureDataset) (float64, bool) { return positive(d.LogBytes) }
	for _, engine := range []struct{ name, label string }{{"nesso", "nesso"}, {"bm25", "BM25"}} {
		size := func(d evalMeasureDataset) (float64, bool) { return d.indexBytes(engine.name) }
		columns = append(columns, ratioColumn{
			header: engine.label + " index ÷ log",
			dataset: func(d evalMeasureDataset) (float64, bool) {
				s, sOK := size(d)
				l, lOK := logBytes(d)
				return ratio(s, sOK, l, lOK)
			},
			aggregate: func(ds []evalMeasureDataset) (float64, bool) { return sumRatio(ds, size, logBytes) },
		})
	}
	return columns
}

func formatRatio(value float64, ok bool) string {
	if !ok || math.IsNaN(value) || math.IsInf(value, 0) {
		return "n/a"
	}
	switch {
	case value >= 100:
		return fmt.Sprintf("%.0f", value)
	case value >= 10:
		return fmt.Sprintf("%.1f", value)
	default:
		return fmt.Sprintf("%.2f", value)
	}
}

func formatRatioCell(value float64, ok bool, prevValue float64, prevOK bool, withPrevious bool) string {
	cell := formatRatio(value, ok)
	if withPrevious {
		cell += " (" + formatRatio(prevValue, prevOK) + ")"
	}
	return cell
}

func runnerLine(label string, measure *evalMeasure) string {
	parts := []string{}
	if measure.CPUModel != "" {
		parts = append(parts, measure.CPUModel)
	}
	if measure.Nproc != nil {
		parts = append(parts, fmt.Sprintf("nproc %d", *measure.Nproc))
	}
	if measure.Repeat != nil {
		parts = append(parts, fmt.Sprintf("median of %d repeats", *measure.Repeat))
	}
	if len(parts) == 0 {
		return ""
	}
	return fmt.Sprintf("%s: %s.", label, strings.Join(parts, ", "))
}

// formatEvalRatios renders the ratio table. previous is nil when there is no
// previous measured report; previousNote then says why.
func formatEvalRatios(title string, current, previous *evalMeasure, previousNote string) string {
	var b strings.Builder
	fmt.Fprintf(&b, "### Speed ratios: %s\n\n", title)
	fmt.Fprintf(&b, "_%s._\n\n", evalRatiosLabel)
	if current == nil {
		b.WriteString("This report has no `measure` section (pull request runs and runs without `--measure` are quality-only); no ratios to show.\n")
		return b.String()
	}
	if len(current.Datasets) == 0 {
		b.WriteString("This report's `measure` section has no datasets; no ratios to show.\n")
		return b.String()
	}
	if line := runnerLine("This run", current); line != "" {
		b.WriteString(line + "\n")
	}
	withPrevious := previous != nil
	if withPrevious {
		if line := runnerLine("Previous nightly, in parentheses", previous); line != "" {
			b.WriteString(line + "\n")
		} else {
			b.WriteString("Previous nightly in parentheses.\n")
		}
	} else if previousNote != "" {
		b.WriteString(previousNote + "\n")
	}
	b.WriteString("Lower is better for nesso. The aggregate row divides summed index wall and summed index bytes, and the medians of the per-dataset p50s.\n\n")

	columns := ratioColumns(nessoModes(current))
	b.WriteString("| dataset |")
	for _, column := range columns {
		b.WriteString(" " + column.header + " |")
	}
	b.WriteString("\n| --- |")
	for range columns {
		b.WriteString(" ---: |")
	}
	b.WriteString("\n")

	previousByName := map[string]evalMeasureDataset{}
	if withPrevious {
		for _, dataset := range previous.Datasets {
			previousByName[dataset.Name] = dataset
		}
	}
	for _, dataset := range current.Datasets {
		prev, havePrev := previousByName[dataset.Name]
		b.WriteString("| " + dataset.Name + " |")
		for _, column := range columns {
			value, ok := column.dataset(dataset)
			var prevValue float64
			prevOK := false
			if havePrev {
				prevValue, prevOK = column.dataset(prev)
			}
			b.WriteString(" " + formatRatioCell(value, ok, prevValue, prevOK, withPrevious) + " |")
		}
		b.WriteString("\n")
	}
	if len(current.Datasets) > 1 {
		b.WriteString("| **aggregate** |")
		for _, column := range columns {
			value, ok := column.aggregate(current.Datasets)
			var prevValue float64
			prevOK := false
			if withPrevious {
				prevValue, prevOK = column.aggregate(previous.Datasets)
			}
			b.WriteString(" " + formatRatioCell(value, ok, prevValue, prevOK, withPrevious) + " |")
		}
		b.WriteString("\n")
	}
	return b.String()
}
