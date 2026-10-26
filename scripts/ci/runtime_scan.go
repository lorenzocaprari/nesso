// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"flag"
	"fmt"
	"os"
	"strings"
)

// runtimeLayerCopies maps a final-stage COPY --from=build source onto the
// context path and the path inside the release .deb. package-deb installs the
// binary at /usr/bin/nesso, the model files under /usr/share/nesso, and the
// copyright and third-party notices under /usr/share/doc/nesso.
// Dockerfile.runtime copies the same files out of the unpublished build stage.
var runtimeLayerCopies = []scanCopySpec{
	{from: "/opt/nesso/bin/nesso", kind: "file", context: "nesso", deb: "usr/bin/nesso"},
	{from: "/opt/nesso/models", kind: "dir", context: "models", deb: "usr/share/nesso"},
	{from: "/opt/nesso/doc", kind: "dir", context: "doc", deb: "usr/share/doc/nesso"},
}

type scanCopySpec struct {
	from    string
	kind    string
	context string
	deb     string
}

type scanCopy struct {
	kind    string
	context string
	deb     string
}

func runtimeScanMain(args []string) int {
	fs := flag.NewFlagSet("runtime-scan-dockerfile", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	runtime := fs.String("runtime", "", "path to Dockerfile.runtime")
	out := fs.String("out", "", "scan Dockerfile to write")
	plan := fs.String("plan", "", "copy plan to write")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci runtime-scan-dockerfile --runtime FILE --out FILE --plan FILE")
		fs.PrintDefaults()
	}
	if err := fs.Parse(args); err != nil {
		return 1
	}
	if *runtime == "" || *out == "" || *plan == "" || fs.NArg() != 0 {
		fs.Usage()
		return 1
	}
	src, err := os.ReadFile(*runtime)
	if err != nil {
		fmt.Fprintf(os.Stderr, "runtime-scan-dockerfile: %v\n", err)
		return 1
	}
	dockerfile, copies, err := runtimeScanDockerfile(string(src))
	if err != nil {
		fmt.Fprintf(os.Stderr, "runtime-scan-dockerfile: %v\n", err)
		return 1
	}
	if err := os.WriteFile(*out, []byte(dockerfile), 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "runtime-scan-dockerfile: %v\n", err)
		return 1
	}
	if err := os.WriteFile(*plan, []byte(formatScanPlan(copies)), 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "runtime-scan-dockerfile: %v\n", err)
		return 1
	}
	return 0
}

func formatScanPlan(copies []scanCopy) string {
	var b strings.Builder
	for _, copy := range copies {
		fmt.Fprintf(&b, "%s\t%s\t%s\n", copy.kind, copy.context, copy.deb)
	}
	return b.String()
}

func runtimeScanDockerfile(src string) (string, []scanCopy, error) {
	lines, err := dockerfileInstructions(src)
	if err != nil {
		return "", nil, err
	}
	stages := splitDockerfileStages(lines)
	if len(stages) == 0 {
		return "", nil, fmt.Errorf("Dockerfile has no FROM")
	}
	final := stages[len(stages)-1]
	var b strings.Builder
	var copies []scanCopy
	seen := map[string]bool{}
	for _, inst := range final {
		word, _, _ := strings.Cut(inst, " ")
		word = strings.ToUpper(word)
		switch word {
		case "FROM", "RUN", "ENV", "ENTRYPOINT":
			b.WriteString(inst)
			b.WriteByte('\n')
		case "COPY":
			copy, err := runtimeScanCopy(inst)
			if err != nil {
				return "", nil, err
			}
			if seen[copy.deb] {
				return "", nil, fmt.Errorf("duplicate COPY for %s", copy.deb)
			}
			seen[copy.deb] = true
			copies = append(copies, copy.scanCopy)
			fmt.Fprintf(&b, "COPY %s %s\n", copy.context, copy.dest)
		default:
			return "", nil, fmt.Errorf("unsupported final-stage instruction %q", word)
		}
	}
	if len(copies) != len(runtimeLayerCopies) {
		return "", nil, fmt.Errorf("final stage must copy the nesso binary, the model directory, and the doc directory")
	}
	return b.String(), copies, nil
}

type parsedCopy struct {
	scanCopy
	dest string
}

func runtimeScanCopy(inst string) (parsedCopy, error) {
	flat := strings.Join(strings.Fields(strings.ReplaceAll(inst, "\\\n", " ")), " ")
	fields := strings.Fields(flat)
	if len(fields) != 4 || fields[1] != "--from=build" {
		return parsedCopy{}, fmt.Errorf("unsupported COPY %q", flat)
	}
	for _, spec := range runtimeLayerCopies {
		if fields[2] == spec.from {
			return parsedCopy{
				scanCopy: scanCopy{kind: spec.kind, context: spec.context, deb: spec.deb},
				dest:     fields[3],
			}, nil
		}
	}
	return parsedCopy{}, fmt.Errorf("unexpected build-stage source %s", fields[2])
}

func splitDockerfileStages(lines []string) [][]string {
	var stages [][]string
	var cur []string
	for _, line := range lines {
		word, _, _ := strings.Cut(line, " ")
		if strings.EqualFold(word, "FROM") && len(cur) > 0 {
			stages = append(stages, cur)
			cur = nil
		}
		cur = append(cur, line)
	}
	if len(cur) > 0 {
		stages = append(stages, cur)
	}
	return stages
}

func dockerfileInstructions(src string) ([]string, error) {
	var lines []string
	var b strings.Builder
	continued := false
	for _, raw := range strings.Split(src, "\n") {
		raw = strings.TrimRight(raw, "\r")
		if !continued {
			trim := strings.TrimSpace(raw)
			if trim == "" || strings.HasPrefix(trim, "#") {
				continue
			}
		}
		if b.Len() > 0 {
			b.WriteByte('\n')
		}
		b.WriteString(raw)
		if strings.HasSuffix(strings.TrimRight(raw, " \t"), "\\") {
			continued = true
			continue
		}
		lines = append(lines, b.String())
		b.Reset()
		continued = false
	}
	if continued {
		return nil, fmt.Errorf("Dockerfile instruction continues past EOF")
	}
	return lines, nil
}
