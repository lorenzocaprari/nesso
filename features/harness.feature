Feature: Harness smoke test
  The step library drives the nesso binary from outside the process.

  Scenario: nesso reports its version
    When I run nesso with arguments --version
    Then the exit code is 0
    And stderr is empty

  Scenario: a missing subcommand is rejected
    When I run nesso without arguments
    Then the exit code is not 0
    And stderr contains "subcommand"

  Scenario: fixture files resolve through the scenario directory
    Given a file "logs/app.log" with:
      """
      disk full on /var
      """
    When I run nesso with arguments grep disk {dir}/logs/app.log --model-dir {dir}/no-model
    Then the exit code is 1
    And stderr contains "Failed to load embedder"
    And stderr does not contain "does not exist"
