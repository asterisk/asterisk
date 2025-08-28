
## Change Log for Release asterisk-18.26.4

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-18.26.4.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/18.26.3...18.26.4)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-18.26.4.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 0
- Security Advisories Resolved: 1
  - [GHSA-557q-795j-wfx2](https://github.com/asterisk/asterisk/security/advisories/GHSA-557q-795j-wfx2): Resource exhaustion (DoS) vulnerability: remotely exploitable leak of RTP UDP ports and internal resources

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- George Joseph: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-557q-795j-wfx2: Resource exhaustion (DoS) vulnerability: remotely exploitable leak of RTP UDP ports and internal resources

### Commits By Author:

- #### George Joseph (1):
  - pjproject: Update bundled to 2.15.1.


### Commit List:

-  pjproject: Update bundled to 2.15.1.

### Commit Details:

#### pjproject: Update bundled to 2.15.1.
  Author: George Joseph
  Date:   2025-08-25

  This resolves a security issue where RTP ports weren't being released
  causing possible resource exhaustion issues.

  Resolves: #GHSA-557q-795j-wfx2

