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

  Scenario: text output escapes terminal controls in grep and search
    Given a file "controls.log" with hex:
      """
      64 61 74 61 62 61 73 65 20 63 6f 6e 6e 65 63 74 69 6f 6e 20 72 65 66 75 73 65 64 1b 5b 32 4a 07 0a
      """
    When I run nesso with arguments grep "database connection" {dir}/controls.log
    Then the exit code is 0
    And stdout contains "database connection refused\\x1B[2J\\x07"
    When I run nesso with arguments index -o {dir}/controls.nesso {dir}/controls.log
    Then the exit code is 0
    When I run nesso with arguments search "database connection" -i {dir}/controls.nesso
    Then the exit code is 0
    And stdout contains "database connection refused\\x1B[2J\\x07"

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

  Scenario: grep and index flush a full embedding batch
    Given a file "app.log" with:
      """
      database connection refused
      """
    And 32 lines "heartbeat ok" in "app.log"
    When I run nesso with arguments grep "database connection error" {dir}/app.log
    Then the exit code is 0
    And stdout contains "database connection refused"
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "database connection refused"

  Scenario: a repeated stdin path is read once
    Given stdin is:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" - -
    Then the exit code is 0
    And stdout contains "-:line 1:"
    And stdout contains "database connection refused"

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

  Scenario: two skip reasons are joined with and
    Given a file "rows.jsonl" with:
      """
      {"message":"database connection refused"}

      {not json
      """
    When I run nesso with arguments grep "database connection error" {dir}/rows.jsonl -k 2
    Then the exit code is 0
    And stderr contains "Skipped 1 empty line and 1 malformed line."

  Scenario: three skip reasons name the plural and the comma
    Given a file "rows.jsonl" with:
      """
      {"message":"database connection refused"}


      {not json
      {"nope":1}
      {"nope":2}
      """
    When I run nesso with arguments grep "database connection error" {dir}/rows.jsonl
    Then the exit code is 0
    And stderr contains "Skipped 2 empty lines, 1 malformed line, and 2 lines missing the \"message\" field."

  Scenario: stdin longer than the binary probe is read to the end
    Given stdin is a line of 5000 "a"
    When I run nesso with arguments grep a -
    Then the exit code is 0
    And stdout contains "a"

  Scenario: binary stdin is rejected
    Given stdin is hex:
      """
      610062
      """
    When I run nesso with arguments grep a -
    Then the exit code is 2
    And stderr contains "the file is binary"

  Scenario: progress is reported on a terminal
    Given a file "app.log" with:
      """
      database connection refused
      """
    And stderr is a terminal
    When I run nesso with arguments grep "database connection error" {dir}/app.log
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stderr contains "Embedded"
    And stderr contains "lines/s"

  Scenario: an explicit thread count and quiet mode are accepted
    Given a file "edges.jsonl" with:
      """
      {"message":"database\nconnection\rrefused { _ \uD840\uDC00 xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"}
      """
    When I run nesso with arguments grep "database connection error" --threads 1 --quiet {dir}/edges.jsonl
    Then the exit code is 0
    And stdout contains "refused"
    And stderr does not contain "Embedded"

  Scenario: the model directory comes from the environment
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" {dir}/app.log
    Then the exit code is 0
    And stdout contains "database connection refused"

  Scenario: repeated lines stay in input order
    Given a file "app.log" with:
      """
      database connection refused
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" -k 2 {dir}/app.log
    Then the exit code is 0
    And stdout contains "line 1:"
    And stdout contains "line 2:"
    And lines that share text share a score
    And stdout does not contain "occurrences"

  Scenario: json rows report how many lines share the text
    Given a file "app.log" with:
      """
      database connection refused
      database connection refused
      sunny beach vacation
      """
    When I run nesso with arguments grep "database connection error" --json -k 3 {dir}/app.log
    Then the exit code is 0
    And stdout contains "\"schema_version\":1"
    And stdout contains "\"occurrences\":2"
    And stdout contains "\"occurrences\":1"
    And stdout contains "\"kind\":\"line\""
    And lines that share text share a score
    And stderr is empty

  Scenario: repeated stdin lines share one json occurrence count
    Given stdin is:
      """
      database connection refused
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" --json -k 2 -
    Then the exit code is 0
    And stdout contains "\"source\":\"-\""
    And stdout contains "\"occurrences\":2"
    And stdout contains "\"schema_version\":1"

  # JSON Lines contract. Separate from the scenarios above so a later rebase stays local.

  Scenario: json lines for one file keep key order and repeat
    Given a file "app.log" with:
      """
      database connection refused
      payment timeout after 30s
      """
    When I run nesso with arguments grep "database connection error" --json -k 2 {dir}/app.log
    Then the exit code is 0
    And stdout contains "\"source\":\"{dir}/app.log\""
    And stdout contains "\"kind\":\"line\""
    And every stdout line is a JSON match
    And running nesso again prints the same stdout
    And stderr is empty

  Scenario: json lines name each file
    Given a file "app.log" with:
      """
      database connection refused
      """
    And a file "other.log" with:
      """
      unrelated noise
      """
    When I run nesso with arguments grep "database connection error" --json -k 2 {dir}/app.log {dir}/other.log
    Then the exit code is 0
    And stdout contains "\"source\":\"{dir}/app.log\""
    And stdout contains "\"source\":\"{dir}/other.log\""
    And every stdout line is a JSON match

  Scenario: json lines read stdin
    Given stdin is:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection error" --json -
    Then the exit code is 0
    And stdout contains "\"source\":\"-\""
    And stdout contains "\"kind\":\"line\""
    And every stdout line is a JSON match

  Scenario: json lines mark an array element
    Given a file "rows.json" with:
      """
      [{"message":"first entry"},{"message":"second entry"}]
      """
    When I run nesso with arguments grep "second entry" --json {dir}/rows.json
    Then the exit code is 0
    And stdout contains "\"kind\":\"element\""
    And stdout contains "\"line\":2"
    And stdout contains "second entry"
    And every stdout line is a JSON match

  Scenario: json lines stop at the result limit
    Given a file "app.log" with:
      """
      database connection refused
      payment timeout after 30s
      disk is nearly full
      """
    When I run nesso with arguments grep "database connection error" --json -k 1 {dir}/app.log
    Then the exit code is 0
    And stdout contains "\"rank\":1"
    And stdout does not contain "\"rank\":2"
    And every stdout line is a JSON match
