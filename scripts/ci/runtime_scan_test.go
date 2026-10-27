// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRuntimeScanDockerfileUsesFinalStageOnly(t *testing.T) {
	src, err := os.ReadFile(filepath.Join("..", "..", "packaging", "Dockerfile.runtime"))
	if err != nil {
		t.Fatal(err)
	}
	got, copies, err := runtimeScanDockerfile(string(src))
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(got, "BUILD_IMAGE") || strings.Contains(got, "--from=build") {
		t.Fatalf("build stage leaked into scan dockerfile:\n%s", got)
	}
	// The final FROM is whatever packaging/Dockerfile.runtime pins, digest
	// included. A base-image bump changes that pin; the scan keeps the final
	// stage line and drops the build stage.
	fromLine := finalStageFrom(t, string(src))
	gotFrom, _, _ := strings.Cut(got, "\n")
	if gotFrom != fromLine || strings.Count(got, "FROM ") != 1 {
		t.Fatalf("scan FROM = %q, final stage FROM = %q\n%s", gotFrom, fromLine, got)
	}
	for _, needle := range []string{
		"apt-get install -y --no-install-recommends ca-certificates libstdc++6 libgomp1",
		"COPY nesso /usr/local/bin/nesso\n",
		"COPY models /usr/share/nesso\n",
		"COPY doc /usr/share/doc/nesso\n",
		"ENV NESSO_MODEL_DIR=/usr/share/nesso\n",
		"ENTRYPOINT [\"nesso\"]\n",
	} {
		if !strings.Contains(got, needle) {
			t.Fatalf("missing %q in\n%s", needle, got)
		}
	}
	if len(copies) != 3 {
		t.Fatalf("copies = %+v", copies)
	}
	if copies[0] != (scanCopy{kind: "file", context: "nesso", deb: "usr/bin/nesso"}) {
		t.Fatalf("binary copy = %+v", copies[0])
	}
	if copies[1] != (scanCopy{kind: "dir", context: "models", deb: "usr/share/nesso"}) {
		t.Fatalf("model copy = %+v", copies[1])
	}
	if copies[2] != (scanCopy{kind: "dir", context: "doc", deb: "usr/share/doc/nesso"}) {
		t.Fatalf("doc copy = %+v", copies[2])
	}
	plan := formatScanPlan(copies)
	if plan != "file\tnesso\tusr/bin/nesso\ndir\tmodels\tusr/share/nesso\ndir\tdoc\tusr/share/doc/nesso\n" {
		t.Fatalf("plan = %q", plan)
	}
}

func TestRuntimeScanDockerfilePreservesFinalFrom(t *testing.T) {
	digest := "sha256:" + strings.Repeat("cd", 32)
	src := "" +
		"ARG BUILD_IMAGE\n" +
		"FROM ${BUILD_IMAGE} AS build\n" +
		"RUN echo build\n" +
		"FROM ubuntu:26.04@" + digest + "\n" +
		"RUN apt-get install -y --no-install-recommends ca-certificates libstdc++6 libgomp1\n" +
		"COPY --from=build /opt/nesso/bin/nesso /usr/local/bin/nesso\n" +
		"COPY --from=build /opt/nesso/models /usr/share/nesso\n" +
		"COPY --from=build /opt/nesso/doc /usr/share/doc/nesso\n" +
		"ENV NESSO_MODEL_DIR=/usr/share/nesso\n" +
		"ENTRYPOINT [\"nesso\"]\n"
	got, _, err := runtimeScanDockerfile(src)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(got, "BUILD_IMAGE") || strings.Contains(got, "--from=build") || strings.Contains(got, "AS build") {
		t.Fatalf("build stage leaked:\n%s", got)
	}
	if strings.Count(got, "FROM ") != 1 || !strings.Contains(got, "FROM ubuntu:26.04@"+digest+"\n") {
		t.Fatalf("final FROM not preserved:\n%s", got)
	}
}

func finalStageFrom(t *testing.T, src string) string {
	t.Helper()
	lines, err := dockerfileInstructions(src)
	if err != nil {
		t.Fatal(err)
	}
	stages := splitDockerfileStages(lines)
	if len(stages) == 0 || len(stages[len(stages)-1]) == 0 {
		t.Fatal("dockerfile has no final stage")
	}
	from := stages[len(stages)-1][0]
	if !strings.HasPrefix(strings.ToUpper(from), "FROM ") {
		t.Fatalf("final stage starts with %q", from)
	}
	return from
}

func TestRuntimeScanDockerfileRejectsDrift(t *testing.T) {
	base := "" +
		"FROM ubuntu:26.04@sha256:abc\n" +
		"RUN apt-get update\n" +
		"COPY --from=build /opt/nesso/bin/nesso /usr/local/bin/nesso\n" +
		"COPY --from=build /opt/nesso/models /usr/share/nesso\n" +
		"COPY --from=build /opt/nesso/doc /usr/share/doc/nesso\n"
	cases := []string{
		base + "USER nobody\n",
		base + "COPY extra /opt/extra\n",
		base + "COPY --from=build /opt/nesso/extra /opt/extra\n",
		base + "COPY --from=build /opt/nesso/doc /usr/share/doc/other\n",
		strings.Replace(base, "COPY --from=build /opt/nesso/doc /usr/share/doc/nesso\n", "", 1),
		"FROM ubuntu:26.04\nRUN true\n",
		"RUN true\n",
	}
	for _, src := range cases {
		if _, _, err := runtimeScanDockerfile(src); err == nil {
			t.Fatalf("accepted drifted dockerfile:\n%s", src)
		}
	}
}

func TestRuntimeCopyPathsMatchPackageDeb(t *testing.T) {
	body, err := os.ReadFile(filepath.Join("..", "package-deb"))
	if err != nil {
		t.Fatal(err)
	}
	text := string(body)
	if !strings.Contains(text, "/usr/bin/nesso") || !strings.Contains(text, "/usr/share/nesso/") ||
		!strings.Contains(text, "usr/share/doc/nesso") {
		t.Fatalf("package-deb install paths drifted from the scan plan")
	}
}
