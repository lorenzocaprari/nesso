// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

// clang-tidy-cache runs clang-tidy on the translation units of a compile
// database and skips the ones whose result is already known to be clean.
//
// A unit's key hashes everything clang-tidy reads: the clang-tidy version,
// every .clang-tidy file in the repository, the unit's path and compile
// arguments, its preprocessed source with comments and macro definitions kept
// (-E -C -dD), and the raw bytes of every repository file the preprocessor
// entered. The raw bytes cover what -E drops, such as #if 0 blocks and unused
// macros, in the unit and in project headers. A key is recorded only
// when clang-tidy exits 0 on that unit, so a cached key always means "clean".
// Keys not used by the current run are deleted, which keeps the cache at one
// marker per unit.

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strings"
	"sync"
)

const tidyCacheVersion = "nesso-clang-tidy-cache v1"

type compileEntry struct {
	Directory string   `json:"directory"`
	File      string   `json:"file"`
	Command   string   `json:"command"`
	Arguments []string `json:"arguments"`
}

type tidyUnit struct {
	file string
	dir  string
	args []string
	key  string
}

type tidyCache struct {
	root       string
	buildDir   string
	cacheDir   string
	tidyBinary string
	jobs       int
	stdout     io.Writer
	// Replaced in tests.
	tidyVersion func(binary string) (string, error)
	preprocess  func(dir string, args []string) ([]byte, error)
	runTidy     func(binary, buildDir, file string) ([]byte, error)
}

func clangTidyCacheMain(args []string) int {
	flags := flag.NewFlagSet("clang-tidy-cache", flag.ContinueOnError)
	flags.SetOutput(os.Stderr)
	root := flags.String("root", ".", "repository root")
	buildDir := flags.String("p", "", "directory that holds compile_commands.json")
	cacheDir := flags.String("cache-dir", "", "directory for clean-result markers")
	binary := flags.String("clang-tidy", "clang-tidy", "clang-tidy binary")
	jobs := flags.Int("j", runtime.NumCPU(), "parallel jobs")
	fileRegex := flags.String("files", `(src|test)/.*`, "regular expression on unit paths")
	flags.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: ci clang-tidy-cache -p BUILD_DIR -cache-dir DIR [-root DIR] [-clang-tidy BIN] [-j N] [-files REGEX]")
		flags.PrintDefaults()
	}
	if err := flags.Parse(args); err != nil {
		return 1
	}
	if *buildDir == "" || *cacheDir == "" || *jobs < 1 || flags.NArg() != 0 {
		flags.Usage()
		return 1
	}
	pattern, err := regexp.Compile(*fileRegex)
	if err != nil {
		fmt.Fprintf(os.Stderr, "clang-tidy-cache: bad -files: %v\n", err)
		return 1
	}
	absRoot, err := filepath.Abs(*root)
	if err != nil {
		fmt.Fprintf(os.Stderr, "clang-tidy-cache: %v\n", err)
		return 1
	}
	cache := &tidyCache{
		root:        absRoot,
		buildDir:    *buildDir,
		cacheDir:    *cacheDir,
		tidyBinary:  *binary,
		jobs:        *jobs,
		stdout:      os.Stdout,
		tidyVersion: commandTidyVersion,
		preprocess:  commandPreprocess,
		runTidy:     commandRunTidy,
	}
	failed, err := cache.run(pattern)
	if err != nil {
		fmt.Fprintf(os.Stderr, "clang-tidy-cache: %v\n", err)
		return 1
	}
	if failed > 0 {
		return 1
	}
	return 0
}

