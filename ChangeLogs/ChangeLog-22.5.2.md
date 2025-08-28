
## Change Log for Release asterisk-22.5.2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.5.2.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.5.1...22.5.2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.5.2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 0
- Security Advisories Resolved: 1
  - [GHSA-64qc-9x89-rx5j](https://github.com/asterisk/asterisk/security/advisories/GHSA-64qc-9x89-rx5j): A specifically malformed Authorization header in an incoming SIP request can cause Asterisk to crash

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- George Joseph: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-64qc-9x89-rx5j: A specifically malformed Authorization header in an incoming SIP request can cause Asterisk to crash

### Commits By Author:

- #### George Joseph (1):
  - res_pjsip_authenticator_digest: Fix SEGV if get_authorization_hdr returns NULL.


### Commit List:

-  res_pjsip_authenticator_digest: Fix SEGV if get_authorization_hdr returns NULL.

### Commit Details:

#### res_pjsip_authenticator_digest: Fix SEGV if get_authorization_hdr returns NULL.
  Author: George Joseph
  Date:   2025-08-28

  In the highly-unlikely event that get_authorization_hdr() couldn't find an
  Authorization header in a request, trying to get the digest algorithm
  would cauase a SEGV.  We now check that we have an auth header that matches
  the realm before trying to get the algorithm from it.

  Resolves: #GHSA-64qc-9x89-rx5j

