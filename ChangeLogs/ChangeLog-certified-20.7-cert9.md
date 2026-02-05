
## Change Log for Release asterisk-certified-20.7-cert9

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-20.7-cert9.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-20.7-cert8...certified-20.7-cert9)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-20.7-cert9.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 4
- Commit Authors: 2
- Issues Resolved: 0
- Security Advisories Resolved: 4
  - [GHSA-85x7-54wr-vh42](https://github.com/asterisk/asterisk/security/advisories/GHSA-85x7-54wr-vh42): Asterisk xml.c uses unsafe XML_PARSE_NOENT leading to potential XXE Injection
  - [GHSA-rvch-3jmx-3jf3](https://github.com/asterisk/asterisk/security/advisories/GHSA-rvch-3jmx-3jf3): ast_coredumper running as root sources ast_debug_tools.conf from /etc/asterisk; potentially leading to privilege escalation
  - [GHSA-v6hp-wh3r-cwxh](https://github.com/asterisk/asterisk/security/advisories/GHSA-v6hp-wh3r-cwxh): The Asterisk embedded web server's /httpstatus page echos user supplied values(cookie and query string) without sanitization
  - [GHSA-xpc6-x892-v83c](https://github.com/asterisk/asterisk/security/advisories/GHSA-xpc6-x892-v83c): ast_coredumper runs as root, and writes gdb init file to world writeable folder; leading to potential privilege escalation 

### User Notes:

- #### ast_coredumper: check ast_debug_tools.conf permissions
  ast_debug_tools.conf must be owned by root and not be
  writable by other users or groups to be used by ast_coredumper or
  by ast_logescalator or ast_loggrabber when run as root.


### Upgrade Notes:

- #### http.c: Change httpstatus to default disabled and sanitize output.
  To prevent possible security issues, the `/httpstatus` page
  served by the internal web server is now disabled by default.  To explicitly
  enable it, set `enable_status=yes` in http.conf.


### Developer Notes:


### Commit Authors:

- George Joseph: (2)
- Mike Bradeen: (2)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-85x7-54wr-vh42: Asterisk xml.c uses unsafe XML_PARSE_NOENT leading to potential XXE Injection
  - !GHSA-rvch-3jmx-3jf3: ast_coredumper running as root sources ast_debug_tools.conf from /etc/asterisk; potentially leading to privilege escalation
  - !GHSA-v6hp-wh3r-cwxh: The Asterisk embedded web server's /httpstatus page echos user supplied values(cookie and query string) without sanitization
  - !GHSA-xpc6-x892-v83c: ast_coredumper runs as root, and writes gdb init file to world writeable folder; leading to potential privilege escalation 

### Commits By Author:

- #### George Joseph (2):

- #### Mike Bradeen (2):

### Commit List:

-  xml.c: Replace XML_PARSE_NOENT with XML_PARSE_NONET for xmlReadFile.
-  ast_coredumper: check ast_debug_tools.conf permissions
-  http.c: Change httpstatus to default disabled and sanitize output.
-  ast_coredumper: create gdbinit file with restrictive permissions

### Commit Details:

#### xml.c: Replace XML_PARSE_NOENT with XML_PARSE_NONET for xmlReadFile.
  Author: George Joseph
  Date:   2026-01-15

  The xmlReadFile XML_PARSE_NOENT flag, which allows parsing of external
  entities, could allow a potential XXE injection attack.  Replacing it with
  XML_PARSE_NONET, which prevents network access, is safer.

  Resolves: #GHSA-85x7-54wr-vh42

#### ast_coredumper: check ast_debug_tools.conf permissions
  Author: Mike Bradeen
  Date:   2026-01-15

  Prevent ast_coredumper from using ast_debug_tools.conf files that are
  not owned by root or are writable by other users or groups.

  Prevent ast_logescalator and ast_loggrabber from doing the same if
  they are run as root.

  Resolves: #GHSA-rvch-3jmx-3jf3

  UserNote: ast_debug_tools.conf must be owned by root and not be
  writable by other users or groups to be used by ast_coredumper or
  by ast_logescalator or ast_loggrabber when run as root.

#### http.c: Change httpstatus to default disabled and sanitize output.
  Author: George Joseph
  Date:   2026-01-15

  To address potential security issues, the httpstatus page is now disabled
  by default and the echoed query string and cookie output is html-escaped.

  Resolves: #GHSA-v6hp-wh3r-cwxh

  UpgradeNote: To prevent possible security issues, the `/httpstatus` page
  served by the internal web server is now disabled by default.  To explicitly
  enable it, set `enable_status=yes` in http.conf.

#### ast_coredumper: create gdbinit file with restrictive permissions
  Author: Mike Bradeen
  Date:   2026-01-15

  Modify gdbinit to use the install command with explicit permissions (-m 600)
  when creating the .ast_coredumper.gdbinit file. This ensures the file is
  created with restricted permissions (readable/writable only by the owner)
  to avoid potential privilege escalation.

  Resolves: #GHSA-xpc6-x892-v83c

