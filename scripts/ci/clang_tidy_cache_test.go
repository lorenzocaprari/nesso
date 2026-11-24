// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"reflect"
	"regexp"
	"sort"
	"strings"
	"sync"
	"testing"
)

func TestSplitCommand(t *testing.T) {
	got, err := splitCommand(`/usr/bin/clang++-23 -DNAME="a b" -I'/x y' -DQ=\"v\" -c  src/a.cpp`)
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"/usr/bin/clang++-23", "-DNAME=a b", "-I/x y", `-DQ="v"`, "-c", "src/a.cpp"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %q, want %q", got, want)
	}
	for _, bad := range []string{`a 'b`, `a "b`, `a \`} {
		if _, err := splitCommand(bad); err == nil {
			t.Fatalf("%q: expected an error", bad)
		}
	}
}

func TestPreprocessArgsDropsOutputsAndDependencyFiles(t *testing.T) {
	got := preprocessArgs([]string{"clang++", "-Iinc", "-MD", "-MT", "x.o", "-MF", "x.d", "-MFy.d", "-o", "x.o", "-ofoo.o", "-c", "a.cpp"})
	want := []string{"clang++", "-Iinc", "a.cpp", "-E", "-C", "-dD", "-o", "-"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestUnitKeyChangesWithEveryInput(t *testing.T) {
	base := unitKey("v21", "cfg", "src/a.cpp", []string{"clang++", "-O2"}, []byte("int a;"))
	variants := []string{
		unitKey("v22", "cfg", "src/a.cpp", []string{"clang++", "-O2"}, []byte("int a;")),
		unitKey("v21", "cfg2", "src/a.cpp", []string{"clang++", "-O2"}, []byte("int a;")),
		unitKey("v21", "cfg", "src/b.cpp", []string{"clang++", "-O2"}, []byte("int a;")),
		unitKey("v21", "cfg", "src/a.cpp", []string{"clang++", "-O3"}, []byte("int a;")),
		unitKey("v21", "cfg", "src/a.cpp", []string{"clang++", "-O", "2"}, []byte("int a;")),
		unitKey("v21", "cfg", "src/a.cpp", []string{"clang++", "-O2"}, []byte("int a; // NOLINT")),
	}
	for i, key := range variants {
		if key == base {
			t.Fatalf("variant %d has the base key", i)
		}
	}
	if unitKey("v21", "cfg", "src/a.cpp", []string{"clang++", "-O2"}, []byte("int a;")) != base {
		t.Fatal("key is not deterministic")
	}
}

func TestHashTidyConfigsSeesNestedFilesAndSkipsBuild(t *testing.T) {
	root := t.TempDir()
	writeTidyFile(t, filepath.Join(root, ".clang-tidy"), "Checks: '*'")
	writeTidyFile(t, filepath.Join(root, "test", ".clang-tidy"), "InheritParentConfig: true")
	before, err := hashTidyConfigs(root)
	if err != nil {
		t.Fatal(err)
	}
	writeTidyFile(t, filepath.Join(root, "build", ".clang-tidy"), "ignored")
	writeTidyFile(t, filepath.Join(root, ".git", ".clang-tidy"), "ignored")
	same, _ := hashTidyConfigs(root)
	if same != before {
		t.Fatal("build/ or a hidden directory changed the config hash")
	}
	writeTidyFile(t, filepath.Join(root, "test", ".clang-tidy"), "InheritParentConfig: false")
	after, _ := hashTidyConfigs(root)
	if after == before {
		t.Fatal("editing test/.clang-tidy did not change the config hash")
	}
}

type fakeTidy struct {
	mu       sync.Mutex
	analysed []string
	fail     map[string]bool
	sources  map[string]string
	noKey    map[string]bool
}

func newCacheFixture(t *testing.T, files ...string) (*tidyCache, *fakeTidy, *bytes.Buffer) {
	t.Helper()
	root := t.TempDir()
	buildDir := filepath.Join(root, "build", "Release")
	writeTidyFile(t, filepath.Join(root, ".clang-tidy"), "Checks: '*'")
	writeTidyFile(t, filepath.Join(root, "src", "shared.hpp"), "#pragma once\n")
	var entries []compileEntry
	for _, file := range files {
		writeTidyFile(t, filepath.Join(root, file), "int "+strings.TrimSuffix(filepath.Base(file), ".cpp")+";\n")
		entries = append(entries, compileEntry{
			Directory: buildDir,
			File:      filepath.Join(root, file),
			Command:   "clang++ -Iinc -o x.o -c " + filepath.Join(root, file),
		})
	}
	data, _ := json.Marshal(entries)
	writeTidyFile(t, filepath.Join(buildDir, "compile_commands.json"), string(data))
	fake := &fakeTidy{fail: map[string]bool{}, sources: map[string]string{}, noKey: map[string]bool{}}
	out := &bytes.Buffer{}
	cache := &tidyCache{
		root:        root,
		buildDir:    buildDir,
		cacheDir:    filepath.Join(root, "cache"),
		tidyBinary:  "clang-tidy",
		jobs:        3,
		stdout:      out,
		tidyVersion: func(string) (string, error) { return "LLVM 21", nil },
		preprocess: func(_ string, args []string) ([]byte, error) {
			file := ""
			for _, arg := range args {
				if strings.HasSuffix(arg, ".cpp") {
					file = filepath.Base(arg)
				}
			}
			fake.mu.Lock()
			defer fake.mu.Unlock()
			if fake.noKey[file] {
				return nil, errors.New("preprocess failed")
			}
			header := filepath.Join(filepath.Dir(filepath.Dir(args[len(args)-6])), "src", "shared.hpp")
			return []byte("# 1 \"" + header + "\"\n# 1 \"/usr/include/stdio.h\"\n" + fake.sources[file] + file), nil
		},
		runTidy: func(_, _, file string) ([]byte, error) {
			fake.mu.Lock()
			defer fake.mu.Unlock()
			fake.analysed = append(fake.analysed, filepath.Base(file))
			if fake.fail[filepath.Base(file)] {
				return []byte("warning: bad\n"), errors.New("exit 1")
			}
			return nil, nil
		},
	}
	return cache, fake, out
}

func analysed(fake *fakeTidy) []string {
	fake.mu.Lock()
	defer fake.mu.Unlock()
	got := append([]string(nil), fake.analysed...)
	fake.analysed = nil
	sort.Strings(got)
	return got
}

func TestTidyCacheSkipsCleanUnitsAndReanalysesChangedOnes(t *testing.T) {
	cache, fake, out := newCacheFixture(t, "src/a.cpp", "src/b.cpp", "test/c.cpp", "bench/d.cpp")
	all := regexp.MustCompile(`(src|test)/.*`)

	failed, err := cache.run(all)
	if err != nil || failed != 0 {
		t.Fatalf("first run: failed=%d err=%v", failed, err)
	}
	if got := analysed(fake); !reflect.DeepEqual(got, []string{"a.cpp", "b.cpp", "c.cpp"}) {
		t.Fatalf("first run analysed %q (bench/ must be filtered out)", got)
	}

	if _, err := cache.run(all); err != nil {
		t.Fatal(err)
	}
	if got := analysed(fake); len(got) != 0 {
		t.Fatalf("second run analysed %q, want none", got)
	}
	if !strings.Contains(out.String(), "3 units, 3 cached clean, 0 analysed, 0 failed") {
		t.Fatalf("summary missing:\n%s", out.String())
	}

	fake.sources["b.cpp"] = "// a header changed\n"
	if _, err := cache.run(all); err != nil {
		t.Fatal(err)
	}
	if got := analysed(fake); !reflect.DeepEqual(got, []string{"b.cpp"}) {
		t.Fatalf("after a change analysed %q, want b.cpp", got)
	}
	markers, _ := os.ReadDir(cache.cacheDir)
	if len(markers) != 3 {
		t.Fatalf("cache holds %d markers, want 3 after pruning the stale key", len(markers))
	}
}

func TestTidyCacheNeverRecordsFailuresOrUnitsWithoutAKey(t *testing.T) {
	cache, fake, out := newCacheFixture(t, "src/a.cpp", "src/b.cpp", "src/c.cpp")
	all := regexp.MustCompile(`src/.*`)
	fake.fail["a.cpp"] = true
	fake.noKey["c.cpp"] = true

	failed, err := cache.run(all)
	if err != nil {
		t.Fatal(err)
	}
	if failed != 1 {
		t.Fatalf("failed = %d, want 1", failed)
	}
	if !strings.Contains(out.String(), "warning: bad") {
		t.Fatal("clang-tidy output was not printed")
	}
	_ = analysed(fake)

	if _, err := cache.run(all); err != nil {
		t.Fatal(err)
	}
	if got := analysed(fake); !reflect.DeepEqual(got, []string{"a.cpp", "c.cpp"}) {
		t.Fatalf("second run analysed %q, want the failed unit and the unit without a key", got)
	}
}

func TestTidyCacheSeesWhatThePreprocessorDrops(t *testing.T) {
	cache, fake, _ := newCacheFixture(t, "src/a.cpp", "src/b.cpp")
	all := regexp.MustCompile(`src/.*`)
	if _, err := cache.run(all); err != nil {
		t.Fatal(err)
	}
	_ = analysed(fake)

	// An unused macro or an #if 0 block changes no preprocessed token.
	writeTidyFile(t, filepath.Join(cache.root, "src", "a.cpp"), "int a;\n#if 0\nbad\n#endif\n")
	if _, err := cache.run(all); err != nil {
		t.Fatal(err)
	}
	if got := analysed(fake); !reflect.DeepEqual(got, []string{"a.cpp"}) {
		t.Fatalf("after editing a.cpp analysed %q, want a.cpp", got)
	}

	// A project header the preprocessor entered counts for every unit.
	writeTidyFile(t, filepath.Join(cache.root, "src", "shared.hpp"), "#pragma once\n#define UNUSED 1\n")
	if _, err := cache.run(all); err != nil {
		t.Fatal(err)
	}
	if got := analysed(fake); len(got) != 2 {
		t.Fatalf("after editing a shared header analysed %q, want both units", got)
	}
}

func TestTidyCacheConfigChangeInvalidatesEverything(t *testing.T) {
	cache, fake, _ := newCacheFixture(t, "src/a.cpp", "src/b.cpp")
	all := regexp.MustCompile(`src/.*`)
	if _, err := cache.run(all); err != nil {
		t.Fatal(err)
	}
	_ = analysed(fake)
	writeTidyFile(t, filepath.Join(cache.root, ".clang-tidy"), "Checks: '-*,bugprone-*'")
	if _, err := cache.run(all); err != nil {
		t.Fatal(err)
	}
	if got := analysed(fake); len(got) != 2 {
		t.Fatalf("after a config change analysed %q, want both units", got)
	}
}

func TestDropWarningCounts(t *testing.T) {
	in := "87633 warnings generated.\nsrc/a.cpp:1:1: error: bad [check]\n1 warning generated.\n"
	if got := string(dropWarningCounts([]byte(in))); got != "src/a.cpp:1:1: error: bad [check]\n" {
		t.Fatalf("got %q", got)
	}
}

func TestClangTidyCacheMainRejectsMissingFlags(t *testing.T) {
	if code := clangTidyCacheMain([]string{"-p", "build"}); code != 1 {
		t.Fatalf("missing -cache-dir: code %d", code)
	}
	if code := clangTidyCacheMain([]string{"-p", "b", "-cache-dir", "c", "-files", "("}); code != 1 {
		t.Fatalf("bad regex: code %d", code)
	}
}

func writeTidyFile(t *testing.T, path, content string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
}
