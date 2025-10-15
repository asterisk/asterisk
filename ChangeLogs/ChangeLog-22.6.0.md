
## Change Log for Release asterisk-22.6.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.6.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.5.2...22.6.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.6.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 54
- Commit Authors: 22
- Issues Resolved: 40
- Security Advisories Resolved: 0

### User Notes:

- #### app_queue.c: Add new global 'log_unpause_on_reason_change'                      
  Add new global option 'log_unpause_on_reason_change' that
  is default disabled. When enabled cause addition of UNPAUSE event on
  every re-PAUSE with reason changed.

- #### pbx_builtins: Allow custom tone for WaitExten.                                  
  The tone used while waiting for digits in WaitExten
  can now be overridden by specifying an argument for the 'd'
  option.

- #### res_tonedetect: Add option for TONE_DETECT detection to auto stop.              
  The 'e' option for TONE_DETECT now allows detection to
  be disabled automatically once the desired number of matches have
  been fulfilled, which can help prevent race conditions in the
  dialplan, since TONE_DETECT does not need to be disabled after
  a hit.

- #### sorcery: Prevent duplicate objects and ensure missing objects are created on u..
  Users relying on Sorcery multiple writable backends configurations
  (e.g., astdb + realtime) may now enable update_or_create_on_update_miss = yes
  in sorcery.conf to ensure missing objects are recreated after temporary backend
  failures. Default behavior remains unchanged unless explicitly enabled.

- #### chan_websocket: Allow additional URI parameters to be added to the outgoing URI.
  A new WebSocket channel driver option `v` has been added to the
  Dial application that allows you to specify additional URI parameters on
  outgoing connections. Run `core show application Dial` from the Asterisk CLI
  to see how to use it.

- #### app_chanspy: Add option to not automatically answer channel.                    
  ChanSpy and ExtenSpy can now be configured to not
  automatically answer the channel by using the 'N' option.

- #### cel: Add STREAM_BEGIN, STREAM_END and DTMF event types.                         
  Enabling the tracking of the
  STREAM_BEGIN and the STREAM_END event
  types in cel.conf will log media files and
  music on hold played to each channel.
  The STREAM_BEGIN event's extra field will
  contain a JSON with the file details (path,
  format and language), or the class name, in
  case of music on hold is played. The DTMF
  event's extra field will contain a JSON with
  the digit and the duration in milliseconds.

- #### res_srtp: Add menuselect options to enable AES_192, AES_256 and AES_GCM         
  Options are now available in the menuselect "Resource Modules"
  category that allow you to enable the AES_192, AES_256 and AES_GCM
  cipher suites in res_srtp. Of course, libsrtp and OpenSSL must support
  them but modern versions do.  Previously, the only way to enable them was
  to set the CFLAGS environment variable when running ./configure.
  The default setting is to disable them preserving existing behavior.

- #### cdr: add CANCEL dispostion in CDR                                               
  A new CDR option "canceldispositionenabled" has been added
  that when set to true, the NO ANSWER disposition will be split into
  two dispositions: CANCEL and NO ANSWER. The default value is 'no'

- #### func_curl: Allow auth methods to be set.                                        
  The httpauth field in CURLOPT now allows the authentication
  methods to be set.

- #### Media over Websocket Channel Driver                                             
  A new channel driver "chan_websocket" is now available. It can
  exchange media over both inbound and outbound websockets and will both frame
  and re-time the media it receives.
  See http://s.asterisk.net/mow for more information.
  The ARI channels/externalMedia API now includes support for the

### Upgrade Notes:


### Developer Notes:

- #### ARI: Add command to indicate progress to a channel                              
  A new ARI endpoint is available at `/channels/{channelId}/progress` to indicate progress to a channel.

- #### options:  Change ast_options from ast_flags to ast_flags64.                     
  The 32-bit ast_options has no room left to accomodate new
  options and so has been converted to an ast_flags64 structure. All internal
  references to ast_options have been updated to use the 64-bit flag
  manipulation macros.  External module references to the 32-bit ast_options
  should continue to work on little-endian systems because the
  least-significant bytes of a 64 bit integer will be in the same location as a
  32-bit integer.  Because that's not the case on big-endian systems, we've
  swapped the bytes in the flags manupulation macros on big-endian systems
  so external modules should still work however you are encouraged to test.


### Commit Authors:

