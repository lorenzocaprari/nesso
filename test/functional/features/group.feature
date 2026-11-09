Feature: grouped matches
  --group prints one row per masked dedup key for grep and search.

  Scenario: help lists --group
    When I run nesso with arguments grep --help
    Then the exit code is 0
    And stdout contains "--group"
    When I run nesso with arguments search --help
    Then the exit code is 0
    And stdout contains "--group"

  Scenario: grep text keeps each masked line unless --group is set
    Given a file "blocks.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      sunny beach vacation
      """
    When I run nesso with arguments grep "block terminating" --mode dense -k 5 {dir}/blocks.log
    Then the exit code is 0
    And stdout contains "blk_38865049064139660"
    And stdout contains "blk_-6952295868487656571"
    And stdout does not contain " (2):"
    When I run nesso with arguments grep "block terminating" --mode dense --group -k 5 {dir}/blocks.log
    Then the exit code is 0
    And stdout contains "line 1 (2):"
    And stdout contains "blk_38865049064139660"
    And stdout does not contain "blk_-6952295868487656571"
    And stdout contains "sunny beach vacation"
    And stdout line count is 2
    When I run nesso with arguments grep "block terminating" --mode dense --group -k 1 {dir}/blocks.log
    Then the exit code is 0
    And stdout line count is 1

  Scenario: grep json group keeps schema version 2
    Given a file "blocks.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      sunny beach vacation
      """
    When I run nesso with arguments grep "block terminating" --mode dense --json --group -k 5 {dir}/blocks.log
    Then the exit code is 0
    And stdout contains "\"schema_version\":2"
    And stdout contains "\"occurrences\":2"
    And stdout contains "\"mode\":\"dense\""
    And stdout contains "\"lexical\":null"
    And stdout contains "blk_38865049064139660"
    And stdout does not contain "blk_-6952295868487656571"
    And every stdout line is a JSON match
    And stdout line count is 2
    And stderr is empty

  Scenario: search text and json group a masked id
    Given a file "blocks.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      sunny beach vacation
      """
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/blocks.log
    Then the exit code is 0
    When I run nesso with arguments search "block terminating" --mode dense --group -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "line 1 (2):"
    And stdout contains "blk_38865049064139660"
    And stdout does not contain "blk_-6952295868487656571"
    And stdout line count is 2
    When I run nesso with arguments search "block terminating" --mode dense --json --group -k 1 -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "\"occurrences\":"
    And every stdout line is a JSON match
    And stdout line count is 1

  Scenario: each mode groups grep and search
    Given a file "blocks.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      """
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/blocks.log
    Then the exit code is 0
    When I run nesso with arguments grep "block terminating" --mode dense --group -k 5 {dir}/blocks.log
    Then the exit code is 0
    And stdout contains "line 1 (2):"
    And stdout contains "blk_38865049064139660"
    And stdout does not contain "blk_-6952295868487656571"
    And stdout line count is 1
    When I run nesso with arguments grep "block terminating" --mode hybrid --group --json -k 5 {dir}/blocks.log
    Then the exit code is 0
    And stdout contains "\"mode\":\"hybrid\""
    And stdout contains "\"occurrences\":2"
    And stdout does not contain "blk_-6952295868487656571"
    And every stdout line is a JSON match
    When I run nesso with arguments grep "block terminating" --mode lexical --group -k 5 {dir}/blocks.log
    Then the exit code is 0
    And stdout contains "line 1 (2):"
    And stdout does not contain "blk_-6952295868487656571"
    When I run nesso with arguments grep "block terminating" --mode auto --group --json -k 5 {dir}/blocks.log
    Then the exit code is 0
    And stdout contains "\"mode\":\"hybrid\""
    And stdout contains "\"occurrences\":2"
    And every stdout line is a JSON match
    When I run nesso with arguments search "block terminating" --mode dense --group -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "line 1 (2):"
    And stdout line count is 1
    When I run nesso with arguments search "block terminating" --mode hybrid --group --json -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "\"mode\":\"hybrid\""
    And stdout contains "\"occurrences\":2"
    And every stdout line is a JSON match
    When I run nesso with arguments search "block terminating" --mode lexical --group -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "line 1 (2):"
    And stdout does not contain "blk_-6952295868487656571"
    When I run nesso with arguments search "block terminating" --mode auto --group --json -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "\"mode\":\"hybrid\""
    And stdout contains "\"occurrences\":2"
    And every stdout line is a JSON match

  Scenario: an identifier query groups to the first raw line
    Given a file "ids.log" with:
      """
      request failed for blk_111111111111 today
      request failed for blk_38865049064139660 today
      """
    When I run nesso with arguments grep "blk_38865049064139660" --mode auto --group --json -k 5 {dir}/ids.log
    Then the exit code is 0
    And stdout contains "\"mode\":\"lexical\""
    And stdout contains "\"occurrences\":2"
    And stdout contains "blk_111111111111"
    And stdout does not contain "blk_38865049064139660"
    And every stdout line is a JSON match
    And stdout line count is 1
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/ids.log
    Then the exit code is 0
    When I run nesso with arguments search "blk_38865049064139660" --mode lexical --group --json -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And stdout contains "\"mode\":\"lexical\""
    And stdout contains "\"occurrences\":2"
    And stdout contains "blk_111111111111"
    And stdout does not contain "blk_38865049064139660"
    And every stdout line is a JSON match

  Scenario: grouped stdout is unchanged for threads 1, 4, and 7
    Given a file "blocks.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      sunny beach vacation
      """
    When I run nesso with arguments grep "block terminating" --mode dense --group --threads 1 -k 5 {dir}/blocks.log
    Then the exit code is 0
    And running nesso with arguments grep "block terminating" --mode dense --group --threads 4 -k 5 {dir}/blocks.log prints the same stdout
    And running nesso with arguments grep "block terminating" --mode dense --group --threads 7 -k 5 {dir}/blocks.log prints the same stdout
    When I run nesso with arguments grep "block terminating" --mode hybrid --group --threads 1 -k 5 {dir}/blocks.log
    Then the exit code is 0
    And running nesso with arguments grep "block terminating" --mode hybrid --group --threads 4 -k 5 {dir}/blocks.log prints the same stdout
    And running nesso with arguments grep "block terminating" --mode hybrid --group --threads 7 -k 5 {dir}/blocks.log prints the same stdout
    When I run nesso with arguments grep "block terminating" --mode lexical --group --threads 1 -k 5 {dir}/blocks.log
    Then the exit code is 0
    And running nesso with arguments grep "block terminating" --mode lexical --group --threads 4 -k 5 {dir}/blocks.log prints the same stdout
    And running nesso with arguments grep "block terminating" --mode lexical --group --threads 7 -k 5 {dir}/blocks.log prints the same stdout
    When I run nesso with arguments grep "block terminating" --mode auto --group --threads 1 -k 5 {dir}/blocks.log
    Then the exit code is 0
    And running nesso with arguments grep "block terminating" --mode auto --group --threads 4 -k 5 {dir}/blocks.log prints the same stdout
    And running nesso with arguments grep "block terminating" --mode auto --group --threads 7 -k 5 {dir}/blocks.log prints the same stdout
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/blocks.log
    Then the exit code is 0
    When I run nesso with arguments search "block terminating" --mode dense --group --threads 1 -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And running nesso with arguments search "block terminating" --mode dense --group --threads 4 -k 5 -i {dir}/corpus.nesso prints the same stdout
    And running nesso with arguments search "block terminating" --mode dense --group --threads 7 -k 5 -i {dir}/corpus.nesso prints the same stdout
    When I run nesso with arguments search "block terminating" --mode hybrid --group --threads 1 -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And running nesso with arguments search "block terminating" --mode hybrid --group --threads 4 -k 5 -i {dir}/corpus.nesso prints the same stdout
    And running nesso with arguments search "block terminating" --mode hybrid --group --threads 7 -k 5 -i {dir}/corpus.nesso prints the same stdout
    When I run nesso with arguments search "block terminating" --mode lexical --group --threads 1 -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And running nesso with arguments search "block terminating" --mode lexical --group --threads 4 -k 5 -i {dir}/corpus.nesso prints the same stdout
    And running nesso with arguments search "block terminating" --mode lexical --group --threads 7 -k 5 -i {dir}/corpus.nesso prints the same stdout
    When I run nesso with arguments search "block terminating" --mode auto --group --threads 1 -k 5 -i {dir}/corpus.nesso
    Then the exit code is 0
    And running nesso with arguments search "block terminating" --mode auto --group --threads 4 -k 5 -i {dir}/corpus.nesso prints the same stdout
    And running nesso with arguments search "block terminating" --mode auto --group --threads 7 -k 5 -i {dir}/corpus.nesso prints the same stdout

  Scenario: grouped rows from several sources and from stdin name the first line
    Given a file "a.log" with:
      """
      PacketResponder 1 for block blk_38865049064139660 terminating
      """
    And a file "b.log" with:
      """
      PacketResponder 0 for block blk_-6952295868487656571 terminating
      """
    When I run nesso with arguments grep "block terminating" --mode dense --group -k 5 {dir}/a.log {dir}/b.log
    Then the exit code is 0
    And stdout contains "{dir}/a.log:line 1 (2):"
    And stdout contains "blk_38865049064139660"
    And stdout does not contain "blk_-6952295868487656571"
    And stdout line count is 1
    Given stdin is:
      """
      user 10 note
      user 20 note
      """
    When I run nesso with arguments grep "note" --mode dense --group -k 5 -
    Then the exit code is 0
    And stdout contains "-:line 1 (2):"
    And stdout contains "user 10 note"
    And stdout does not contain "user 20 note"
    When I run nesso with arguments index -o {dir}/stdin.nesso -
    Then the exit code is 0
    When I run nesso with arguments search "20" --mode lexical --group -k 5 -i {dir}/stdin.nesso
    Then the exit code is 0
    And stdout contains "-:line 1 (2):"
    And stdout contains "user 10 note"
    And stdout does not contain "user 20 note"
