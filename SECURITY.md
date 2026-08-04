# Security Policy

## Supported Versions

The Asterisk project maintains an
[Asterisk-Versions](https://docs.asterisk.org/About-the-Project/Asterisk-Versions/)
page on the project's [documentation website](https://docs.asterisk.org).
Each version is listed with its release date, security fix only date, and end of life
date. Consult this wiki page to see if the version of Asterisk you are reporting a
security vulnerability against is still supported.

## Reporting a Vulnerability

Please see the
[Asterisk Security Vulnerabilities](https://docs.asterisk.org/About-the-Project/Asterisk-Security-Vulnerabilities/)
page on the [documentation website](https://docs.asterisk.org) then use the
"Report a vulnerability" button under the
["Security"](https://github.com/asterisk/asterisk/security)
tab of this project's GitHub repository.
**Never use regular GitHub issues to report security vulnerabilities!**

#### Please report only one vulnerability per security advisory!

Reporting multiple vulnerability in one advisory creates the following issues:

* They'll probably need different CVEs.
* They may have different Common Weakness Enumerator (CWE) values.  While you can list multiple CWEs in a single advisory, you can't indicate which vulnerability has which weakness.
* They may have different severities.
* They may affect different Asterisk versions.
* It makes it harder to associate fix pull requests to a vulnerability.
* It makes it harder for our automation tasks to to validate fixes and create releases.

#### Do NOT use the "Start a temporary private fork" security advisory feature!  

Private forks created from security advisories are severly limited by GitHub
and cannot run the workflows necessary for validation and testing.  Once an
advisory is accepted, the reporter will be given instructions on how to
submit or test a fix pull request.
