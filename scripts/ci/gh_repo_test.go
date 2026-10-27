// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

// gh reads GH_REPO, then --repo, then the git remote of a checkout. The
// release job has no checkout, so a bare `gh run download` fails with
// "fatal: not a git repository" before it calls the API.

type envSetting struct {
	present bool
	value   string
}

type ghCall struct {
	text          string
	selfContained bool
}

type scannedStep struct {
	name        string
	conditional bool
	checkout    bool
	env         envSetting
	calls       []ghCall
}

type scannedJob struct {
	env   envSetting
	steps []scannedStep
}

func TestReleaseWorkflowsResolveGhRepo(t *testing.T) {
	root := filepath.Join("..", "..")
	for _, rel := range []string{".github/workflows/release.yml", ".github/workflows/package.yml"} {
		text := readWorkflow(t, filepath.Join(root, rel))
		problems := ghRepoProblems(text)
		if len(problems) > 0 {
			t.Fatalf("%s\n%s", rel, strings.Join(problems, "\n"))
		}
	}
	release := readWorkflow(t, filepath.Join(root, ".github/workflows/release.yml"))
	jobs, _ := mustScanJobs(t, release)
	job, ok := jobs["release"]
	if !ok {
		t.Fatal("release workflow has no release job")
	}
	if !job.env.present || job.env.value != "${{ github.repository }}" {
		t.Fatalf("release job GH_REPO = present:%v %q", job.env.present, job.env.value)
	}
	calls := 0
	for _, step := range job.steps {
		calls += len(step.calls)
	}
	if calls == 0 {
		t.Fatal("release job: parser did not see gh run download")
	}
}

func TestGhRepoChecker(t *testing.T) {
	const bare = `
name: t
on: push
jobs:
  release:
    runs-on: ubuntu-latest
    steps:
      - name: Download .deb provenance
        run: |
          set -euo pipefail
          gh run download "${GITHUB_RUN_ID}" --name "${PROVENANCE_NAME}" --dir "${dest}"
`
	if got := ghRepoProblems(bare); len(got) != 1 || !strings.Contains(got[0], "Download .deb provenance") {
		t.Fatalf("bare gh: %v", got)
	}

	const jobEnv = `
jobs:
  release:
    env:
      GH_REPO: ${{ github.repository }}
    steps:
      - name: Download
        run: gh run download "${GITHUB_RUN_ID}" --name n --dir d
      - name: Later
        run: gh run view "${GITHUB_RUN_ID}"
`
	if got := ghRepoProblems(jobEnv); len(got) != 0 {
		t.Fatalf("job env: %v", got)
	}

	const stepOnly = `
jobs:
  release:
    steps:
      - name: First
        env:
          GH_REPO: ${{ github.repository }}
        run: gh run view 1
      - name: Second
        run: gh run download 1 --name n
`
	got := ghRepoProblems(stepOnly)
	if len(got) != 1 || !strings.Contains(got[0], "Second") {
		t.Fatalf("step env does not cover the next step: %v", got)
	}

	const emptyOverride = `
jobs:
  release:
    env:
      GH_REPO: ${{ github.repository }}
    steps:
      - name: Cleared
        env:
          GH_REPO: ""
        run: gh run view 1
`
	if got := ghRepoProblems(emptyOverride); len(got) != 1 {
		t.Fatalf("empty step GH_REPO: %v", got)
	}

	const checkout = `
jobs:
  build:
    steps:
      - uses: actions/checkout@0123456789abcdef0123456789abcdef01234567 # v0
      - name: After checkout
        run: gh release view
`
	if got := ghRepoProblems(checkout); len(got) != 0 {
		t.Fatalf("checkout: %v", got)
	}

	const checkoutAfter = `
jobs:
  build:
    steps:
      - name: Too early
        run: gh release view
      - uses: actions/checkout@0123456789abcdef0123456789abcdef01234567
`
	if got := ghRepoProblems(checkoutAfter); len(got) != 1 {
		t.Fatalf("checkout after gh: %v", got)
	}

	const flags = `
jobs:
  release:
    steps:
      - run: |
          gh run view 1 --repo owner/name || true
          GH_REPO=owner/name gh run view 1
          echo ok && gh run download 1 -R owner/name --name n
          echo ghcr.io/owner/nesso
          # gh run download should not count
`
	if got := ghRepoProblems(flags); len(got) != 0 {
		t.Fatalf("self-contained gh: %v", got)
	}

	const workflowEnv = `
env:
  GH_REPO: ${{ github.repository }}
jobs:
  release:
    steps:
      - run: gh run view 1
`
	if got := ghRepoProblems(workflowEnv); len(got) != 0 {
		t.Fatalf("workflow env: %v", got)
	}

	const defaultsRun = `
jobs:
  repro-build:
    defaults:
      run:
        shell: bash
    steps:
      - uses: actions/checkout@0123456789abcdef0123456789abcdef01234567
      - run: |
          echo ghcr.io/example/nesso
          case "${ARCH}:$(uname -m)" in
            amd64:x86_64) ;;
          esac
`
	if got := ghRepoProblems(defaultsRun); len(got) != 0 {
		t.Fatalf("defaults.run: %v", got)
	}

	const conditionalCheckout = `
jobs:
  build:
    steps:
      - uses: actions/checkout@0123456789abcdef0123456789abcdef01234567
        if: github.event_name == 'push'
      - run: gh release view
`
	if got := ghRepoProblems(conditionalCheckout); len(got) != 1 {
		t.Fatalf("conditional checkout: %v", got)
	}
}

