// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package steps

import (
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
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

func (b *bindings) setStdin(content *godog.DocString) error {
	b.world.Stdin = content.Content + "\n"
	return nil
}

func (b *bindings) setStdinLine(count int, character string) error {
	if count <= 0 {
		return fmt.Errorf("line count must be positive")
	}
	b.world.Stdin = strings.Repeat(character, count) + "\n"
	return nil
}

func (b *bindings) setStdinHex(content *godog.DocString) error {
	raw, err := decodeHex(content.Content)
	if err != nil {
		return err
	}
	b.world.Stdin = string(raw)
	return nil
}

func (b *bindings) setTerminal() error {
	b.world.Terminal = true
	return nil
}

func (b *bindings) setEnv(name, value string) error {
	if b.world.Env == nil {
		b.world.Env = map[string]string{}
	}
	b.world.Env[name] = value
	return nil
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

func (b *bindings) touchFile(name string) error {
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	// A later mtime with the same size takes the modified-time side of the staleness check.
	later := time.Now().Add(time.Second)
	return os.Chtimes(target, later, later)
}

func (b *bindings) removeFile(name string) error {
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	return os.Remove(target)
}

func (b *bindings) makeDirectory(name string) error {
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	return os.MkdirAll(target, 0o755)
}

func (b *bindings) appendNumberedLines(count int, text, name string) error {
	if count <= 0 {
		return fmt.Errorf("line count must be positive")
	}
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	var body strings.Builder
	for i := 1; i <= count; i++ {
		fmt.Fprintf(&body, "%s %d\n", text, i)
	}
	file, err := os.OpenFile(target, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer file.Close()
	_, err = file.WriteString(body.String())
	return err
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
