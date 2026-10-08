// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package steps

import (
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"unicode"

	"github.com/cucumber/godog"
)

func (b *bindings) scenarioPath(name string) (string, error) {
	relative := filepath.FromSlash(name)
	if !filepath.IsLocal(relative) {
		return "", fmt.Errorf("fixture path %q escapes the scenario directory", name)
	}
	target := filepath.Join(b.world.Dir, relative)
	if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
		return "", err
	}
	return target, nil
}

func (b *bindings) writeFile(name string, content *godog.DocString) error {
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	return os.WriteFile(target, []byte(content.Content+"\n"), 0o644)
}

func (b *bindings) writeEmptyFile(name string) error {
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	return os.WriteFile(target, nil, 0o644)
}

func (b *bindings) makeDirectory(name string) error {
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	return os.MkdirAll(target, 0o755)
}

func (b *bindings) appendRepeatedLine(count int, character, name string) error {
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	line := strings.Repeat(character, count) + "\n"
	file, err := os.OpenFile(target, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer file.Close()
	_, err = file.WriteString(line)
	return err
}

func (b *bindings) writeHexFile(name string, content *godog.DocString) error {
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	raw, err := decodeHex(content.Content)
	if err != nil {
		return err
	}
	return os.WriteFile(target, raw, 0o644)
}

func decodeHex(text string) ([]byte, error) {
	var compact strings.Builder
	for _, r := range text {
		if unicode.IsSpace(r) {
			continue
		}
		compact.WriteRune(r)
	}
	raw, err := hex.DecodeString(compact.String())
	if err != nil {
		return nil, fmt.Errorf("hex fixture: %w", err)
	}
	return raw, nil
}
