
## Change Log for Release asterisk-certified-22.8-cert6

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-22.8-cert6.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-22.8-cert5...certified-22.8-cert6)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-22.8-cert6.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 1
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:

- #### res_rtp_asterisk: Assign unique ICE host candidate priorities.
  ICE host candidates now receive distinct priorities.
  When include_local_address is enabled in rtp.conf, local interface
  candidates have higher priority than additional mapped candidates.
  This can change ICE media path selection. No configuration changes
  are required.


### Developer Notes:


### Commit Authors:

- Mike Bradeen: (1)

## Issue and Commit Detail:

### Closed Issues:

  - #2298: [bug]: ICE candidates of the same type have duplicate priorities

### Commits By Author:

- **Mike Bradeen** (1):
  - res_rtp_asterisk: Assign unique ICE host candidate priorities.

### Commit List:

-  res_rtp_asterisk: Assign unique ICE host candidate priorities.

### Commit Details:

__res_rtp_asterisk: Assign unique ICE host candidate priorities.__
  Author: Mike Bradeen
  Date:   2026-11-18

  Add local interfaces above additional mappings, making sure that
  each gets a unique priority as they are added.

  Fixes: #2298

  UpgradeNote: ICE host candidates now receive distinct priorities.
  When include_local_address is enabled in rtp.conf, local interface
  candidates have higher priority than additional mapped candidates.
  This can change ICE media path selection. No configuration changes
  are required.


