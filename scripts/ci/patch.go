// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"bufio"
	"bytes"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path"
	"sort"
	"strconv"
	"strings"
)

const maxUncoveredListed = 200

type patchResult struct {
	covered   int
	total     int
	uncovered []string
	prefixes  []string
	threshold int
}

func patchMain(args []string) int {
	fs := flag.NewFlagSet("patch-coverage", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	lcovPath := fs.String("lcov", "", "lcov tracefile")
	diffPath := fs.String("diff", "", "unified diff file, instead of git")
	repo := fs.String("repo", "", "repository root (default: working directory)")
	base := fs.String("base", "", "git base revision")
	head := fs.String("head", "HEAD", "git head revision")
	threshold := fs.Int("threshold", 80, "minimum percent of changed executable lines")
	summary := fs.String("summary", "", "markdown file to append (default: GITHUB_STEP_SUMMARY)")
	var prefixes []string
	fs.Func("prefix", "path prefix to count (repeatable; default src)", func(s string) error {
		if strings.TrimSpace(s) == "" {
			return fmt.Errorf("empty prefix")
		}
		prefixes = append(prefixes, cleanRel(s))
		return nil
	})
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci patch-coverage --lcov FILE (--diff FILE | --base REV) [--head REV] [--prefix DIR] [--threshold N]")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	if fs.NArg() != 0 {
		fmt.Fprintln(os.Stderr, "patch-coverage: unexpected arguments")
		fs.Usage()
		return 1
	}
	if *lcovPath == "" {
		fmt.Fprintln(os.Stderr, "patch-coverage: --lcov is required")
		return 1
	}
	if *threshold < 0 || *threshold > 100 {
		fmt.Fprintln(os.Stderr, "patch-coverage: --threshold must be from 0 to 100")
		return 1
	}
	if (*diffPath == "") == (*base == "") {
		fmt.Fprintln(os.Stderr, "patch-coverage: pass exactly one of --diff or --base")
		return 1
	}
	if len(prefixes) == 0 {
		prefixes = []string{"src"}
	}
	root := *repo
	if root == "" {
		wd, err := os.Getwd()
		if err != nil {
			fmt.Fprintf(os.Stderr, "patch-coverage: %v\n", err)
			return 1
		}
		root = wd
	}
	diffText, err := loadDiff(*diffPath, root, *base, *head, prefixes)
	if err != nil {
		fmt.Fprintf(os.Stderr, "patch-coverage: %v\n", err)
		return 1
	}
	traceFile, err := os.Open(*lcovPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "patch-coverage: %v\n", err)
		return 1
	}
	defer traceFile.Close()
	result, err := evaluatePatch(diffText, traceFile, root, prefixes, *threshold)
	if err != nil {
		fmt.Fprintf(os.Stderr, "patch-coverage: %v\n", err)
		return 1
	}
	text := formatPatch(result)
	fmt.Print(text)
	if err := appendSummary(summaryDestination(*summary), text); err != nil {
		fmt.Fprintf(os.Stderr, "patch-coverage: %v\n", err)
		return 1
	}
	if !patchPasses(result) {
		fmt.Fprintf(os.Stderr, "ERROR: patch coverage %s < %d%%\n", formatPercent(result.covered, result.total), result.threshold)
		return 2
	}
	return 0
}

func loadDiff(diffPath, repo, base, head string, prefixes []string) (string, error) {
	if diffPath != "" {
		body, err := os.ReadFile(diffPath)
		if err != nil {
			return "", err
		}
		return string(body), nil
	}
	if !validRev(base) || !validRev(head) {
		return "", fmt.Errorf("invalid git revision")
	}
	args := []string{"diff", "--unified=0", "--no-color", "--no-ext-diff", base, head, "--"}
	args = append(args, prefixes...)
	cmd := exec.Command("git", args...)
	cmd.Dir = repo
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		msg := strings.TrimSpace(stderr.String())
		if msg == "" {
			return "", fmt.Errorf("git diff: %w", err)
		}
		return "", fmt.Errorf("git diff: %w: %s", err, msg)
	}
	return stdout.String(), nil
}

func validRev(s string) bool {
	if s == "" || strings.HasPrefix(s, "-") || len(s) > 128 {
		return false
	}
	for _, r := range s {
		switch {
		case r >= 'a' && r <= 'z':
		case r >= 'A' && r <= 'Z':
		case r >= '0' && r <= '9':
		case strings.ContainsRune("._~^/@", r):
		default:
			return false
		}
	}
	return true
}

