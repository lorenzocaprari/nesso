// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package deb_test

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

func repoRoot(t *testing.T) string {
	t.Helper()
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("runtime.Caller failed")
	}
	return filepath.Dir(filepath.Dir(filepath.Dir(file)))
}

func envWithout(keys ...string) []string {
	drop := map[string]bool{}
	for _, key := range keys {
		drop[key] = true
	}
	var out []string
	for _, entry := range os.Environ() {
		key, _, _ := strings.Cut(entry, "=")
		if drop[key] {
			continue
		}
		out = append(out, entry)
	}
	return out
}

func compileHello(t *testing.T, dir string) string {
	t.Helper()
	if _, err := exec.LookPath("gcc"); err != nil {
		t.Skip("gcc is required")
	}
	if _, err := exec.LookPath("dpkg-deb"); err != nil {
		t.Skip("dpkg-deb is required")
	}
	bin := filepath.Join(dir, "hello")
	cmd := exec.Command("gcc", "-o", bin, "-x", "c", "-")
	cmd.Stdin = strings.NewReader("int main(void){return 0;}\n")
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("gcc: %v\n%s", err, out)
	}
	return bin
}

func writeModels(t *testing.T, dir string, body byte) string {
	t.Helper()
	models := filepath.Join(dir, "models")
	if err := os.MkdirAll(models, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(models, "vocab.txt"), []byte("token\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(models, "model.onnx"), []byte{body}, 0o644); err != nil {
		t.Fatal(err)
	}
	return models
}

func packageDeb(t *testing.T, epoch, binary, models string) []byte {
	t.Helper()
	outDir := t.TempDir()
	cmd := exec.Command(filepath.Join(repoRoot(t), "scripts", "package-deb"), "0.1.0", binary, models, outDir)
	cmd.Env = append(envWithout("SOURCE_DATE_EPOCH", "TZ", "LC_ALL"), "SOURCE_DATE_EPOCH="+epoch, "TZ=UTC", "LC_ALL=C")
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("package-deb: %v\n%s", err, out)
	}
	matches, err := filepath.Glob(filepath.Join(outDir, "nesso_0.1.0_*.deb"))
	if err != nil {
		t.Fatal(err)
	}
	if len(matches) != 1 {
		t.Fatalf("debs: %v\n%s", matches, out)
	}
	body, err := os.ReadFile(matches[0])
	if err != nil {
		t.Fatal(err)
	}
	return body
}

func writeELF(t *testing.T, path string, machine uint16) {
	t.Helper()
	buf := make([]byte, 64)
	copy(buf, []byte{0x7f, 'E', 'L', 'F', 2, 1, 1})
	buf[18] = byte(machine)
	buf[19] = byte(machine >> 8)
	if err := os.WriteFile(path, buf, 0o755); err != nil {
		t.Fatal(err)
	}
}

func stubObjdump(t *testing.T, dir string) string {
	t.Helper()
	bindir := filepath.Join(dir, "bin")
	if err := os.Mkdir(bindir, 0o755); err != nil {
		t.Fatal(err)
	}
	stub := filepath.Join(bindir, "objdump")
	body := "#!/bin/sh\nprintf '%s\\n' '000000 DF *UND* 000000 GLIBC_2.38 sym'\n"
	if err := os.WriteFile(stub, []byte(body), 0o755); err != nil {
		t.Fatal(err)
	}
	return bindir
}

func tarListing(t *testing.T, deb []byte, which string) string {
	t.Helper()
	dir := t.TempDir()
	path := filepath.Join(dir, "pkg.deb")
	if err := os.WriteFile(path, deb, 0o644); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command("dpkg-deb", which, path)
	tar := exec.Command("tar", "-tv", "--full-time")
	tar.Env = append(envWithout("TZ"), "TZ=UTC")
	var listing bytes.Buffer
	tar.Stdout = &listing
	pipe, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	tar.Stdin = pipe
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	tar.Stderr = &stderr
	if err := tar.Start(); err != nil {
		t.Fatal(err)
	}
	if err := cmd.Run(); err != nil {
		t.Fatalf("dpkg-deb %s: %v\n%s", which, err, stderr.String())
	}
	if err := tar.Wait(); err != nil {
		t.Fatalf("tar: %v\n%s", err, stderr.String())
	}
	return listing.String()
}

