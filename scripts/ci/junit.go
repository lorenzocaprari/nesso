// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"encoding/xml"
	"errors"
	"flag"
	"fmt"
	"os"
	"strings"
)

type suiteSpec struct {
	name string
	path string
}

type xmlFailure struct {
	Message string `xml:"message,attr"`
	Body    string `xml:",chardata"`
}

type xmlCase struct {
	Name      string       `xml:"name,attr"`
	Classname string       `xml:"classname,attr"`
	Time      float64      `xml:"time,attr"`
	Failures  []xmlFailure `xml:"failure"`
	Errors    []xmlFailure `xml:"error"`
	SystemOut string       `xml:"system-out"`
	SystemErr string       `xml:"system-err"`
}

type xmlSuite struct {
	Tests    int       `xml:"tests,attr"`
	Failures int       `xml:"failures,attr"`
	Errors   int       `xml:"errors,attr"`
	Skipped  int       `xml:"skipped,attr"`
	Disabled int       `xml:"disabled,attr"`
	Cases    []xmlCase `xml:"testcase"`
}

type failureView struct {
	suite   string
	name    string
	message string
}

func junitMain(args []string) int {
	fs := flag.NewFlagSet("junit-summary", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	summary := fs.String("summary", "", "markdown file to append (default: GITHUB_STEP_SUMMARY)")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci junit-summary [--summary FILE] name=junit.xml ...")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	if fs.NArg() == 0 {
		fs.Usage()
		return 1
	}
	specs := make([]suiteSpec, 0, fs.NArg())
	for _, arg := range fs.Args() {
		name, path, ok := strings.Cut(arg, "=")
		if !ok || name == "" || path == "" {
			fmt.Fprintf(os.Stderr, "junit-summary: expected name=path, got %q\n", arg)
			return 1
		}
		if !validSuiteName(name) {
			fmt.Fprintf(os.Stderr, "junit-summary: invalid suite name %q\n", name)
			return 1
		}
		specs = append(specs, suiteSpec{name: name, path: path})
	}
	text, err := formatJUnit(specs)
	if err != nil {
		fmt.Fprintf(os.Stderr, "junit-summary: %v\n", err)
		return 1
	}
	fmt.Print(text)
	if err := appendSummary(summaryDestination(*summary), text); err != nil {
		fmt.Fprintf(os.Stderr, "junit-summary: %v\n", err)
		return 1
	}
	return 0
}

func validSuiteName(name string) bool {
	if name == "" || len(name) > 64 {
		return false
	}
	for _, r := range name {
		switch {
		case r >= 'a' && r <= 'z':
		case r >= 'A' && r <= 'Z':
		case r >= '0' && r <= '9':
		case r == '_' || r == '-' || r == '.':
		default:
			return false
		}
	}
	return true
}

func formatJUnit(specs []suiteSpec) (string, error) {
	var b strings.Builder
	b.WriteString("### JUnit\n\n")
	b.WriteString("| suite | tests | failed | skipped | seconds |\n")
	b.WriteString("| --- | --- | --- | --- | --- |\n")
	var failures []failureView
	present := 0
	for _, spec := range specs {
		suite, found, err := readSuite(spec.path)
		if err != nil {
			return "", fmt.Errorf("%s: %w", spec.path, err)
		}
		if !found {
			b.WriteString("| " + spec.name + " | — | — | — | not run |\n")
			continue
		}
		present++
		tests, failed, skipped, seconds := suiteCounts(suite)
		fmt.Fprintf(&b, "| %s | %d | %d | %d | %.2f |\n", spec.name, tests, failed, skipped, seconds)
		failures = append(failures, suiteFailures(spec.name, suite)...)
	}
	b.WriteByte('\n')
	if len(failures) == 0 {
		if present == 0 {
			b.WriteString("No JUnit reports were written.\n")
		} else {
			b.WriteString("No failing tests.\n")
		}
		return b.String(), nil
	}
	b.WriteString("#### Failures\n\n")
	for _, fail := range failures {
		fmt.Fprintf(&b, "**%s / %s**\n\n", specInline(fail.suite), specInline(fail.name))
		b.WriteString(fence(clip(fail.message)))
		b.WriteString("\n\n")
	}
	return b.String(), nil
}

