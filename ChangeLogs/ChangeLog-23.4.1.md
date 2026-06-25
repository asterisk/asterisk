
## Change Log for Release asterisk-23.4.1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-23.4.1.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/23.4.0...23.4.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-23.4.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 19
- Commit Authors: 6
- Issues Resolved: 0
- Security Advisories Resolved: 20
  - [GHSA-3g56-cgrh-95p5](https://github.com/asterisk/asterisk/security/advisories/GHSA-3g56-cgrh-95p5): chan_unistim DIALPAGE digit handling can overflow phone_number and crash Asterisk
  - [GHSA-3rhj-hhw7-m6fw](https://github.com/asterisk/asterisk/security/advisories/GHSA-3rhj-hhw7-m6fw): NULL Pointer Dereference in HTTP AMI Digest Authentication
  - [GHSA-4pgv-j3mr-3rcp](https://github.com/asterisk/asterisk/security/advisories/GHSA-4pgv-j3mr-3rcp): Reflected XSS in Phone Provisioning HTTP Error Pages
  - [GHSA-589g-qgf8-m6mx](https://github.com/asterisk/asterisk/security/advisories/GHSA-589g-qgf8-m6mx): Stack buffer overflow in MWI NOTIFY Message-Account parsing
  - [GHSA-746q-794h-cc7f](https://github.com/asterisk/asterisk/security/advisories/GHSA-746q-794h-cc7f): Out-of-Bounds Read in Q.931 Information Element Parser (H.323 Addon)
  - [GHSA-8jhw-m2hg-vp3h](https://github.com/asterisk/asterisk/security/advisories/GHSA-8jhw-m2hg-vp3h): Heap Buffer Overflow in OGG/Speex File Playback (format_ogg_speex)
  - [GHSA-8jw3-ccr9-xrmf](https://github.com/asterisk/asterisk/security/advisories/GHSA-8jw3-ccr9-xrmf): Buffer over-read in Asterisk PJSIP MWI body parser
  - [GHSA-g8q2-p36q-94f6](https://github.com/asterisk/asterisk/security/advisories/GHSA-g8q2-p36q-94f6): Heap-use-after-free in Asterisk PJSIP TCP/SDP handling when TCP connection closes during SDP processing
  - [GHSA-h5hv-jmgj-92q2](https://github.com/asterisk/asterisk/security/advisories/GHSA-h5hv-jmgj-92q2): CVE-2022-37325 fix is absent from current chan_ooh323 Q.931 party-number parser
  - [GHSA-j2mm-57pq-jh94](https://github.com/asterisk/asterisk/security/advisories/GHSA-j2mm-57pq-jh94): Possible RED T.140 Generation Accumulation OOB Write
  - [GHSA-mxgm-8c6f-5p8f](https://github.com/asterisk/asterisk/security/advisories/GHSA-mxgm-8c6f-5p8f): Stack buffer overflow in res_xmpp XMPP namespace prefix handling
  - [GHSA-ph27-3m5q-mj5m](https://github.com/asterisk/asterisk/security/advisories/GHSA-ph27-3m5q-mj5m): SQL Injection in cel_pgsql and cel_tds via CELGenUserEvent eventtype Field
  - [GHSA-q9fr-m7g8-6ph5](https://github.com/asterisk/asterisk/security/advisories/GHSA-q9fr-m7g8-6ph5): Asterisk app_sms.c copies externally controlled SMS lengths into fixed in-struct buffers
  - [GHSA-qf8j-jp7h-c5hx](https://github.com/asterisk/asterisk/security/advisories/GHSA-qf8j-jp7h-c5hx): Out-of-Bounds Write in Codec2 Decoder Due to Floor/Ceil Sample Count Mismatch
  - [GHSA-r6c2-hwc2-j4mp](https://github.com/asterisk/asterisk/security/advisories/GHSA-r6c2-hwc2-j4mp): LDAP Filter Injection in res_config_ldap via SIP Username (Unauthenticated Information Disclosure)
  - [GHSA-vfhr-r9x9-c687](https://github.com/asterisk/asterisk/security/advisories/GHSA-vfhr-r9x9-c687): Possible RED T.140 Heap Buffer Overflow
  - [GHSA-vrfp-mg3q-3959](https://github.com/asterisk/asterisk/security/advisories/GHSA-vrfp-mg3q-3959): ARI setChannelVar bypasses live_dangerously and permits FILE() writes
  - [GHSA-wcvv-g26m-wx5c](https://github.com/asterisk/asterisk/security/advisories/GHSA-wcvv-g26m-wx5c): ARI REST-over-WebSocket read-only bypass allows arbitrary module path load and conditional RCE
  - [GHSA-x348-j6c9-77f3](https://github.com/asterisk/asterisk/security/advisories/GHSA-x348-j6c9-77f3): Stack Buffer Overflow in H.323 ooTrace() via Unbounded vsprintf into Fixed 2048-byte Buffer
  - [GHSA-xgj6-2gc5-5x9c](https://github.com/asterisk/asterisk/security/advisories/GHSA-xgj6-2gc5-5x9c): ast_loggrabber executes python script in world writable directory(`/tmp`) leading to potential privilege escalation And RCE

### User Notes:


### Upgrade Notes:


### Developer Notes:

- #### ARI: Make ARI applications respect live_dangerously.
  ARI applications can no longer call "dangerous" dialplan
  functions like DB(), FILE(), SHELL(), CURL(), STAT(), etc. without
  enabling "live_dangerously" in asterisk.conf.
  Resolves: #GHSA-vrfp-mg3q-3959


### Commit Authors:

- George Joseph: (6)
- Mike Bradeen: (3)
- Milan Kyselica: (7)
- Pengpeng Hou: (1)
- Roberto Paleari: (1)
- ThatTotallyRealMyth: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-3g56-cgrh-95p5: chan_unistim DIALPAGE digit handling can overflow phone_number and crash Asterisk
  - !GHSA-3rhj-hhw7-m6fw: NULL Pointer Dereference in HTTP AMI Digest Authentication
  - !GHSA-4pgv-j3mr-3rcp: Reflected XSS in Phone Provisioning HTTP Error Pages
  - !GHSA-589g-qgf8-m6mx: Stack buffer overflow in MWI NOTIFY Message-Account parsing
  - !GHSA-746q-794h-cc7f: Out-of-Bounds Read in Q.931 Information Element Parser (H.323 Addon)
  - !GHSA-8jhw-m2hg-vp3h: Heap Buffer Overflow in OGG/Speex File Playback (format_ogg_speex)
  - !GHSA-8jw3-ccr9-xrmf: Buffer over-read in Asterisk PJSIP MWI body parser
  - !GHSA-g8q2-p36q-94f6: Heap-use-after-free in Asterisk PJSIP TCP/SDP handling when TCP connection closes during SDP processing
  - !GHSA-h5hv-jmgj-92q2: CVE-2022-37325 fix is absent from current chan_ooh323 Q.931 party-number parser
  - !GHSA-j2mm-57pq-jh94: Possible RED T.140 Generation Accumulation OOB Write
  - !GHSA-mxgm-8c6f-5p8f: Stack buffer overflow in res_xmpp XMPP namespace prefix handling
  - !GHSA-ph27-3m5q-mj5m: SQL Injection in cel_pgsql and cel_tds via CELGenUserEvent eventtype Field
  - !GHSA-q9fr-m7g8-6ph5: Asterisk app_sms.c copies externally controlled SMS lengths into fixed in-struct buffers
  - !GHSA-qf8j-jp7h-c5hx: Out-of-Bounds Write in Codec2 Decoder Due to Floor/Ceil Sample Count Mismatch
  - !GHSA-r6c2-hwc2-j4mp: LDAP Filter Injection in res_config_ldap via SIP Username (Unauthenticated Information Disclosure)
  - !GHSA-vfhr-r9x9-c687: Possible RED T.140 Heap Buffer Overflow
  - !GHSA-vrfp-mg3q-3959: ARI setChannelVar bypasses live_dangerously and permits FILE() writes
  - !GHSA-wcvv-g26m-wx5c: ARI REST-over-WebSocket read-only bypass allows arbitrary module path load and conditional RCE
  - !GHSA-x348-j6c9-77f3: Stack Buffer Overflow in H.323 ooTrace() via Unbounded vsprintf into Fixed 2048-byte Buffer
  - !GHSA-xgj6-2gc5-5x9c: ast_loggrabber executes python script in world writable directory(`/tmp`) leading to potential privilege escalation And RCE

### Commits By Author:

- #### George Joseph (6):
  - chan_unistim.c: Prevent overrun of phone_number field.
  - res_ari: Ensure read-only users are properly authorized via REST Over WebSocket.
  - pjsip_message_filter: Use pj_strdup instead of pj_strassign to save local address.
  - ooh323c/ooq931.c: Ensure ooQ931Decode doesn't run out-of-bounds.
  - ARI: Make ARI applications respect live_dangerously.
  - res_rtp_asterisk.c: Address 2 potential T.140 RED buffer overruns.

- #### Mike Bradeen (3):
  - ooh323c: not checking for IE minimum length
  - manager: Use remote address in user error logging
  - ooh323: Prevent potential buffer overflow in trace logging

- #### Milan Kyselica (7):
  - res_xmpp: Fix stack buffer overflow in namespace prefix handling
  - res_pjsip_pubsub: Add width limit to sscanf in MWI NOTIFY parser
  - res_config_ldap: Escape LDAP filter values per RFC 4515
  - cel_pgsql, cel_tds: Escape eventtype field to prevent SQL injection
  - http: Escape error page text to prevent reflected XSS
  - codec_codec2: Only process complete Codec2 frames in decoder
  - format_ogg_speex: Add bounds check to prevent heap buffer overflow

- #### Pengpeng Hou (1):
  - app_sms: Bound protocol 1 SMS unpacking to fixed-size buffers

- #### Roberto Paleari (1):
  - res/res_pjsip_pubsub.c: Fix buffer over-read in MWI body parser

- #### ThatTotallyRealMyth (1):
  - ast_loggrabber: Install the ast_tsconvert.py script to a secure temp directory.

### Commit List:

-  ast_loggrabber: Install the ast_tsconvert.py script to a secure temp directory.
-  chan_unistim.c: Prevent overrun of phone_number field.
-  ooh323c: not checking for IE minimum length
-  res_ari: Ensure read-only users are properly authorized via REST Over WebSocket.
-  pjsip_message_filter: Use pj_strdup instead of pj_strassign to save local address.
-  ooh323c/ooq931.c: Ensure ooQ931Decode doesn't run out-of-bounds.
-  ARI: Make ARI applications respect live_dangerously.
-  res_rtp_asterisk.c: Address 2 potential T.140 RED buffer overruns.
-  res/res_pjsip_pubsub.c: Fix buffer over-read in MWI body parser
-  manager: Use remote address in user error logging
-  ooh323: Prevent potential buffer overflow in trace logging
-  app_sms: Bound protocol 1 SMS unpacking to fixed-size buffers
-  res_xmpp: Fix stack buffer overflow in namespace prefix handling
-  res_pjsip_pubsub: Add width limit to sscanf in MWI NOTIFY parser
-  res_config_ldap: Escape LDAP filter values per RFC 4515
-  cel_pgsql, cel_tds: Escape eventtype field to prevent SQL injection
-  http: Escape error page text to prevent reflected XSS
-  codec_codec2: Only process complete Codec2 frames in decoder
-  format_ogg_speex: Add bounds check to prevent heap buffer overflow

### Commit Details:

#### ast_loggrabber: Install the ast_tsconvert.py script to a secure temp directory.
  Author: ThatTotallyRealMyth
  Date:   2026-03-19

  The ast_tsconvert.py script called by ast_loggrabber is now installed in a
  temporary directory that isn't world readable or writable.

  Resolves: #GHSA-xgj6-2gc5-5x9c

#### chan_unistim.c: Prevent overrun of phone_number field.
  Author: George Joseph
  Date:   2026-06-15

  Add a check to key_dial_page() to ensure that dialed digits won't overrun
  the phone_number field.

  Resolves: #GHSA-3g56-cgrh-95p5

#### ooh323c: not checking for IE minimum length
  Author: Mike Bradeen
  Date:   2022-06-06

  When decoding q.931 encoded calling/called number
  now checking for length being less than minimum required.

  Resolves: #GHSA-h5hv-jmgj-92q2

#### res_ari: Ensure read-only users are properly authorized via REST Over WebSocket.
  Author: George Joseph
  Date:   2026-06-12

  The REST over WebSocket path now properly prevents non-GET methods from
  being executed on inbound WebSockets.

  * The query parameters from the original incoming GET request that caused the
  upgrade to WebSocket are now passed to all REST requests that come from the
  client. This ensures that if the client authenticated with a read-only
  userid using the "api_key" query_string parameter, REST requests coming
  in over the WebSocket will only be able to execute GETs on resources.
  The HTTP headers were already passed to the REST requests so if the
  client had authenticated via an "Authorization" it was properly handled.

  * New tests have been added to test_ari.c to check that read-only users
  are properly denied access to resources using non-GET methods.  Several
  memory leaks were also squashed.

  Resolves: #GHSA-wcvv-g26m-wx5c

#### pjsip_message_filter: Use pj_strdup instead of pj_strassign to save local address.
  Author: George Joseph
  Date:   2026-06-10

  The filter_on_tx_message() function was using pj_strassign() to save the pointer
  of the pjproject transport local address to a local pj_str_t variable.  That
  variable was ultimately used to set the Contact header's uri->host and the SDP
  connection attribute's address again using pj_strassign.  pj_strassign() doesn't
  copy the actual value of the pj_str_t however, it just copies the pointer so
  if a connection-oriented transport is disconnected before the 200 OK with the
  SDP is sent, those pointers will be invalid which can cause use-after-free
  issues. To prevent this, filter_on_tx_message() now uses pj_strdup with the
  tdata->pool as the backing store to save the local IP address to the local
  variable.  pj_strassign() can then be used safely later on since the tdata
  will be available for the life of the transaction.

  Resolves: #GHSA-g8q2-p36q-94f6

#### ooh323c/ooq931.c: Ensure ooQ931Decode doesn't run out-of-bounds.
  Author: George Joseph
  Date:   2026-06-02

  Several bounds checks have been edded to ooQ931Decode to prevent it from
  running past the end of the data buffer when parsing information elements.

  Resolves: #GHSA-746q-794h-cc7f

#### ARI: Make ARI applications respect live_dangerously.
  Author: George Joseph
  Date:   2026-05-21

  DeveloperNote: ARI applications can no longer call "dangerous" dialplan
  functions like DB(), FILE(), SHELL(), CURL(), STAT(), etc. without
  enabling "live_dangerously" in asterisk.conf.

  Resolves: #GHSA-vrfp-mg3q-3959

#### res_rtp_asterisk.c: Address 2 potential T.140 RED buffer overruns.
  Author: George Joseph
  Date:   2026-04-27

  * Add check to red_t140_to_red() to ensure that the new primary payload
  can't cause the rtp_red->len array items to wrap or cause an overrun of
  the rtp_red->t140red_data buffer.

  * Add check to rtp_red_buffer() to ensure that a T.140 frame to be sent
  can't cause rtp_red->len array items to wrap or cause an overrun of
  the rtp_red->buf_data buffer.

  Resolves: #GHSA-vfhr-r9x9-c687
  Resolves: #GHSA-j2mm-57pq-jh94

#### res/res_pjsip_pubsub.c: Fix buffer over-read in MWI body parser
  Author: Roberto Paleari
  Date:   2026-04-29

  Add constraint checks to prevent unauthenticated users from crashing Asterisk
  instance by sending a crafted inbound SIP NOTIFY request with "Content-Type:
  application/simple-message-summary".

  Resolves: #GHSA-8jw3-ccr9-xrmf

#### manager: Use remote address in user error logging
  Author: Mike Bradeen
  Date:   2026-03-30

  To avoid a potential null dereference use the remote address
  in error logging when there is no user or the user acl fails.

  Resolves: #GHSA-3rhj-hhw7-m6fw

#### ooh323: Prevent potential buffer overflow in trace logging
  Author: Mike Bradeen
  Date:   2026-03-31

  Replace a call to vsprintf with a call to ast_vasprintf to
  prevent a possible buffer overflow.

  Resolves: #GHSA-x348-j6c9-77f3

#### app_sms: Bound protocol 1 SMS unpacking to fixed-size buffers
  Author: Pengpeng Hou
  Date:   2026-04-01

  The protocol 1 unpack helpers trusted externally controlled lengths and wrote
   them directly into fixed-size buffers in sms_t. Clamp the address, header,
   and body copies to the destination array sizes so malformed messages cannot
   overwrite adjacent state.

  Resolves: #GHSA-q9fr-m7g8-6ph5

#### res_xmpp: Fix stack buffer overflow in namespace prefix handling
  Author: Milan Kyselica
  Date:   2026-03-26

  The snprintf size parameter in xmpp_action_hook() is computed from
  the attacker-controlled namespace prefix length and is not bounded
  by the 256-byte stack buffer size. When a remote XMPP peer sends a
  stanza with a child element whose namespace prefix exceeds 249
  characters, snprintf writes past the buffer boundary.

  Use sizeof(attr) as the snprintf size limit and %.*s precision to
  extract only the prefix portion of the element name, preserving
  the original truncation behavior for valid inputs.

  Resolves: #GHSA-mxgm-8c6f-5p8f

#### res_pjsip_pubsub: Add width limit to sscanf in MWI NOTIFY parser
  Author: Milan Kyselica
  Date:   2026-03-24

  The parse_simple_message_summary() function uses sscanf with an
  unbounded %s format specifier to parse the Message-Account field
  from incoming SIP NOTIFY bodies into a fixed-size 512-byte stack
  buffer (PJSIP_MAX_URL_SIZE). A single unauthenticated SIP NOTIFY
  with a Message-Account value exceeding 512 bytes overflows the
  buffer, corrupting adjacent stack data and permanently disabling
  the PJSIP transport layer without crashing the process.

  Add a width specifier (%511s) to limit the sscanf write to
  PJSIP_MAX_URL_SIZE - 1 bytes plus the NUL terminator, matching
  the destination buffer size.

  Resolves: #GHSA-589g-qgf8-m6mx

#### res_config_ldap: Escape LDAP filter values per RFC 4515
  Author: Milan Kyselica
  Date:   2026-03-23

  The LDAP realtime driver constructs search filters by directly
  concatenating user-supplied values without RFC 4515 escaping.
  When LDAP is used as a realtime backend for endpoint
  identification, characters with special meaning in LDAP filters
  (*, (, ), \) can be injected via the SIP From header username.

  Add ldap_filter_escape_value() that escapes RFC 4515 special
  characters to their \HH hex representation, and apply it to
  non-LIKE query values. The LIKE query path preserves the existing
  wildcard conversion behavior with a note for maintainers.

  Resolves: #GHSA-r6c2-hwc2-j4mp

#### cel_pgsql, cel_tds: Escape eventtype field to prevent SQL injection
  Author: Milan Kyselica
  Date:   2026-03-23

  The eventtype column handler in cel_pgsql.c inserts
  record.user_defined_name directly into the SQL query without
  calling PQescapeStringConn(), while all other string fields in
  the same function are properly escaped. Similarly, cel_tds.c
  passes the raw user_defined_name into the SQL INSERT without
  routing it through anti_injection(), while all other fields are
  processed through that function.

  For cel_pgsql.c, escape the eventtype value using
  PQescapeStringConn(), matching the existing pattern used for all
  other string fields at lines 308-331 of the same function.

  For cel_tds.c, route the eventtype value through
  anti_injection() consistent with how all other fields are handled
  in the same function.

  Resolves: #GHSA-ph27-3m5q-mj5m

#### http: Escape error page text to prevent reflected XSS
  Author: Milan Kyselica
  Date:   2026-04-08

  The text parameter in ast_http_create_response() is inserted into
  the HTML body without escaping, while the server name on the same
  page is properly escaped via ast_xml_escape(). When res_phoneprov
  passes the decoded request URI as the text of a 404 response, HTML
  metacharacters in the URI are rendered by the browser.

  Apply ast_xml_escape() to the text parameter before inserting it
  into the HTML template, using the same function already used for
  the server name.

  Resolves: #GHSA-4pgv-j3mr-3rcp

#### codec_codec2: Only process complete Codec2 frames in decoder
  Author: Milan Kyselica
  Date:   2026-04-08

  The codec2_samples() function uses floor division (160 * datalen/6)
  to compute expected output samples, but the decode loop condition
  (x < datalen) iterates with ceiling behavior when datalen is not a
  multiple of CODEC2_FRAME_LEN. This mismatch causes the loop to
  decode one extra frame beyond what the framework bounds check
  budgeted for, leading to an out-of-bounds write on the output buffer.

  Change the loop condition to only process complete frames, matching
  the floor-division behavior of codec2_samples(). This also prevents
  an out-of-bounds read on the input side when fewer than
  CODEC2_FRAME_LEN bytes remain.

  Resolves: #GHSA-qf8j-jp7h-c5hx

#### format_ogg_speex: Add bounds check to prevent heap buffer overflow
  Author: Milan Kyselica
  Date:   2026-03-23

  The ogg_speex_read() function copies OGG packet data via memcpy()
  without validating the packet size against the destination buffer
  (BUF_SIZE = 200 bytes). A crafted .spx file with an oversized OGG
  audio packet causes a heap buffer overflow that corrupts the
  adjacent speex_desc structure containing libogg heap pointers,
  leading to a crash (SIGSEGV) on playback.

  Add a bounds check for both negative and oversized values before
  the memcpy, consistent with how format_ogg_vorbis bounds its reads
  via ov_read().

  Resolves: #GHSA-8jhw-m2hg-vp3h

