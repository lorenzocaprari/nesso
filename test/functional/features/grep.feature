Feature: grep
  Semantic search over log, JSON, and JSONL files.

  Scenario: a log line is ranked
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log
    Then the exit code is 0
    And stdout contains "line 1:"
    And stdout contains "database connection refused"
    And stderr is empty

  Scenario: the same grep prints the same scores
    Given a file "app.log" with:
      """
      database connection refused
      payment timeout after 30s
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log
    Then the exit code is 0
    And running nesso again prints the same stdout

  Scenario: a JSON message is ranked
    Given a file "one.json" with:
      """
      {"message":"standalone entry"}
      """
    When I run nesso with arguments grep standalone {dir}/one.json
    Then the exit code is 0
    And stdout contains "standalone entry"

  Scenario: extensionless, rotated, and uppercase names are lines
    Given a file "syslog" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/syslog
    Then the exit code is 0
    And stdout contains "database connection refused"
    Given a file "app.log.1" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log.1
    Then the exit code is 0
    And stdout contains "database connection refused"
    Given a file "APP.LOG" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/APP.LOG
    Then the exit code is 0
    And stdout contains "database connection refused"
    Given a file "notes.txt" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/notes.txt
    Then the exit code is 0
    And stdout contains "database connection refused"

  Scenario: a JSON array keeps string message fields
    Given a file "rows.json" with:
      """
      [{"message":"first entry"},{"not_message":"ignored"},{"message":"second entry"}]
      """
    When I run nesso with arguments grep "second entry" {dir}/rows.json
    Then the exit code is 0
    And stdout contains "second entry"
    And stdout contains "element 3:"
    And stderr contains "Skipped 1 line missing the \"message\" field."
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/rows.json
    Then the exit code is 0
    When I run nesso with arguments search "second entry" -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "element 3:"

  Scenario: a JSONL file skips objects without a string message
    Given a file "rows.jsonl" with:
      """
      {"message":"database connection refused"}
      {"not_message":"ignored"}
      """
    When I run nesso with arguments grep "database connection error" {dir}/rows.jsonl
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stderr contains "Skipped 1 line missing the \"message\" field."

  Scenario: empty lines are skipped and overlong lines are truncated
    Given a file "app.log" with:
      """
      keep this line

      """
    And a line of 5000 "x" in "app.log"
    When I run nesso with arguments grep keep {dir}/app.log
    Then the exit code is 0
    And stdout contains "keep this line"
    And stdout contains "xxxxx"
    And stderr contains "Skipped 1 empty line."
    And stderr contains "Truncated 1 line."

  Scenario: json-field selects which string is ranked
    Given a file "rows.jsonl" with:
      """
      {"message":"ignored text","msg":"database connection refused"}
      """
    When I run nesso with arguments grep "database connection error" --json-field msg {dir}/rows.jsonl
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout does not contain "ignored text"

  Scenario: several files prefix each match with its path
    Given a file "app.log" with:
      """
      database connection refused
      """
    And a file "other.log" with:
      """
      unrelated noise
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log {dir}/other.log
    Then the exit code is 0
    And stdout contains "{dir}/app.log:line 1:"

  Scenario: an uppercase JSON extension is still JSON
    Given a file "rows.JSON" with:
      """
      {"message":"database connection refused"}
      """
    When I run nesso with arguments grep "database connection error" {dir}/rows.JSON
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "line 1:"

  Scenario: stdin is read as lines
    Given stdin is:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" -
    Then the exit code is 0
    And stdout contains "-:line 1:"
    And stdout contains "database connection refused"
    When I run nesso with arguments index -o {dir}/corpus.nesso -
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "-:line 1:"

  Scenario: the model directory comes from the environment
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log
    Then the exit code is 0
    And stdout contains "database connection refused"
