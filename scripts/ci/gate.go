// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

const (
	gateJobID   = "ci-gate"
	gateJobName = "CI gate"
)

const (
	// classUnknown fails closed. A job left unclassified, or an if this
	// checker does not know, is not an intentional skip.
	classUnknown = iota
	classAlways
	classDocs
	classPullRequestOff
	classMainHistory
	classGate
)

type pipelineJob struct {
	id     string
	ifText string
	class  int
}

type resultBind struct {
	key  string
	need string
}

type pipeline struct {
	jobs     []pipelineJob
	gateName string
	gateIf   string
	needs    []string
	results  []resultBind
}

type gateRow struct {
	job     string
	result  string
	verdict string
}

var (
	jobHeader  = regexp.MustCompile(`^  ([A-Za-z0-9][A-Za-z0-9_-]*):[ \t]*(?:#.*)?$`)
	needItem   = regexp.MustCompile(`^      - ([A-Za-z0-9][A-Za-z0-9_-]*)[ \t]*(?:#.*)?$`)
	resultLine = regexp.MustCompile(`^[ \t]*([A-Za-z0-9][A-Za-z0-9_-]*)=\$\{\{[ \t]*needs\.([A-Za-z0-9][A-Za-z0-9_-]*)\.result[ \t]*\}\}[ \t]*$`)
	testJobIf  = regexp.MustCompile(`^\$\{\{ always\(\) && !cancelled\(\) && needs\.changes\.result == 'success' && needs\.changes\.outputs\.code == 'true' && needs\.([A-Za-z0-9][A-Za-z0-9_-]*)\.result == 'success' \}\}$`)
)

func ciGateNeedsMain(args []string) int {
	workflow, ok := gateWorkflowFlag("ci-gate-needs", args)
	if !ok {
		return 1
	}
	problems := gateWorkflowFileProblems(workflow)
	for _, problem := range problems {
		fmt.Fprintf(os.Stderr, "ci-gate-needs: %s\n", problem)
	}
	if len(problems) > 0 {
		return 1
	}
	fmt.Println("ci-gate-needs: ok")
	return 0
}

func ciGateMain(args []string) int {
	workflow, ok := gateWorkflowFlag("ci-gate", args)
	if !ok {
		return 1
	}
	text, err := os.ReadFile(workflow)
	if err != nil {
		fmt.Fprintf(os.Stderr, "ci-gate: %v\n", err)
		return 1
	}
	parsed, problems := inspectPipeline(string(text))
	event := os.Getenv("GITHUB_EVENT_NAME")
	changesCode := strings.TrimSpace(os.Getenv("CHANGES_CODE"))
	results, resultProblems := parseJobResults(os.Getenv("JOB_RESULTS"))
	problems = append(problems, resultProblems...)
	if event == "" {
		problems = append(problems, "GITHUB_EVENT_NAME is empty")
	}
	rows, evalProblems := evaluateGate(parsed, results, event, changesCode)
	problems = append(problems, evalProblems...)
	var successes, skips int
	for _, row := range rows {
		fmt.Printf("ci-gate: %s %s (%s)\n", row.job, row.result, row.verdict)
		switch row.verdict {
		case "success":
			successes++
		case "skipped, docs-only pull request", "skipped, pull request", "skipped, not a push to main":
			skips++
		}
	}
	for _, problem := range problems {
		fmt.Fprintf(os.Stderr, "ci-gate: %s\n", problem)
	}
	if len(problems) > 0 {
		return 1
	}
	fmt.Printf("ci-gate: ok (%d success, %d skipped)\n", successes, skips)
	return 0
}

func gateWorkflowFlag(name string, args []string) (string, bool) {
	fs := flag.NewFlagSet(name, flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	workflow := fs.String("workflow", findPipelineWorkflow(), "Pipeline workflow to read")
	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "usage: ci %s [--workflow FILE]\n", name)
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return "", false
	}
	if fs.NArg() != 0 {
		fs.Usage()
		return "", false
	}
	return *workflow, true
}

func findPipelineWorkflow() string {
	const rel = ".github/workflows/ci.yml"
	dir, err := os.Getwd()
	if err != nil {
		return rel
	}
	for {
		candidate := filepath.Join(dir, rel)
		if _, err := os.Stat(candidate); err == nil {
			return candidate
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return rel
		}
		dir = parent
	}
}

func gateWorkflowFileProblems(path string) []string {
	text, err := os.ReadFile(path)
	if err != nil {
		return []string{err.Error()}
	}
	_, problems := inspectPipeline(string(text))
	return problems
}

