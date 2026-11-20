
## Change Log for Release asterisk-certified-20.7-cert13

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-20.7-cert13.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-20.7-cert12...certified-20.7-cert13)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-20.7-cert13.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 2
- Commit Authors: 2
- Issues Resolved: 2
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
- Naveen Albert: (1)

## Issue and Commit Detail:

### Closed Issues:

  - #1369: [bug]: test_res_prometheus: Compilation failure in devmode due to curlopts not using long type
  - #2298: [bug]: ICE candidates of the same type have duplicate priorities

### Commits By Author:

- **Mike Bradeen** (1):
  - res_rtp_asterisk: Assign unique ICE host candidate priorities.

- **Naveen Albert** (1):
  - test_res_prometheus: Fix compilation failure on Debian 13.

### Commit List:

-  test_res_prometheus: Fix compilation failure on Debian 13.
-  res_rtp_asterisk: Assign unique ICE host candidate priorities.

### Commit Details:

__test_res_prometheus: Fix compilation failure on Debian 13.__
  Author: Naveen Albert
  Date:   2025-08-14

  curl_easy_setopt expects long types, so be explicit.

  Resolves: #1369

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