func TestPackageDebBytesFollowTheEpoch(t *testing.T) {
	dir := t.TempDir()
	binary := compileHello(t, dir)
	models := writeModels(t, dir, 'a')
	old := time.Unix(1000, 0)
	if err := os.Chtimes(filepath.Join(models, "model.onnx"), old, old); err != nil {
		t.Fatal(err)
	}

	first := packageDeb(t, "1700000000", binary, models)
	time.Sleep(1100 * time.Millisecond)
	now := time.Now()
	if err := os.Chtimes(filepath.Join(models, "vocab.txt"), now, now); err != nil {
		t.Fatal(err)
	}
	if err := os.Chtimes(binary, now, now); err != nil {
		t.Fatal(err)
	}
	wd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	other := t.TempDir()
	if err := os.Chdir(other); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = os.Chdir(wd) })
	second := packageDeb(t, "1700000000", binary, models)
	if !bytes.Equal(first, second) {
		t.Fatalf("debs differ under the same SOURCE_DATE_EPOCH (%d vs %d bytes)", len(first), len(second))
	}

	data := tarListing(t, first, "--fsys-tarfile")
	if !strings.Contains(data, "2023-11-14 22:13:20") {
		t.Fatalf("filesystem tar mtimes are not the epoch:\n%s", data)
	}
	if !strings.Contains(data, "root/root") {
		t.Fatalf("filesystem tar owner is not root:\n%s", data)
	}
	ctrl := tarListing(t, first, "--ctrl-tarfile")
	if !strings.Contains(ctrl, "2023-11-14 22:13:20") {
		t.Fatalf("control tar mtime is not the epoch:\n%s", ctrl)
	}

	otherEpoch := packageDeb(t, "1700000100", binary, models)
	if bytes.Equal(first, otherEpoch) {
		t.Fatal("different SOURCE_DATE_EPOCH values produced the same bytes")
	}
	changed := writeModels(t, t.TempDir(), 'b')
	if bytes.Equal(first, packageDeb(t, "1700000000", binary, changed)) {
		t.Fatal("a model byte change did not change the deb")
	}
}

func TestPackageDebRejectsABadEpoch(t *testing.T) {
	dir := t.TempDir()
	binary := compileHello(t, dir)
	models := writeModels(t, dir, 'a')
	cmd := exec.Command(filepath.Join(repoRoot(t), "scripts", "package-deb"), "0.1.0", binary, models, dir)
	cmd.Env = append(envWithout("SOURCE_DATE_EPOCH"), "SOURCE_DATE_EPOCH=yesterday")
	out, err := cmd.CombinedOutput()
	if err == nil {
		t.Fatal("expected package-deb to reject a non-numeric epoch")
	}
	if !strings.Contains(string(out), "SOURCE_DATE_EPOCH") {
		t.Fatalf("stderr: %s", out)
	}
}