// evaluateGate judges every needed job on its own result. success passes.
// failure and cancelled fail. skipped passes only for skipOK. A downstream
// skip caused by a failed dependency is not one of those cases.
func evaluateGate(parsed pipeline, results map[string]string, event, changesCode string) ([]gateRow, []string) {
	var problems []string
	seen := map[string]bool{}
	for _, job := range parsed.jobs {
		if job.id != gateJobID {
			seen[job.id] = true
		}
	}
	var extras []string
	for id := range results {
		if !seen[id] {
			extras = append(extras, id)
		}
	}
	sort.Strings(extras)
	for _, id := range extras {
		problems = append(problems, fmt.Sprintf("unexpected result for %s", id))
	}
	changesResult := results["changes"]
	rows := make([]gateRow, 0, len(seen))
	for _, job := range parsed.jobs {
		if job.id == gateJobID {
			continue
		}
		result, ok := results[job.id]
		row := gateRow{job: job.id, result: result}
		switch {
		case !ok:
			row.verdict = "missing"
			problems = append(problems, fmt.Sprintf("%s has no result", job.id))
		case result == "success":
			row.verdict = "success"
		case result == "failure" || result == "cancelled":
			row.verdict = result
			problems = append(problems, fmt.Sprintf("%s %s", job.id, result))
		case result == "skipped" && skipOK(job.class, event, changesResult, changesCode):
			row.verdict = skipVerdict(job.class)
		case result == "skipped":
			row.verdict = "skipped, not intentional"
			problems = append(problems, fmt.Sprintf("%s skipped, not an intentional skip", job.id))
		default:
			row.verdict = "invalid result"
			problems = append(problems, fmt.Sprintf("%s result %q", job.id, result))
		}
		rows = append(rows, row)
	}
	return rows, problems
}

func skipOK(class int, event, changesResult, changesCode string) bool {
	switch class {
	case classDocs:
		return event == "pull_request" && changesResult == "success" && changesCode == "false"
	case classPullRequestOff:
		return event == "pull_request"
	case classMainHistory:
		// A push must record history, including when the ref is missing.
		// Pull requests, the nightly schedule, and workflow_dispatch skip it.
		switch event {
		case "pull_request", "schedule", "workflow_dispatch":
			return true
		default:
			return false
		}
	default:
		return false
	}
}

func skipVerdict(class int) string {
	switch class {
	case classDocs:
		return "skipped, docs-only pull request"
	case classPullRequestOff:
		return "skipped, pull request"
	case classMainHistory:
		return "skipped, not a push to main"
	default:
		return "skipped"
	}
}

func parseJobResults(raw string) (map[string]string, []string) {
	results := map[string]string{}
	var problems []string
	for _, line := range strings.Split(strings.ReplaceAll(raw, "\r\n", "\n"), "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		key, value, ok := strings.Cut(line, "=")
		key = strings.TrimSpace(key)
		value = strings.TrimSpace(value)
		if !ok || key == "" {
			problems = append(problems, fmt.Sprintf("malformed result line %q", line))
			continue
		}
		if _, exists := results[key]; exists {
			problems = append(problems, fmt.Sprintf("duplicate result for %s", key))
			continue
		}
		results[key] = value
	}
	return results, problems
}

func inspectPipeline(text string) (pipeline, []string) {
	parsed, err := parsePipeline(text)
	if err != nil {
		return parsed, []string{err.Error()}
	}
	return parsed, pipelineProblems(parsed)
}

func parsePipeline(text string) (pipeline, error) {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	lines := strings.Split(text, "\n")
	start := -1
	for i, line := range lines {
		if strings.TrimRight(line, " \t") == "jobs:" {
			start = i
			break
		}
	}
	if start < 0 {
		return pipeline{}, fmt.Errorf("workflow has no jobs key")
	}
	type chunk struct {
		job  pipelineJob
		body []string
	}
	var chunks []chunk
	seen := map[string]bool{}
	for i := start + 1; i < len(lines); i++ {
		line := lines[i]
		if line != "" && line[0] != ' ' && line[0] != '#' && line[0] != '\t' {
			break
		}
		if m := jobHeader.FindStringSubmatch(line); m != nil {
			if seen[m[1]] {
				return pipeline{}, fmt.Errorf("duplicate job %s", m[1])
			}
			seen[m[1]] = true
			chunks = append(chunks, chunk{job: pipelineJob{id: m[1]}})
			continue
		}
		if len(chunks) == 0 {
			continue
		}
		chunks[len(chunks)-1].body = append(chunks[len(chunks)-1].body, line)
	}
	var parsed pipeline
	for _, chunk := range chunks {
		job := chunk.job
		ifText, ifCount, err := extractIf(chunk.body)
		if err != nil {
			return pipeline{}, err
		}
		if ifCount > 1 {
			return pipeline{}, fmt.Errorf("job %s has more than one if", job.id)
		}
		job.ifText = ifText
		if job.id == gateJobID {
			job.class = classGate
			parsed.gateIf = ifText
			parsed.gateName = extractScalar(chunk.body, "name")
			parsed.needs = extractNeeds(chunk.body)
			parsed.results = extractResults(chunk.body)
		} else if class, ok := classifyIf(ifText, seen); ok {
			job.class = class
		}
		parsed.jobs = append(parsed.jobs, job)
	}
	return parsed, nil
}

