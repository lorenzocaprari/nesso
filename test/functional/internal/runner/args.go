// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package runner

import (
	"fmt"
	"strings"
)

// SplitArguments splits on whitespace and keeps double-quoted spans together.
func SplitArguments(line string) ([]string, error) {
	var (
		args    []string
		current strings.Builder
		quoted  bool
		started bool
	)
	for _, r := range line {
		switch {
		case r == '"':
			quoted = !quoted
			started = true
		case (r == ' ' || r == '\t') && !quoted:
			if started {
				args = append(args, current.String())
				current.Reset()
				started = false
			}
		default:
			current.WriteRune(r)
			started = true
		}
	}
	if quoted {
		return nil, fmt.Errorf("unterminated quote in %q", line)
	}
	if started {
		args = append(args, current.String())
	}
	return args, nil
}