func (c *tidyCache) run(pattern *regexp.Regexp) (int, error) {
	entries, err := readCompileDatabase(filepath.Join(c.buildDir, "compile_commands.json"))
	if err != nil {
		return 0, err
	}
	version, err := c.tidyVersion(c.tidyBinary)
	if err != nil {
		return 0, fmt.Errorf("%s --version: %w", c.tidyBinary, err)
	}
	config, err := hashTidyConfigs(c.root)
	if err != nil {
		return 0, err
	}
	units, err := c.selectUnits(entries, pattern)
	if err != nil {
		return 0, err
	}
	if err := os.MkdirAll(c.cacheDir, 0o755); err != nil {
		return 0, err
	}

	// Keys first: preprocessing is cheap next to clang-tidy.
	c.parallel(len(units), func(i int) {
		unit := &units[i]
		source, err := c.preprocess(unit.dir, preprocessArgs(unit.args))
		if err != nil {
			// No key: the unit is analysed and never recorded.
			return
		}
		raw, err := c.projectFileBytes(unit, source)
		if err != nil {
			return
		}
		unit.key = unitKey(version, config, c.relative(unit.file), unit.args, append(source, raw...))
	})

	var misses []int
	hits := 0
	used := map[string]bool{}
	for i, unit := range units {
		if unit.key != "" && fileExists(filepath.Join(c.cacheDir, unit.key)) {
			hits++
			used[unit.key] = true
			continue
		}
		misses = append(misses, i)
	}

	var mu sync.Mutex
	failed := 0
	c.parallel(len(misses), func(n int) {
		unit := units[misses[n]]
		output, err := c.runTidy(c.tidyBinary, c.buildDir, unit.file)
		mu.Lock()
		defer mu.Unlock()
		fmt.Fprintf(c.stdout, "[%d/%d] clang-tidy %s\n", n+1, len(misses), c.relative(unit.file))
		if shown := dropWarningCounts(output); len(shown) > 0 {
			_, _ = c.stdout.Write(shown)
		}
		if err != nil {
			failed++
			return
		}
		if unit.key != "" {
			if werr := os.WriteFile(filepath.Join(c.cacheDir, unit.key), nil, 0o644); werr == nil {
				used[unit.key] = true
			}
		}
	})

	pruned, err := pruneTidyCache(c.cacheDir, used)
	if err != nil {
		return failed, err
	}
	fmt.Fprintf(c.stdout, "clang-tidy cache: %d units, %d cached clean, %d analysed, %d failed, %d stale markers removed\n",
		len(units), hits, len(misses), failed, pruned)
	return failed, nil
}

func (c *tidyCache) selectUnits(entries []compileEntry, pattern *regexp.Regexp) ([]tidyUnit, error) {
	seen := map[string]bool{}
	var units []tidyUnit
	for _, entry := range entries {
		file := entry.File
		if !filepath.IsAbs(file) {
			file = filepath.Join(entry.Directory, file)
		}
		file = filepath.Clean(file)
		if seen[file] || !pattern.MatchString(c.relative(file)) {
			continue
		}
		args := entry.Arguments
		if len(args) == 0 {
			split, err := splitCommand(entry.Command)
			if err != nil {
				return nil, fmt.Errorf("%s: %w", file, err)
			}
			args = split
		}
		if len(args) == 0 {
			return nil, fmt.Errorf("%s: empty compile command", file)
		}
		seen[file] = true
		units = append(units, tidyUnit{file: file, dir: entry.Directory, args: args})
	}
	sort.Slice(units, func(i, j int) bool { return units[i].file < units[j].file })
	return units, nil
}

func (c *tidyCache) relative(file string) string {
	if rel, err := filepath.Rel(c.root, file); err == nil && !strings.HasPrefix(rel, "..") {
		return filepath.ToSlash(rel)
	}
	return filepath.ToSlash(file)
}

func (c *tidyCache) parallel(n int, work func(int)) {
	jobs := c.jobs
	if jobs > n {
		jobs = n
	}
	next := make(chan int)
	var wg sync.WaitGroup
	for w := 0; w < jobs; w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := range next {
				work(i)
			}
		}()
	}
	for i := 0; i < n; i++ {
		next <- i
	}
	close(next)
	wg.Wait()
}

var lineMarker = regexp.MustCompile(`(?m)^# [0-9]+ "((?:[^"\\]|\\.)*)"`)

