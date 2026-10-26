// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
)

func TestPipelineWorkflowSatisfiesGate(t *testing.T) {
	parsed, problems := inspectPipeline(readPipeline(t))
	if len(problems) > 0 {
		t.Fatalf("%s", strings.Join(problems, "\n"))
	}
	var always []string
	for _, job := range parsed.jobs {
		switch job.id {
		case "fuzz-test":
			if job.class != classPullRequestOff {
				t.Fatalf("fuzz-test class %d", job.class)
			}
		case "bench-history":
			if job.class != classMainHistory {
				t.Fatalf("bench-history class %d", job.class)
			}
		case gateJobID:
			if job.class != classGate || parsed.gateName != gateJobName || parsed.gateIf != "always()" {
				t.Fatalf("gate class %d name %q if %q", job.class, parsed.gateName, parsed.gateIf)
			}
		default:
			if job.class == classAlways {
				always = append(always, job.id)
			} else if job.class != classDocs {
				t.Fatalf("job %s class %d if %q", job.id, job.class, job.ifText)
			}
		}
	}
	sort.Strings(always)
	if strings.Join(always, ",") != "changes,lint,prepare-image-amd64" {
		t.Fatalf("always-run jobs %v", always)
	}
	if code := ciGateNeedsMain([]string{"--workflow", pipelinePath(t)}); code != 0 {
		t.Fatal(code)
	}
}

func TestSkipClassification(t *testing.T) {
	parsed := mustPipeline(t)
	docs := map[string]string{}
	for _, job := range parsed.jobs {
		if job.class == classDocs || job.class == classPullRequestOff || job.class == classMainHistory {
			docs[job.id] = "skipped"
		}
	}
	cases := []struct {
		name    string
		event   string
		code    string
		patch   map[string]string
		wantErr string
		verdict map[string]string
	}{
		{
			name:  "code pull request",
			event: "pull_request",
			code:  "true",
			patch: map[string]string{"fuzz-test": "skipped", "bench-history": "skipped"},
			verdict: map[string]string{
				"fuzz-test":     "skipped, pull request",
				"bench-history": "skipped, not a push to main",
				"lint":          "success",
				"clang-tidy":    "success",
			},
		},
		{
			name:  "docs pull request",
			event: "pull_request",
			code:  "false",
			patch: docs,
			verdict: map[string]string{
				"changes":             "success",
				"prepare-image-amd64": "success",
				"lint":                "success",
				"clang-tidy":          "skipped, docs-only pull request",
				"test-gcc-26-amd64":   "skipped, docs-only pull request",
				"prepare-image-arm64": "skipped, docs-only pull request",
				"fuzz-test":           "skipped, pull request",
				"bench-history":       "skipped, not a push to main",
			},
		},
		{
			name:  "dependency failure is its own skip",
			event: "pull_request",
			code:  "true",
			patch: map[string]string{
				"fuzz-test":           "skipped",
				"bench-history":       "skipped",
				"prepare-image-amd64": "failure",
				"clang-tidy":          "skipped",
				"build-gcc-26-amd64":  "failure",
				"test-gcc-26-amd64":   "skipped",
			},
			wantErr: "prepare-image-amd64 failure\nclang-tidy skipped, not an intentional skip\nbuild-gcc-26-amd64 failure\ntest-gcc-26-amd64 skipped, not an intentional skip",
		},
		{
			name:    "cancelled and unknown",
			event:   "push",
			code:    "true",
			patch:   map[string]string{"lint": "cancelled", "cppcheck": "neutral"},
			wantErr: "lint cancelled\ncppcheck result \"neutral\"",
		},
		{
			name:  "push runs fuzz and history",
			event: "push",
			code:  "true",
		},
		{
			name:    "push skips history",
			event:   "push",
			code:    "true",
			patch:   map[string]string{"bench-history": "skipped"},
			wantErr: "bench-history skipped",
		},
		{
			name:    "push skips fuzz",
			event:   "push",
			code:    "true",
			patch:   map[string]string{"fuzz-test": "skipped"},
			wantErr: "fuzz-test skipped",
		},
		{
			name:  "schedule skips history only",
			event: "schedule",
			code:  "true",
			patch: map[string]string{"bench-history": "skipped"},
		},
		{
			name:    "schedule skips fuzz",
			event:   "schedule",
			code:    "true",
			patch:   map[string]string{"fuzz-test": "skipped", "bench-history": "skipped"},
			wantErr: "fuzz-test skipped",
		},
		{
			name:  "dispatch skips history",
			event: "workflow_dispatch",
			code:  "true",
			patch: map[string]string{"bench-history": "skipped"},
		},
		{
			name:    "empty event does not excuse fuzz",
			event:   "",
			code:    "true",
			patch:   map[string]string{"fuzz-test": "skipped"},
			wantErr: "fuzz-test skipped",
		},
		{
			name:    "failed changes is not docs-only",
			event:   "pull_request",
			code:    "false",
			patch:   map[string]string{"changes": "failure", "clang-tidy": "skipped"},
			wantErr: "changes failure\nclang-tidy skipped, not an intentional skip",
		},
		{
			name:    "code false on a push is not docs-only",
			event:   "push",
			code:    "false",
			patch:   map[string]string{"coverage": "skipped"},
			wantErr: "coverage skipped, not an intentional skip",
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			results := successResults(parsed)
			for id, result := range tc.patch {
				results[id] = result
			}
			rows, problems := evaluateGate(parsed, results, tc.event, tc.code)
			joined := strings.Join(problems, "\n")
			if tc.wantErr == "" && len(problems) > 0 {
				t.Fatalf("%s", joined)
			}
			for _, want := range strings.Split(tc.wantErr, "\n") {
				if want != "" && !strings.Contains(joined, want) {
					t.Errorf("missing %q in\n%s", want, joined)
				}
			}
			if tc.name == "dependency failure is its own skip" && (strings.Contains(joined, "fuzz-test") || strings.Contains(joined, "bench-history")) {
				t.Fatalf("deliberate skips reported:\n%s", joined)
			}
			for job, verdict := range tc.verdict {
				assertVerdict(t, rows, job, verdict)
			}
		})
	}
}