func evaluatePatch(diffText string, trace io.Reader, repo string, prefixes []string, threshold int) (patchResult, error) {
	changes, err := parseDiff(diffText)
	if err != nil {
		return patchResult{}, err
	}
	records, err := parseLCov(trace)
	if err != nil {
		return patchResult{}, err
	}
	indexed := indexLCov(records, repo, prefixes)
	paths := make([]string, 0, len(changes))
	for p := range changes {
		paths = append(paths, p)
	}
	sort.Strings(paths)
	result := patchResult{prefixes: prefixes, threshold: threshold}
	for _, file := range paths {
		if !hasPrefix(file, prefixes) {
			continue
		}
		hitsByLine := indexed[file]
		for _, line := range uniqueSorted(changes[file]) {
			hits, ok := hitsByLine[line]
			// Lines the trace does not mention are not executable in the
			// measured set: comments, declarations, and files the floor removes.
			if !ok {
				continue
			}
			result.total++
			if hits > 0 {
				result.covered++
			} else {
				result.uncovered = append(result.uncovered, fmt.Sprintf("%s:%d", file, line))
			}
		}
	}
	return result, nil
}

// Cross-multiply so an exact ratio passes: 4/5 is 80%, 3/5 is not.
func patchPasses(r patchResult) bool {
	if r.total == 0 {
		return true
	}
	return int64(r.covered)*100 >= int64(r.threshold)*int64(r.total)
}

func formatPatch(r patchResult) string {
	var b strings.Builder
	b.WriteString("### Patch coverage\n\n")
	where := prefixPhrase(r.prefixes)
	if r.total == 0 {
		fmt.Fprintf(&b, "No changed executable lines under `%s`. Patch coverage passed.\n", where)
		return b.String()
	}
	fmt.Fprintf(&b, "%s of changed executable lines under `%s` are covered (%d/%d). Required >= %d%%.\n",
		formatPercent(r.covered, r.total), where, r.covered, r.total, r.threshold)
	if len(r.uncovered) == 0 {
		return b.String()
	}
	b.WriteString("\nUncovered changed lines:\n\n```\n")
	limit := len(r.uncovered)
	if limit > maxUncoveredListed {
		limit = maxUncoveredListed
	}
	for _, line := range r.uncovered[:limit] {
		b.WriteString(line)
		b.WriteByte('\n')
	}
	if len(r.uncovered) > limit {
		fmt.Fprintf(&b, "... and %d more\n", len(r.uncovered)-limit)
	}
	b.WriteString("```\n")
	return b.String()
}

func formatPercent(covered, total int) string {
	if total == 0 {
		return "n/a"
	}
	tenths := int64(covered) * 1000 / int64(total)
	return fmt.Sprintf("%d.%d%%", tenths/10, tenths%10)
}

func prefixPhrase(prefixes []string) string {
	if len(prefixes) == 1 {
		return prefixes[0] + "/"
	}
	parts := make([]string, len(prefixes))
	for i, pre := range prefixes {
		parts[i] = pre + "/"
	}
	return strings.Join(parts, ", ")
}

func parseLCov(r io.Reader) (map[string]map[int]int, error) {
	sc := bufio.NewScanner(r)
	sc.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	records := map[string]map[int]int{}
	var current map[int]int
	sf := ""
	for sc.Scan() {
		line := strings.TrimRight(sc.Text(), "\r")
		switch {
		case strings.HasPrefix(line, "SF:"):
			sf = strings.TrimSpace(strings.TrimPrefix(line, "SF:"))
			current = map[int]int{}
		case strings.HasPrefix(line, "DA:") && current != nil:
			lineNo, hits, ok := parseDA(line)
			if !ok {
				return nil, fmt.Errorf("malformed lcov record %q", line)
			}
			// A zero hit must be stored. The map's zero value is also 0, so a
			// greater-than update would drop the line and treat it as non-executable.
			if prev, exists := current[lineNo]; !exists || hits > prev {
				current[lineNo] = hits
			}
		case line == "end_of_record":
			if sf != "" && current != nil {
				dst := records[sf]
				if dst == nil {
					dst = map[int]int{}
					records[sf] = dst
				}
				for lineNo, hits := range current {
					if prev, exists := dst[lineNo]; !exists || hits > prev {
						dst[lineNo] = hits
					}
				}
			}
			sf = ""
			current = nil
		}
	}
	if err := sc.Err(); err != nil {
		return nil, err
	}
	return records, nil
}

func parseDA(line string) (int, int, bool) {
	parts := strings.Split(strings.TrimPrefix(line, "DA:"), ",")
	if len(parts) < 2 {
		return 0, 0, false
	}
	lineNo, err := strconv.Atoi(strings.TrimSpace(parts[0]))
	if err != nil || lineNo <= 0 {
		return 0, 0, false
	}
	hits, err := strconv.ParseInt(strings.TrimSpace(parts[1]), 10, 64)
	if err != nil {
		return 0, 0, false
	}
	if hits > int64(^uint(0)>>1) {
		hits = int64(^uint(0) >> 1)
	}
	return lineNo, int(hits), true
}

func indexLCov(records map[string]map[int]int, repo string, prefixes []string) map[string]map[int]int {
	out := map[string]map[int]int{}
	for sf, lines := range records {
		rel := toRepoPath(repo, sf, prefixes)
		dst := out[rel]
		if dst == nil {
			dst = map[int]int{}
			out[rel] = dst
		}
		for lineNo, hits := range lines {
			if prev, exists := dst[lineNo]; !exists || hits > prev {
				dst[lineNo] = hits
			}
		}
	}
	return out
}