- Alexei Gradinari: (2)
- Alexey Khabulyak: (2)
- Allan Nathanson: (1)
- Artem Umerov: (1)
- Ben Ford: (1)
- George Joseph: (12)
- Igor Goncharovsky: (2)
- Jaco Kroon: (1)
- Joe Garlick: (1)
- Jose Lopes: (1)
- Kodokaii: (1)
- Martin Tomec: (1)
- Mike Bradeen: (1)
- Mkmer: (1)
- Naveen Albert: (15)
- Sean Bright: (2)
- Sperl Viktor: (2)
- Stanislav Abramenkov: (1)
- Stuart Henderson: (1)
- Sven Kube: (2)
- Tinet-Mucw: (2)
- Zhou_jiajian: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 401: [bug]: app_dial: Answer Gosub option passthrough regression
  - 927: [bug]: no audio when media source changed during the call
  - 1176: [bug]: ast_slinear_saturated_multiply_float produces potentially audible distortion artifacts
  - 1259: [bug]: New TenantID feature doesn't seem to set CDR for incoming calls
  - 1260: [bug]: Asterisk sends RTP audio stream before ICE/DTLS completes
  - 1269: [bug]: MixMonitor with D option produces corrupt file
  - 1273: [bug]: When executed with GotoIf, the action Redirect does not take effect and causes confusion in dialplan execution.
  - 1280: [improvement]: logging playback of audio per channel
  - 1289: [bug]: sorcery - duplicate objects from multiple backends and backend divergence on update
  - 1301: [bug]: sig_analog: fgccamamf doesn't handle STP, STP2, or STP3
  - 1304: [bug]: FLUSH_MEDIA does not reset frame_queue_length in WebSocket channel
  - 1305: [bug]: Realtime incorrectly falls back to next backend on record-not-found (SQL_NO_DATA), causing incorrect behavior and delay
  - 1307: [improvement]: ast_tls_cert: Allow certificate validity to be configurable
  - 1309: [bug]: Crash with C++ alternative storage backend enabled
  - 1315:  [bug]: When executed with dialplan, the action Redirect does not take effect.
  - 1317: [bug]: AGI command buffer overflow with long variables
  - 1321: [improvement]: app_agent_pool: Remove obsolete documentation
  - 1323: [new-feature]: add CANCEL dispostion in CDR
  - 1327: [bug]: res_stasis_device_state: can't delete ARI Devicestate after asterisk restart
  - 1332: [new-feature]: func_curl: Allow auth methods to be set
  - 1349: [bug]: Race condition on redirect can cause missing Diversion header
  - 1352: [improvement]: Websocket channel with custom URI
  - 1353: [bug]: AST_DATA_DIR/sounds/custom directory not searched
  - 1358: [new-feature]: app_chanspy: Add option to not automatically answer channel
  - 1364: [bug]: bridge.c: BRIDGE_NOANSWER not always obeyed
  - 1366: [improvement]: func_frame_drop: Handle allocation failure properly
  - 1369: [bug]: test_res_prometheus: Compilation failure in devmode due to curlopts not using long type
  - 1371: [improvement]: func_frame_drop: Add debug messages for frames that can be dropped
  - 1375: [improvement]: dsp.c: Improve logging in tone_detect().
  - 1378: [bug]: chan_dahdi: dialmode feature is not properly reset between calls
  - 1380: [bug]: sig_analog: Segfault due to calling strcmp on NULL
  - 1384: [bug]: chan_websocket: asterisk crashes on hangup after STOP_MEDIA_BUFFERING command with id
  - 1386: [bug]: enabling announceposition_only_up prevents any queue position announcements
  - 1390: [improvement]: res_tonedetect: Add option to automatically end detection in TONE_DETECT
  - 1394: [improvement]: sig_analog: Skip Caller ID spill if Caller ID is disabled
  - 1396: [new-feature]: pbx_builtins: Make tone option for WaitExten configurable
  - 1401: [bug]: app_waitfornoise timeout is always less then configured because of time() usage
  - 1457: [bug]: segmentation fault because of a wrong ari config
  - 1462: [bug]: chan_websocket isn't handling the "opus" codec correctly.
  - 1474: [bug]: Media doesn't flow for video conference after res_rtp_asterisk change to stop media flow before DTLS completes

### Commits By Author:

- #### Alexei Gradinari (2):
  - res_config_odbc: Prevent Realtime fallback on record-not-found (SQL_NO_DATA)
  - sorcery: Prevent duplicate objects and ensure missing objects are created on u..

- #### Alexey Khabulyak (2):
  - app_dial.c: Moved channel lock to prevent deadlock
  - pbx_lua.c: segfault when pass null data to term_color function

- #### Allan Nathanson (1):
  - file.c: with "sounds_search_custom_dir = yes", search "custom" directory

- #### Artem Umerov (1):
  - Fix missing ast_test_flag64 in extconf.c

- #### Ben Ford (1):
  - res_rtp_asterisk: Don't send RTP before DTLS has negotiated.

- #### George Joseph (12):
  - Media over Websocket Channel Driver
  - app_mixmonitor:  Update the documentation concerning the "D" option.
  - cdr.c: Set tenantid from party_a->base instead of chan->base.
  - options:  Change ast_options from ast_flags to ast_flags64.
  - res_srtp: Add menuselect options to enable AES_192, AES_256 and AES_GCM
  - channelstorage_cpp_map_name_id.cc: Refactor iterators for thread-safety.
  - xmldoc.c: Fix rendering of CLI output.
  - chan_websocket: Fix buffer overrun when processing TEXT websocket frames.
  - chan_websocket: Allow additional URI parameters to be added to the outgoing URI.
  - res_ari: Ensure outbound websocket config has a websocket_client_id.
  - chan_websocket: Fix codec validation and add passthrough option.
  - res_rtp_asterisk.c: Use rtp->dtls in __rtp_sendto when rtcp mux is used.

- #### Igor Goncharovsky (2):
  - app_waitforsilence.c: Use milliseconds to calculate timeout time
  - app_queue.c: Add new global 'log_unpause_on_reason_change'

- #### Jaco Kroon (1):
  - res_musiconhold: Appropriately lock channel during start.

- #### Joe Garlick (1):
  - chan_websocket.c: Add DTMF messages

- #### Jose Lopes (1):
  - res_stasis_device_state: Fix delete ARI Devicestates after asterisk restart.

- #### Martin Tomec (1):
  - chan_pjsip.c: Change SSRC after media source change

- #### Mike Bradeen (1):
  - res_pjsip_diversion: resolve race condition between Diversion header processin..

