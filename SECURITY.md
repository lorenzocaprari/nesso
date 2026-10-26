# Security policy

## Supported versions

| Version | Supported |
| ------- | --------- |
| 0.2.x   | Yes       |
| 0.1.x   | Yes       |

`0.2.x` is the upcoming release. `0.1.0` is the only published release. Fixes for `0.1.x` go into the next `0.1.x` tag. Untagged commits are not a supported version.

## Reporting a vulnerability

Report privately through GitHub's private vulnerability reporting: open the **Security** tab, then [Report a vulnerability](https://github.com/lorenzocaprari/nesso/security/advisories/new). Do not open a public issue, pull request, or discussion for a suspected vulnerability.

Include the version (`nesso --version`, the `.deb` version, the image tag, or the git SHA), the command (`grep`, `index`, or `search`), and a minimal input. Strip secrets and private log data. Nesso runs offline on files you pass it; parser, model-load, and package bugs are still in scope.

## Image scanning

The Package workflow and tag releases scan the runtime image with Trivy and upload SARIF to GitHub code scanning. The job summary lists counts by severity. A finding there does not fail the workflow. The published image's operating-system packages come from the pinned Ubuntu digest in `Dockerfile.runtime`.

Hosted runners that execute nesso workflows also run StepSecurity Harden-Runner in audit mode. That agent sends network and runner metadata to StepSecurity. The link for a run is in the hardened step's log.
