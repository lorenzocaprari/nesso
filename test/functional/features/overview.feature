Feature: event type overview
  `overview` lists the masked dedup keys of its inputs, rarest or newest first, without a model.

  Scenario: overview runs without a model or a thread budget and is in the help
    Given a directory "empty-models"
    And the environment variable "NESSO_MODEL_DIR" is "empty-models"
    And the environment variable "NESSO_THREADS" is "none"
    And a file "app.log" with:
      """
      disk sda is healthy
      kernel panic on dn228
      disk sda is healthy
      """
    When I run nesso with arguments overview {dir}/app.log
    Then the exit code is 0
    And stdout is:
      """
      {dir}/app.log:line 2 (1): kernel panic on dn228
      {dir}/app.log:line 1 (2): disk sda is healthy
      """
    And stderr contains "Listed 2 of 2 event types in 3 lines."
    When I run nesso with arguments --help
    Then the exit code is 0
    And stdout contains "overview"
    When I run nesso with arguments overview --help
    Then the exit code is 0
    And stdout contains "--by"
    And stdout does not contain "--model-dir"

  Scenario Outline: overview rejects options it does not have
    Given a file "app.log" with:
      """
      disk sda is healthy
      """
    When I run nesso with arguments overview <option> {dir}/app.log
    Then the exit code is 2
    And stdout is empty

    Examples:
      | option             |
      | --mode dense       |
      | --group            |
      | --provisional      |
      | -o {dir}/out.txt   |
      | --threads 2        |
      | --model-dir models |
      | --quiet            |
      | -k 0               |
      | --by old           |

  Scenario: rare lists the fewest lines first, then the later type
    Given a file "app.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      Received block blk_1 of size 91178
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      kernel panic on dn228
      Received block blk_2 of size 3
      PacketResponder 2 for block blk_7128370237687728475 terminating
      disk quota full for user 1001
      """
    When I run nesso with arguments overview {dir}/app.log
    Then the exit code is 0
    And stdout is:
      """
      {dir}/app.log:line 7 (1): disk quota full for user 1001
      {dir}/app.log:line 4 (1): kernel panic on dn228
      {dir}/app.log:line 2 (2): Received block blk_1 of size 91178
      {dir}/app.log:line 1 (3): PacketResponder 1 for block blk_38865049064139660 terminating
      """
    And stderr contains "Listed 4 of 4 event types in 7 lines."
    When I run nesso with arguments overview --by rare -k 2 {dir}/app.log
    Then the exit code is 0
    And stdout is:
      """
      {dir}/app.log:line 7 (1): disk quota full for user 1001
      {dir}/app.log:line 4 (1): kernel panic on dn228
      """
    And stderr contains "Listed 2 of 4 event types in 7 lines."

  Scenario: new lists the latest first appearance first, in argument order
    Given a file "old.log" with:
      """
      service started on dn1
      disk sda is healthy
      """
    And a file "new.log" with:
      """
      disk sda is healthy
      service started on dn2
      kernel panic on dn228
      disk sda is healthy
      """
    When I run nesso with arguments overview --by new {dir}/old.log {dir}/new.log
    Then the exit code is 0
    And stdout is:
      """
      {dir}/new.log:line 3 (1): kernel panic on dn228
      {dir}/old.log:line 2 (3): disk sda is healthy
      {dir}/old.log:line 1 (2): service started on dn1
      """
    When I run nesso with arguments overview --by new {dir}/new.log {dir}/old.log
    Then the exit code is 0
    And stdout is:
      """
      {dir}/new.log:line 3 (1): kernel panic on dn228
      {dir}/new.log:line 2 (2): service started on dn2
      {dir}/new.log:line 1 (3): disk sda is healthy
      """

  Scenario: equal counts list the later type first
    Given a file "ties.log" with:
      """
      alpha event
      beta event
      gamma event
      beta event
      alpha event
      """
    When I run nesso with arguments overview {dir}/ties.log
    Then the exit code is 0
    And stdout is:
      """
      {dir}/ties.log:line 3 (1): gamma event
      {dir}/ties.log:line 2 (2): beta event
      {dir}/ties.log:line 1 (2): alpha event
      """

  Scenario: identical lines are one event type
    Given a file "same.log" with:
      """
      connection reset by peer
      connection reset by peer
      connection reset by peer
      """
    When I run nesso with arguments overview {dir}/same.log
    Then the exit code is 0
    And stdout is:
      """
      {dir}/same.log:line 1 (3): connection reset by peer
      """
    And stderr contains "Listed 1 of 1 event type in 3 lines."

  Scenario: overview reads stdin and directories like grep
    Given a directory "logs"
    And a file "logs/b.log" with:
      """
      beta event
      """
    And a file "logs/a.log" with:
      """
      alpha event
      beta event
      """
    When I run nesso with arguments overview {dir}/logs
    Then the exit code is 0
    And stdout is:
      """
      {dir}/logs/a.log:line 1 (1): alpha event
      {dir}/logs/a.log:line 2 (2): beta event
      """
    Given stdin is:
      """
      one
      two
      one
      """
    When I run nesso with arguments overview -
    Then the exit code is 0
    And stdout is:
      """
      -:line 2 (1): two
      -:line 1 (2): one
      """

  Scenario: nothing to list exits 1
    Given an empty file "empty.log"
    And a file "blank.log" with:
      """

      """
    When I run nesso with arguments overview {dir}/empty.log
    Then the exit code is 1
    And stdout is empty
    And stderr contains "the file is empty"
    When I run nesso with arguments overview {dir}/empty.log {dir}/blank.log
    Then the exit code is 1
    And stdout is empty
    When I run nesso with arguments overview {dir}/missing.log
    Then the exit code is 2
    And stdout is empty

  Scenario: archive members are cited as bundle:entry
    Given an archive "bundle.tar" containing:
      | var/log/syslog   | database connection refused |
      | var/log/auth.log | authentication failure      |
    When I run nesso with arguments overview {dir}/bundle.tar
    Then the exit code is 0
    And stdout is:
      """
      {dir}/bundle.tar:var/log/auth.log:line 1 (1): authentication failure
      {dir}/bundle.tar:var/log/syslog:line 1 (1): database connection refused
      """

  Scenario: text rows escape terminal controls and JSON keeps the bytes
    Given a file "controls.log" with hex:
      """
      64 69 73 6b 20 66 75 6c 6c 1b 5b 32 4a 07 0a
      """
    When I run nesso with arguments overview {dir}/controls.log
    Then the exit code is 0
    And stdout is:
      """
      {dir}/controls.log:line 1 (1): disk full\x1B[2J\x07
      """
    When I run nesso with arguments overview --json {dir}/controls.log
    Then the exit code is 0
    And stdout contains "\"text\":\"disk full\u001b[2J\u0007\""

  Scenario: JSON records name the record type and share schema version 2
    Given a file "app.log" with:
      """
      user 1 logged in
      user 2 logged in
      kernel panic on dn228
      """
    And a file "rows.json" with:
      """
      [{"message":"quota full"},{"other":"x"}]
      """
    When I run nesso with arguments overview --json {dir}/app.log {dir}/rows.json
    Then the exit code is 0
    And stdout is:
      """
      {"schema_version":2,"record":"event_type","rank":1,"count":1,"source":"{dir}/rows.json","kind":"element","line":1,"text":"quota full","key":"quota full","first_seen":4,"last_seen":4,"lines":4,"types":3}
      {"schema_version":2,"record":"event_type","rank":2,"count":1,"source":"{dir}/app.log","kind":"line","line":3,"text":"kernel panic on dn228","key":"kernel panic on <*>","first_seen":3,"last_seen":3,"lines":4,"types":3}
      {"schema_version":2,"record":"event_type","rank":3,"count":2,"source":"{dir}/app.log","kind":"line","line":1,"text":"user 1 logged in","key":"user <*> logged in","first_seen":1,"last_seen":2,"lines":4,"types":3}
      """
    And stderr contains "Skipped 1 line missing the \"message\" field."
    And stderr contains "Listed 3 of 3 event types in 4 lines."

  Scenario: repeated runs print the same bytes
    Given a directory "logs"
    And a file "logs/a.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      kernel panic on dn228
      Received block blk_1 of size 91178
      """
    And a file "logs/b.log" with:
      """
      Received block blk_2 of size 3
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      disk quota full for user 1001
      """
    When I run nesso with arguments overview {dir}/logs
    Then the exit code is 0
    And running nesso again prints the same stdout
    When I run nesso with arguments overview --by new --json {dir}/logs
    Then the exit code is 0
    And running nesso again prints the same stdout