- #### Naveen Albert (15):
  - sig_analog: Properly handle STP, ST2P, and ST3P for fgccamamf.
  - ast_tls_cert: Make certificate validity configurable.
  - app_agent_pool: Remove documentation for removed option.
  - func_curl: Allow auth methods to be set.
  - bridge.c: Obey BRIDGE_NOANSWER variable to skip answering channel.
  - func_frame_drop: Handle allocation failure properly.
  - test_res_prometheus: Fix compilation failure on Debian 13.
  - func_frame_drop: Add debug messages for dropped frames.
  - app_chanspy: Add option to not automatically answer channel.
  - dsp.c: Improve debug logging in tone_detect().
  - sig_analog: Fix SEGV due to calling strcmp on NULL.
  - chan_dahdi: Fix erroneously persistent dialmode.
  - sig_analog: Skip Caller ID spill if usecallerid=no.
  - res_tonedetect: Add option for TONE_DETECT detection to auto stop.
  - pbx_builtins: Allow custom tone for WaitExten.

- #### Sean Bright (2):
  - res_musiconhold.c: Annotate when the channel is locked.
  - res_musiconhold.c: Ensure we're always locked around music state access.

- #### Sperl Viktor (2):
  - res_agi: Increase AGI command buffer size from 2K to 8K
  - cel: Add STREAM_BEGIN, STREAM_END and DTMF event types.

- #### Stanislav Abramenkov (1):
  - bundled_pjproject: Avoid deadlock between transport and transaction

- #### Stuart Henderson (1):
  - app_queue: fix comparison for announce-position-only-up

- #### Sven Kube (2):
  - resource_channels.c: Don't call ast_channel_get_by_name on empty optional argu..
  - ARI: Add command to indicate progress to a channel

- #### Tinet-mucw (2):
  - pbx.c: when set flag AST_SOFTHANGUP_ASYNCGOTO, ast_explicit_goto should return..
  - pbx.c: When the AST_SOFTHANGUP_ASYNCGOTO flag is set, pbx_extension_helper sho..

- #### kodokaii (1):
  - chan_websocket: Reset frame_queue_length to 0 after FLUSH_MEDIA

- #### mkmer (1):
  - utils.h: Add rounding to float conversion to int.

- #### zhou_jiajian (1):
  - cdr: add CANCEL dispostion in CDR


### Commit List:

-  res_rtp_asterisk.c: Use rtp->dtls in __rtp_sendto when rtcp mux is used.
-  chan_websocket: Fix codec validation and add passthrough option.
-  res_ari: Ensure outbound websocket config has a websocket_client_id.
-  chan_websocket.c: Add DTMF messages
-  app_queue.c: Add new global 'log_unpause_on_reason_change'
-  app_waitforsilence.c: Use milliseconds to calculate timeout time
-  Fix missing ast_test_flag64 in extconf.c
-  pbx_builtins: Allow custom tone for WaitExten.
-  res_tonedetect: Add option for TONE_DETECT detection to auto stop.
-  app_queue: fix comparison for announce-position-only-up
-  sig_analog: Skip Caller ID spill if usecallerid=no.
-  chan_dahdi: Fix erroneously persistent dialmode.
-  chan_websocket: Fix buffer overrun when processing TEXT websocket frames.
-  sig_analog: Fix SEGV due to calling strcmp on NULL.
-  ARI: Add command to indicate progress to a channel
-  dsp.c: Improve debug logging in tone_detect().
-  res_stasis_device_state: Fix delete ARI Devicestates after asterisk restart.
-  app_chanspy: Add option to not automatically answer channel.
-  xmldoc.c: Fix rendering of CLI output.
-  func_frame_drop: Add debug messages for dropped frames.
-  test_res_prometheus: Fix compilation failure on Debian 13.
-  func_frame_drop: Handle allocation failure properly.
-  pbx_lua.c: segfault when pass null data to term_color function
-  bridge.c: Obey BRIDGE_NOANSWER variable to skip answering channel.
-  res_rtp_asterisk: Don't send RTP before DTLS has negotiated.
-  app_dial.c: Moved channel lock to prevent deadlock
-  file.c: with "sounds_search_custom_dir = yes", search "custom" directory
-  cel: Add STREAM_BEGIN, STREAM_END and DTMF event types.
-  channelstorage_cpp_map_name_id.cc: Refactor iterators for thread-safety.
-  res_srtp: Add menuselect options to enable AES_192, AES_256 and AES_GCM
-  cdr: add CANCEL dispostion in CDR
-  func_curl: Allow auth methods to be set.
-  options:  Change ast_options from ast_flags to ast_flags64.
-  res_config_odbc: Prevent Realtime fallback on record-not-found (SQL_NO_DATA)
-  app_agent_pool: Remove documentation for removed option.
-  res_agi: Increase AGI command buffer size from 2K to 8K
-  ast_tls_cert: Make certificate validity configurable.
-  cdr.c: Set tenantid from party_a->base instead of chan->base.
-  app_mixmonitor:  Update the documentation concerning the "D" option.
-  sig_analog: Properly handle STP, ST2P, and ST3P for fgccamamf.
-  chan_websocket: Reset frame_queue_length to 0 after FLUSH_MEDIA
-  chan_pjsip.c: Change SSRC after media source change
-  Media over Websocket Channel Driver
-  bundled_pjproject: Avoid deadlock between transport and transaction
-  utils.h: Add rounding to float conversion to int.
-  res_musiconhold.c: Ensure we're always locked around music state access.
-  res_musiconhold.c: Annotate when the channel is locked.
-  res_musiconhold: Appropriately lock channel during start.

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

#### chan_websocket.c: Add DTMF messages
  Author: Joe Garlick
  Date:   2025-09-04

  Added DTMF messages to the chan_websocket feature.

  When a user presses DTMF during a call over chan_websocket it will send a message like:
  "DTMF_END digit:1"

  Resolves: https://github.com/asterisk/asterisk-feature-requests/issues/70

