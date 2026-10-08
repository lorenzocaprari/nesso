// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package normalize

import "testing"

func TestExpandDir(t *testing.T) {
	if got := ExpandDir("{dir}/a.log", "/tmp/x", ""); got != "/tmp/x/a.log" {
		t.Fatalf("got %q", got)
	}
}

func TestOutputStripsCarriageReturns(t *testing.T) {
	if got := Output("a\r\nb\r\n", "/tmp/x", ""); got != "a\nb\n" {
		t.Fatalf("got %q", got)
	}
}

func TestOutputReplacesDirectory(t *testing.T) {
	if got := Output("open /tmp/x/a.log failed", "/tmp/x", ""); got != "open {dir}/a.log failed" {
		t.Fatalf("got %q", got)
	}
}

func TestExpandFixture(t *testing.T) {
	if got := ExpandDir("{fixture}/one-chunk.nesso", "/tmp/x", "/opt/fixtures"); got != "/opt/fixtures/one-chunk.nesso" {
		t.Fatalf("got %q", got)
	}
}

func TestOutputReplacesFixture(t *testing.T) {
	if got := Output("open /opt/fixtures/one-chunk.nesso", "/tmp/x", "/opt/fixtures"); got != "open {fixture}/one-chunk.nesso" {
		t.Fatalf("got %q", got)
	}
}

func TestOutputUsesForwardSlashAfterPlaceholder(t *testing.T) {
	if got := Output(`C:\w\a\sub\a.log`, `C:\w\a`, ""); got != "{dir}/sub\\a.log" {
		t.Fatalf("got %q", got)
	}
}