func toRepoPath(repo, sf string, prefixes []string) string {
	sf = cleanSlash(sf)
	repo = cleanSlash(repo)
	if repo != "" && repo != "." && strings.HasPrefix(sf, repo+"/") {
		return strings.TrimPrefix(sf, repo+"/")
	}
	for _, pre := range prefixes {
		pre = cleanRel(pre)
		if sf == pre || strings.HasPrefix(sf, pre+"/") {
			return sf
		}
		token := "/" + pre + "/"
		if idx := strings.LastIndex(sf, token); idx >= 0 {
			return sf[idx+1:]
		}
	}
	return sf
}

func parseDiff(diff string) (map[string][]int, error) {
	out := map[string][]int{}
	pathName := ""
	inHunk := false
	newLine := 0
	for _, raw := range strings.Split(diff, "\n") {
		line := strings.TrimRight(raw, "\r")
		if strings.HasPrefix(line, "diff --git ") {
			parsed, ok := gitBPath(line)
			if !ok {
				return nil, fmt.Errorf("malformed diff header %q", line)
			}
			pathName = cleanRel(parsed)
			inHunk = false
			continue
		}
		if pathName == "" {
			continue
		}
		if strings.HasPrefix(line, "@@ ") {
			start, ok := hunkNewStart(line)
			if !ok {
				return nil, fmt.Errorf("malformed hunk %q", line)
			}
			newLine = start
			inHunk = true
			continue
		}
		if !inHunk || line == "" {
			continue
		}
		if strings.HasPrefix(line, `\`) {
			continue
		}
		switch line[0] {
		case '+':
			out[pathName] = append(out[pathName], newLine)
			newLine++
		case '-':
		case ' ':
			newLine++
		default:
			return nil, fmt.Errorf("malformed diff line %q", line)
		}
	}
	return out, nil
}

func gitBPath(header string) (string, bool) {
	rest := strings.TrimSpace(strings.TrimPrefix(header, "diff --git "))
	_, b, ok := splitGitPair(rest)
	if !ok {
		return "", false
	}
	if strings.HasPrefix(b, "b/") {
		b = b[2:]
	}
	return b, b != ""
}

func splitGitPair(rest string) (string, string, bool) {
	if rest == "" {
		return "", "", false
	}
	if rest[0] == '"' {
		a, used, ok := unquoteGit(rest)
		if !ok {
			return "", "", false
		}
		b, _, ok := unquoteGit(strings.TrimSpace(rest[used:]))
		if !ok {
			return "", "", false
		}
		return a, b, true
	}
	a, b, ok := strings.Cut(rest, " ")
	if !ok || a == "" || b == "" || strings.Contains(b, " ") {
		return "", "", false
	}
	return a, b, true
}

func unquoteGit(s string) (string, int, bool) {
	if len(s) < 2 || s[0] != '"' {
		return "", 0, false
	}
	var b strings.Builder
	for i := 1; i < len(s); i++ {
		switch s[i] {
		case '\\':
			if i+1 >= len(s) {
				return "", 0, false
			}
			i++
			switch s[i] {
			case 'n':
				b.WriteByte('\n')
			case 't':
				b.WriteByte('\t')
			case '\\', '"':
				b.WriteByte(s[i])
			default:
				b.WriteByte(s[i])
			}
		case '"':
			return b.String(), i + 1, true
		default:
			b.WriteByte(s[i])
		}
	}
	return "", 0, false
}

func hunkNewStart(line string) (int, bool) {
	// @@ -oldStart[,oldCount] +newStart[,newCount] @@
	const marker = " +"
	plus := strings.Index(line, marker)
	if plus < 0 {
		return 0, false
	}
	rest := line[plus+len(marker):]
	end := strings.IndexByte(rest, ' ')
	if end < 0 {
		end = len(rest)
	}
	spec := rest[:end]
	count := spec
	if comma := strings.IndexByte(spec, ','); comma >= 0 {
		count = spec[:comma]
	}
	n, err := strconv.Atoi(count)
	if err != nil || n < 0 {
		return 0, false
	}
	return n, true
}

func hasPrefix(file string, prefixes []string) bool {
	for _, pre := range prefixes {
		if file == pre || strings.HasPrefix(file, pre+"/") {
			return true
		}
	}
	return false
}

func uniqueSorted(lines []int) []int {
	if len(lines) == 0 {
		return nil
	}
	sort.Ints(lines)
	out := lines[:1]
	for _, line := range lines[1:] {
		if line != out[len(out)-1] {
			out = append(out, line)
		}
	}
	return out
}

func cleanRel(p string) string {
	p = cleanSlash(p)
	p = strings.TrimPrefix(p, "./")
	return p
}

func cleanSlash(p string) string {
	p = strings.ReplaceAll(strings.TrimSpace(p), "\\", "/")
	if p == "" {
		return ""
	}
	return path.Clean(p)
}