// projectFileBytes returns the raw content of the unit and of every file under
// the repository root that the preprocessor entered, in path order.
func (c *tidyCache) projectFileBytes(unit *tidyUnit, source []byte) ([]byte, error) {
	files := map[string]bool{unit.file: true}
	for _, match := range lineMarker.FindAllSubmatch(source, -1) {
		path := strings.ReplaceAll(string(match[1]), `\\`, `\`)
		if path == "" || strings.HasPrefix(path, "<") {
			continue
		}
		if !filepath.IsAbs(path) {
			path = filepath.Join(unit.dir, path)
		}
		path = filepath.Clean(path)
		if rel, err := filepath.Rel(c.root, path); err != nil || strings.HasPrefix(rel, "..") {
			continue
		}
		files[path] = true
	}
	paths := make([]string, 0, len(files))
	for path := range files {
		paths = append(paths, path)
	}
	sort.Strings(paths)
	var out []byte
	for _, path := range paths {
		data, err := os.ReadFile(path)
		if err != nil {
			return nil, err
		}
		out = append(out, fmt.Sprintf("\x00%s\x00%d\x00", c.relative(path), len(data))...)
		out = append(out, data...)
	}
	return out, nil
}

func readCompileDatabase(path string) ([]compileEntry, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var entries []compileEntry
	if err := json.Unmarshal(data, &entries); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	return entries, nil
}

// hashTidyConfigs hashes every .clang-tidy file under root, outside build/ and
// hidden directories, by relative path and content.
func hashTidyConfigs(root string) (string, error) {
	var paths []string
	err := filepath.WalkDir(root, func(path string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() {
			name := entry.Name()
			if path != root && (name == "build" || strings.HasPrefix(name, ".")) {
				return filepath.SkipDir
			}
			return nil
		}
		if entry.Name() == ".clang-tidy" {
			paths = append(paths, path)
		}
		return nil
	})
	if err != nil {
		return "", err
	}
	sort.Strings(paths)
	hash := sha256.New()
	for _, path := range paths {
		data, err := os.ReadFile(path)
		if err != nil {
			return "", err
		}
		rel, _ := filepath.Rel(root, path)
		fmt.Fprintf(hash, "%s\x00%d\x00", filepath.ToSlash(rel), len(data))
		hash.Write(data)
	}
	return hex.EncodeToString(hash.Sum(nil)), nil
}

func unitKey(version, config, file string, args []string, source []byte) string {
	hash := sha256.New()
	fmt.Fprintf(hash, "%s\x00%s\x00%s\x00%s\x00", tidyCacheVersion, version, config, file)
	for _, arg := range args {
		fmt.Fprintf(hash, "%d\x00%s\x00", len(arg), arg)
	}
	hash.Write(source)
	return hex.EncodeToString(hash.Sum(nil))
}

// preprocessArgs turns a compile command into one that writes the
// preprocessed unit, comments included, to stdout and touches no files.
func preprocessArgs(args []string) []string {
	out := []string{args[0]}
	for i := 1; i < len(args); i++ {
		arg := args[i]
		switch {
		case arg == "-c" || arg == "-MD" || arg == "-MMD":
			continue
		case arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ":
			i++
			continue
		case strings.HasPrefix(arg, "-o") && len(arg) > 2,
			strings.HasPrefix(arg, "-MF") && len(arg) > 3,
			strings.HasPrefix(arg, "-MT") && len(arg) > 3,
			strings.HasPrefix(arg, "-MQ") && len(arg) > 3:
			continue
		}
		out = append(out, arg)
	}
	return append(out, "-E", "-C", "-dD", "-o", "-")
}

// splitCommand splits a compile_commands.json "command" string the way a
// POSIX shell would for the quoting CMake emits: single quotes, double quotes
// and backslash escapes.
func splitCommand(command string) ([]string, error) {
	var args []string
	var current strings.Builder
	inArg := false
	for i := 0; i < len(command); i++ {
		ch := command[i]
		switch {
		case ch == '\\':
			if i+1 >= len(command) {
				return nil, errors.New("trailing backslash in compile command")
			}
			i++
			current.WriteByte(command[i])
			inArg = true
		case ch == '\'':
			end := strings.IndexByte(command[i+1:], '\'')
			if end < 0 {
				return nil, errors.New("unterminated single quote in compile command")
			}
			current.WriteString(command[i+1 : i+1+end])
			i += end + 1
			inArg = true
		case ch == '"':
			i++
			for ; i < len(command) && command[i] != '"'; i++ {
				if command[i] == '\\' && i+1 < len(command) && strings.IndexByte("\"\\$`", command[i+1]) >= 0 {
					i++
				}
				current.WriteByte(command[i])
			}
			if i >= len(command) {
				return nil, errors.New("unterminated double quote in compile command")
			}
			inArg = true
		case ch == ' ' || ch == '\t' || ch == '\n':
			if inArg {
				args = append(args, current.String())
				current.Reset()
				inArg = false
			}
		default:
			current.WriteByte(ch)
			inArg = true
		}
	}
	if inArg {
		args = append(args, current.String())
	}
	return args, nil
}

func pruneTidyCache(dir string, used map[string]bool) (int, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return 0, err
	}
	removed := 0
	for _, entry := range entries {
		if entry.IsDir() || used[entry.Name()] {
			continue
		}
		if err := os.Remove(filepath.Join(dir, entry.Name())); err != nil {
			return removed, err
		}
		removed++
	}
	return removed, nil
}

var warningCountLine = regexp.MustCompile(`(?m)^[0-9]+ warnings? generated\.\n?`)

// dropWarningCounts removes clang's "N warnings generated." lines. They count
// diagnostics the header filter suppressed, so they are noise for every unit.
func dropWarningCounts(output []byte) []byte {
	return warningCountLine.ReplaceAll(output, nil)
}

func fileExists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && info.Mode().IsRegular()
}

func commandTidyVersion(binary string) (string, error) {
	out, err := exec.Command(binary, "--version").Output()
	return string(out), err
}

func commandPreprocess(dir string, args []string) ([]byte, error) {
	cmd := exec.Command(args[0], args[1:]...)
	cmd.Dir = dir
	return cmd.Output()
}

func commandRunTidy(binary, buildDir, file string) ([]byte, error) {
	return exec.Command(binary, "-p="+buildDir, "--quiet", file).CombinedOutput()
}
