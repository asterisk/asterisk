
## Change Log for Release asterisk-certified-18.9-cert16

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-18.9-cert16.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert15...certified-18.9-cert16)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-18.9-cert16.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 0
- Security Advisories Resolved: 1
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

- ThatTotallyRealMyth: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-v9q8-9j8m-5xwp: Uncontrolled Search-Path Element in safe_asterisk script may allow local privilege escalation.

### Commits By Author:

- #### ThatTotallyRealMyth (1):
  - safe_asterisk: Add ownership checks for /etc/asterisk/startup.d and its files.


### Commit List:

-  safe_asterisk: Add ownership checks for /etc/asterisk/startup.d and its files.

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

