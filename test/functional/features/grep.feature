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
    And stdout does not contain "the AMX guard is not in effect"
    And stderr is empty
    And stderr does not contain "the AMX guard is not in effect"

  Scenario: text output escapes terminal controls in grep and search
    Given a file "controls.log" with hex:
      """
      64 61 74 61 62 61 73 65 20 63 6f 6e 6e 65 63 74 69 6f 6e 20 72 65 66 75 73 65 64 1b 5b 32 4a 07 0a
      """
    When I run nesso with arguments grep "database connection" {dir}/controls.log
    Then the exit code is 0
    And stdout contains "database connection refused\x1B[2J\x07"
    When I run nesso with arguments index -o {dir}/controls.nesso {dir}/controls.log
    Then the exit code is 0
    When I run nesso with arguments search "database connection" -i {dir}/controls.nesso
    Then the exit code is 0
    And stdout contains "database connection refused\x1B[2J\x07"

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

  Scenario: whitespace-only log lines are not searched or indexed
    Given a file "spaces.log" with hex:
      """
      20 20 20 0a 09 20 0d 0a 20 6b 65 65 70 20 74 68 69 73 20 6c 69 6e 65 20 0a
      """
    When I run nesso with arguments grep keep {dir}/spaces.log
    Then the exit code is 0
    And stdout contains "line 3:"
    And stdout contains " keep this line"
    And stdout does not contain "line 1:"
    And stdout does not contain "line 2:"
    And stderr contains "Skipped 2 empty lines."
    When I run nesso with arguments index -o {dir}/spaces.nesso {dir}/spaces.log
    Then the exit code is 0
    And stderr contains "Skipped 2 empty lines."
    When I run nesso with arguments search keep -i {dir}/spaces.nesso
    Then the exit code is 0
    And stdout contains "line 3:"
    And stdout does not contain "line 1:"
    And stdout does not contain "line 2:"

  Scenario: blank JSONL lines and string values are skipped
    Given a file "rows.jsonl" with hex:
      """
      20 09 0d 0a
      7b 22 6d 65 73 73 61 67 65 22 3a 22 22 7d 0a
      7b 22 6d 65 73 73 61 67 65 22 3a 22 20 5c 74 20 22 7d 0a
      7b 22 6d 65 73 73 61 67 65 22 3a 22 6b 65 65 70 20 6a 73 6f 6e 22 7d 0a
      """
    When I run nesso with arguments grep keep {dir}/rows.jsonl
    Then the exit code is 0
    And stdout contains "line 4:"
    And stdout does not contain "line 2:"
    And stdout does not contain "line 3:"
    And stderr contains "Skipped 3 empty lines."
    And stderr does not contain "malformed"
    When I run nesso with arguments index -o {dir}/rows.nesso {dir}/rows.jsonl
    Then the exit code is 0
    And stderr contains "Skipped 3 empty lines."
    When I run nesso with arguments search keep -i {dir}/rows.nesso
    Then the exit code is 0
    And stdout contains "line 4:"
    And stdout does not contain "line 2:"
    And stdout does not contain "line 3:"

  Scenario: blank JSON array values retain the remaining element number
    Given a file "rows.json" with:
      """
      [{"message":""},{"message":"   "},{"message":"keep json"}]
      """
    When I run nesso with arguments grep keep {dir}/rows.json
    Then the exit code is 0
    And stdout contains "element 3:"
    And stdout does not contain "element 1:"
    And stdout does not contain "element 2:"
    And stderr contains "Skipped 2 empty lines."
    When I run nesso with arguments index -o {dir}/rows.nesso {dir}/rows.json
    Then the exit code is 0
    And stderr contains "Skipped 2 empty lines."
    When I run nesso with arguments search keep -i {dir}/rows.nesso
    Then the exit code is 0
    And stdout contains "element 3:"
    And stdout does not contain "element 1:"
    And stdout does not contain "element 2:"

  Scenario: whitespace-only stdin lines are skipped
    Given stdin is hex:
      """
      20 09 0d 0a 6b 65 65 70 0a 20 09
      """
    When I run nesso with arguments grep keep -
    Then the exit code is 0
    And stdout contains "-:line 2:"
    And stderr contains "Skipped 2 empty lines."
    When I run nesso with arguments index -o {dir}/stdin.nesso -
    Then the exit code is 0
    And stderr contains "Skipped 2 empty lines."
    When I run nesso with arguments search keep -i {dir}/stdin.nesso
    Then the exit code is 0
    And stdout contains "-:line 2:"

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

  Scenario: provisional results are drawn on a terminal and stdout is unchanged
    Given 1100 distinct lines "database connection refused on host" in "big.log"
    And stderr is a terminal
    And the environment variable "TERM" is "xterm"
    When I run nesso with arguments grep "database connection error" {dir}/big.log
    Then the exit code is 0
    And stderr contains "Provisional (dense) top-5 after 1024/1100 lines:"
    And stderr contains "Embedded 1100/1100 lines"
    And stdout line count is 5
    And running nesso with arguments grep "database connection error" --no-provisional {dir}/big.log prints the same stdout
    And stderr does not contain "Provisional"

  Scenario: quiet mode and a dumb terminal hide the provisional view
    Given 1100 distinct lines "database connection refused on host" in "big.log"
    And stderr is a terminal
    And the environment variable "TERM" is "dumb"
    When I run nesso with arguments grep "database connection error" {dir}/big.log
    Then the exit code is 0
    And stderr contains "Embedded 1100/1100 lines"
    And stderr does not contain "Provisional"
    Given the environment variable "TERM" is "xterm"
    When I run nesso with arguments grep "database connection error" --quiet {dir}/big.log
    Then the exit code is 0
    And stderr does not contain "Provisional"
    And stderr does not contain "Embedded"

  Scenario: --json --provisional adds provisional records that repeat run to run
    Given 1100 distinct lines "database connection refused on host" in "big.log"
    When I run nesso with arguments grep "database connection error" --json --provisional {dir}/big.log
    Then the exit code is 0
    And stdout contains "\"mode\":\"dense\",\"dense\":"
    And stdout contains ",\"final\":false,\"refresh\":1}"
    And stderr does not contain "Provisional"
    And running nesso again prints the same stdout
    And stdout without provisional records is the stdout of nesso with arguments grep "database connection error" --json {dir}/big.log
    And stdout does not contain "\"final\""
    And every stdout line is a JSON match

  Scenario: the window that holds the needle is embedded first
    Given 2100 distinct lines "database connection refused on host" in "big.log"
    And 1 lines "kernel panic on node" in "big.log"
    When I run nesso with arguments grep "kernel panic" --json --provisional {dir}/big.log
    Then the exit code is 0
    And provisional refresh 1 contains "\"text\":\"kernel panic on node 1\""
    And stdout without provisional records is the stdout of nesso with arguments grep "kernel panic" --json {dir}/big.log

  Scenario: lexical grep over three windows prints the input-order bytes
    Given 2100 distinct lines "database connection refused on host" in "big.log"
    And 1 lines "kernel panic on node" in "big.log"
    When I run nesso with arguments grep "kernel panic on host" --mode lexical --json {dir}/big.log
    Then the exit code is 0
    And stdout is:
      """
      {"schema_version":2,"rank":1,"source":"{dir}/big.log","kind":"line","line":2101,"score":15.665007,"text":"kernel panic on node 1","occurrences":1,"mode":"lexical","dense":null,"lexical":15.665007}
      {"schema_version":2,"rank":2,"source":"{dir}/big.log","kind":"line","line":353,"score":0.0010536795,"text":"database connection refused on host on","occurrences":1,"mode":"lexical","dense":null,"lexical":0.0010536795}
      {"schema_version":2,"rank":3,"source":"{dir}/big.log","kind":"line","line":1,"score":0.00095172384,"text":"database connection refused on host a","occurrences":1,"mode":"lexical","dense":null,"lexical":0.00095172384}
      {"schema_version":2,"rank":4,"source":"{dir}/big.log","kind":"line","line":2,"score":0.00095172384,"text":"database connection refused on host b","occurrences":1,"mode":"lexical","dense":null,"lexical":0.00095172384}
      {"schema_version":2,"rank":5,"source":"{dir}/big.log","kind":"line","line":3,"score":0.00095172384,"text":"database connection refused on host c","occurrences":1,"mode":"lexical","dense":null,"lexical":0.00095172384}
      """
    And running nesso with arguments grep "kernel panic on host" --mode lexical --json --threads 2 {dir}/big.log prints the same stdout

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
    And stdout contains "\"schema_version\":2"
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
    And stdout contains "\"schema_version\":2"

  Scenario: lines that differ only by an id share a score
    Given a file "blocks.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      """
    When I run nesso with arguments grep "block terminating" --mode dense -k 2 {dir}/blocks.log
    Then the exit code is 0
    And stdout contains "blk_38865049064139660"
    And stdout contains "blk_-6952295868487656571"
    And lines that differ only by an id share a score
    And stdout does not contain "occurrences"

  Scenario: json rows count masked id groups
    Given a file "blocks.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      sunny beach vacation
      """
    When I run nesso with arguments grep "block terminating" --mode dense --json -k 3 {dir}/blocks.log
    Then the exit code is 0
    And stdout contains "\"schema_version\":2"
    And stdout contains "\"occurrences\":2"
    And stdout contains "\"occurrences\":1"
    And stdout contains "blk_38865049064139660"
    And stdout contains "blk_-6952295868487656571"
    And stdout contains "sunny beach vacation"
    And lines that differ only by an id share a score
    And stderr is empty

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
    And stdout does not contain "the AMX guard is not in effect"
    And stderr is empty
    And stderr does not contain "the AMX guard is not in effect"

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