func TestPackageDebArchitectureFollowsTheElfMachine(t *testing.T) {
	if _, err := exec.LookPath("dpkg-deb"); err != nil {
		t.Skip("dpkg-deb is required")
	}
	if _, err := exec.LookPath("od"); err != nil {
		t.Skip("od is required")
	}
	root := repoRoot(t)
	for _, tc := range []struct {
		machine uint16
		arch    string
	}{
		{62, "amd64"},
		{183, "arm64"},
	} {
		t.Run(tc.arch, func(t *testing.T) {
			dir := t.TempDir()
			binary := filepath.Join(dir, "nesso")
			writeELF(t, binary, tc.machine)
			models := writeModels(t, dir, 'a')
			outDir := filepath.Join(dir, "out")
			cmd := exec.Command(filepath.Join(root, "scripts", "package-deb"), "0.1.0", binary, models, outDir)
			cmd.Env = append(envWithout("PATH", "SOURCE_DATE_EPOCH"), "PATH="+stubObjdump(t, dir)+string(os.PathListSeparator)+os.Getenv("PATH"), "SOURCE_DATE_EPOCH=1700000000")
			out, err := cmd.CombinedOutput()
			if err != nil {
				t.Fatalf("package-deb: %v\n%s", err, out)
			}
			deb := filepath.Join(outDir, "nesso_0.1.0_"+tc.arch+".deb")
			info, err := exec.Command("dpkg-deb", "-f", deb, "Architecture").CombinedOutput()
			if err != nil {
				t.Fatalf("dpkg-deb: %v\n%s", err, info)
			}
			if strings.TrimSpace(string(info)) != tc.arch {
				t.Fatalf("Architecture %q", info)
			}
		})
	}
}

func TestPackageDebRejectsANonElfOrUnknownMachine(t *testing.T) {
	if _, err := exec.LookPath("od"); err != nil {
		t.Skip("od is required")
	}
	root := repoRoot(t)
	dir := t.TempDir()
	models := writeModels(t, dir, 'a')
	binary := filepath.Join(dir, "nesso")
	if err := os.WriteFile(binary, []byte("not-an-elf-file-at-all!!"), 0o755); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(filepath.Join(root, "scripts", "package-deb"), "0.1.0", binary, models, dir)
	cmd.Env = append(envWithout("SOURCE_DATE_EPOCH"), "SOURCE_DATE_EPOCH=1700000000")
	out, err := cmd.CombinedOutput()
	if err == nil {
		t.Fatal("expected package-deb to reject a non-ELF file")
	}
	if !strings.Contains(string(out), "not an ELF") {
		t.Fatalf("stderr: %s", out)
	}

	writeELF(t, binary, 21)
	cmd = exec.Command(filepath.Join(root, "scripts", "package-deb"), "0.1.0", binary, models, dir)
	cmd.Env = append(envWithout("PATH", "SOURCE_DATE_EPOCH"), "PATH="+stubObjdump(t, dir)+string(os.PathListSeparator)+os.Getenv("PATH"), "SOURCE_DATE_EPOCH=1700000000")
	out, err = cmd.CombinedOutput()
	if err == nil {
		t.Fatal("expected package-deb to reject an unknown e_machine")
	}
	if !strings.Contains(string(out), "e_machine") {
		t.Fatalf("stderr: %s", out)
	}
}

func TestReleaseConanProfile(t *testing.T) {
	script := filepath.Join(repoRoot(t), "scripts", "build-release-binary")
	for _, tc := range []struct {
		machine string
		profile string
	}{
		{"x86_64", "./conan/profiles/gcc-26"},
		{"aarch64", "./conan/profiles/gcc-26-armv8"},
	} {
		cmd := exec.Command("bash", "-c", `source "$1"; release_conan_profile "$2"`, "bash", script, tc.machine)
		out, err := cmd.CombinedOutput()
		if err != nil {
			t.Fatalf("%s: %v\n%s", tc.machine, err, out)
		}
		if strings.TrimSpace(string(out)) != tc.profile {
			t.Fatalf("%s profile: %s", tc.machine, out)
		}
		if _, err := os.Stat(filepath.Join(repoRoot(t), tc.profile)); err != nil {
			t.Fatal(err)
		}
	}
	cmd := exec.Command("bash", "-c", `source "$1"; release_conan_profile ppc64le`, "bash", script)
	out, err := cmd.CombinedOutput()
	if err == nil {
		t.Fatalf("unknown machine exited 0: %s", out)
	}
	if !strings.Contains(string(out), "ppc64le") {
		t.Fatalf("stderr: %s", out)
	}
}