func extractIf(body []string) (string, int, error) {
	count := 0
	text := ""
	for i, line := range body {
		if !strings.HasPrefix(line, "    if:") {
			continue
		}
		count++
		rest := strings.TrimSpace(strings.TrimPrefix(line, "    if:"))
		switch rest {
		case ">-", ">", "|-", "|", "|+":
			var parts []string
			for _, next := range body[i+1:] {
				if strings.TrimSpace(next) == "" {
					continue
				}
				if len(next)-len(strings.TrimLeft(next, " ")) <= 4 {
					break
				}
				parts = append(parts, strings.TrimSpace(next))
			}
			text = strings.Join(parts, " ")
		default:
			if rest == "" {
				return "", count, fmt.Errorf("empty if")
			}
			text = rest
		}
	}
	return text, count, nil
}

func extractScalar(body []string, key string) string {
	prefix := "    " + key + ":"
	for _, line := range body {
		if strings.HasPrefix(line, prefix) {
			return strings.TrimSpace(strings.TrimPrefix(line, prefix))
		}
	}
	return ""
}

func extractNeeds(body []string) []string {
	var needs []string
	for i, line := range body {
		if strings.TrimRight(line, " \t") != "    needs:" {
			continue
		}
		for _, next := range body[i+1:] {
			m := needItem.FindStringSubmatch(next)
			if m == nil {
				if strings.TrimSpace(next) == "" {
					continue
				}
				break
			}
			needs = append(needs, m[1])
		}
	}
	return needs
}

func extractResults(body []string) []resultBind {
	var binds []resultBind
	for _, line := range body {
		if m := resultLine.FindStringSubmatch(line); m != nil {
			binds = append(binds, resultBind{key: m[1], need: m[2]})
		}
	}
	return binds
}

func classifyIf(ifText string, ids map[string]bool) (int, bool) {
	switch ifText {
	case "":
		return classAlways, true
	case "needs.changes.outputs.code == 'true'":
		return classDocs, true
	case "github.event_name != 'pull_request'":
		return classPullRequestOff, true
	case "always() && !cancelled() && github.event_name == 'push' && github.ref == 'refs/heads/main' && (needs.bench.result == 'success' || needs.bench.result == 'failure')":
		return classMainHistory, true
	case "${{ needs.changes.outputs.code == 'true' && !cancelled() && needs.prepare-image-arm64.result == 'success' }}":
		return classDocs, true
	}
	if m := testJobIf.FindStringSubmatch(ifText); m != nil && ids[m[1]] {
		return classDocs, true
	}
	return classUnknown, false
}

func pipelineProblems(parsed pipeline) []string {
	var problems []string
	want := map[string]bool{}
	var gate *pipelineJob
	for i := range parsed.jobs {
		job := &parsed.jobs[i]
		if job.id == gateJobID {
			gate = job
			continue
		}
		want[job.id] = true
		if job.class == classUnknown {
			shown := job.ifText
			if shown == "" {
				shown = "<empty>"
			}
			problems = append(problems, fmt.Sprintf("job %s has an unrecognized if %q", job.id, shown))
		}
	}
	if gate == nil {
		return append(problems, "workflow has no ci-gate job")
	}
	if parsed.gateName != gateJobName {
		problems = append(problems, fmt.Sprintf("ci-gate name is %q, want %q", parsed.gateName, gateJobName))
	}
	if parsed.gateIf != "always()" {
		problems = append(problems, fmt.Sprintf("ci-gate if is %q, want always()", parsed.gateIf))
	}
	problems = append(problems, setProblems("ci-gate needs", want, countIDs(parsed.needs))...)
	got := map[string]int{}
	for _, bind := range parsed.results {
		got[bind.key]++
		if bind.key != bind.need {
			problems = append(problems, fmt.Sprintf("JOB_RESULTS key %s reads needs.%s.result", bind.key, bind.need))
		}
	}
	problems = append(problems, setProblems("JOB_RESULTS", want, got)...)
	return problems
}

func countIDs(ids []string) map[string]int {
	got := map[string]int{}
	for _, id := range ids {
		got[id]++
	}
	return got
}

func setProblems(kind string, want map[string]bool, got map[string]int) []string {
	var problems []string
	var missing, extra []string
	for id := range want {
		if got[id] == 0 {
			missing = append(missing, id)
		}
	}
	for id, n := range got {
		if n > 1 {
			problems = append(problems, fmt.Sprintf("%s lists %s %d times", kind, id, n))
		}
		if !want[id] {
			extra = append(extra, id)
		}
	}
	sort.Strings(missing)
	sort.Strings(extra)
	for _, id := range missing {
		problems = append(problems, fmt.Sprintf("%s is missing %s", kind, id))
	}
	for _, id := range extra {
		problems = append(problems, fmt.Sprintf("%s lists unknown job %s", kind, id))
	}
	return problems
}
