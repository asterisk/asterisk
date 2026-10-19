
## Change Log for Release asterisk-certified-22.8-cert5

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-22.8-cert5.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-22.8-cert4...certified-22.8-cert5)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-22.8-cert5.tar.gz)  
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

- Mike Bradeen: (1)

## Issue and Commit Detail:

### Closed Issues:

  - #2230: [bug]: The timestamp on the first packet after in-band digit generation is incorrect

### Commits By Author:

- **Mike Bradeen** (1):
  - res_rtp_asterisk: Fix rtp timestamp calculation during DTMF generation

### Commit List:

-  res_rtp_asterisk: Fix rtp timestamp calculation during DTMF generation

### Commit Details:

__res_rtp_asterisk: Fix rtp timestamp calculation during DTMF generation__
  Author: Mike Bradeen
  Date:   2026-10-13

  Avoid advancing the last tx timestamp when audio is suppressed during DTMF
  generation to avoid mis-calculating the timestamp on the first packet after
  the RTP Event end.

  Resolves: #2230


