// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package steps

import (
	"bytes"
	"testing"
)

func TestDecodeHexIgnoresWhitespace(t *testing.T) {
	got, err := decodeHex("4e45\n  5343")
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, []byte("NESC")) {
		t.Fatalf("got %q", got)
	}
}

func TestDecodeHexRejectsOddLength(t *testing.T) {
	if _, err := decodeHex("abc"); err == nil {
		t.Fatal("expected an error")
	}
}
