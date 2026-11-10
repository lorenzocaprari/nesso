Feature: tar and zip bundles
  grep and index read tar, tar.gz, tgz, tar.xz, and zip bundles in place.
  Member names are labels. Nothing in the bundle is written to disk.

  Scenario: a tar bundle is searched and indexed
    Given an archive "bundle.tar" containing:
      | var/log/syslog | database connection refused |
      | var/log/auth.log | authentication failure |
    When I run nesso with arguments grep "database connection error" {dir}/bundle.tar
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "bundle.tar:var/log/syslog"
    And stdout contains "bundle.tar:var/log/auth.log"
    When I run nesso with arguments index -o {dir}/bundle.nesso {dir}/bundle.tar
    Then the exit code is 0
    When I run nesso with arguments search "database connection error" -i {dir}/bundle.nesso
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "bundle.tar:var/log/syslog"
    And stderr does not contain "Warning"

  Scenario: a gzip tar and a tgz bundle are read
    Given an archive "app.tar.gz" containing:
      | var/log/messages | database connection refused |
    And an archive "app.tgz" containing:
      | var/log/messages | database connection refused |
    When I run nesso with arguments grep "database connection error" {dir}/app.tar.gz
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "app.tar.gz:var/log/messages"
    When I run nesso with arguments grep "database connection error" {dir}/app.tgz
    Then the exit code is 0
    And stdout contains "app.tgz:var/log/messages"

  Scenario: a zip bundle is read
    Given an archive "logs.zip" containing:
      | var/log/syslog | database connection refused |
    When I run nesso with arguments grep "database connection error" {dir}/logs.zip
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "logs.zip:var/log/syslog"

  Scenario: a sosreport-style tar.xz is read and the text stays in the corpus
    Given an archive "sosreport-host.tar.xz" containing:
      | sos_logs/sos.log | sosreport collection finished |
      | var/log/messages | database connection refused |
      | sos_commands/logs/journalctl | kernel oops waiting for the database |
    When I run nesso with arguments grep "database connection error" {dir}/sosreport-host.tar.xz
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "sosreport-host.tar.xz:var/log/messages"
    And stdout contains "sosreport-host.tar.xz:sos_commands/logs/journalctl"
    When I run nesso with arguments index -o {dir}/sos.nesso {dir}/sosreport-host.tar.xz
    Then the exit code is 0
    When the file "sosreport-host.tar.xz" is removed
    And I run nesso with arguments search "database connection error" -i {dir}/sos.nesso
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "sosreport-host.tar.xz:var/log/messages"
    And stderr does not contain "Warning"

  Scenario: a directory of bundles is read
    Given an archive "incoming/app.tar" containing:
      | var/log/syslog | database connection refused |
    And an archive "incoming/more.zip" containing:
      | var/log/auth.log | authentication failure |
    When I run nesso with arguments grep "database" {dir}/incoming
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "authentication failure"
    And stdout contains "incoming/app.tar:var/log/syslog"
    And stdout contains "incoming/more.zip:var/log/auth.log"

  Scenario: a binary member is skipped and counted
    Given an archive "mix.tar" containing:
      | var/log/syslog | database connection refused |
      | lib/mod.bin | hello\0world |
    When I run nesso with arguments grep "database connection error" {dir}/mix.tar
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "mix.tar:var/log/syslog"
    And stderr contains "Skipped 1 entry"
    And stderr contains "mix.tar"
    And stderr contains "1 binary file"
    And stdout does not contain "hello"
    When I run nesso with arguments grep "database connection error" {dir}/mix.tar --json
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stderr contains "Skipped 1 entry"
    And stderr contains "1 binary file"

  Scenario: skipped bundle entries are summarized on stderr
    Given an archive "skippy.tar" containing:
      | var/log/syslog | database connection refused |
      | lib/mod.bin | hello\0world |
    When I run nesso with arguments grep "database connection error" {dir}/skippy.tar
    Then the exit code is 0
    And stdout contains "database connection refused"
    And stdout contains "skippy.tar:var/log/syslog"
    And stderr contains "Skipped 1 entry"
    And stderr contains "skippy.tar"
    And stderr contains "1 binary file"
    And running nesso again prints the same stdout

  Scenario: an encrypted zip entry exits 2 with a decrypt hint
    Given an encrypted archive "secret.zip" containing:
      | secret.log | database connection refused |
    When I run nesso with arguments grep "database connection error" {dir}/secret.zip
    Then the exit code is 2
    And stdout is empty
    And stderr contains "Error: failed to parse"
    And stderr contains "secret.zip"
    And stderr contains "entry 'secret.log' is encrypted; decrypt it first"
    And stderr does not contain "archive_read"

  Scenario: an armored OpenPGP file exits 2 with a decrypt hint
    Given a file "sosreport-host.tar.xz" with:
      """
      -----BEGIN PGP MESSAGE-----

      """
    When I run nesso with arguments grep "database connection error" {dir}/sosreport-host.tar.xz
    Then the exit code is 2
    And stdout is empty
    And stderr contains "Error: failed to parse"
    And stderr contains "sosreport-host.tar.xz"
    And stderr contains "the archive is an encrypted OpenPGP file; decrypt it first"
    And stderr contains "gpg --decrypt"

  Scenario: a binary SKESK-headed file exits 2 as OpenPGP
    Given a file "bundle.tar" with hex:
      """
      8c0100
      """
    When I run nesso with arguments grep "database connection error" {dir}/bundle.tar
    Then the exit code is 2
    And stderr contains "the archive is an encrypted OpenPGP file; decrypt it first"
    And stderr does not contain "truncated or corrupt"

  Scenario: a truncated tar.xz stays corrupt
    Given an archive "full.tar.xz" containing:
      | a.log | database connection refused |
    Given a file "cut.tar.xz" with hex:
      """
      fd377a585a0000
      """
    When I run nesso with arguments grep "database connection error" {dir}/cut.tar.xz
    Then the exit code is 2
    And stderr contains "the archive is truncated or corrupt"
    And stderr does not contain "encrypted"

  Scenario: a corrupt archive exits 2
    Given a file "bad.tar" with hex:
      """
      6e6f74206120746172
      """
    When I run nesso with arguments grep "database connection error" {dir}/bad.tar
    Then the exit code is 2
    And stderr contains "Error: failed to parse"
    And stderr contains "the archive is truncated or corrupt"
    And stderr contains "bad.tar"
    When I run nesso with arguments index -o {dir}/bad.nesso {dir}/bad.tar
    Then the exit code is 2
    And stderr contains "the archive is truncated or corrupt"

  Scenario: an empty bundle exits 1
    Given an empty file "empty.tar"
    When I run nesso with arguments grep "database connection error" {dir}/empty.tar
    Then the exit code is 1
    And stdout is empty
    And stderr contains "the file is empty"
    When I run nesso with arguments index -o {dir}/empty.nesso {dir}/empty.tar
    Then the exit code is 1
    And stderr contains "no indexable lines"

  Scenario: archive limits name the cap and the override flag
    Given an archive "wide.tar" containing:
      | a.log | database connection refused |
      | b.log | authentication failure |
    When I run nesso with arguments grep --archive-max-entries 1 "database" {dir}/wide.tar
    Then the exit code is 2
    And stderr contains "the archive entry count exceeds 1"
    And stderr contains "--archive-max-entries"
    When I run nesso with arguments grep --archive-max-entry-bytes 8 "database" {dir}/wide.tar
    Then the exit code is 2
    And stderr contains "an archive entry is larger than 8 bytes"
    And stderr contains "--archive-max-entry-bytes"
    When I run nesso with arguments grep --archive-max-bytes 8 "database" {dir}/wide.tar
    Then the exit code is 2
    And stderr contains "the archive expands past 8 uncompressed bytes"
    And stderr contains "--archive-max-bytes"
    Given an archive "dense.tar.xz" containing:
      | pad.log | AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA |
    When I run nesso with arguments grep --archive-max-ratio 1 "database" {dir}/dense.tar.xz
    Then the exit code is 2
    And stderr contains "the archive compression ratio exceeds 1"
    And stderr contains "--archive-max-ratio"
