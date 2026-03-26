
## Change Log for Release asterisk-21.12.2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-21.12.2.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/21.12.1...21.12.2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21.12.2.tar.gz)  
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

  - 1833: [bug]: Address security vulnerabilities in pjproject

### Commits By Author:

- #### Mike Bradeen (1):
  - res_pjsip: Address pjproject security vulnerabilities

### Commit List:

-  res_pjsip: Address pjproject security vulnerabilities

### Commit Details:

#### res_pjsip: Address pjproject security vulnerabilities
  Author: Mike Bradeen
  Date:   2026-03-25

  Address the following pjproject security vulnerabilities

  [GHSA-j29p-pvh2-pvqp - Buffer overflow in ICE with long username](https://github.com/pjsip/pjproject/security/advisories/GHSA-j29p-pvh2-pvqp)
  [GHSA-8fj4-fv9f-hjpc - Heap use-after-free in PJSIP presense subscription termination header](https://github.com/pjsip/pjproject/security/advisories/GHSA-8fj4-fv9f-hjpc)
  [GHSA-g88q-c2hm-q7p7 - ICE session use-after-free race conditions](https://github.com/pjsip/pjproject/security/advisories/GHSA-g88q-c2hm-q7p7)
  [GHSA-x5pq-qrp4-fmrj - Out-of-bounds read in SIP multipart parsing](https://github.com/pjsip/pjproject/security/advisories/GHSA-x5pq-qrp4-fmrj)

  Resolves: #1833

