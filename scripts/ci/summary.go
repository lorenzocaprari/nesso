// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package main

import (
	"bytes"
	"errors"
	"os"
	"strings"
)

func summaryDestination(flagPath string) string {
	if flagPath != "" {
		return flagPath
	}
	return os.Getenv("GITHUB_STEP_SUMMARY")
}

func appendSummary(path, text string) error {
	if path == "" {
		return nil
	}
	if !strings.HasSuffix(text, "\n") {
		text += "\n"
	}
	existing, err := os.ReadFile(path)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	prefix := ""
	if len(existing) > 0 {
		prefix = "\n"
		if !bytes.HasSuffix(existing, []byte("\n")) {
			prefix = "\n\n"
		}
	}
	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = f.WriteString(prefix + text)
	return err
}