func TestMissingNeedsAndResultsFail(t *testing.T) {
	text := readPipeline(t)
	checks := []struct {
		old  string
		new  string
		want string
	}{
		{"      - clang-tidy\n", "", "ci-gate needs is missing clang-tidy"},
		{"            clang-tidy=${{ needs.clang-tidy.result }}\n", "", "JOB_RESULTS is missing clang-tidy"},
		{"clang-tidy=${{ needs.clang-tidy.result }}", "clang-tidy=${{ needs.lint.result }}", "JOB_RESULTS key clang-tidy reads needs.lint.result"},
		{"    if: github.event_name != 'pull_request'\n", "    if: github.event_name == 'schedule'\n", "job fuzz-test has an unrecognized if"},
	}
	for _, check := range checks {
		problems := joinProblems(strings.Replace(text, check.old, check.new, 1))
		if !strings.Contains(problems, check.want) {
			t.Fatalf("missing %q in\n%s", check.want, problems)
		}
	}
}

func pipelinePath(t *testing.T) string {
	t.Helper()
	return filepath.Join("..", "..", ".github", "workflows", "ci.yml")
}

func readPipeline(t *testing.T) string {
	t.Helper()
	body, err := os.ReadFile(pipelinePath(t))
	if err != nil {
		t.Fatal(err)
	}
	return string(body)
}

func mustPipeline(t *testing.T) pipeline {
	t.Helper()
	parsed, problems := inspectPipeline(readPipeline(t))
	if len(problems) > 0 {
		t.Fatalf("%s", strings.Join(problems, "\n"))
	}
	return parsed
}

func successResults(parsed pipeline) map[string]string {
	results := map[string]string{}
	for _, job := range parsed.jobs {
		if job.id != gateJobID {
			results[job.id] = "success"
		}
	}
	return results
}

func assertVerdict(t *testing.T, rows []gateRow, job, verdict string) {
	t.Helper()
	for _, row := range rows {
		if row.job == job {
			if row.verdict != verdict {
				t.Fatalf("%s verdict %q, want %q", job, row.verdict, verdict)
			}
			return
		}
	}
	t.Fatalf("missing row %s", job)
}

func joinProblems(text string) string {
	_, problems := inspectPipeline(text)
	return strings.Join(problems, "\n")
}
