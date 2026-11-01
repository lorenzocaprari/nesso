Feature: command errors
  grep, index, and search reject input they cannot use.

  Scenario: help documents the exit codes
    When I run nesso with arguments --help
    Then the exit code is 0
    And stdout contains "Examples:"
    And stdout contains "nesso grep \"database connection error\" app.log"
    And stdout contains "nesso grep \"payment timeout\" app.log events.jsonl -k 5"
    And stdout contains "nesso index -o corpus.nesso app.log"
    And stdout contains "nesso search \"payment timeout\" -i corpus.nesso"
    And stdout contains "0 matches printed or corpus written"
    And stdout contains "1 nothing to search or index"
    And stdout contains "2 error"

  Scenario: grep and index help document quiet mode
    When I run nesso with arguments grep --help
    Then the exit code is 0
    And stdout contains "--quiet"
    And stdout does not contain "(default: 5)"
    And stdout contains "[5]"
    And stdout does not contain "(default: message)"
    And stdout contains "[message]"
    And the "--threads" help is at most 2 lines
    When I run nesso with arguments index --help
    Then the exit code is 0
    And stdout contains "--quiet"
    And stdout does not contain "(default: message)"
    And stdout contains "[message]"
    And the "--threads" help is at most 2 lines
    When I run nesso with arguments search --help
    Then the exit code is 0
    And stdout does not contain "(default: 5)"
    And stdout contains "[5]"
    And the "--threads" help is at most 2 lines

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

  Scenario: grep of several empty logs names no file
    Given an empty file "a.log"
    And an empty file "b.log"
    When I run nesso with arguments grep "database connection error" {dir}/a.log {dir}/b.log
    Then the exit code is 1
    And stderr contains "nothing to search:"
    And stderr does not contain "a.log"

  Scenario: grep rejects an empty log
    Given an empty file "empty.log"
    When I run nesso with arguments grep "database connection error" {dir}/empty.log
    Then the exit code is 1
    And stdout is empty
    And stderr contains "the file is empty"

  Scenario: a missing model directory names the directory it tried
    Given an empty file "app.log"
    And a directory "nomodel"
    When I run nesso with arguments grep "database connection error" --model-dir {dir}/nomodel {dir}/app.log
    Then the exit code is 2
    And stderr contains "failed to load the embedder"
    And stderr contains "Tried"

  Scenario: a log of only blank lines has nothing to index
    Given a file "blank.log" with hex:
      """
      0a 20 09 0d 0a 20 09
      """
    When I run nesso with arguments grep "database connection error" {dir}/blank.log
    Then the exit code is 1
    And stdout is empty
    And stderr contains "no indexable lines"
    And stderr contains "Skipped 3 empty lines."
    When I run nesso with arguments index -o {dir}/blank.nesso {dir}/blank.log
    Then the exit code is 1
    And stdout is empty
    And stderr contains "no indexable lines"
    And stderr contains "Skipped 3 empty lines."
    When I run nesso with arguments search "database connection error" -i {dir}/blank.nesso
    Then the exit code is 2
    And stderr contains "File does not exist"

  Scenario: a malformed JSON document is skipped
    Given a file "bad.json" with:
      """
      {not json
      """
    When I run nesso with arguments grep "database connection error" {dir}/bad.json
    Then the exit code is 1
    And stderr contains "no indexable lines"

  Scenario: a missing file is rejected
    When I run nesso with arguments grep "database connection error" {dir}/missing.log
    Then the exit code is 2
    And stderr contains "file does not exist"

  Scenario: NESSO_THREADS must be a positive integer
    Given a file "app.log" with:
      """
      database connection refused
      """
    And the environment variable "NESSO_THREADS" is "abc"
    When I run nesso with arguments grep "database connection error" {dir}/app.log
    Then the exit code is 2
    And stderr contains "NESSO_THREADS must be a positive integer"

  Scenario: an out-of-range NESSO_THREADS is a usage error
    Given a file "app.log" with:
      """
      database connection refused
      """
    And the environment variable "NESSO_THREADS" is "99999999999999999999999"
    When I run nesso with arguments grep "database connection error" {dir}/app.log
    Then the exit code is 2
    And stderr contains "NESSO_THREADS must be a positive integer"

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

  Scenario: a corpus with a changed byte cannot be searched
    When I run nesso with arguments search "database connection error" -i {fixture}/damaged.nesso
    Then the exit code is 2
    And stderr contains "failed to read the corpus"
    And stderr contains "the file is corrupt"

  Scenario: the corpus cannot overwrite one of its inputs
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments index -o {dir}/app.log {dir}/app.log
    Then the exit code is 2
    And stderr contains "failed to write the corpus"
    And stderr contains "the output is one of the inputs"
    When I run nesso with arguments grep "connection" {dir}/app.log
    Then the exit code is 0
    And stdout contains "database connection refused"

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
    And stderr contains "failed to read the corpus"
    And stderr contains "the corpus was built with a different model; re-run nesso index"

  Scenario: a corpus built with another model cannot be searched
    When I run nesso with arguments search "database connection error" -i {fixture}/other-model.nesso
    Then the exit code is 2
    And stderr contains "failed to read the corpus"
    And stderr contains "the corpus was built with a different model; re-run nesso index"

  Scenario: a corpus in a newer format cannot be searched
    When I run nesso with arguments search "database connection error" -i {fixture}/future-version.nesso
    Then the exit code is 2
    And stderr contains "failed to read the corpus"
    And stderr contains "corpus format v4 is not supported; re-run nesso index"

  Scenario: a corpus in format v2 must be re-indexed
    When I run nesso with arguments search "database connection error" -i {fixture}/v2.nesso
    Then the exit code is 2
    And stderr contains "failed to read the corpus"
    And stderr contains "corpus format v2 is not supported; re-run nesso index"

  # JSON Lines contract. Separate from the scenarios above so a later rebase stays local.

  Scenario: json grep of an empty file prints nothing
    Given an empty file "empty.log"
    When I run nesso with arguments grep "database connection error" --json {dir}/empty.log
    Then the exit code is 1
    And stdout is empty
    And stderr contains "the file is empty"

  Scenario: index rejects json output
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments index --json -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 2
    And stdout is empty
    And stderr contains "--json"
