// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"sort"
	"strings"
)

var trivySeverityOrder = []string{"CRITICAL", "HIGH", "MEDIUM", "LOW", "UNKNOWN"}

type trivyFile struct {
	Results []trivyResult `json:"Results"`
}

type trivyResult struct {
	Vulnerabilities []trivyVuln `json:"Vulnerabilities"`
}

type trivyVuln struct {
	VulnerabilityID string `json:"VulnerabilityID"`
	PkgName         string `json:"PkgName"`
	Severity        string `json:"Severity"`
	FixedVersion    string `json:"FixedVersion"`
}

type trivyRow struct {
	severity string
	count    int
	fixable  int
}

type trivyFix struct {
	severity string
	id       string
	pkg      string
	fixed    string
}

func trivyMain(args []string) int {
	fs := flag.NewFlagSet("trivy-summary", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	jsonPath := fs.String("json", "", "Trivy JSON report")
	arch := fs.String("arch", "", "image architecture label")
	summary := fs.String("summary", "", "markdown file to append (default: GITHUB_STEP_SUMMARY)")
	fail := fs.Bool("fail-fixed-high-critical", false, "exit 1 when a HIGH or CRITICAL finding has a fixed version")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci trivy-summary --json FILE [--arch NAME] [--summary FILE] [--fail-fixed-high-critical]")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	if *jsonPath == "" || fs.NArg() != 0 {
		fs.Usage()
		return 1
	}
	if *arch != "" && !validSuiteName(*arch) {
		fmt.Fprintf(os.Stderr, "trivy-summary: invalid arch %q\n", *arch)
		return 1
	}
	raw, err := os.ReadFile(*jsonPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "trivy-summary: %v\n", err)
		return 1
	}
	text, fixes, err := formatTrivy(raw, *arch, *fail)
	if err != nil {
		fmt.Fprintf(os.Stderr, "trivy-summary: %v\n", err)
		return 1
	}
	fmt.Print(text)
	if err := appendSummary(summaryDestination(*summary), text); err != nil {
		fmt.Fprintf(os.Stderr, "trivy-summary: %v\n", err)
		return 1
	}
	if *fail && len(fixes) > 0 {
		fmt.Fprintf(os.Stderr, "trivy-summary: %d fixable HIGH or CRITICAL findings\n", len(fixes))
		return 1
	}
	return 0
}

func formatTrivy(raw []byte, arch string, fail bool) (string, []trivyFix, error) {
	var report trivyFile
	if err := json.Unmarshal(raw, &report); err != nil {
		return "", nil, err
	}
	rows, fixes := countTrivy(report)
	var b strings.Builder
	if arch == "" {
		b.WriteString("### Trivy runtime image\n\n")
	} else {
		fmt.Fprintf(&b, "### Trivy runtime image (%s)\n\n", arch)
	}
	b.WriteString("OS packages and language libraries.\n\n")
	b.WriteString("| Severity | Count | Fixable |\n")
	b.WriteString("| --- | ---: | ---: |\n")
	for _, row := range rows {
		fmt.Fprintf(&b, "| %s | %d | %d |\n", row.severity, row.count, row.fixable)
	}
	b.WriteByte('\n')
	if len(fixes) == 0 {
		b.WriteString("No fixable HIGH or CRITICAL findings.\n")
	} else {
		b.WriteString("Fixable HIGH and CRITICAL:\n\n")
		limit := len(fixes)
		if limit > 30 {
			limit = 30
		}
		for _, fix := range fixes[:limit] {
			fmt.Fprintf(&b, "- %s %s %s fixed %s\n", fix.severity, fix.id, fix.pkg, fix.fixed)
		}
		if len(fixes) > limit {
			fmt.Fprintf(&b, "- and %d more\n", len(fixes)-limit)
		}
	}
	b.WriteByte('\n')
	if fail {
		b.WriteString("The job fails when that fixable HIGH or CRITICAL count is not zero.\n")
	} else {
		b.WriteString("The job reports these findings and does not fail on them.\n")
	}
	return b.String(), fixes, nil
}

func countTrivy(report trivyFile) ([]trivyRow, []trivyFix) {
	counts := map[string]int{}
	fixable := map[string]int{}
	var fixes []trivyFix
	for _, result := range report.Results {
		for _, vuln := range result.Vulnerabilities {
			severity := strings.ToUpper(strings.TrimSpace(vuln.Severity))
			if severity == "" {
				severity = "UNKNOWN"
			}
			counts[severity]++
			fixed := strings.TrimSpace(vuln.FixedVersion)
			if fixed == "" {
				continue
			}
			fixable[severity]++
			if severity == "HIGH" || severity == "CRITICAL" {
				fixes = append(fixes, trivyFix{
					severity: severity,
					id:       vuln.VulnerabilityID,
					pkg:      vuln.PkgName,
					fixed:    fixed,
				})
			}
		}
	}
	order := append([]string{}, trivySeverityOrder...)
	extra := make([]string, 0)
	seen := map[string]bool{}
	for _, name := range order {
		seen[name] = true
	}
	for name := range counts {
		if !seen[name] {
			extra = append(extra, name)
		}
	}
	sort.Strings(extra)
	order = append(order, extra...)
	rows := make([]trivyRow, 0, len(order))
	for _, name := range order {
		rows = append(rows, trivyRow{severity: name, count: counts[name], fixable: fixable[name]})
	}
	return rows, fixes
}
