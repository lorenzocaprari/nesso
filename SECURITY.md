# Security policy

## Supported versions

| Version | Supported |
| ------- | --------- |
| 0.1.x   | Yes       |

`0.1.0` is the only published release. Fixes go into the next `0.1.x` tag. Untagged commits are not a supported version.

## Reporting a vulnerability

Report privately through [GitHub private vulnerability reporting](https://github.com/lorenzocaprari/nesso/security/advisories/new).

The maintainer, [@lorenzocaprari](https://github.com/lorenzocaprari), has to enable that form first: repository **Settings → Advanced Security → Private vulnerability reporting → Enable**. Until then the page does not accept reports. Do not open a public issue, pull request, or discussion for a suspected vulnerability.

Include the version (`nesso --version`, the `.deb` version, the image tag, or the git SHA), the command (`grep`, `index`, or `search`), and a minimal input. Strip secrets and private log data. Nesso runs offline on files you pass it; parser, model-load, and package bugs are still in scope.