func readWorkflow(t *testing.T, path string) string {
	t.Helper()
	text, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	return string(text)
}

func mustScanJobs(t *testing.T, text string) (map[string]scannedJob, []string) {
	t.Helper()
	jobs, order, _, err := scanWorkflowJobs(text)
	if err != nil {
		t.Fatal(err)
	}
	return jobs, order
}

func ghRepoProblems(text string) []string {
	jobs, order, workflowEnv, err := scanWorkflowJobs(text)
	if err != nil {
		return []string{err.Error()}
	}
	var problems []string
	for _, name := range order {
		job, ok := jobs[name]
		if !ok {
			continue
		}
		seenCheckout := false
		for i, step := range job.steps {
			env := resolveGHRepo(workflowEnv, job.env, step.env)
			label := stepLabel(step, i+1)
			for _, call := range step.calls {
				if env != "" || seenCheckout || call.selfContained {
					continue
				}
				problems = append(problems, fmt.Sprintf(
					"job %s, step %s: gh needs GH_REPO, --repo, or a checkout: %s",
					name, label, call.text,
				))
			}
			if step.checkout && !step.conditional {
				seenCheckout = true
			}
		}
	}
	return problems
}

func scanWorkflowJobs(text string) (map[string]scannedJob, []string, envSetting, error) {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	lines := strings.Split(text, "\n")
	workflowEnv, jobsAt, err := workflowGHRepo(lines)
	if err != nil {
		return nil, nil, envSetting{}, err
	}
	jobs := map[string]scannedJob{}
	var order []string
	var current string
	var body []string
	flush := func() {
		if current == "" {
			return
		}
		jobs[current] = scanJob(body)
		body = nil
	}
	for _, line := range lines[jobsAt+1:] {
		if line != "" && line[0] != ' ' && line[0] != '#' && line[0] != '\t' {
			break
		}
		if m := jobHeader.FindStringSubmatch(line); m != nil {
			if _, exists := jobs[m[1]]; exists {
				return nil, nil, envSetting{}, fmt.Errorf("duplicate job %s", m[1])
			}
			flush()
			current = m[1]
			order = append(order, current)
			continue
		}
		if current == "" {
			continue
		}
		body = append(body, line)
	}
	flush()
	return jobs, order, workflowEnv, nil
}

func workflowGHRepo(lines []string) (envSetting, int, error) {
	var setting envSetting
	inEnv := false
	for i, line := range lines {
		trim := strings.TrimSpace(line)
		if trim == "" || strings.HasPrefix(trim, "#") {
			continue
		}
		indent := leadingSpaces(line)
		if inEnv && indent > 0 {
			if indent == 2 {
				if key, val, ok := splitYAMLKey(trim); ok && key == "GH_REPO" {
					setting = parseEnvValue(val)
				}
			}
			continue
		}
		inEnv = false
		if indent != 0 {
			continue
		}
		key, _, ok := splitYAMLKey(trim)
		if !ok {
			continue
		}
		if key == "jobs" {
			return setting, i, nil
		}
		inEnv = key == "env"
	}
	return envSetting{}, -1, fmt.Errorf("workflow has no jobs key")
}

