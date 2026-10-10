Feature: command errors
  grep, index, and search reject input they cannot use.

  Scenario: help documents the exit codes
    When I run nesso with arguments --help
    Then the exit code is 0
    And stdout contains "Exit codes"
    And stdout contains "2 on error"

  Scenario: an unknown flag is rejected
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep -p query {dir}/app.log
    Then the exit code is 2
    And stderr contains "-p"

  Scenario: init and store are not commands
    When I run nesso with arguments init
    Then the exit code is 2
    And stderr contains "subcommand"
    When I run nesso with arguments store
    Then the exit code is 2
    And stderr contains "subcommand"

  Scenario: index requires an output path
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments index {dir}/app.log
    Then the exit code is 2
    And stderr contains "--output is required"

  Scenario: a missing model directory is rejected
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log --model-dir {dir}/missing
    Then the exit code is 2
    And stderr contains "failed to load the embedder"
    And stderr contains "could not load the model"
    And stderr contains "{dir}/missing"
    And stderr contains "scripts/fetch-model"
    When I run nesso with arguments index --model-dir {dir}/missing -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 2
    And stderr contains "failed to load the embedder"

  Scenario: a binary file is rejected
    Given a file "blob.log" with hex:
      """
      68656c6c6f00776f726c64
      """
    When I run nesso with arguments grep "database connection error" {dir}/blob.log
    Then the exit code is 2
    And stderr contains "failed to parse"
    And stderr contains "the file is binary"
    And stderr contains "blob.log"
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/blob.log
    Then the exit code is 2
    And stderr contains "the file is binary"
    And stderr contains "blob.log"

  Scenario: grep rejects an empty log
    Given an empty file "empty.log"
    When I run nesso with arguments grep "database connection error" {dir}/empty.log
    Then the exit code is 1
    And stdout is empty
    And stderr contains "the file is empty"

  Scenario: a zero result limit is a failure
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log -k 0
    Then the exit code is 2
    And stderr contains "must be a positive integer"
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso -k 0
    Then the exit code is 2
    And stderr contains "must be a positive integer"

  Scenario: a corrupt corpus cannot be searched
    Given a file "bad.nesso" with:
      """
      bad!
      """
    When I run nesso with arguments search "database connection error" -i {dir}/bad.nesso
    Then the exit code is 2
    And stderr contains "failed to read the corpus"
    And stderr contains "the file is corrupt"

  Scenario: the corpus cannot be written onto a directory
    Given a file "app.log" with:
      """
      database connection refused
      """
    And a directory "outdir"
    When I run nesso with arguments index -o {dir}/outdir {dir}/app.log
    Then the exit code is 2
    And stderr contains "failed to write the corpus"
    And stderr contains "could not open the file"
    And stderr contains "{dir}/outdir"

  Scenario: a corpus with the wrong dimensions cannot be searched
    When I run nesso with arguments search "database connection error" -i {fixture}/mismatch.nesso
    Then the exit code is 2
    And stderr contains "failed to search"
    And stderr contains "the embedding width does not match"
