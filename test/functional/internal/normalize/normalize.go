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

// ExpandDir replaces the placeholder in an argument with the real directory.
func ExpandDir(argument, dir string) string {
	return strings.ReplaceAll(argument, DirPlaceholder, dir)
}

// Output yields LF line endings, forward slashes inside the scenario
// directory, and the directory itself as DirPlaceholder.
func Output(text, dir string) string {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	text = strings.ReplaceAll(text, dir, DirPlaceholder)
	text = strings.ReplaceAll(text, filepath.ToSlash(dir), DirPlaceholder)
	return strings.ReplaceAll(text, DirPlaceholder+`\`, DirPlaceholder+"/")
}