const (
	modeJob = iota
	modeJobEnv
	modeSteps
	modeStep
	modeStepEnv
	modeRun
)

func scanJob(body []string) scannedJob {
	var job scannedJob
	var steps []scannedStep
	var cur *scannedStep
	var script strings.Builder
	mode := modeJob
	finish := func() {
		if cur == nil {
			return
		}
		cur.calls = ghCalls(script.String())
		script.Reset()
		steps = append(steps, *cur)
		cur = nil
	}
	for i := 0; i < len(body); i++ {
		line := body[i]
		if mode == modeRun {
			if strings.TrimSpace(line) == "" || leadingSpaces(line) > 8 {
				script.WriteString(line)
				script.WriteByte('\n')
				continue
			}
			mode = modeStep
		}
		if strings.TrimSpace(line) == "" || strings.HasPrefix(strings.TrimSpace(line), "#") {
			continue
		}
		indent := leadingSpaces(line)
		trim := strings.TrimSpace(line)
		switch mode {
		case modeJobEnv:
			if indent > 4 {
				if key, val, ok := splitYAMLKey(trim); ok && key == "GH_REPO" {
					job.env = parseEnvValue(val)
				}
				continue
			}
			mode = modeJob
		case modeStepEnv:
			if indent > 8 {
				if key, val, ok := splitYAMLKey(trim); ok && key == "GH_REPO" {
					cur.env = parseEnvValue(val)
				}
				continue
			}
			mode = modeStep
		}
		if indent == 4 {
			key, _, ok := splitYAMLKey(trim)
			if ok && key == "env" {
				mode = modeJobEnv
				continue
			}
			if ok && key == "steps" {
				mode = modeSteps
				continue
			}
			if mode == modeStep {
				finish()
				mode = modeJob
			}
			continue
		}
		if mode != modeSteps && mode != modeStep {
			continue
		}
		if strings.HasPrefix(line, "      - ") {
			finish()
			cur = &scannedStep{}
			mode = modeStep
			applyStepKey(cur, &script, &mode, strings.TrimSpace(strings.TrimPrefix(line, "      - ")))
			continue
		}
		if mode == modeStep && indent == 8 && cur != nil {
			applyStepKey(cur, &script, &mode, trim)
		}
	}
	finish()
	job.steps = steps
	return job
}

func applyStepKey(step *scannedStep, script *strings.Builder, mode *int, rest string) {
	key, val, ok := splitYAMLKey(rest)
	if !ok {
		return
	}
	val = stripInlineComment(val)
	switch key {
	case "name":
		if step.name == "" {
			step.name = unquoteScalar(val)
		}
	case "if":
		step.conditional = true
	case "uses":
		if isCheckout(val) {
			step.checkout = true
		}
	case "env":
		*mode = modeStepEnv
	case "run":
		if isBlockScalar(val) {
			*mode = modeRun
			return
		}
		script.WriteString(unquoteScalar(val))
		script.WriteByte('\n')
	}
}

func resolveGHRepo(workflow, job, step envSetting) string {
	if step.present {
		return strings.TrimSpace(step.value)
	}
	if job.present {
		return strings.TrimSpace(job.value)
	}
	if workflow.present {
		return strings.TrimSpace(workflow.value)
	}
	return ""
}

func stepLabel(step scannedStep, n int) string {
	if step.name != "" {
		return fmt.Sprintf("%q", step.name)
	}
	return fmt.Sprintf("#%d", n)
}

func ghCalls(script string) []ghCall {
	var calls []ghCall
	for _, line := range strings.Split(script, "\n") {
		trim := strings.TrimSpace(line)
		if trim == "" || strings.HasPrefix(trim, "#") {
			continue
		}
		for _, seg := range splitShell(line) {
			call, ok := ghCallFromSegment(seg)
			if ok {
				calls = append(calls, call)
			}
		}
	}
	return calls
}

