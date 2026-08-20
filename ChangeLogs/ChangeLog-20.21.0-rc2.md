
## Change Log for Release asterisk-20.21.0-rc2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.21.0-rc2.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.21.0-rc1...20.21.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.21.0-rc2.tar.gz)  
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

- George Joseph: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 2085: [bug]: res_http_websocket: SEGV if we're a server and receive a PONG frame

### Commits By Author:

- #### George Joseph (1):
  - res_http_websocket: Check for client before handling PONG frames.

### Commit List:

-  res_http_websocket: Check for client before handling PONG frames.

### Commit Details:

#### res_http_websocket: Check for client before handling PONG frames.
  Author: George Joseph
  Date:   2026-08-13

  websocket_handled_pong_or_close() now checks that session->client is valid
  before trying to check missed_pong_count.

  Resolves: #2085

