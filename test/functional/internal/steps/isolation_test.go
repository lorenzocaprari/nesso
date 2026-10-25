// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

package steps

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/cucumber/godog"
)

const isolationFeature = `Feature: scenario isolation
  Scenario: alpha keeps its own directory
    Given a file "marker.txt" with:
      """
      alpha
      """
    Then the scenario directory holds "alpha"

  Scenario: beta keeps its own directory
    Given a file "marker.txt" with:
      """
      beta
      """
    Then the scenario directory holds "beta"
`

func TestConcurrentScenariosHaveIsolatedDirectories(t *testing.T) {
	featureDir := t.TempDir()
	featurePath := filepath.Join(featureDir, "isolation.feature")
	if err := os.WriteFile(featurePath, []byte(isolationFeature), 0o644); err != nil {
		t.Fatal(err)
	}

	var mu sync.Mutex
	var dirs []string
	var arrived atomic.Int32
	gate := make(chan struct{})

	suite := godog.TestSuite{
		ScenarioInitializer: func(sc *godog.ScenarioContext) {
			InitializeScenario(sc)
			sc.Step(`^the scenario directory holds "([^"]*)"$`, func(ctx context.Context, marker string) error {
				dir, ok := scenarioDir(ctx)
				if !ok || dir == "" {
					return fmt.Errorf("scenario directory was not stored")
				}
				body, err := os.ReadFile(filepath.Join(dir, "marker.txt"))
				if err != nil {
					return err
				}
				if strings.TrimSuffix(string(body), "\n") != marker {
					return fmt.Errorf("directory %s holds %q, want %s", dir, body, marker)
				}
				mu.Lock()
				dirs = append(dirs, dir)
				mu.Unlock()
				if arrived.Add(1) == 2 {
					close(gate)
				}
				select {
				case <-gate:
					return nil
				case <-time.After(10 * time.Second):
					return fmt.Errorf("the other scenario did not reach the barrier")
				}
			})
		},
		Options: &godog.Options{
			Format:      "pretty",
			NoColors:    true,
			Paths:       []string{featurePath},
			Strict:      true,
			Concurrency: 2,
			TestingT:    t,
		},
	}
	if suite.Run() != 0 {
		t.Fatal("isolation scenarios failed")
	}
	mu.Lock()
	defer mu.Unlock()
	if len(dirs) != 2 || dirs[0] == dirs[1] {
		t.Fatalf("scenario directories = %v", dirs)
	}
}