#### app_queue.c: Add new global 'log_unpause_on_reason_change'
  Author: Igor Goncharovsky
  Date:   2025-09-02

  In many asterisk-based systems, the pause reason is used to separate
  pauses by type,and logically, changing the reason defines two intervals
  that should be accounted for separately. The introduction of a new
  option allows me to separate the intervals of operator inactivity in
  the log by the event of unpausing.

  UserNote: Add new global option 'log_unpause_on_reason_change' that
  is default disabled. When enabled cause addition of UNPAUSE event on
  every re-PAUSE with reason changed.


#### app_waitforsilence.c: Use milliseconds to calculate timeout time
  Author: Igor Goncharovsky
  Date:   2025-09-04

  The functions WaitForNoise() and WaitForSilence() use the time()
  functions to calculate elapsed time, which causes the timer to fire on
  a whole second boundary, and the actual function execution time to fire
  the timer may be 1 second less than expected. This fix replaces time()
  with ast_tvnow().

  Fixes: #1401

#### Fix missing ast_test_flag64 in extconf.c
  Author: Artem Umerov
  Date:   2025-08-29

  Fix missing ast_test_flag64 after https://github.com/asterisk/asterisk/commit/43bf8a4ded7a65203b766b91eaf8331a600e9d8d


#### pbx_builtins: Allow custom tone for WaitExten.
  Author: Naveen Albert
  Date:   2025-08-25

  Currently, the 'd' option will play dial tone while waiting
  for digits. Allow it to accept an argument for any tone from
  indications.conf.

  Resolves: #1396

  UserNote: The tone used while waiting for digits in WaitExten
  can now be overridden by specifying an argument for the 'd'
  option.


#### res_tonedetect: Add option for TONE_DETECT detection to auto stop.
  Author: Naveen Albert
  Date:   2025-08-22

  One of the problems with TONE_DETECT as it was originally written
  is that if a tone is detected multiple times, it can trigger
  the redirect logic multiple times as well. For example, if we
  do an async goto in the dialplan after detecting a tone, because
  the detector is still active until explicitly disabled, if we
  detect the tone again, we will branch again and start executing
  that dialplan a second time. This is rarely ever desired behavior,
  and can happen if the detector is not removed quickly enough.

  Add a new option, 'e', which automatically disables the detector
  once the desired number of matches have been heard. This eliminates
  the potential race condition where previously the detector would
  need to be disabled immediately, but doing so quickly enough
  was not guaranteed. This also allows match criteria to be retained
  longer if needed, so the detector does not need to be destroyed
  prematurely.

  Resolves: #1390

  UserNote: The 'e' option for TONE_DETECT now allows detection to
  be disabled automatically once the desired number of matches have
  been fulfilled, which can help prevent race conditions in the
  dialplan, since TONE_DETECT does not need to be disabled after
  a hit.


#### app_queue: fix comparison for announce-position-only-up
  Author: Stuart Henderson
  Date:   2025-08-21

  Numerically comparing that the current queue position is less than
  last_pos_said can only be done after at least one announcement has been
  made, otherwise last_pos_said is at the default (0).

  Fixes: #1386

#### sorcery: Prevent duplicate objects and ensure missing objects are created on u..
  Author: Alexei Gradinari
  Date:   2025-07-07

  This patch resolves two issues in Sorcery objectset handling with multiple
  backends:

  1. Prevent duplicate objects:
     When an object exists in more than one backend (e.g., a contact in both
     'astdb' and 'realtime'), the objectset previously returned multiple instances
     of the same logical object. This caused logic failures in components like the
     PJSIP registrar, where duplicate contact entries led to overcounting and
     incorrect deletions, when max_contacts=1 and remove_existing=yes.

     This patch ensures only one instance of an object with a given key is added
     to the objectset, avoiding these duplicate-related side effects.

  2. Ensure missing objects are created:
     When using multiple writable backends, a temporary backend failure can lead
     to objects missing permanently from that backend.
     Currently, .update() silently fails if the object is not present,
     and no .create() is attempted.
     This results in inconsistent state across backends (e.g. astdb vs. realtime).

     This patch introduces a new global option in sorcery.conf:
       [general]
       update_or_create_on_update_miss = yes|no

     Default: no (preserves existing behavior).

     When enabled: if .update() fails with no data found, .create() is attempted
     in that backend. This ensures that objects missing due to temporary backend
     outages are re-synchronized once the backend is available again.

     Added a new CLI command:
       sorcery show settings
     Displays global Sorcery settings, including the current value of
     update_or_create_on_update_miss.

     Updated tests to validate both flag enabled/disabled behavior.

  Fixes: #1289

  UserNote: Users relying on Sorcery multiple writable backends configurations
  (e.g., astdb + realtime) may now enable update_or_create_on_update_miss = yes
  in sorcery.conf to ensure missing objects are recreated after temporary backend
  failures. Default behavior remains unchanged unless explicitly enabled.


#### sig_analog: Skip Caller ID spill if usecallerid=no.
  Author: Naveen Albert
  Date:   2025-08-25

  If Caller ID is disabled for an FXS port, then we should not send any
  Caller ID spill on the line, as we have no Caller ID information that
  we can/should be sending.

  Resolves: #1394

