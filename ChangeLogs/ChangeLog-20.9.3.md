
## Change Log for Release asterisk-20.9.3

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.9.3.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.9.2...20.9.3)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.9.3.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 0
- Security Advisories Resolved: 1
  - [GHSA-v428-g3cw-7hv9](https://github.com/asterisk/asterisk/security/advisories/GHSA-v428-g3cw-7hv9): A malformed Contact or Record-Route URI in an incoming SIP request can cause Asterisk to crash when res_resolver_unbound is used

### User Notes:


### Upgrade Notes:


### Commit Authors:

- George Joseph: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-v428-g3cw-7hv9: A malformed Contact or Record-Route URI in an incoming SIP request can cause Asterisk to crash when res_resolver_unbound is used

### Commits By Author:

- #### George Joseph (1):
  - res_resolver_unbound: Test for NULL ub_result in unbound_resolver_callback


### Commit List:

-  res_resolver_unbound: Test for NULL ub_result in unbound_resolver_callback

### Commit Details:

#### res_resolver_unbound: Test for NULL ub_result in unbound_resolver_callback
  Author: George Joseph
  Date:   2024-08-12

  The ub_result pointer passed to unbound_resolver_callback by
  libunbound can be NULL if the query was for something malformed
  like `.1` or `[.1]`.  If it is, we now set a 'ns_r_formerr' result
  and return instead of crashing with a SEGV.  This causes pjproject
  to simply cancel the transaction with a "No answer record in the DNS
  response" error.  The existing "off nominal" unit test was also
  updated to check this condition.

  Although not necessary for this fix, we also made
  ast_dns_resolver_completed() tolerant of a NULL result.

  Resolves: GHSA-v428-g3cw-7hv9

