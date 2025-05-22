
## Change Log for Release asterisk-22.4.1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.4.1.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.4.0...22.4.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.4.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 2
- Commit Authors: 1
- Issues Resolved: 0
- Security Advisories Resolved: 2
  - [GHSA-2grh-7mhv-fcfw](https://github.com/asterisk/asterisk/security/advisories/GHSA-2grh-7mhv-fcfw): Using malformed From header can forge identity with ";" or NULL in name portion
  - [GHSA-c7p6-7mvq-8jq2](https://github.com/asterisk/asterisk/security/advisories/GHSA-c7p6-7mvq-8jq2): cli_permissions.conf: deny option does not work for disallowing shell commands

### User Notes:

- #### asterisk.c: Add option to restrict shell access from remote consoles.           
  A new asterisk.conf option 'disable_remote_console_shell' has
  been added that, when set, will prevent remote consoles from executing
  shell commands using the '!' prefix.
  Resolves: #GHSA-c7p6-7mvq-8jq2


### Upgrade Notes:


### Commit Authors:

- George Joseph: (2)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-2grh-7mhv-fcfw: Using malformed From header can forge identity with ";" or NULL in name portion
  - !GHSA-c7p6-7mvq-8jq2: cli_permissions.conf: deny option does not work for disallowing shell commands

### Commits By Author:

- #### George Joseph (2):
  - res_pjsip_messaging.c: Mask control characters in received From display name
  - asterisk.c: Add option to restrict shell access from remote consoles.


### Commit List:

-  asterisk.c: Add option to restrict shell access from remote consoles.
-  res_pjsip_messaging.c: Mask control characters in received From display name

### Commit Details:

#### asterisk.c: Add option to restrict shell access from remote consoles.
  Author: George Joseph
  Date:   2025-05-19

  UserNote: A new asterisk.conf option 'disable_remote_console_shell' has
  been added that, when set, will prevent remote consoles from executing
  shell commands using the '!' prefix.

  Resolves: #GHSA-c7p6-7mvq-8jq2

#### res_pjsip_messaging.c: Mask control characters in received From display name
  Author: George Joseph
  Date:   2025-03-24

  Incoming SIP MESSAGEs will now have their From header's display name
  sanitized by replacing any characters < 32 (space) with a space.

  Resolves: #GHSA-2grh-7mhv-fcfw