#### chan_dahdi: Fix erroneously persistent dialmode.
  Author: Naveen Albert
  Date:   2025-08-18

  It is possible to modify the dialmode setting in the chan_dahdi/sig_analog
  private using the CHANNEL function, to modify it during calls. However,
  it was not being reset between calls, meaning that if, for example, tone
  dialing was disabled, it would never work again unless explicitly enabled.

  This fixes the setting by pairing it with a "perm" version of the setting,
  as a few other features have, so that it can be reset to the permanent
  setting between calls. The documentation is also clarified to explain
  the interaction of this setting and the digitdetect setting more clearly.

  Resolves: #1378

#### chan_websocket: Allow additional URI parameters to be added to the outgoing URI.
  Author: George Joseph
  Date:   2025-08-13

  * Added a new option to the WebSocket dial string to capture the additional
    URI parameters.
  * Added a new API ast_uri_verify_encoded() that verifies that a string
    either doesn't need URI encoding or that it has already been encoded.
  * Added a new API ast_websocket_client_add_uri_params() to add the params
    to the client websocket session.
  * Added XML documentation that will show up with `core show application Dial`
    that shows how to use it.

  Resolves: #1352

  UserNote: A new WebSocket channel driver option `v` has been added to the
  Dial application that allows you to specify additional URI parameters on
  outgoing connections. Run `core show application Dial` from the Asterisk CLI
  to see how to use it.


#### chan_websocket: Fix buffer overrun when processing TEXT websocket frames.
  Author: George Joseph
  Date:   2025-08-19

  ast_websocket_read() receives data into a fixed 64K buffer then continually
  reallocates a final buffer that, after all continuation frames have been
  received, is the exact length of the data received and returns that to the
  caller.  process_text_message() in chan_websocket was attempting to set a
  NULL terminator on the received payload assuming the payload buffer it
  received was the large 64K buffer.  The assumption was incorrect so when it
  tried to set a NULL terminator on the payload, it could, depending on the
  state of the heap at the time, cause heap corruption.

  process_text_message() now allocates its own payload_len + 1 sized buffer,
  copies the payload received from ast_websocket_read() into it then NULL
  terminates it prevent the possibility of the overrun and corruption.

  Resolves: #1384

#### sig_analog: Fix SEGV due to calling strcmp on NULL.
  Author: Naveen Albert
  Date:   2025-08-18

  Add an additional check to guard against the channel application being
  NULL.

  Resolves: #1380

#### ARI: Add command to indicate progress to a channel
  Author: Sven Kube
  Date:   2025-07-30

  Adds an ARI command to send a progress indication to a channel.

  DeveloperNote: A new ARI endpoint is available at `/channels/{channelId}/progress` to indicate progress to a channel.

#### dsp.c: Improve debug logging in tone_detect().
  Author: Naveen Albert
  Date:   2025-08-15

  The debug logging during DSP processing has always been kind
  of overwhelming and annoying to troubleshoot. Simplify and
  improve the logging in a few ways to aid DSP debugging:

  * If we had a DSP hit, don't also emit the previous debug message that
    was always logged. It is duplicated by the hit message, so this can
    reduce the number of debug messages during detection by 50%.
  * Include the hit count and required number of hits in the message so
    on partial detections can be more easily troubleshot.
  * Use debug level 9 for hits instead of 10, so we can focus on hits
    without all the noise from the per-frame debug message.
  * 1-index the hit count in the debug messages. On the first hit, it
    currently logs '0', just as when we are not detecting anything,
    which can be confusing.

  Resolves: #1375

#### res_stasis_device_state: Fix delete ARI Devicestates after asterisk restart.
  Author: Jose Lopes
  Date:   2025-07-30

  After an asterisk restart, the deletion of ARI Devicestates didn't
  return error, but the devicestate was not deleted.
  Found a typo on populate_cache function that created wrong cache for
  device states.
  This bug caused wrong assumption that devicestate didn't exist,
  since it was not in cache, so deletion didn't returned error.

  Fixes: #1327

#### app_chanspy: Add option to not automatically answer channel.
  Author: Naveen Albert
  Date:   2025-08-13

  Add an option for ChanSpy and ExtenSpy to not answer the channel
  automatically. Most applications that auto-answer by default
  already have an option to disable this behavior if unwanted.

  Resolves: #1358

  UserNote: ChanSpy and ExtenSpy can now be configured to not
  automatically answer the channel by using the 'N' option.


#### xmldoc.c: Fix rendering of CLI output.
  Author: George Joseph
  Date:   2025-08-14

  If you do a `core show application Dial`, you'll see it's kind of a mess.
  Indents are wrong is some places, examples are printed in black which makes
  them invisible on most terminals, and the lack of line breaks in some cases
  makes it hard to follow.

  * Fixed the rendering of examples so they are indented properly and changed
  the color so they can be seen.
  * There is now a line break before each option.
  * Options are now printed on their own line with all option content indented
  below them.

  Example from Dial before fixes:
  ```
      Example: Dial 555-1212 on first available channel in group 1, searching
      from highest to lowest

      Example: Ringing FXS channel 4 with ring cadence 2

      Example: Dial 555-1212 on channel 3 and require answer confirmation

  ...

      O([mode]):
          mode - With <mode> either not specified or set to '1', the originator
          hanging up will cause the phone to ring back immediately.
   - With <mode> set to '2', when the operator flashes the trunk, it will ring
   their phone back.
  Enables *operator services* mode.  This option only works when bridging a DAHDI
  channel to another DAHDI channel only. If specified on non-DAHDI interfaces, it
  will be ignored. When the destination answers (presumably an operator services
  station), the originator no longer has control of their line. They may hang up,
  but the switch will not release their line until the destination party (the
  operator) hangs up.

      p: This option enables screening mode. This is basically Privacy mode
      without memory.
  ```

  After:
  ```
      Example: Dial 555-1212 on first available channel in group 1, searching
      from highest to lowest

       same => n,Dial(DAHDI/g1/5551212)

      Example: Ringing FXS channel 4 with ring cadence 2

       same => n,Dial(DAHDI/4r2)

      Example: Dial 555-1212 on channel 3 and require answer confirmation

       same => n,Dial(DAHDI/3c/5551212)

  ...

      O([mode]):
          mode - With <mode> either not specified or set to '1', the originator
          hanging up will cause the phone to ring back immediately.
          With <mode> set to '2', when the operator flashes the trunk, it will
          ring their phone back.
          Enables *operator services* mode.  This option only works when bridging
          a DAHDI channel to another DAHDI channel only. If specified on
          non-DAHDI interfaces, it will be ignored. When the destination answers
          (presumably an operator services station), the originator no longer has
          control of their line. They may hang up, but the switch will not
          release their line until the destination party (the operator) hangs up.

      p:
          This option enables screening mode. This is basically Privacy mode
          without memory.
  ```

  There are still things we can do to make this more readable but this is a
  start.


