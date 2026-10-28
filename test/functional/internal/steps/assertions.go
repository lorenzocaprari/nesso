// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package steps

import (
	"encoding/json"
	"errors"
	"fmt"
	"regexp"
	"runtime"
	"strconv"
	"strings"
)

func (b *bindings) requireRun() error {
	if !b.world.Ran {
		return errors.New("no nesso command has run in this scenario")
	}
	return nil
}

func (b *bindings) exitCodeIs(want string) error {
	if err := b.requireRun(); err != nil {
		return err
	}
	expected, _ := strconv.Atoi(want)
	result := b.world.Result
	if result.ExitCode != expected {
		return fmt.Errorf("exit code %d, want %d\nstdout:\n%s\nstderr:\n%s",
			result.ExitCode, expected, result.Stdout, result.Stderr)
	}
	return nil
}

func (b *bindings) exitCodeIsNot(unwanted string) error {
	if err := b.requireRun(); err != nil {
		return err
	}
	rejected, _ := strconv.Atoi(unwanted)
	result := b.world.Result
	if result.ExitCode == rejected {
		return fmt.Errorf("exit code is %d\nstdout:\n%s\nstderr:\n%s", result.ExitCode, result.Stdout, result.Stderr)
	}
	return nil
}

func (b *bindings) stream(name string) (string, error) {
	if err := b.requireRun(); err != nil {
		return "", err
	}
	if name == "stdout" {
		return b.world.Result.Stdout, nil
	}
	return b.world.Result.Stderr, nil
}

func phrase(fragment string) string {
	// Feature files write \" for a quote inside a step.
	return strings.ReplaceAll(fragment, `\"`, `"`)
}

func (b *bindings) hostIntraOpFloor() error {
	text, err := b.stream("stdout")
	if err != nil {
		return err
	}
	int8Line := "int8: 1 single-thread session"
	fp32Line := "fp32: 1 session, 1 intra-op thread"
	if runtime.GOARCH == "arm64" {
		int8Line = "int8: 1 session, 2 intra-op threads"
		fp32Line = "fp32: 1 session, 2 intra-op threads"
	}
	if !strings.Contains(text, int8Line) || !strings.Contains(text, fp32Line) {
		return fmt.Errorf("stdout does not contain %q and %q:\n%s", int8Line, fp32Line, text)
	}
	return nil
}

func (b *bindings) hostBudgetTwo() error {
	text, err := b.stream("stdout")
	if err != nil {
		return err
	}
	int8Line := "int8: 2 single-thread"
	if runtime.GOARCH == "arm64" {
		int8Line = "int8: 1 session, 2 intra-op threads"
	}
	if !strings.Contains(text, int8Line) {
		return fmt.Errorf("stdout does not contain %q:\n%s", int8Line, text)
	}
	return nil
}

func (b *bindings) streamContains(name, fragment string) error {
	text, err := b.stream(name)
	if err != nil {
		return err
	}
	fragment = phrase(fragment)
	if !strings.Contains(text, fragment) {
		return fmt.Errorf("%s does not contain %q:\n%s", name, fragment, text)
	}
	return nil
}

func (b *bindings) streamOmits(name, fragment string) error {
	text, err := b.stream(name)
	if err != nil {
		return err
	}
	fragment = phrase(fragment)
	if strings.Contains(text, fragment) {
		return fmt.Errorf("%s contains %q:\n%s", name, fragment, text)
	}
	return nil
}

func (b *bindings) optionHelpAtMost(flag, limit string) error {
	text, err := b.stream("stdout")
	if err != nil {
		return err
	}
	maxLines, convErr := strconv.Atoi(limit)
	if convErr != nil {
		return convErr
	}
	count, err := optionHelpLines(text, flag)
	if err != nil {
		return err
	}
	if count > maxLines {
		return fmt.Errorf("%s help is %d lines, want at most %d\n%s", flag, count, maxLines, text)
	}
	return nil
}

func optionHelpLines(text, flag string) (int, error) {
	lines := strings.Split(strings.TrimRight(text, "\n"), "\n")
	start := -1
	for i, line := range lines {
		if strings.Contains(line, flag) {
			start = i
			break
		}
	}
	if start < 0 {
		return 0, fmt.Errorf("stdout has no %s help:\n%s", flag, text)
	}
	count := 1
	for i := start + 1; i < len(lines); i++ {
		trimmed := strings.TrimLeft(lines[i], " ")
		if trimmed == "" || strings.HasPrefix(trimmed, "-") || strings.HasSuffix(strings.TrimRight(trimmed, " "), ":") {
			break
		}
		count++
	}
	return count, nil
}

var textMatchScore = regexp.MustCompile(`(?:line|element) \d+: ([0-9]+\.[0-9]+): (.*)$`)

func matchBodyScore(line string) (string, string, error) {
	if strings.HasPrefix(strings.TrimSpace(line), "{") {
		var row struct {
			Text  string          `json:"text"`
			Score json.RawMessage `json:"score"`
		}
		if err := json.Unmarshal([]byte(line), &row); err != nil {
			return "", "", fmt.Errorf("stdout row is not JSON: %s", line)
		}
		return row.Text, string(row.Score), nil
	}
	match := textMatchScore.FindStringSubmatch(line)
	if match == nil {
		return "", "", fmt.Errorf("stdout match line has no score: %s", line)
	}
	return match[2], match[1], nil
}

func (b *bindings) idVariantsShareScore() error {
	text, err := b.stream("stdout")
	if err != nil {
		return err
	}
	scoreTexts := map[string]map[string]struct{}{}
	for _, line := range strings.Split(strings.TrimRight(text, "\n"), "\n") {
		if line == "" {
			continue
		}
		body, score, parseErr := matchBodyScore(line)
		if parseErr != nil {
			return parseErr
		}
		if scoreTexts[score] == nil {
			scoreTexts[score] = map[string]struct{}{}
		}
		scoreTexts[score][body] = struct{}{}
	}
	for _, bodies := range scoreTexts {
		if len(bodies) >= 2 {
			return nil
		}
	}
	return fmt.Errorf("no two different lines share a score:\n%s", text)
}

func (b *bindings) sharedTextSharesScore() error {
	text, err := b.stream("stdout")
	if err != nil {
		return err
	}
	scoreByText := map[string]string{}
	shared := 0
	for _, line := range strings.Split(strings.TrimRight(text, "\n"), "\n") {
		if line == "" {
			continue
		}
		var body, score string
		if strings.HasPrefix(strings.TrimSpace(line), "{") {
			var row struct {
				Text  string          `json:"text"`
				Score json.RawMessage `json:"score"`
			}
			if err := json.Unmarshal([]byte(line), &row); err != nil {
				return fmt.Errorf("stdout row is not JSON: %s", line)
			}
			body = row.Text
			score = string(row.Score)
		} else {
			match := textMatchScore.FindStringSubmatch(line)
			if match == nil {
				return fmt.Errorf("stdout match line has no score: %s", line)
			}
			score = match[1]
			body = match[2]
		}
		if previous, ok := scoreByText[body]; ok {
			shared++
			if previous != score {
				return fmt.Errorf("text %q scored %s and %s\n%s", body, previous, score, text)
			}
		} else {
			scoreByText[body] = score
		}
	}
	if shared == 0 {
		return fmt.Errorf("stdout has no repeated text:\n%s", text)
	}
	return nil
}

func (b *bindings) streamIsEmpty(name string) error {
	text, err := b.stream(name)
	if err != nil {
		return err
	}
	if text != "" {
		return fmt.Errorf("%s is not empty:\n%s", name, text)
	}
	return nil
}
