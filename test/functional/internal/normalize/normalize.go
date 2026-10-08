// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

// Package normalize makes scenario text identical across platforms.
package normalize

import (
	"path/filepath"
	"strings"
)

// DirPlaceholder stands for the scenario directory in arguments and output.
const DirPlaceholder = "{dir}"

// FixturePlaceholder stands for the checked-in corpus directory.
const FixturePlaceholder = "{fixture}"

// ExpandDir replaces the scenario and fixture placeholders in an argument.
func ExpandDir(argument, dir, fixture string) string {
	argument = strings.ReplaceAll(argument, FixturePlaceholder, fixture)
	return strings.ReplaceAll(argument, DirPlaceholder, dir)
}

// Output yields LF line endings, forward slashes inside the scenario
// directory, and the directory itself as DirPlaceholder.
func Output(text, dir, fixture string) string {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	if fixture != "" {
		text = strings.ReplaceAll(text, fixture, FixturePlaceholder)
		text = strings.ReplaceAll(text, filepath.ToSlash(fixture), FixturePlaceholder)
	}
	text = strings.ReplaceAll(text, dir, DirPlaceholder)
	text = strings.ReplaceAll(text, filepath.ToSlash(dir), DirPlaceholder)
	return strings.ReplaceAll(text, DirPlaceholder+`\`, DirPlaceholder+"/")
}