#### func_frame_drop: Add debug messages for dropped frames.
  Author: Naveen Albert
  Date:   2025-08-14

  Add debug messages in scenarios where frames that are usually processed
  are dropped or skipped.

  Resolves: #1371

#### test_res_prometheus: Fix compilation failure on Debian 13.
  Author: Naveen Albert
  Date:   2025-08-14

  curl_easy_setopt expects long types, so be explicit.

  Resolves: #1369

#### func_frame_drop: Handle allocation failure properly.
  Author: Naveen Albert
  Date:   2025-08-14

  Handle allocation failure and simplify the allocation using asprintf.

  Resolves: #1366

#### pbx_lua.c: segfault when pass null data to term_color function
  Author: Alexey Khabulyak
  Date:   2025-08-14

  This can be reproduced under certain curcomstences.
  For example: call app.playback from lua with invalid data: app.playback({}).
  pbx_lua.c will try to get data for this playback using lua_tostring function.
  This function returs NULL for everything but strings and numbers.
  Then, it calls term_color with NULL data.
  term_color function can call(if we don't use vt100 compat term)
  ast_copy_string with NULL inbuf which cause segfault. bt example:
  ast_copy_string (size=8192, src=0x0, dst=0x7fe44b4be8b0)
  at /usr/src/asterisk/asterisk-20.11.0/include/asterisk/strings.h:412

  Resolves: https://github.com/asterisk/asterisk/issues/1363

#### bridge.c: Obey BRIDGE_NOANSWER variable to skip answering channel.
  Author: Naveen Albert
  Date:   2025-08-14

  If the BRIDGE_NOANSWER variable is set on a channel, it is not supposed
  to answer when another channel bridges to it using Bridge(), and this is
  checked when ast_bridge_call* is called. However, another path exists
  (bridge_exec -> ast_bridge_add_channel) where this variable was not
  checked and channels would be answered. We now check the variable there.

  Resolves: #401
  Resolves: #1364

#### res_rtp_asterisk: Don't send RTP before DTLS has negotiated.
  Author: Ben Ford
  Date:   2025-08-04

  There was no check in __rtp_sendto that prevented Asterisk from sending
  RTP before DTLS had finished negotiating. This patch adds logic to do
  so.

  Fixes: #1260

#### app_dial.c: Moved channel lock to prevent deadlock
  Author: Alexey Khabulyak
  Date:   2025-08-04

  It's reproducible with pbx_lua, not regular dialplan.

  deadlock description:
  1. asterisk locks a channel
  2. calls function onedigit_goto
  3. calls ast_goto_if_exists funciton
  4. checks ast_exists_extension -> pbx_extension_helper
  5. pbx_extension_helper calls pbx_find_extension
  6. Then asterisk starts autoservice in a new thread
  7. autoservice run tries to lock the channel again

  Because our channel is locked already, autoservice can't lock.
  Autoservice can't lock -> autoservice stop is waiting forever.
  onedigit_goto waits for autoservice stop.

  Resolves: https://github.com/asterisk/asterisk/issues/1335

#### res_pjsip_diversion: resolve race condition between Diversion header processin..
  Author: Mike Bradeen
  Date:   2025-08-07

  Based on the firing order of the PJSIP call-backs on a redirect, it was possible for
  the Diversion header to not be included in the outgoing 181 response to the UAC and
  the INVITE to the UAS.

  This change moves the Diversion header processing to an earlier PJSIP callback while also
  preventing the corresponding update that can cause a duplicate 181 response when processing
  the header at that time.

  Resolves: #1349

#### file.c: with "sounds_search_custom_dir = yes", search "custom" directory
  Author: Allan Nathanson
  Date:   2025-08-10

  With `sounds_search_custom_dir = yes`, we are supposed to search for sounds
  in the `AST_DATA_DIR/sounds/custom` directory before searching the normal
  directories.  Unfortunately, a recent change
  (https://github.com/asterisk/asterisk/pull/1172) had a typo resulting in
  the "custom" directory not being searched.  This change restores this
  expected behavior.

  Resolves: #1353

#### cel: Add STREAM_BEGIN, STREAM_END and DTMF event types.
  Author: Sperl Viktor
  Date:   2025-06-30

  Fixes: #1280

  UserNote: Enabling the tracking of the
  STREAM_BEGIN and the STREAM_END event
  types in cel.conf will log media files and
  music on hold played to each channel.
  The STREAM_BEGIN event's extra field will
  contain a JSON with the file details (path,
  format and language), or the class name, in
  case of music on hold is played. The DTMF
  event's extra field will contain a JSON with
  the digit and the duration in milliseconds.


#### channelstorage_cpp_map_name_id.cc: Refactor iterators for thread-safety.
  Author: George Joseph
  Date:   2025-07-30

  The fact that deleting an object from a map invalidates any iterator
  that happens to currently point to that object was overlooked in the initial
  implementation.  Unfortunately, there's no way to detect that an iterator
  has been invalidated so the result was an occasional SEGV triggered by modules
  like app_chanspy that opens an iterator and can keep it open for a long period
  of time.  The new implementation doesn't keep the underlying C++ iterator
  open across calls to ast_channel_iterator_next() and uses a read lock
  on the map to ensure that, even for the few microseconds we use the
  iterator, another thread can't delete a channel from under it.  Even with
  this change, the iterators are still WAY faster than the ao2_legacy
  storage driver.

  Full details about the new implementation are located in the comments for
  iterator_next() in channelstorage_cpp_map_name_id.cc.

  Resolves: #1309

#### res_srtp: Add menuselect options to enable AES_192, AES_256 and AES_GCM
  Author: George Joseph
  Date:   2025-08-05

  UserNote: Options are now available in the menuselect "Resource Modules"
  category that allow you to enable the AES_192, AES_256 and AES_GCM
  cipher suites in res_srtp. Of course, libsrtp and OpenSSL must support
  them but modern versions do.  Previously, the only way to enable them was
  to set the CFLAGS environment variable when running ./configure.
  The default setting is to disable them preserving existing behavior.


#### cdr: add CANCEL dispostion in CDR
  Author: zhou_jiajian
  Date:   2025-07-24

  In the original implementation, both CANCEL and NO ANSWER states were
  consolidated under the NO ANSWER disposition. This patch introduces a
  separate CANCEL disposition, with an optional configuration switch to
  enable this new disposition.

  Resolves: #1323

  UserNote: A new CDR option "canceldispositionenabled" has been added
  that when set to true, the NO ANSWER disposition will be split into
  two dispositions: CANCEL and NO ANSWER. The default value is 'no'


#### func_curl: Allow auth methods to be set.
  Author: Naveen Albert
  Date:   2025-08-01

  Currently the CURL function only supports Basic Authentication,
  the default auth method in libcurl. Add an option that also
  allows enabling digest authentication.

  Resolves: #1332

  UserNote: The httpauth field in CURLOPT now allows the authentication
  methods to be set.


#### options:  Change ast_options from ast_flags to ast_flags64.
  Author: George Joseph
  Date:   2025-07-21

  DeveloperNote: The 32-bit ast_options has no room left to accomodate new
  options and so has been converted to an ast_flags64 structure. All internal
  references to ast_options have been updated to use the 64-bit flag
  manipulation macros.  External module references to the 32-bit ast_options
  should continue to work on little-endian systems because the
  least-significant bytes of a 64 bit integer will be in the same location as a
  32-bit integer.  Because that's not the case on big-endian systems, we've
  swapped the bytes in the flags manupulation macros on big-endian systems
  so external modules should still work however you are encouraged to test.


#### res_config_odbc: Prevent Realtime fallback on record-not-found (SQL_NO_DATA)
  Author: Alexei Gradinari
  Date:   2025-07-15

  This patch fixes an issue in the ODBC Realtime engine where Asterisk incorrectly
  falls back to the next configured backend when the current one returns
  SQL_NO_DATA (i.e., no record found).
  This is a logical error and performance risk in multi-backend configurations.

  Solution:
  Introduced CONFIG_RT_NOT_FOUND ((void *)-1) as a special return marker.
  ODBC Realtime backend now return CONFIG_RT_NOT_FOUND when no data is found.
  Core engine stops iterating on this marker, avoiding unnecessary fallback.

  Notes:
  Other Realtime backends (PostgreSQL, LDAP, etc.) can be updated similarly.
  This patch only covers ODBC.

  Fixes: #1305

#### resource_channels.c: Don't call ast_channel_get_by_name on empty optional argu..
  Author: Sven Kube
  Date:   2025-07-30

  `ast_ari_channels_create` and `ast_ari_channels_dial` called the
  `ast_channel_get_by_name` function with optional arguments. Since
  8f1982c4d6, this function logs an error for empty channel names.
  This commit adds checks for empty optional arguments that are used
  to call `ast_channel_get_by_name` to prevent these error logs.


#### app_agent_pool: Remove documentation for removed option.
  Author: Naveen Albert
  Date:   2025-07-28

  The already-deprecated "password" option for the AGENT function was
  removed in commit d43b17a872e8227aa8a9905a21f90bd48f9d5348 for
  Asterisk 12, but the documentation for it wasn't removed then.

  Resolves: #1321

#### pbx.c: When the AST_SOFTHANGUP_ASYNCGOTO flag is set, pbx_extension_helper sho..
  Author: Tinet-mucw
  Date:   2025-07-22

  Under certain circumstances the context/extens/prio are set in the ast_async_goto, for example action Redirect.
  In the situation that action Redirect is broken by pbx_extension_helper this info is changed.
  This will cause the current dialplan location to be executed twice.
  In other words, the Redirect action does not take effect.

  Resolves: #1315

#### res_agi: Increase AGI command buffer size from 2K to 8K
  Author: Sperl Viktor
  Date:   2025-07-22

  Fixes: #1317

#### ast_tls_cert: Make certificate validity configurable.
  Author: Naveen Albert
  Date:   2025-07-16

  Currently, the ast_tls_cert script is hardcoded to produce certificates
  with a validity of 365 days, which is not generally desirable for self-
  signed certificates. Make this parameter configurable.

  Resolves: #1307

#### cdr.c: Set tenantid from party_a->base instead of chan->base.
  Author: George Joseph
  Date:   2025-07-17

  The CDR tenantid was being set in cdr_object_alloc from the channel->base
  snapshot.  Since this happens at channel creation before the dialplan is even
  reached, calls to `CHANNEL(tenantid)=<something>` in the dialplan were being
  ignored.  Instead we now take tenantid from party_a when
  cdr_object_create_public_records() is called which is after the call has
  ended and all channel snapshots rebuilt.  This is exactly how accountcode
  and amaflags, which can also be set in tha dialplpan, are handled.

  Resolves: #1259

#### app_mixmonitor:  Update the documentation concerning the "D" option.
  Author: George Joseph
  Date:   2025-07-16

  When using the "D" option to output interleaved audio, the file extension
  must be ".raw".  That info wasn't being properly rendered in the markdown
  and HTML on the documentation site.  The XML was updated to move the
  note in the option section to a warning in the description.

  Resolves: #1269

#### sig_analog: Properly handle STP, ST2P, and ST3P for fgccamamf.
  Author: Naveen Albert
  Date:   2025-07-14

  Previously, we were only using # (ST) as a terminator, and not handling
  A (STP), B (ST2P), or C (ST3P), which erroneously led to it being
  treated as part of the dialed number. Parse any of these as the start
  digit.

  Resolves: #1301

#### chan_websocket: Reset frame_queue_length to 0 after FLUSH_MEDIA
  Author: kodokaii
  Date:   2025-07-03

  In the WebSocket channel driver, the FLUSH_MEDIA command clears all frames from
  the queue but does not reset the frame_queue_length counter.

  As a result, the driver incorrectly thinks the queue is full after flushing,
  which prevents new multimedia frames from being sent, especially after multiple
  flush commands.

  This fix sets frame_queue_length to 0 after flushing, ensuring the queue state
  is consistent with its actual content.

  Fixes: #1304

#### chan_pjsip.c: Change SSRC after media source change
  Author: Martin Tomec
  Date:   2025-06-25

  When the RTP media source changes, such as after a blind transfer, the new source introduces a discontinuous timestamp. According to RFC 3550, Section 5.1, an RTP stream's timestamp for a given SSRC must increment monotonically and linearly.
  To comply with the standard and avoid a large timestamp jump on the existing SSRC, a new SSRC is generated for the new media stream.
  This change resolves known interoperability issues with certain SBCs (like Sonus/Ribbon) that stop forwarding media when they detect such a timestamp violation. This code uses the existing implementation from chan_sip.

  Resolves: #927

#### Media over Websocket Channel Driver
  Author: George Joseph
  Date:   2025-04-28

  * Created chan_websocket which can exchange media over both inbound and
  outbound websockets which the driver will frame and time.
  See http://s.asterisk.net/mow for more information.

  * res_http_websocket: Made defines for max message size public and converted
  a few nuisance verbose messages to debugs.

  * main/channel.c: Changed an obsolete nuisance error to a debug.

  * ARI channels: Updated externalMedia to include chan_websocket as a supported
  transport.

  UserNote: A new channel driver "chan_websocket" is now available. It can
  exchange media over both inbound and outbound websockets and will both frame
  and re-time the media it receives.
  See http://s.asterisk.net/mow for more information.

  UserNote: The ARI channels/externalMedia API now includes support for the
  WebSocket transport provided by chan_websocket.


#### bundled_pjproject: Avoid deadlock between transport and transaction
  Author: Stanislav Abramenkov
  Date:   2025-07-01

  Backport patch from upstream
  * Avoid deadlock between transport and transaction
  https://github.com/pjsip/pjproject/commit/edde06f261ac

  Issue described in
  https://github.com/pjsip/pjproject/issues/4442


#### utils.h: Add rounding to float conversion to int.
  Author: mkmer
  Date:   2025-03-23

  Quote from an audio engineer NR9V:
  There is a minor issue of a small amount of crossover distortion though as a result of `ast_slinear_saturated_multiply_float()` not rounding the float. This could result in some quiet but potentially audible distortion artifacts in lower volume parts of the signal. If you have for example a sign wave function with a max amplitude of just a few samples, all samples between -1 and 1 will be truncated to zero, resulting in the waveform no longer being a sine wave and in harmonic distortion.

  Resolves: #1176

#### pbx.c: when set flag AST_SOFTHANGUP_ASYNCGOTO, ast_explicit_goto should return..
  Author: Tinet-mucw
  Date:   2025-06-18

  Under certain circumstances the context/extens/prio are set in the ast_async_goto, for example action Redirect.
  In the situation that action Redirect is broken by GotoIf this info is changed.
  that will causes confusion in dialplan execution.

  Resolves: #1273

#### res_musiconhold.c: Ensure we're always locked around music state access.
  Author: Sean Bright
  Date:   2025-04-08


#### res_musiconhold.c: Annotate when the channel is locked.
  Author: Sean Bright
  Date:   2025-04-08


#### res_musiconhold: Appropriately lock channel during start.
  Author: Jaco Kroon
  Date:   2024-12-19

  This relates to #829

  This doesn't sully solve the Ops issue, but it solves the specific crash
  there.  Further PRs to follow.

  In the specific crash the generator was still under construction when
  moh was being stopped, which then proceeded to close the stream whilst
  it was still in use.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

