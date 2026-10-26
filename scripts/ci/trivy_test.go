// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestTrivySummaryCountsAndReports(t *testing.T) {
	raw := []byte(`{
	  "Results": [
	    {
	      "Vulnerabilities": [
	        {"VulnerabilityID": "CVE-1", "PkgName": "openssl", "Severity": "HIGH", "FixedVersion": "2"},
	        {"VulnerabilityID": "CVE-2", "PkgName": "pebble", "Severity": "critical", "FixedVersion": ""},
	        {"VulnerabilityID": "CVE-3", "PkgName": "libc", "Severity": "LOW", "FixedVersion": "9"},
	        {"VulnerabilityID": "CVE-4", "PkgName": "libssl", "Severity": "HIGH"}
	      ]
	    },
	    {
	      "Vulnerabilities": [
	        {"VulnerabilityID": "CVE-5", "PkgName": "stdlib", "Severity": "HIGH", "FixedVersion": "1.2"}
	      ]
	    }
	  ]
	}`)
	text, fixes, err := formatTrivy(raw, "amd64", false)
	if err != nil {
		t.Fatal(err)
	}
	for _, needle := range []string{
		"### Trivy runtime image (amd64)",
		"| CRITICAL | 1 | 0 |",
		"| HIGH | 3 | 2 |",
		"| MEDIUM | 0 | 0 |",
		"| LOW | 1 | 1 |",
		"| UNKNOWN | 0 | 0 |",
		"- HIGH CVE-1 openssl fixed 2",
		"- HIGH CVE-5 stdlib fixed 1.2",
		"does not fail on them",
	} {
		if !strings.Contains(text, needle) {
			t.Fatalf("missing %q in\n%s", needle, text)
		}
	}
	if len(fixes) != 2 {
		t.Fatalf("fixes = %+v", fixes)
	}
	if strings.Contains(text, "CVE-2") || strings.Contains(text, "CVE-4") {
		t.Fatalf("unfixed high/critical listed:\n%s", text)
	}
}

func TestTrivySummaryFailFlag(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "trivy.json")
	body := `{"Results":[{"Vulnerabilities":[{"VulnerabilityID":"CVE-9","PkgName":"a","Severity":"CRITICAL","FixedVersion":"1"}]}]}`
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	summary := filepath.Join(dir, "summary.md")
	if code := trivyMain([]string{"--json", path, "--arch", "arm64", "--summary", summary, "--fail-fixed-high-critical"}); code != 1 {
		t.Fatalf("exit = %d", code)
	}
	written, err := os.ReadFile(summary)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(written), "fails when that fixable") {
		t.Fatalf("summary = %s", written)
	}
	if code := trivyMain([]string{"--json", path, "--arch", "arm64"}); code != 0 {
		t.Fatalf("report-only exit = %d", code)
	}
}

func TestTrivySummaryRejectsBadInput(t *testing.T) {
	if _, _, err := formatTrivy([]byte("{"), "", false); err == nil {
		t.Fatal("accepted invalid json")
	}
	if code := trivyMain(nil); code != 1 {
		t.Fatalf("missing json exit = %d", code)
	}
	if code := trivyMain([]string{"--json", "nope", "--arch", "bad arch"}); code != 1 {
		t.Fatalf("bad arch exit = %d", code)
	}
}

func TestTrivyDistributionPins(t *testing.T) {
	trivy, err := os.ReadFile(filepath.Join("..", "trivy"))
	if err != nil {
		t.Fatal(err)
	}
	scan, err := os.ReadFile(filepath.Join("..", "scan-runtime-image"))
	if err != nil {
		t.Fatal(err)
	}
	text := stripBashComments(string(trivy) + "\n" + string(scan))
	for _, needle := range []string{
		"0.76.1",
		"c7cbe3137fb8a48a17574bede3dd6beb16742d1e9772929940fc90082c39d9c0",
		"e174dff2a6dba5e3ae41f8d0537a6324405bd08132497340304c6b20e0f16a1f",
		"--db-repository ghcr.io/aquasecurity/trivy-db:2",
		"--skip-java-db-update",
		"--pkg-types \"os,library\"",
	} {
		if !strings.Contains(text, needle) {
			t.Fatalf("missing pin %s", needle)
		}
	}
	for _, banned := range []string{
		"trivy-action",
		"setup-trivy",
		"get.trivy.dev",
		"mirror.gcr.io",
		"aquasec/trivy",
	} {
		if strings.Contains(text, banned) {
			t.Fatalf("scanner scripts reference %s", banned)
		}
	}
}

func stripBashComments(src string) string {
	var b strings.Builder
	for _, line := range strings.Split(src, "\n") {
		if i := strings.Index(line, "#"); i >= 0 {
			line = line[:i]
		}
		b.WriteString(line)
		b.WriteByte('\n')
	}
	return b.String()
}
