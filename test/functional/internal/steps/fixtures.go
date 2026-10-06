// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: MIT

package steps

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/cucumber/godog"
)

func (b *bindings) writeFile(name string, content *godog.DocString) error {
	relative := filepath.FromSlash(name)
	if !filepath.IsLocal(relative) {
		return fmt.Errorf("fixture path %q escapes the scenario directory", name)
	}
	target := filepath.Join(b.world.Dir, relative)
	if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
		return err
	}
	return os.WriteFile(target, []byte(content.Content+"\n"), 0o644)
}