func TestPublishRuntimeIndex(t *testing.T) {
	root := repoRoot(t)
	script := filepath.Join(root, "scripts", "publish-runtime-index")
	amd64 := "sha256:" + strings.Repeat("ab", 32)
	arm64 := "sha256:" + strings.Repeat("cd", 32)
	index := "sha256:" + strings.Repeat("01", 32)

	cmd := exec.Command(script, "ghcr.io/example/nesso", "v1.2.3", "9.9.9", amd64, arm64)
	out, err := cmd.CombinedOutput()
	if err == nil {
		t.Fatalf("tag/version mismatch exited 0: %s", out)
	}

	cmd = exec.Command(script, "ghcr.io/example/nesso", "v1.2.3", "1.2.3", "sha256:nope", arm64)
	out, err = cmd.CombinedOutput()
	if err == nil {
		t.Fatalf("bad digest exited 0: %s", out)
	}
	if !strings.Contains(string(out), "digest") {
		t.Fatalf("stderr: %s", out)
	}

	dir := t.TempDir()
	argsPath := filepath.Join(dir, "docker-args")
	stubDir := filepath.Join(dir, "bin")
	if err := os.Mkdir(stubDir, 0o755); err != nil {
		t.Fatal(err)
	}
	stub := "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"" + argsPath + "\"\n" +
		"if [ \"$1\" = buildx ] && [ \"$2\" = imagetools ] && [ \"$3\" = inspect ] && [ \"$4\" = --format ]; then\n" +
		"  printf '%s\\n' '" + index + "'\n  exit 0\nfi\n" +
		"if [ \"$1\" = buildx ] && [ \"$2\" = imagetools ] && [ \"$3\" = inspect ]; then\n" +
		"  printf '%s\\n' 'Platform: linux/amd64' 'Platform: linux/arm64'\n  exit 0\nfi\nexit 0\n"
	if err := os.WriteFile(filepath.Join(stubDir, "docker"), []byte(stub), 0o755); err != nil {
		t.Fatal(err)
	}
	cmd = exec.Command(script, "ghcr.io/example/nesso", "v1.2.3", "1.2.3", amd64, arm64)
	cmd.Env = append(envWithout("PATH"), "PATH="+stubDir+string(os.PathListSeparator)+os.Getenv("PATH"))
	out, err = cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("publish: %v\n%s", err, out)
	}
	if !strings.Contains(string(out), index) {
		t.Fatalf("stdout: %s", out)
	}
	args, err := os.ReadFile(argsPath)
	if err != nil {
		t.Fatal(err)
	}
	text := string(args)
	for _, want := range []string{
		"imagetools create",
		"ghcr.io/example/nesso:v1.2.3",
		"ghcr.io/example/nesso:1.2.3",
		"ghcr.io/example/nesso:latest",
		"ghcr.io/example/nesso@" + amd64,
		"ghcr.io/example/nesso@" + arm64,
	} {
		if !strings.Contains(text, want) {
			t.Fatalf("docker args missing %q:\n%s", want, text)
		}
	}

	stub = "#!/bin/sh\nif [ \"$1\" = buildx ] && [ \"$2\" = imagetools ] && [ \"$3\" = inspect ] && [ \"$4\" = --format ]; then\n" +
		"  printf '%s\\n' '" + index + "'\n  exit 0\nfi\n" +
		"printf '%s\\n' 'Platform: linux/amd64'\nexit 0\n"
	if err := os.WriteFile(filepath.Join(stubDir, "docker"), []byte(stub), 0o755); err != nil {
		t.Fatal(err)
	}
	cmd = exec.Command(script, "ghcr.io/example/nesso", "v1.2.3", "1.2.3", amd64, arm64)
	cmd.Env = append(envWithout("PATH"), "PATH="+stubDir+string(os.PathListSeparator)+os.Getenv("PATH"))
	out, err = cmd.CombinedOutput()
	if err == nil {
		t.Fatalf("missing arm64 exited 0: %s", out)
	}
	if !strings.Contains(string(out), "linux/arm64") {
		t.Fatalf("stderr: %s", out)
	}
}

