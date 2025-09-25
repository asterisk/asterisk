
## Change Log for Release asterisk-21.11.0-rc2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-21.11.0-rc2.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/21.11.0-rc1...21.11.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21.11.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 3
- Commit Authors: 1
- Issues Resolved: 3
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- George Joseph: (3)

## Issue and Commit Detail:

### Closed Issues:

  - 1457: [bug]: segmentation fault because of a wrong ari config
  - 1462: [bug]: chan_websocket isn't handling the "opus" codec correctly.
  - 1474: [bug]: Media doesn't flow for video conference after res_rtp_asterisk change to stop media flow before DTLS completes

### Commits By Author:

- #### George Joseph (3):
  - res_ari: Ensure outbound websocket config has a websocket_client_id.
  - chan_websocket: Fix codec validation and add passthrough option.
  - res_rtp_asterisk.c: Use rtp->dtls in __rtp_sendto when rtcp mux is used.


### Commit List:

-  res_rtp_asterisk.c: Use rtp->dtls in __rtp_sendto when rtcp mux is used.
-  chan_websocket: Fix codec validation and add passthrough option.
-  res_ari: Ensure outbound websocket config has a websocket_client_id.

### Commit Details:

#### res_rtp_asterisk.c: Use rtp->dtls in __rtp_sendto when rtcp mux is used.
  Author: George Joseph
  Date:   2025-09-23

  In __rtp_sendto(), the check for DTLS negotiation completion for rtcp packets
  needs to use the rtp->dtls structure instead of rtp->rtcp->dtls when
  AST_RTP_INSTANCE_RTCP_MUX is set.

  Resolves: #1474

#### chan_websocket: Fix codec validation and add passthrough option.
  Author: George Joseph
  Date:   2025-09-17

  * Fixed an issue in webchan_write() where we weren't detecting equivalent
    codecs properly.
  * Added the "p" dialstring option that puts the channel driver in
    "passthrough" mode where it will not attempt to re-frame or re-time
    media coming in over the websocket from the remote app.  This can be used
    for any codec but MUST be used for codecs that use packet headers or whose
    data stream can't be broken up on arbitrary byte boundaries. In this case,
    the remote app is fully responsible for correctly framing and timing media
    sent to Asterisk and the MEDIA text commands that could be sent over the
    websocket are disabled.  Currently, passthrough mode is automatically set
    for the opus, speex and g729 codecs.
  * Now calling ast_set_read_format() after ast_channel_set_rawreadformat() to
    ensure proper translation paths are set up when switching between native
    frames and slin silence frames.  This fixes an issue with codec errors
    when transcode_via_sln=yes.

  Resolves: #1462

#### res_ari: Ensure outbound websocket config has a websocket_client_id.
  Author: George Joseph
  Date:   2025-09-12

  Added a check to outbound_websocket_apply() that makes sure an outbound
  websocket config object in ari.conf has a websocket_client_id parameter.

  Resolves: #1457

