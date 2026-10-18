Feature: Harness smoke test
  The step library drives the nesso binary from outside the process.

  Scenario: nesso reports its version
    When I run nesso with arguments --version
    Then the exit code is 0
    And stderr is empty

  Scenario: NESSO_THREADS overrides the session count
    Given the environment variable "NESSO_THREADS" is "2"
    When I run nesso with arguments --version --verbose
    Then the exit code is 0
    And stdout contains "int8: 2 single-thread"
    And stdout contains "fp32: 1 session, 2 intra-op threads"
    And stderr is empty

  Scenario: version can print the session policy
    When I run nesso with arguments --version --verbose
    Then the exit code is 0
    And stdout contains "int8:"
    And stdout contains "fp32:"
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
    Then the exit code is 2
    And stderr contains "failed to load the embedder"
    And stderr does not contain "does not exist"