func TestCompareDeb(t *testing.T) {
	root := repoRoot(t)
	script := filepath.Join(root, "scripts", "compare-deb")
	dir := t.TempDir()
	left := filepath.Join(dir, "left.deb")
	right := filepath.Join(dir, "right.deb")
	if err := os.WriteFile(left, []byte("same"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(right, []byte("same"), 0o644); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(script, left, right)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("identical: %v\n%s", err, out)
	}

	if err := os.WriteFile(right, []byte("different"), 0o644); err != nil {
		t.Fatal(err)
	}
	stubDir := filepath.Join(dir, "bin")
	if err := os.Mkdir(stubDir, 0o755); err != nil {
		t.Fatal(err)
	}
	stub := filepath.Join(stubDir, "diffoscope")
	if err := os.WriteFile(stub, []byte("#!/bin/sh\necho diffoscope-saw \"$@\"\nexit 1\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	report := filepath.Join(dir, "report")
	cmd = exec.Command(script, left, right, report)
	cmd.Env = append(envWithout("PATH"), "PATH="+stubDir+string(os.PathListSeparator)+os.Getenv("PATH"))
	out, err := cmd.CombinedOutput()
	if err == nil {
		t.Fatalf("mismatch exited 0: %s", out)
	}
	body, err := os.ReadFile(filepath.Join(report, "diffoscope.txt"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(body), "diffoscope-saw") {
		t.Fatalf("report: %s", body)
	}
	if _, err := os.Stat(filepath.Join(report, "sha256.txt")); err != nil {
		t.Fatal(err)
	}
}

func TestSmokeDebRecordsInstallAndPurge(t *testing.T) {
	root := repoRoot(t)
	dir := t.TempDir()
	deb := filepath.Join(dir, "nesso.deb")
	if err := os.WriteFile(deb, []byte("not-a-deb"), 0o644); err != nil {
		t.Fatal(err)
	}
	stubDir := filepath.Join(dir, "bin")
	if err := os.Mkdir(stubDir, 0o755); err != nil {
		t.Fatal(err)
	}
	argsPath := filepath.Join(dir, "docker-args")
	stub := "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"" + argsPath + "\"\nexit 0\n"
	if err := os.WriteFile(filepath.Join(stubDir, "docker"), []byte(stub), 0o755); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(filepath.Join(root, "scripts", "smoke-deb"), deb, "ubuntu:26.10")
	cmd.Env = append(envWithout("PATH"), "PATH="+stubDir+string(os.PathListSeparator)+os.Getenv("PATH"))
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("smoke-deb: %v\n%s", err, out)
	}
	args, err := os.ReadFile(argsPath)
	if err != nil {
		t.Fatal(err)
	}
	text := string(args)
	for _, want := range []string{
		"ubuntu:26.10",
		"apt-get install -y /tmp/nesso.deb",
		"nesso -V",
		`nesso grep "connection refused" /data/grep_sample.log -k 3`,
		"apt-get remove -y nesso",
		"apt-get purge -y nesso",
	} {
		if !strings.Contains(text, want) {
			t.Fatalf("docker args missing %q:\n%s", want, text)
		}
	}

	cmd = exec.Command(filepath.Join(root, "scripts", "smoke-deb"), deb)
	cmd.Env = append(envWithout("PATH"), "PATH="+stubDir+string(os.PathListSeparator)+os.Getenv("PATH"))
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("default image: %v\n%s", err, out)
	}
	args, err = os.ReadFile(argsPath)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(args), "ubuntu:26.04") {
		t.Fatalf("default image: %s", args)
	}
}
