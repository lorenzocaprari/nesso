Feature: index and search
  index writes a corpus and search reads it back.

  Scenario: search finds a line that index stored
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 0
    And stderr contains "Indexed "
    And stderr contains "{dir}/corpus.nesso"
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "line 1:"

  Scenario: search prefixes the source after several files were indexed
    Given a file "app.log" with:
      """
      database connection refused
      """
    And a file "other.log" with:
      """
      unrelated noise
      """
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log {dir}/other.log
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "{dir}/app.log:line 1:"

  Scenario: search warns when an indexed source changes
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 0
    Given a file "app.log" with:
      """
      database connection refused
      changed after indexing
      """
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stderr contains "changed since indexing"
    And stderr contains "{dir}/app.log"

  Scenario: search warns when only the source mtime changes
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 0
    Given the file "app.log" is touched
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso
    Then the exit code is 0
    And stderr contains "changed since indexing"

  Scenario: search warns when an indexed source is missing
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 0
    Given the file "app.log" is removed
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stderr contains "is missing"
    And stderr contains "{dir}/app.log"

  Scenario: an empty log is not indexed
    Given an empty file "empty.log"
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/empty.log
    Then the exit code is 1
    And stdout is empty
    And stderr contains "no indexable lines"
    And stderr contains "{dir}/empty.log"
    And stderr contains "the file is empty"
    And stderr contains "--json-field"
    And stderr does not contain "Indexed"
    And stderr does not contain "Error:"
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso
    Then the exit code is 2
    And stdout is empty
    And stderr contains "File does not exist"
    And stderr does not contain "the corpus is empty"

  Scenario: a JSON file with no indexable field is not indexed
    Given a file "events.json" with:
      """
      {"host": "web"}
      """
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/events.json
    Then the exit code is 1
    And stdout is empty
    And stderr contains "no indexable lines"
    And stderr contains "{dir}/events.json"
    And stderr contains "--json-field"
    And stderr contains "Skipped "
    And stderr does not contain "Indexed"
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso
    Then the exit code is 2
    And stderr contains "File does not exist"

  Scenario: search reads a checked-in corpus
    When I run nesso with arguments search "database connection error" -i {fixture}/one-chunk.nesso
    Then the exit code is 0
    And stdout contains "checked in line"
    And stdout contains "line 1:"

  Scenario: search json rows count lines that share text
    Given a file "app.log" with:
      """
      database connection refused
      database connection refused
      """
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" --json -k 2 -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "\"schema_version\":1"
    And stdout contains "\"occurrences\":2"
    And stdout contains "\"kind\":\"line\""
    And lines that share text share a score
    And stderr is empty

  Scenario: indexing reports truncated lines
    Given a file "long.log" with:
      """
      keep this line
      """
    And a line of 5000 "x" in "long.log"
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/long.log
    Then the exit code is 0
    And stderr contains "Truncated "
    And stderr contains "Indexed "
    And stderr does not contain "Skipped "
