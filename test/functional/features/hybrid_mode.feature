Feature: hybrid search modes
  grep and search accept --mode auto|hybrid|dense|lexical.

  Scenario: each mode runs on grep and search
    Given a file "app.log" with:
      """
      database connection refused
      payment timeout after 30s
      auth failure for user
      """
    When I run nesso with arguments grep "auth failure" {dir}/app.log --mode dense -k 3
    Then the exit code is 0
    And stdout contains "auth failure"
    When I run nesso with arguments grep "auth failure" {dir}/app.log --mode hybrid -k 3
    Then the exit code is 0
    And stdout contains "auth failure"
    When I run nesso with arguments grep "auth failure" {dir}/app.log --mode lexical -k 3
    Then the exit code is 0
    And stdout contains "auth failure"
    When I run nesso with arguments grep "auth failure" {dir}/app.log --mode auto -k 3
    Then the exit code is 0
    And stdout contains "auth failure"
    When I run nesso with arguments index -o {dir}/corpus.nesso {dir}/app.log
    Then the exit code is 0
    When I run nesso with arguments search "auth failure" -i {dir}/corpus.nesso --mode dense -k 3
    Then the exit code is 0
    When I run nesso with arguments search "auth failure" -i {dir}/corpus.nesso --mode hybrid -k 3
    Then the exit code is 0
    When I run nesso with arguments search "auth failure" -i {dir}/corpus.nesso --mode lexical -k 3
    Then the exit code is 0
    When I run nesso with arguments search "auth failure" -i {dir}/corpus.nesso --mode auto -k 3
    Then the exit code is 0

  Scenario: identifier class ranks the identifier line first
    Given a file "ids.log" with:
      """
      unrelated prose about networking
      request failed for blk_-1608999687919862906 on node
      another line without the token
      """
    When I run nesso with arguments grep "blk_-1608999687919862906" {dir}/ids.log --mode auto -k 1
    Then the exit code is 0
    And stdout contains "blk_-1608999687919862906"
    And stdout contains "line 2:"

  Scenario: natural-language and mixed classes under auto
    Given a file "app.log" with:
      """
      connection failed with ERR-5001
      timeout waiting for response
      """
    When I run nesso with arguments grep "timeout" {dir}/app.log --mode auto -k 1 --json
    Then the exit code is 0
    And stdout contains "\"mode\":\"hybrid\""
    When I run nesso with arguments grep "connection failed with ERR-5001" {dir}/app.log --mode auto -k 1 --json
    Then the exit code is 0
    And stdout contains "\"mode\":\"hybrid\""

  Scenario: lexical fallback warns and uses hybrid
    Given a file "app.log" with:
      """
      completely unrelated content here
      another prose line without tokens
      """
    When I run nesso with arguments grep "zzzznotpresent99" {dir}/app.log --mode lexical -k 2
    Then the exit code is 0
    And stderr contains "Warning: no lexical match for the query; using hybrid"

  Scenario: dense JSON always uses schema_version 2
    Given a file "app.log" with:
      """
      database connection refused
      """
    When I run nesso with arguments grep "database connection" {dir}/app.log --mode dense --json
    Then the exit code is 0
    And stdout contains "\"schema_version\":2"
    And stdout contains "\"mode\":\"dense\""
    And stdout contains "\"lexical\":null"
    And every stdout line is a JSON match

  Scenario: missing postings exit 2 for lexical modes but dense works
    When I run nesso with arguments search "database connection error" -i {fixture}/one-chunk.nesso --mode dense -k 1
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {fixture}/one-chunk.nesso --mode lexical -k 1
    Then the exit code is 2
    And stderr contains "Error: corpus has no lexical postings; re-run `nesso index`"
    When I run nesso with arguments search "database connection error" -i {fixture}/one-chunk.nesso --mode hybrid -k 1
    Then the exit code is 2
    And stderr contains "Error: corpus has no lexical postings; re-run `nesso index`"
    When I run nesso with arguments search "database connection error" -i {fixture}/one-chunk.nesso --mode auto -k 1
    Then the exit code is 2
    And stderr contains "Error: corpus has no lexical postings; re-run `nesso index`"

  Scenario: term-version mismatch exit 2 for lexical modes but dense works
    When I run nesso with arguments search "database connection error" -i {fixture}/lexical-terms-mismatch.nesso --mode dense -k 1
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {fixture}/lexical-terms-mismatch.nesso --mode lexical -k 1
    Then the exit code is 2
    And stderr contains "Error: corpus lexical terms are version"
    And stderr contains "this build uses version"
    And stderr contains "re-run `nesso index`"
    When I run nesso with arguments search "database connection error" -i {fixture}/lexical-terms-mismatch.nesso --mode auto -k 1
    Then the exit code is 2
    And stderr contains "Error: corpus lexical terms are version"
