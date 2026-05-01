
## Change Log for Release asterisk-certified-18.9-cert18

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-18.9-cert18.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert17...certified-18.9-cert18)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-18.9-cert18.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 1
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- Naveen Albert: (1)

## Issue and Commit Detail:

### Closed Issues:

  - ASTERISK-30265: res_pjsip_session: Fix missing PLAR support on INVITEs

### Commits By Author:

- #### Naveen Albert (1):
  - res_pjsip_session.c: Map empty extensions in INVITEs to s.

### Commit List:

-  res_pjsip_session.c: Map empty extensions in INVITEs to s.

### Commit Details:

#### res_pjsip_session.c: Map empty extensions in INVITEs to s.
  Author: Naveen Albert
  Date:   2022-10-17

  Some SIP devices use an empty extension for PLAR functionality.

  Rather than rejecting these empty extensions, we now use the s
  extension for such calls to mirror the existing PLAR functionality
  in Asterisk (e.g. chan_dahdi).

  ASTERISK-30265 #close


