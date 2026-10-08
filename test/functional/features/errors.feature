Feature: command errors
  grep, index, and search reject input they cannot use.

  Scenario: an unknown flag is rejected
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep -p query {dir}/app.log
    Then the exit code is not 0
    And stderr contains "-p"

  Scenario: init and store are not commands
    When I run nesso with arguments init
    Then the exit code is not 0
    And stderr contains "subcommand"
    When I run nesso with arguments store
    Then the exit code is not 0
    And stderr contains "subcommand"

  Scenario: index requires an output path
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments index {dir}/app.log
    Then the exit code is not 0
    And stderr contains "--output is required"

  Scenario: a missing model directory is rejected
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log --model-dir {dir}/missing
    Then the exit code is 1
    And stderr contains "Failed to load embedder"
    When I run nesso with arguments index --model-dir {dir}/missing -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 1
    And stderr contains "Failed to load embedder"

  Scenario: an unsupported extension is rejected
    Given a file "notes.txt" with:
      """
      not a log
      """
    When I run nesso with arguments grep "database connection error" {dir}/notes.txt
    Then the exit code is 1
    And stderr contains "Failed to parse"
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/notes.txt
    Then the exit code is 1
    And stderr contains "Failed to parse"

  Scenario: grep rejects an empty log
    Given an empty file "empty.log"
    When I run nesso with arguments grep "database connection error" {dir}/empty.log
    Then the exit code is 1
    And stdout is empty

  Scenario: a zero result limit is a failure
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log -k 0
    Then the exit code is 1
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso -k 0
    Then the exit code is 1

  Scenario: a corrupt corpus cannot be searched
    Given a file "bad.nesso" with:
      """
      bad!
      """
    When I run nesso with arguments search "database connection error" -i {dir}/bad.nesso
    Then the exit code is 1
    And stderr contains "Failed to read corpus"

  Scenario: the corpus cannot be written onto a directory
    Given a file "app.log" with:
      """
      database connection refused
      """
    And a directory "outdir"
    When I run nesso with arguments index -o {dir}/outdir {dir}/app.log
    Then the exit code is 1
    And stderr contains "Failed to write corpus"

  Scenario: a corpus with the wrong dimensions cannot be searched
    Given a file "short.nesso" with hex:
      """
      4e4553430100000002000000000000000100000000000000
      010000000000000001000000000000000100000000000000
      73740000803f00000000
      """
    When I run nesso with arguments search "database connection error" -i {dir}/short.nesso
    Then the exit code is 1
    And stderr contains "Semantic search failed"
