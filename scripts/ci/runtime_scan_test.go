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
	src, err := os.ReadFile(filepath.Join("..", "..", "Dockerfile.runtime"))
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
	for _, needle := range []string{
		"FROM ubuntu:26.04@sha256:f144425ff09be612d6d9ad965196e9cdc23dae1f42110a8a11a3e9a8198759f7\n",
		"apt-get install -y --no-install-recommends ca-certificates libstdc++6 libgomp1",
		"COPY nesso /usr/local/bin/nesso\n",
		"COPY models /usr/share/nesso\n",
		"ENV NESSO_MODEL_DIR=/usr/share/nesso\n",
		"ENTRYPOINT [\"nesso\"]\n",
	} {
		if !strings.Contains(got, needle) {
			t.Fatalf("missing %q in\n%s", needle, got)
		}
	}
	if len(copies) != 2 {
		t.Fatalf("copies = %+v", copies)
	}
	if copies[0] != (scanCopy{kind: "file", context: "nesso", deb: "usr/bin/nesso"}) {
		t.Fatalf("binary copy = %+v", copies[0])
	}
	if copies[1] != (scanCopy{kind: "dir", context: "models", deb: "usr/share/nesso"}) {
		t.Fatalf("model copy = %+v", copies[1])
	}
	plan := formatScanPlan(copies)
	if plan != "file\tnesso\tusr/bin/nesso\ndir\tmodels\tusr/share/nesso\n" {
		t.Fatalf("plan = %q", plan)
	}
}

func TestRuntimeScanDockerfileRejectsDrift(t *testing.T) {
	base := "" +
		"FROM ubuntu:26.04@sha256:abc\n" +
		"RUN apt-get update\n" +
		"COPY --from=build /opt/nesso/bin/nesso /usr/local/bin/nesso\n" +
		"COPY --from=build /opt/nesso/models /usr/share/nesso\n"
	cases := []string{
		base + "USER nobody\n",
		base + "COPY extra /opt/extra\n",
		base + "COPY --from=build /opt/nesso/extra /opt/extra\n",
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
	if !strings.Contains(text, "/usr/bin/nesso") || !strings.Contains(text, "/usr/share/nesso/") {
		t.Fatalf("package-deb install paths drifted from the scan plan")
	}
}
