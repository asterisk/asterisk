
Change Log for Release asterisk-20.5.1
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.5.1.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.5.0...20.5.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.5.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- res_pjsip_header_funcs: Duplicate new header value, don't copy.
- res_pjsip: disable raw bad packet logging
- res_rtp_asterisk.c: Check DTLS packets against ICE candidate list
- manager.c: Prevent path traversal with GetConfig.

User Notes:
----------------------------------------


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

None

Commits By Author:
----------------------------------------

- ### Ben Ford (1):
  - manager.c: Prevent path traversal with GetConfig.

- ### George Joseph (1):
  - res_rtp_asterisk.c: Check DTLS packets against ICE candidate list

- ### Gitea (1):
  - res_pjsip_header_funcs: Duplicate new header value, don't copy.

- ### Mike Bradeen (1):
  - res_pjsip: disable raw bad packet logging


Detail:
----------------------------------------

- ### res_pjsip_header_funcs: Duplicate new header value, don't copy.
  Author: Gitea  
  Date:   2023-07-10  

  When updating an existing header the 'update' code incorrectly
  just copied the new value into the existing buffer. If the
  new value exceeded the available buffer size memory outside
  of the buffer would be written into, potentially causing
  a crash.

  This change makes it so that the 'update' now duplicates
  the new header value instead of copying it into the existing
  buffer.

- ### res_pjsip: disable raw bad packet logging
  Author: Mike Bradeen  
  Date:   2023-07-25  

  Add patch to split the log level for invalid packets received on the
  signaling port.  The warning regarding the packet will move to level 2
  so that it can still be displayed, while the raw packet will be at level
  4.

- ### res_rtp_asterisk.c: Check DTLS packets against ICE candidate list
  Author: George Joseph  
  Date:   2023-11-09  

  When ICE is in use, we can prevent a possible DOS attack by allowing
  DTLS protocol messages (client hello, etc) only from sources that
  are in the active remote candidates list.

  Resolves: GHSA-hxj9-xwr8-w8pq

- ### manager.c: Prevent path traversal with GetConfig.
  Author: Ben Ford  
  Date:   2023-11-13  

  When using AMI GetConfig, it was possible to access files outside of the
  Asterisk configuration directory by using filenames with ".." and "./"
  even while live_dangerously was not enabled. This change resolves the
  full path and ensures we are still in the configuration directory before
  attempting to access the file.

