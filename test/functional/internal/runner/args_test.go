// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package runner

import (
	"reflect"
	"testing"
)

func TestSplitArguments(t *testing.T) {
	cases := []struct {
		name string
		line string
		want []string
	}{
		{"empty", "", nil},
		{"whitespace only", "  \t ", nil},
		{"plain words", "grep disk a.log", []string{"grep", "disk", "a.log"}},
		{"quoted span", `grep "disk full" a.log`, []string{"grep", "disk full", "a.log"}},
		{"empty quoted argument", `grep "" a.log`, []string{"grep", "", "a.log"}},
		{"quote inside word", `--query="a b"`, []string{"--query=a b"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got, err := SplitArguments(tc.line)
			if err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(got, tc.want) {
				t.Fatalf("got %#v, want %#v", got, tc.want)
			}
		})
	}
}

func TestSplitArgumentsRejectsUnterminatedQuote(t *testing.T) {
	if _, err := SplitArguments(`grep "disk`); err == nil {
		t.Fatal("expected an error")
	}
}
