
## Change Log for Release asterisk-20.22.0-rc3

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.22.0-rc3.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.22.0-rc2...20.22.0-rc3)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.22.0-rc3.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 1
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- Mike Bradeen: (1)

## Issue and Commit Detail:

### Closed Issues:

  - #2232: [bug]:  Update pjsip_options.c WebSocket contacts results in PJSIP_EUNSUPTRANSPORT

### Commits By Author:

- **Mike Bradeen** (1):
  - res_pjsip: Preserve target hostname for qualified OPTIONS destination

### Commit List:

-  res_pjsip: Preserve target hostname for qualified OPTIONS destination

### Commit Details:

__res_pjsip: Preserve target hostname for qualified OPTIONS destination__
  Author: Mike Bradeen
  Date:   2026-10-16

  As Asterisk now does the endpoint resolution manually, we need to also manually
  copy the hostname from the resolution target as PJProject only does this when
  resolving the endpoint itself.

  Fixes: #2232


