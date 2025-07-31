
## Change Log for Release asterisk-22.5.1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.5.1.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.5.0...22.5.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.5.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 2
- Commit Authors: 2
- Issues Resolved: 0
- Security Advisories Resolved: 2
  - [GHSA-mrq5-74j5-f5cr](https://github.com/asterisk/asterisk/security/advisories/GHSA-mrq5-74j5-f5cr): Remote DoS and possible RCE in asterisk/res/res_stir_shaken/verification.c
  - [GHSA-v9q8-9j8m-5xwp](https://github.com/asterisk/asterisk/security/advisories/GHSA-v9q8-9j8m-5xwp): Uncontrolled Search-Path Element in safe_asterisk script may allow local privilege escalation.

### User Notes:


### Upgrade Notes:

- #### safe_asterisk: Add ownership checks for /etc/asterisk/startup.d and its files.  
  The safe_asterisk script now checks that, if it was run by the
  root user, the /etc/asterisk/startup.d directory and all the files it contains
  are owned by root.  If the checks fail, safe_asterisk will exit with an error
  and Asterisk will not be started.  Additionally, the default logging
  destination is now stderr instead of tty "9" which probably won't exist
  in modern systems.


### Developer Notes:


### Commit Authors:

- George Joseph: (1)
- ThatTotallyRealMyth: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-mrq5-74j5-f5cr: Remote DoS and possible RCE in asterisk/res/res_stir_shaken/verification.c
  - !GHSA-v9q8-9j8m-5xwp: Uncontrolled Search-Path Element in safe_asterisk script may allow local privilege escalation.

### Commits By Author:

- #### George Joseph (1):
  - res_stir_shaken: Test for missing semicolon in Identity header.

- #### ThatTotallyRealMyth (1):
  - safe_asterisk: Add ownership checks for /etc/asterisk/startup.d and its files.


### Commit List:

-  safe_asterisk: Add ownership checks for /etc/asterisk/startup.d and its files.
-  res_stir_shaken: Test for missing semicolon in Identity header.

### Commit Details:

#### safe_asterisk: Add ownership checks for /etc/asterisk/startup.d and its files.
  Author: ThatTotallyRealMyth
  Date:   2025-06-10

  UpgradeNote: The safe_asterisk script now checks that, if it was run by the
  root user, the /etc/asterisk/startup.d directory and all the files it contains
  are owned by root.  If the checks fail, safe_asterisk will exit with an error
  and Asterisk will not be started.  Additionally, the default logging
  destination is now stderr instead of tty "9" which probably won't exist
  in modern systems.

  Resolves: #GHSA-v9q8-9j8m-5xwp

#### res_stir_shaken: Test for missing semicolon in Identity header.
  Author: George Joseph
  Date:   2025-07-31

  ast_stir_shaken_vs_verify() now makes sure there's a semicolon in
  the Identity header to prevent a possible segfault.

  Resolves: #GHSA-mrq5-74j5-f5cr

