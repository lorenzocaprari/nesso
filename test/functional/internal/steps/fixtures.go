// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package steps

import (
	"bytes"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
	"unicode"

	"github.com/cucumber/godog"

	"github.com/lorenzocaprari/nesso/test/functional/internal/normalize"
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

func archiveKind(name string) (string, error) {
	lower := strings.ToLower(name)
	switch {
	case strings.HasSuffix(lower, ".tar.gz"), strings.HasSuffix(lower, ".tgz"):
		return "tgz", nil
	case strings.HasSuffix(lower, ".tar.xz"), strings.HasSuffix(lower, ".txz"):
		return "txz", nil
	case strings.HasSuffix(lower, ".tar"):
		return "tar", nil
	case strings.HasSuffix(lower, ".zip"):
		return "zip", nil
	default:
		return "", fmt.Errorf("unsupported archive name %q", name)
	}
}

func (b *bindings) writeArchive(name string, table *godog.Table) error {
	if table == nil || len(table.Rows) == 0 {
		return fmt.Errorf("archive %q needs at least one entry", name)
	}
	kind, err := archiveKind(name)
	if err != nil {
		return err
	}
	target, err := b.scenarioPath(name)
	if err != nil {
		return err
	}
	type entry struct {
		Name string `json:"name"`
		Data string `json:"data"`
	}
	payload := struct {
		Dest    string  `json:"dest"`
		Kind    string  `json:"kind"`
		Entries []entry `json:"entries"`
	}{Dest: target, Kind: kind}
	for _, row := range table.Rows {
		if len(row.Cells) != 2 {
			return fmt.Errorf("archive row needs a path and a body")
		}
		body := strings.ReplaceAll(row.Cells[1].Value, `\0`, "\x00")
		if !strings.Contains(body, "\x00") && !strings.HasSuffix(body, "\n") {
			body += "\n"
		}
		payload.Entries = append(payload.Entries, entry{
			Name: row.Cells[0].Value,
			Data: base64.StdEncoding.EncodeToString([]byte(body)),
		})
	}
	encoded, err := json.Marshal(payload)
	if err != nil {
		return err
	}
	cmd := exec.Command("python3", "-c", archiveWriter)
	cmd.Stdin = bytes.NewReader(encoded)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("write archive %s: %w: %s", name, err, output)
	}
	return nil
}

const archiveWriter = `
import base64, io, json, sys, tarfile, zipfile
from pathlib import Path

spec = json.load(sys.stdin)
dest = Path(spec["dest"])
kind = spec["kind"]

def body(entry):
    return base64.b64decode(entry["data"])

def write_tar(mode):
    with tarfile.open(dest, mode) as archive:
        for entry in spec["entries"]:
            data = body(entry)
            info = tarfile.TarInfo(entry["name"])
            info.size = len(data)
            info.mtime = 0
            info.mode = 0o644
            info.uid = 0
            info.gid = 0
            info.uname = ""
            info.gname = ""
            archive.addfile(info, io.BytesIO(data))

if kind == "zip":
    with zipfile.ZipFile(dest, "w") as archive:
        for entry in spec["entries"]:
            data = body(entry)
            info = zipfile.ZipInfo(filename=entry["name"], date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
elif kind == "tar":
    write_tar("w")
elif kind == "tgz":
    write_tar("w:gz")
elif kind == "txz":
    write_tar("w:xz")
else:
    raise SystemExit("unknown archive kind " + kind)
`

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

func (b *bindings) filesHaveSameBytes(left, right string) error {
	fixture := os.Getenv(fixtureEnv)
	leftPath := normalize.ExpandDir(left, b.world.Dir, fixture)
	rightPath := normalize.ExpandDir(right, b.world.Dir, fixture)
	leftBytes, err := os.ReadFile(leftPath)
	if err != nil {
		return err
	}
	rightBytes, err := os.ReadFile(rightPath)
	if err != nil {
		return err
	}
	if !bytes.Equal(leftBytes, rightBytes) {
		return fmt.Errorf("%s (%d bytes) and %s (%d bytes) differ", leftPath, len(leftBytes), rightPath, len(rightBytes))
	}
	return nil
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
