Feature: directory and gzip input
  grep and index read a directory in path order, and read gzip streams.

  Scenario: a directory is searched and indexed
    Given a file "logs/app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/logs
    Then the exit code is 0
    And stdout contains "database connection refused"
    When I run nesso with arguments index -o {dir}/logs.nesso {dir}/logs
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {dir}/logs.nesso
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stderr does not contain "Warning"

  Scenario: nested directories are read
    Given a file "tree/outer.log" with:
      """
      database connection refused
      """
    And a file "tree/deep/inner.log" with:
      """
      payment timeout waiting for the database
      """
    When I run nesso with arguments grep "database" {dir}/tree
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "payment timeout waiting for the database"
    And stdout contains "{dir}/tree/outer.log"
    And stdout contains "{dir}/tree/deep/inner.log"

  Scenario: a gzip log is read
    Given a file "app.log.gz" with hex:
      """
      1f8b0800f722f26a02ff4b492c494c4a2c4e5548cecfcb4b4d2ec9cccf53284a4d2b2d4e4de10200e756bf4f1c000000
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log.gz
    Then the exit code is 0
    And stdout contains "database connection refused"
    When I run nesso with arguments index -o {dir}/gzip.nesso {dir}/app.log.gz
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {dir}/gzip.nesso
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stderr does not contain "Warning"

  Scenario: a directory mixes a plain log and a gzip log
    Given a file "mix/plain.log" with:
      """
      disk is nearly full
      """
    And a file "mix/app.log.gz" with hex:
      """
      1f8b0800f722f26a02ff4b492c494c4a2c4e5548cecfcb4b4d2ec9cccf53284a4d2b2d4e4de10200e756bf4f1c000000
      """
    When I run nesso with arguments grep "database connection error" {dir}/mix
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "disk is nearly full"

  Scenario: a corrupt gzip file is rejected
    Given a file "bad.gz" with hex:
      """
      1f8b0800
      """
    When I run nesso with arguments grep "database connection error" {dir}/bad.gz
    Then the exit code is 2
    And stderr contains "Error: failed to parse"
    And stderr contains "the gzip stream is truncated or corrupt"
    And stderr contains "bad.gz"
    When I run nesso with arguments index -o {dir}/bad.nesso {dir}/bad.gz
    Then the exit code is 2
    And stderr contains "Error: failed to parse"
    And stderr contains "the gzip stream is truncated or corrupt"
    And stderr contains "bad.gz"