func readSuite(path string) (xmlSuite, bool, error) {
	body, err := os.ReadFile(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return xmlSuite{}, false, nil
		}
		return xmlSuite{}, false, err
	}
	suite, err := parseJUnit(body)
	if err != nil {
		return xmlSuite{}, false, err
	}
	return suite, true, nil
}

func parseJUnit(body []byte) (xmlSuite, error) {
	decoder := xml.NewDecoder(strings.NewReader(string(body)))
	for {
		tok, err := decoder.Token()
		if err != nil {
			return xmlSuite{}, fmt.Errorf("xml: %w", err)
		}
		start, ok := tok.(xml.StartElement)
		if !ok {
			continue
		}
		switch start.Name.Local {
		case "testsuites":
			var wrap struct {
				Suites []xmlSuite `xml:"testsuite"`
			}
			if err := xml.Unmarshal(body, &wrap); err != nil {
				return xmlSuite{}, err
			}
			return mergeSuites(wrap.Suites), nil
		case "testsuite":
			var suite xmlSuite
			if err := xml.Unmarshal(body, &suite); err != nil {
				return xmlSuite{}, err
			}
			return suite, nil
		default:
			return xmlSuite{}, fmt.Errorf("unsupported junit root <%s>", start.Name.Local)
		}
	}
}

func mergeSuites(suites []xmlSuite) xmlSuite {
	var out xmlSuite
	for _, suite := range suites {
		out.Tests += suite.Tests
		out.Failures += suite.Failures
		out.Errors += suite.Errors
		out.Skipped += suite.Skipped
		out.Disabled += suite.Disabled
		out.Cases = append(out.Cases, suite.Cases...)
	}
	return out
}

func suiteCounts(suite xmlSuite) (tests, failed, skipped int, seconds float64) {
	tests = suite.Tests
	if tests < len(suite.Cases) {
		tests = len(suite.Cases)
	}
	failed = suite.Failures + suite.Errors
	counted := 0
	for _, tc := range suite.Cases {
		seconds += tc.Time
		if len(tc.Failures)+len(tc.Errors) > 0 {
			counted++
		}
	}
	if counted > failed {
		failed = counted
	}
	skipped = suite.Skipped + suite.Disabled
	return tests, failed, skipped, seconds
}

func suiteFailures(suiteName string, suite xmlSuite) []failureView {
	var out []failureView
	for _, tc := range suite.Cases {
		if len(tc.Failures)+len(tc.Errors) == 0 {
			continue
		}
		name := tc.Name
		if name == "" {
			name = tc.Classname
		}
		out = append(out, failureView{
			suite:   suiteName,
			name:    name,
			message: failureText(tc),
		})
	}
	return out
}

// ctest records the assertion text in system-out. The failure element is
// often just message="Failed".
func failureText(tc xmlCase) string {
	var parts []string
	add := func(text string) {
		text = strings.TrimSpace(text)
		if text == "" || text == "Failed" {
			return
		}
		for _, existing := range parts {
			if existing == text {
				return
			}
		}
		parts = append(parts, text)
	}
	for _, fail := range tc.Failures {
		add(fail.Body)
		add(fail.Message)
	}
	for _, fail := range tc.Errors {
		add(fail.Body)
		add(fail.Message)
	}
	if len(parts) == 0 {
		add(tc.SystemOut)
		add(tc.SystemErr)
	}
	if len(parts) == 0 {
		return "Failed"
	}
	return strings.Join(parts, "\n")
}

func specInline(s string) string {
	s = strings.ReplaceAll(s, "\n", " ")
	s = strings.ReplaceAll(s, "`", "'")
	return s
}

func clip(s string) string {
	s = strings.ReplaceAll(s, "\r\n", "\n")
	s = strings.TrimSpace(s)
	const maxLines = 30
	const maxChars = 2000
	lines := strings.Split(s, "\n")
	truncated := false
	if len(lines) > maxLines {
		lines = lines[:maxLines]
		truncated = true
	}
	s = strings.Join(lines, "\n")
	if len(s) > maxChars {
		s = s[:maxChars]
		truncated = true
	}
	if truncated {
		s += "\n..."
	}
	return s
}

func fence(s string) string {
	if strings.Contains(s, "```") {
		return "~~~\n" + s + "\n~~~"
	}
	return "```\n" + s + "\n```"
}