func ghCallFromSegment(seg string) (ghCall, bool) {
	seg = strings.TrimSpace(seg)
	if seg == "" {
		return ghCall{}, false
	}
	tokens := strings.Fields(seg)
	i := 0
	inline := false
	for i < len(tokens) && envAssign.MatchString(tokens[i]) {
		key, val, _ := strings.Cut(tokens[i], "=")
		if key == "GH_REPO" && strings.Trim(val, `"'`) != "" {
			inline = true
		}
		i++
	}
	if i >= len(tokens) || commandBase(tokens[i]) != "gh" {
		return ghCall{}, false
	}
	return ghCall{text: seg, selfContained: inline || hasRepoArg(tokens[i+1:])}, true
}

var envAssign = regexp.MustCompile(`^[A-Za-z_][A-Za-z0-9_]*=`)

func commandBase(token string) string {
	token = unquoteScalar(token)
	if i := strings.LastIndexAny(token, `/\`); i >= 0 {
		token = token[i+1:]
	}
	return token
}

func hasRepoArg(args []string) bool {
	for i, arg := range args {
		arg = unquoteScalar(arg)
		switch {
		case arg == "--":
			return false
		case arg == "--repo" || arg == "-R":
			return i+1 < len(args) && strings.TrimSpace(unquoteScalar(args[i+1])) != ""
		case strings.HasPrefix(arg, "--repo=") && len(arg) > len("--repo="):
			return true
		}
	}
	return false
}

func splitShell(line string) []string {
	var parts []string
	var b strings.Builder
	inSingle := false
	inDouble := false
	flush := func() {
		parts = append(parts, b.String())
		b.Reset()
	}
	for i := 0; i < len(line); i++ {
		c := line[i]
		if inSingle {
			b.WriteByte(c)
			if c == '\'' {
				inSingle = false
			}
			continue
		}
		if inDouble {
			b.WriteByte(c)
			if c == '\\' && i+1 < len(line) {
				i++
				b.WriteByte(line[i])
				continue
			}
			if c == '"' {
				inDouble = false
			}
			continue
		}
		switch c {
		case '\'':
			inSingle = true
			b.WriteByte(c)
		case '"':
			inDouble = true
			b.WriteByte(c)
		case ';':
			flush()
		case '|':
			if i+1 < len(line) && line[i+1] == '|' {
				i++
			}
			flush()
		case '&':
			if i+1 < len(line) && line[i+1] == '&' {
				i++
			}
			flush()
		case '(':
			if b.Len() > 0 && b.String()[b.Len()-1] == '$' {
				s := b.String()
				b.Reset()
				b.WriteString(s[:len(s)-1])
			}
			flush()
		case '`':
			flush()
		case '#':
			if b.Len() == 0 || line[i-1] == ' ' || line[i-1] == '\t' {
				flush()
				return parts
			}
			b.WriteByte(c)
		default:
			b.WriteByte(c)
		}
	}
	flush()
	return parts
}

func splitYAMLKey(rest string) (string, string, bool) {
	rest = strings.TrimSpace(rest)
	key, val, ok := strings.Cut(rest, ":")
	if !ok || key == "" || strings.ContainsAny(key, " \t{}") {
		return "", "", false
	}
	return key, strings.TrimSpace(val), true
}

func parseEnvValue(raw string) envSetting {
	raw = stripInlineComment(strings.TrimSpace(raw))
	if raw == "" || isBlockScalar(raw) {
		return envSetting{present: true, value: ""}
	}
	return envSetting{present: true, value: unquoteScalar(raw)}
}

func isBlockScalar(val string) bool {
	switch strings.TrimSpace(val) {
	case "|", "|-", "|+", ">", ">-", ">+":
		return true
	default:
		return false
	}
}

func isCheckout(uses string) bool {
	uses = unquoteScalar(strings.TrimSpace(uses))
	return uses == "actions/checkout" || strings.HasPrefix(uses, "actions/checkout@")
}

func stripInlineComment(val string) string {
	if i := strings.Index(val, " #"); i >= 0 {
		return strings.TrimSpace(val[:i])
	}
	return strings.TrimSpace(val)
}

func unquoteScalar(s string) string {
	if len(s) >= 2 {
		if (s[0] == '"' && s[len(s)-1] == '"') || (s[0] == '\'' && s[len(s)-1] == '\'') {
			return s[1 : len(s)-1]
		}
	}
	return s
}

func leadingSpaces(line string) int {
	n := 0
	for n < len(line) && line[n] == ' ' {
		n++
	}
	return n
}
