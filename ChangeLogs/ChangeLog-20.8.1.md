
## Change Log for Release asterisk-20.8.1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.8.1.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.8.0...20.8.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.8.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 0
- Security Advisories Resolved: 1
  - [GHSA-qqxj-v78h-hrf9](https://github.com/asterisk/asterisk/security/advisories/GHSA-qqxj-v78h-hrf9): res_pjsip_endpoint_identifier_ip: wrongly matches ALL unauthorized SIP requests

### User Notes:


### Upgrade Notes:


### Commit Authors:

- George Joseph: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-qqxj-v78h-hrf9: res_pjsip_endpoint_identifier_ip: wrongly matches ALL unauthorized SIP requests

### Commits By Author:

- ### George Joseph (1):
  - Revert "res_pjsip_endpoint_identifier_ip: Add endpoint identifier transport ad..


### Commit List:


### Commit Details:

#### Revert "res_pjsip_endpoint_identifier_ip: Add endpoint identifier transport ad..
  Author: George Joseph
  Date:   2024-05-17

  This reverts PR #602

  Resolves: #GHSA-qqxj-v78h-hrf9

