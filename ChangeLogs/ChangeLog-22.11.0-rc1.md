
## Change Log for Release asterisk-22.11.0-rc1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.11.0-rc1.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.10.1...22.11.0-rc1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.11.0-rc1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 38
- Commit Authors: 16
- Issues Resolved: 27
- Security Advisories Resolved: 0

### User Notes:

- #### bridging: Add support for TOUCH_MIXMONITOR_OPTIONS
  The TOUCH_MIXMONITOR_OPTIONS channel variable can now be used
  to configure the options used by the mixmonitor application when it is
  started via automixmon. If both TOUCH_MIXMONITOR_OPTIONS and
  TOUCH_MIXMONITOR_BEEP are set, TOUCH_MIXMONITOR_BEEP is ignored with
  a warning.

- #### Audiohooks: Allow whisper audiohooks to work without an underlying stream
  There is no longer a requirement to play silence or otherwise have
  a stream of audio being sent to a hooked channel for whispered frames to be
  heard.

- #### Bundled pjproject: Make it easier to override options in config_site.h.
  Bundled pjproject: It's now possible to override some of the pjproject
  build options contained in ./third-party/pjproject/patches/config_site.h by
  adding PJPROJECT_CFLAGS to your Asterisk ./configure command line.  For example:
  `./configure ... PJPROJECT_CFLAGS='-DPJ_OPT1=8192 -DPJ_OPT2=512'`
  Any option in config_site.h that's wrapped in a `#ifndef` block can be overridden
  and many of the `PJSIP` options displayed by `pjproject show buildopts` can be
  set.  WARNING: Adjusting these options without understanding their effect can
  cripple your Asterisk instances.  You shouldn't adjust them unless you need
  to solve a specific issue.
  Resolves: #2011

- #### manager: Move away from shared linked list for events.
  The "manager show eventq" CLI command has been
  removed as there is no longer a single event queue to display.

- #### res_pjsip: Add external_signaling_hostname transport option
  A new pjsip.conf transport option 'external_signaling_hostname'
  has been added. When set, this value will be used in SIP Contact and Via
  headers instead of the automatically determined IP address. This option
  is mutually exclusive with 'external_signaling_address'.

- #### WebSocket Enhancements: Proxies and Keepalives for ARI and Media Outbound Websockets.
  Forward/outbound proxies can now be specified for outbound websockets.
  See the websocket_client.conf.sample file for configuration information.
  TCP-level or WebSocket PING/PONG keepalives can now be enabled on

### Upgrade Notes:

- #### jansson: Upgrade version to jansson 2.15.1
  jansson has been upgraded to 2.15.1. For more
  information visit jansson Github page: https://github.com/akheron/jansson/releases/tag/v2.15.1


### Developer Notes:

- #### WebSocket Enhancements: Proxies and Keepalives for ARI and Media Outbound Websockets.
  The addition of the proxy and keepalive configuration parameters
  pushed the websocket client parameter count over 32. This necessitated changing
  the size of the ast_ws_client_fields enum from a 32 bit bitfield to a 64-bit
  bitfield with a corresponding change to the ast_websocket_client structure.


### Commit Authors:

- Alexandre Fournier: (1)
- Alexis Chenard: (1)
- Aqeel Abbas: (1)
- Ben Ford: (1)
- George Joseph: (6)
- Jeremy Lainé: (2)
- Joshua C. Colp: (4)
- Mehrdad Seifzadeh: (3)
- Mike Bradeen: (6)
- Naveen Albert: (4)
- Roel van Meer: (1)
- Sean Bright: (3)
- Stanislav Abramenkov: (1)
- Sven Kube: (1)
- Thomas Guebels: (1)
- aabolfazl: (2)

## Issue and Commit Detail:

### Closed Issues:

  - 871: [bug]: CHANNEL(PJSIP,local_addr) and CHANNEL(PJSIP,remote_addr) not available on outgoing channels
  - 1398: [bug]:  AOR matching for inbound registration not working with domain involved and identifying by contact header
  - 1625: [bug]: `ast_openstream` should stop at the first compatible file
  - 1749: [improvement]: Make Asterisk compatible with Microsoft Teams Phone System.
  - 1779: [bug]: MixMonitor with flag D produces garbage in a 16KHz bridge
  - 1849: [improvement]: chan_local: Update references to chan_local for Local channels
  - 1881: [improvement]:  Add HTTP proxy support for outbound WebSocket connections in websocket_client.conf (chan_websocket / ARI ExternalMedia)
  - 1882: [new-feature]: Ensure errors and warnings get through logger when reloading
  - 1933: [improvement]: Outbound Websockets: Add keepalive mechanisms
  - 1946: [bug]: Deadlock between RTP instance and ICE session
  - 1965: [bug]: Memory leak when receiving malformed 200 OK in response to re-INVITE
  - 1966: [improvement]: Allow whisper audiohooks to work without an underlying audio stream
  - 1979: [bug]: res_http_websocket: outbound client handshake read has no timeout — a non-responsive server hangs the calling thread forever while holding the channel lock, stalling the whole process
  - 1981: [improvement]: Support recording multichannel raw files with automixmon
  - 2006: [bug]: WebSocket Channel, odd frame is dropped after `STOP_MEDIA_BUFFERING`
  - 2011: [bug]: PJSIP TCP transport stops processing after upgrade to Asterisk 22.10.1 / bundled PJPROJECT 2.17 with ioqueue_epol free_list assertion
  - 2020: [bug]: Possible leak of inbound websocket session
  - 2021: [bug]: Segfault in res_pjsip_refer when doing an ARI controlled transfer with Refer-Sub: false.
  - 2024: [improvement]:Re-add pjsua application to third party build
  - 2026: [improvement]: res_pjsip: dtmf_mode documentation is incomplete
  - 2033: [improvement]: jansson: Upgrade version to jansson 2.15.1
  - 2038: [bug]: Using chan_websocket prevents using "dangerous" functions in the dialplan
  - 2051: [bug]: res_musiconhold: answeredonly leaks a mohclass reference on unanswered channels
  - 2054: [bug]: backtrace.c: Compilation failure due to use of removed type
  - 2056: [bug]: pjsua fails to build if the address sanitizer is used.
  - 2059: [bug]: Occasional call drops during re-INVITE when codec changes
  - 2061: [bug]: backtrace.c: stdbool.h needed for portability

### Commits By Author:

- #### Alexandre Fournier (1):
  - format_cap: guard against NULL src in *_from_cap helpers

- #### Alexis Chenard (1):
  - res_pjsip: Add external_signaling_hostname transport option

- #### Aqeel Abbas (1):
  - func_strings: Fix syntax error in STRBETWEEN documentation example

- #### Ben Ford (1):
  - Logger: Don't discard WARNING or ERROR messages.

- #### George Joseph (6):
  - SECURITY.md: Add warning about reporting multiple issues in one advisory.
  - res_pjsip_refer: Fix issues with Refer-Sub:false and ARI.
  - Bundled pjproject: Make it easier to override options in config_site.h.
  - chan_websocket: Use leftover data if no frames are available when the timer fires.
  - res_http_websocket: Add timeout to client handshakes.
  - WebSocket Enhancements: Proxies and Keepalives for ARI and Media Outbound Websockets.

- #### Jeremy Lainé (2):
  - tcptls: Don't inhibit escalations for outbound client connections.
  - file.c: Ensure opening a stream opens at most one file

- #### Joshua C. Colp (4):
  - pjsip: Eliminate some unique taskprocessors.
  - test_performance: Add performance experimentation test module.
  - manager: Move away from shared linked list for events.
  - extension_state: Add new extension state API.

- #### Mehrdad Seifzadeh (3):
  - chan_pjsip: Store transport info for outgoing channels
  - res_pjsip_registrar: Resolve AOR for identified endpoints
  - res_pjsip_session: Bound delayed BYE behind UAC INVITE

- #### Mike Bradeen (6):
  - res_pjsip_sdp_rtp: Fix intermittent call drop on reINVITE sdp change
  - pjproject: disable building pjsua when a sanitizer is used
  - res_rtp_asterisk: Avoid two lock inversion deadlocks with pj project
  - build: re-add pjsua test application
  - Audiohooks: Allow whisper audiohooks to work without an underlying stream
  - app_mixmonitor: Fix duplex recording for non 8K codecs

- #### Naveen Albert (4):
  - backtrace.c: Include stdbool.h
  - backtrace.c: Avoid removed bfd_boolean type.
  - res_pjsip: Document 'none' option for 'dtmf_mode'.
  - chan_local: Update chan_local references for Local channels.

- #### Roel van Meer (1):
  - bridging: Add support for TOUCH_MIXMONITOR_OPTIONS

- #### Sean Bright (3):
  - app_voicemail.c: Fix may-be-uninitialized error
  - extensions.ael.sample: Restore removed macros
  - configs: Comment out `values` setting to avoid parse error

- #### Stanislav Abramenkov (1):
  - jansson: Upgrade version to jansson 2.15.1

- #### Sven Kube (1):
  - chan_websocket: Fix NULL requestor dereference in webchan_request.

- #### Thomas Guebels (1):
  - res_http_websocket: Unref session when it fails to establish.

- #### aabolfazl (2):
  - res_musiconhold: Fix mohclass reference leak on answeredonly early return.
  - func_strings: Fix misspelling of "occurred" in FIELDNUM documentation.

### Commit List:

-  res_pjsip_sdp_rtp: Fix intermittent call drop on reINVITE sdp change
-  chan_websocket: Fix NULL requestor dereference in webchan_request.
-  backtrace.c: Include stdbool.h
-  res_musiconhold: Fix mohclass reference leak on answeredonly early return.
-  backtrace.c: Avoid removed bfd_boolean type.
-  app_voicemail.c: Fix may-be-uninitialized error
-  pjproject: disable building pjsua when a sanitizer is used
-  SECURITY.md: Add warning about reporting multiple issues in one advisory.
-  chan_pjsip: Store transport info for outgoing channels
-  func_strings: Fix misspelling of "occurred" in FIELDNUM documentation.
-  tcptls: Don't inhibit escalations for outbound client connections.
-  res_rtp_asterisk: Avoid two lock inversion deadlocks with pj project
-  jansson: Upgrade version to jansson 2.15.1
-  file.c: Ensure opening a stream opens at most one file
-  pjsip: Eliminate some unique taskprocessors.
-  bridging: Add support for TOUCH_MIXMONITOR_OPTIONS
-  res_pjsip_refer: Fix issues with Refer-Sub:false and ARI.
-  build: re-add pjsua test application
-  res_pjsip: Document 'none' option for 'dtmf_mode'.
-  Audiohooks: Allow whisper audiohooks to work without an underlying stream
-  Logger: Don't discard WARNING or ERROR messages.
-  func_strings: Fix syntax error in STRBETWEEN documentation example
-  res_pjsip_registrar: Resolve AOR for identified endpoints
-  res_http_websocket: Unref session when it fails to establish.
-  Bundled pjproject: Make it easier to override options in config_site.h.
-  test_performance: Add performance experimentation test module.
-  manager: Move away from shared linked list for events.
-  chan_websocket: Use leftover data if no frames are available when the timer fires.
-  res_pjsip_session: Bound delayed BYE behind UAC INVITE
-  extensions.ael.sample: Restore removed macros
-  format_cap: guard against NULL src in *_from_cap helpers
-  configs: Comment out `values` setting to avoid parse error
-  app_mixmonitor: Fix duplex recording for non 8K codecs
-  res_http_websocket: Add timeout to client handshakes.
-  extension_state: Add new extension state API.
-  res_pjsip: Add external_signaling_hostname transport option
-  WebSocket Enhancements: Proxies and Keepalives for ARI and Media Outbound Websockets.
-  chan_local: Update chan_local references for Local channels.

### Commit Details:

#### res_pjsip_sdp_rtp: Fix intermittent call drop on reINVITE sdp change
  Author: Mike Bradeen
  Date:   2026-08-05

  A reINVITE that changed the offered codec set (e.g. ulaw+alaw -> alaw
  only) could lead to call teardown when a bridge write still using the
  old format occurred during the re-negotiation.

  This change avoids the termination by making the following changes.

  First, Asterisk now holds the lock across both the tx payload update
  and the channel format update so they commit atomically.

  Second, when processing the sdp on the new incoming offer, merge the
  new offer with the old until the negotiation is complete.

  Also adds unit tests for the sdp merge

  Fixes: #2059

#### chan_websocket: Fix NULL requestor dereference in webchan_request.
  Author: Sven Kube
  Date:   2026-08-05

  Two log statements in webchan_request() passed `requestor` straight to
  ast_channel_name(), which dereferences the channel with no NULL check.
  `requestor` is NULL whenever no originator channel is supplied: ARI POST
  /channels/externalMedia always passes NULL, and POST /channels/create
  passes NULL when `originator` is omitted.

#### backtrace.c: Include stdbool.h
  Author: Naveen Albert
  Date:   2026-08-04

  stdbool.h is needed now that we use the bool type directly
  instead of bfd_boolean. See also 1d95b744c06974f0a00c143c6e0fc979af930908.

  Resolves: #2061

#### res_musiconhold: Fix mohclass reference leak on answeredonly early return.
  Author: aabolfazl
  Date:   2026-08-02

  local_ast_moh_start() returns -1 from the answeredonly check without
  releasing the mohclass reference it holds, unlike every other exit path
  in the function. Nothing else ever releases that reference, so
  moh_class_destructor() never runs for the object. With realtime music
  on hold and cachertclasses disabled, each suppressed request leaks the
  class object, its monitor thread, the external application process and
  two file descriptors for the lifetime of Asterisk. For static classes
  the stale references prevent the class from ever being destroyed after
  it is replaced by a reload.

  Release the reference before returning, matching the other exit paths.

  Fixes: #2051

#### backtrace.c: Avoid removed bfd_boolean type.
  Author: Naveen Albert
  Date:   2026-08-03

  bfd_boolean has been deprecated for some time and has now been
  removed in gdb, so use bool directly instead.

  See: https://github.com/gnutools/binutils-gdb/commit/1d95b744c06974f0a00c143c6e0fc979af930908

  Resolves: #2054

#### app_voicemail.c: Fix may-be-uninitialized error
  Author: Sean Bright
  Date:   2026-08-02

  A pointer to `new` is passed to `inboxcount(...)` which increments the
  pointed-to value.

#### pjproject: disable building pjsua when a sanitizer is used
  Author: Mike Bradeen
  Date:   2026-08-03

  Disable building the pjsua test application when any of the
  SANITIZER flags are set.

  Fixes: #2056

#### SECURITY.md: Add warning about reporting multiple issues in one advisory.
  Author: George Joseph
  Date:   2026-08-04


#### chan_pjsip: Store transport info for outgoing channels
  Author: Mehrdad Seifzadeh
  Date:   2026-06-05

  Outgoing PJSIP channels did not have transport information stored in
  their session datastore when CHANNEL(pjsip,local_addr) or
  CHANNEL(pjsip,remote_addr) was read. As a result, the address fields
  were empty on B-leg channels even though other dialog fields such as
  call-id and URIs were available.

  Store the selected local transport address and destination address for
  outgoing UAC session requests once the request is transmitted and the
  transport information is known.

  Fixes: #871

#### func_strings: Fix misspelling of "occurred" in FIELDNUM documentation.
  Author: aabolfazl
  Date:   2026-07-29

  The FIELDNUM description misspelled "occurred" as "occured". This text
  is user-visible: it is rendered on docs.asterisk.org and printed by
  "core show function FIELDNUM".

#### tcptls: Don't inhibit escalations for outbound client connections.
  Author: Jeremy Lainé
  Date:   2026-07-23

  handle_tcptls_connection() marks the current thread as inhibiting
  privilege escalations and as an external user interface so that
  dialplan functions considered 'dangerous' (STAT, SHELL, ...) cannot be
  executed on behalf of an external protocol.

  This is correct for inbound (server) connections, each of which runs on
  its own dedicated worker thread. However, the outbound (client) path
  calls handle_tcptls_connection() synchronously on the caller's own
  thread. When that caller is a channel/PBX thread, the thread-local
  flags are set and never cleared, permanently tainting the dialplan
  thread.

  Examples:

  - Calling Dial() for an outbound WebSocket and resuming dialplan
    execution with the "g" flag.
  - Running ExternalIVR() then continuing dialplan executing.

  Outbound connections are initiated by Asterisk itself and are not
  external user interfaces, so the flags should not be set for them. Gate
  the flag-setting on tcptls_session->client so it applies only to inbound
  connections.

  Resolves: #2038

#### res_rtp_asterisk: Avoid two lock inversion deadlocks with pj project
  Author: Mike Bradeen
  Date:   2026-06-26

  Fixes two related instance, group lock inversion deadlocks.

  When writing the RTP stream via rtp_sendto, the RTP instance was only unlocked
  before being passed to pj project when the instance was un-bundled (ie video
  was not bundled onto the audio stream's ICE transport.)

  The first change unlocks the instance whether bundled or unbundled to avoid the
  deadlock, then re-checks the bundled status upon return to know wether or not the
  transport should be used.

  For the second change, when an incoming RTCP NACK record was recieved, the parent
  (transport) and child (instance) locks were both held before the call to rtp_sendto,
  which is only able to release the child lock. Now in the un-bundled case the parent
  and child will be unlocked and re-locked in the correct order via a new helper
  function before and after the call to send_to.

  Fixes: #1946

#### jansson: Upgrade version to jansson 2.15.1
  Author: Stanislav Abramenkov
  Date:   2026-07-21

  UpgradeNote: jansson has been upgraded to 2.15.1. For more
  information visit jansson Github page: https://github.com/akheron/jansson/releases/tag/v2.15.1

  Resolves: #2033

#### file.c: Ensure opening a stream opens at most one file
  Author: Jeremy Lainé
  Date:   2025-12-03

  The functions used to stream files eventually end up calling into
  `filehelper(.., ACTION_OPEN)` which takes care of iterating over
  supported extensions and opening the first existing file.

  To do this, `filehelper` uses two nested loops:

  - An outer loop over the supported file formats.
  - An inner loop over the possible extensions for that format.

  We need to break out of both loops as soon as a file was successfully
  opened. Otherwise if a file exists in multiple formats (e.g `foo.wav`
  and `foo.alaw`) both files will be opened successively.

  Resolves: #1625

#### pjsip: Eliminate some unique taskprocessors.
  Author: Joshua C. Colp
  Date:   2026-07-15

  When placing outbound calls each call would get its own
  taskprocessor for things related to the call. In practice this
  is overkill as few things actually occur. Inbound calls on
  the other hand already use one of the fixed number of
  distributor taskprocessors. This also occurred for OPTIONS
  requests with each AOR having its own taskprocessor.

  This change moves both to using a distributor taskprocessor
  instead.

#### bridging: Add support for TOUCH_MIXMONITOR_OPTIONS
  Author: Roel van Meer
  Date:   2026-06-29

  This channel variable can be used to override the default automixmon
  options (which are 'b' or 'bB(<n>)'). If both TOUCH_MIXMONITOR_OPTIONS
  and TOUCH_MIXMONITOR_BEEP are set, TOUCH_MIXMONITOR_BEEP is ignored with
  a warning.
  It is the responsibility of the user to configure valid options in
  TOUCH_MIXMONITOR_OPTIONS.

  Fixes: #1981

  UserNote: The TOUCH_MIXMONITOR_OPTIONS channel variable can now be used
  to configure the options used by the mixmonitor application when it is
  started via automixmon. If both TOUCH_MIXMONITOR_OPTIONS and
  TOUCH_MIXMONITOR_BEEP are set, TOUCH_MIXMONITOR_BEEP is ignored with
  a warning.

#### res_pjsip_refer: Fix issues with Refer-Sub:false and ARI.
  Author: George Joseph
  Date:   2026-07-01

  When an attended transfer is controlled by ARI using
  PJSIP_TRANSFER_HANDLING()=ari-only, and a REFER request is received with
  a Refer-Sub: false header to suppress the automatic progress
  subscription, a segfault occurs in
  res_pjsip_refer:refer_incoming_ari_request().

  However, even if the segfault were prevented, there's another issue...
  Suppressing the subscription and NOTIFYs to the referer also prevents
  the ARI app from getting any progress events for the transfer which it's
  controlling.

  So...

  * The segfault has been fixed.
  * Suppressing the subscription no longer suppress the ARI events from being
  sent to the controlling ARI app.
  * A good amount of tracing has also been added.

  Resolves: #2021

#### build: re-add pjsua test application
  Author: Mike Bradeen
  Date:   2026-07-14

  pjsua and it's python bindings were removed as part of PR
  1854. Testing for RTT requires the pjsua application be
  re-added to the third pary build process.  This applies to
  the pjsua application ONLY and not the associated python
  bindings.

  Resolves: #2024

#### res_pjsip: Document 'none' option for 'dtmf_mode'.
  Author: Naveen Albert
  Date:   2026-07-14

  The 'none' value can be used to disable DSP processing for DTMF
  on PJSIP channels and is sometimes necessary for this reason,
  and already exists in the code, but is not documented. Add it to
  the documentation enum.

  Resolves: #2026

#### Audiohooks: Allow whisper audiohooks to work without an underlying stream
  Author: Mike Bradeen
  Date:   2026-06-02

  Audiohook whispers work by mixing audio with the current stream of frames being
  written to the hooked channel. Prior to this change if there were no audio
  frames being written to the channel outside of the audiohook, then any whisper
  audio has nothing to mix with and so is not heard on the hooked channel.

  This change removes that requirement by creating a framehook driven timer on the
  channel that creates silent frames the whispered frames can be mixed into.

  The framehook is automatically added with the first attached whisper audiohook
  and is removed when the last one is detached.  If there are no attached streams
  being written to the channel, the timer is used to generate a default one. If
  a stream is attached; the timer is ignored until the stream stops. This stop
  and start is automatically triggered by the presence of a written stream and does
  not require any external action.

  Resolves: #1966

  UserNote: There is no longer a requirement to play silence or otherwise have
  a stream of audio being sent to a hooked channel for whispered frames to be
  heard.

#### Logger: Don't discard WARNING or ERROR messages.
  Author: Ben Ford
  Date:   2026-06-01

  When the logger reaches it's threshold for messages, don't discard the
  WARNING or ERROR messages up to a certain amount. If THAT threshold
  reaches its maximum, start discarding the oldest non-WARNING/ERROR
  messages until we only have WARNING and ERROR messages left. Everything
  will be discarded after that until we have room again for more.

  Also added a test that limits the queue sizes and logs many messages to
  see the queues hitting their maximums and discarding the appropriate
  messages.

  Fixes: #1882

#### func_strings: Fix syntax error in STRBETWEEN documentation example
  Author: Aqeel Abbas
  Date:   2026-07-12

  The STRBETWEEN function documentation example was missing a closing
  brace in the SendDTMF call. Corrected the syntax to ensure the
  dialplan example is functional.

#### res_pjsip_registrar: Resolve AOR for identified endpoints
  Author: Mehrdad Seifzadeh
  Date:   2026-06-10

  When an inbound REGISTER is matched to an endpoint using an
  identification method such as IP, header, or request URI, the endpoint can
  be identified and authenticated successfully while registrar AOR
  resolution still fails.

  find_registrar_aor() only resolved AOR names for username and
  auth_username identify methods. A header-only endpoint therefore left
  aor_name unset and failed registration with "AOR '' not found" even when
  the REGISTER To URI matched one of the endpoint's configured AORs.

  Resolve the REGISTER AOR from the To URI for endpoint identification
  methods that do not directly provide an AOR name, while still
  constraining the match to the already-identified endpoint's configured AOR
  list.

  Fixes: #1398

#### res_http_websocket: Unref session when it fails to establish.
  Author: Thomas Guebels
  Date:   2026-07-09

  Two error paths in res_http_websocket could leak an ast_websocket session by
  failing to drop a reference after the session had been allocated.

  The first occurs when session ID generation fails. While this is largely
  theoretical, as it would require an out-of-memory condition, the session
  reference should still be released correctly.

  The second occurs when the ast_websocket_pre_callback session_attempted callback
  returns an error. This path is reachable through ari_websockets, which
  implements this callback and can legitimately fail under certain conditions.

  Fixes: #2020

#### Bundled pjproject: Make it easier to override options in config_site.h.
  Author: George Joseph
  Date:   2026-07-02

  Bundled pjproject uses ./third-party/pjproject/patches/config_site.h to set
  many pjproject build options like PJ_IOQUEUE_MAX_HANDLES, PJSIP_MAX_URL_SIZE,
  etc. Editing that file however, causes the git tree to become dirty which
  is inconvenient.  So...

  * Updated the Asterisk configure scripts to pass the PJPROJECT_CFLAGS variable
  down to the pjproject configure scripts.  See the UserNote below for details.

  * Updated ./third-party/pjproject/patches/config_site.h to allow the following
  options to be overridden:
  PJ_MAX_HOSTNAME, PJSIP_MAX_URL_SIZE, PJ_IOQUEUE_MAX_HANDLES.
  Other options in config_site.h are not overridable because they can have a
  bad effect on the overall operation of pjproject.  This may be revisited in
  the future.  Options not already set in config_site.h can still be set.

  * Fixed an issue where if the Linux `epoll` facility is used (which it is by
  default) the default PJ_IOQUEUE_MAX_HANDLES and PJSIP_MAX_TRANSPORTS were
  being left at 1024 instead of being icnreased to 5000.

  * The `pjproject show buildopts` CLI command previously only showed options from
  the top level `PJ` pjproject layer but now also shows many from the `PJSIP`
  layer.  Many of these, such as PJSIP_MAX_TRANSPORTS, can be set using
  PJPROJECT_CFLAGS provided they're not already unconditionally set in
  config_site.h.

  * The `pjsip dump endpt` CLI command previously required that the pjproject
  log level be already set to at least 3 or no output would be produced.  The
  command now does that automatically then sets it back to whatever it was.
  This isn't strictly related to this PR but was just nagging me.

  UserNote: Bundled pjproject: It's now possible to override some of the pjproject
  build options contained in ./third-party/pjproject/patches/config_site.h by
  adding PJPROJECT_CFLAGS to your Asterisk ./configure command line.  For example:
  `./configure ... PJPROJECT_CFLAGS='-DPJ_OPT1=8192 -DPJ_OPT2=512'`
  Any option in config_site.h that's wrapped in a `#ifndef` block can be overridden
  and many of the `PJSIP` options displayed by `pjproject show buildopts` can be
  set.  WARNING: Adjusting these options without understanding their effect can
  cripple your Asterisk instances.  You shouldn't adjust them unless you need
  to solve a specific issue.

  Resolves: #2011

#### test_performance: Add performance experimentation test module.
  Author: Joshua C. Colp
  Date:   2026-06-20

  This is a module which is only built when TEST_FRAMEWORK is enabled
  and provides CLI commands for testing performance of certain things
  on the system they are invoked on. The first CLI commands added
  cover the most common container usage: storage of objects with a
  lookup based on a string key. These commands take various arguments
  and allow you to see how they perform. There is also an "all" command
  named "performance test container_key_lookup_all" that will execute
  all of these container tests and pass through the given arguments,
  which makes it easy to run all of the tests for given usage.

  To facilitate a vector bsearch test a new AST_VECTOR_BSEARCH macro
  has been added that allows more efficient searching of sorted vectors.

#### manager: Move away from shared linked list for events.
  Author: Joshua C. Colp
  Date:   2026-05-15

  This change moves manager away from a shared linked list
  for events to a per-session vector of pending events. This
  allows early filtering of events which eliminates the need
  to wake up threads for events that they have no interest in.

  TCP based connections have also been moved to an alert pipe
  based wakeup mechanism to reduce locking and simplify usage.

  UserNote: The "manager show eventq" CLI command has been
  removed as there is no longer a single event queue to display.

#### chan_websocket: Use leftover data if no frames are available when the timer fires.
  Author: George Joseph
  Date:   2026-06-29

  When the 20ms channel timer fires but there are no frames available in
  the queue, we now check for leftover data in the buffer and if there is
  any, we create a frame with it and send it to the core. This resolves an
  issue with the leftover data being delayed if a STOP_MEDIA_BUFFERING
  command is delayed. Some existing comments were also clarified to
  account for the new behavior.

  Resolves: #2006

#### res_pjsip_session: Bound delayed BYE behind UAC INVITE
  Author: Mehrdad Seifzadeh
  Date:   2026-06-06

  When a confirmed session is being terminated while an outgoing in-dialog
  INVITE transaction is still outstanding, the BYE is delayed until the
  outstanding transaction terminates.

  If that INVITE has already received a provisional response and the final
  response is malformed and rejected before transaction processing, the
  transaction can remain outstanding and the delayed BYE can keep the
  session, media state, RTP instance, and PJPROJECT pools referenced after
  the channels are gone.

  When a BYE is delayed behind an outstanding UAC INVITE, set a PJPROJECT
  transaction timeout on that INVITE so the delayed cleanup path has a
  bounded wait. If PJPROJECT terminates the dialog as a result of the
  timeout, discard the delayed BYE instead of sending a duplicate BYE.

  Fixes: #1965

#### extensions.ael.sample: Restore removed macros
  Author: Sean Bright
  Date:   2026-06-24

  Commit e8f548c1 removed AEL `macro` definition and calls from the
  sample configuration file, but those do not use the deprecated/removed
  `Macro` app - they use `Gosub` under the hood.

#### format_cap: guard against NULL src in *_from_cap helpers
  Author: Alexandre Fournier
  Date:   2026-06-23

  ast_format_cap_append_from_cap() and ast_format_cap_replace_from_cap()
  dereference 'src' (src->preference_order) without checking it for NULL.

  A dummy channel allocated with ast_dummy_channel_alloc() never sets a
  native-format capability, so ast_channel_nativeformats() returns NULL on
  such channels. When CHANNEL(audionativeformat) / CHANNEL(videonativeformat)
  is evaluated against a dummy channel (e.g. via ARI channelvars during a
  Stasis VarSet event raised while app_voicemail builds the notification
  email on a dummy channel), func_channel_read() passes that NULL straight
  into ast_format_cap_append_from_cap(), causing a NULL dereference at
  offset 0x28 and a SIGSEGV.

  Guard both helpers against a NULL source. A NULL source simply means
  "no formats to copy", so appending/replacing nothing is the correct
  no-op behaviour. This also protects all other callers.

  Fixes https://github.com/asterisk/asterisk/issues/1992

  AI disclosure: this was generated using Claude Opus 4.8, tested to fix the issue. Not sure if it is the *right* way to do it.

#### configs: Comment out `values` setting to avoid parse error
  Author: Sean Bright
  Date:   2026-06-23

  Fixes the following after a `make samples`:

  ```
  config.c:2281 process_text_line: parse error: No category context for line 64 of ...
  ```

#### app_mixmonitor: Fix duplex recording for non 8K codecs
  Author: Mike Bradeen
  Date:   2026-06-10

  The native sampling of duplex recording is set to match the raw 8K
  output format. If one or more of the streams being recorded is above
  8K, the frame size coming into the mixmonitor is too large and needs
  to be translated to 8K before being mixed into the stereo frame to
  avoid garbled and mistimed audio

  Fixes: #1779

#### res_http_websocket: Add timeout to client handshakes.
  Author: George Joseph
  Date:   2026-06-09

  The websocket client proxy and server handshakes use ast_iostream_gets which
  are blocking calls.  If the outgoing connection succeeds at the TCP or TLS
  layer but the proxy (if configured) or the websocket server fails to respond
  to the CONNECT or GET requests, the process can hang indefinitely and escalate
  to a deadlock.  To address this, the handshakes are now guarded with calls to
  ast_iostream_set_timeout_sequence() with the timeout set to the client's
  (connection_timeout * 2) milliseconds.

  In order to use ast_iostream_set_timeout_sequence(), the iostream has to be
  set to non-blocking with ast_iostream_nonblock() but there was no way to
  reset the stream back to blocking mode so a new API ast_iostream_blocking()
  was added for it.

  Tracing was also enabled in the websocket_client_handshake function for
  future troubleshooting.

  Resolves: #1979

#### extension_state: Add new extension state API.
  Author: Joshua C. Colp
  Date:   2026-02-10

  Extension state to this point has been an API implemented
  inside the PBX core resulting in its state being intermingled
  with that of the dialplan. This increased the complexity of
  the PBX core and made it difficult to enact improvements.

  This change adds a new separate extension state API
  which receives updates from the PBX core as configuration
  changes but maintains its own separate state. The API is
  also written to fully take advantage of modern APIs in a
  more selective manner by subscribing each extension state to
  only the devices it is interested in, ultimately reducing
  resource consumption during updates. Presence state updates
  being infrequently done use a single shared subscription that
  goes through the extension states to find and update ones
  that the update is applicable to.

  Legacy API support is provided by reimplementing the API
  as wrappers over the new extension state API. This improves
  the legacy API by making it multithreaded, with each callback
  being individually subscribed.

  Autohints support is maintained but has been separated out
  into a self contained new implementation.

  Synchronous subscription support has also been added to
  Stasis to remove the overhead of asynchronous publishing when
  the handling of published messages is small and fast.

#### res_pjsip: Add external_signaling_hostname transport option
  Author: Alexis Chenard
  Date:   2026-06-01

  Adds a new transport option 'external_signaling_hostname' which allows
  a hostname or FQDN to be used in SIP Contact and Via headers instead of
  the automatically determined IP address. This is useful when a remote
  SIP endpoint requires a fully qualified domain name in these headers.

  The option is mutually exclusive with 'external_signaling_address' and
  an error is raised at transport load time if both are set simultaneously.

  Resolves: #1749

  UserNote: A new pjsip.conf transport option 'external_signaling_hostname'
  has been added. When set, this value will be used in SIP Contact and Via
  headers instead of the automatically determined IP address. This option
  is mutually exclusive with 'external_signaling_address'.

#### WebSocket Enhancements: Proxies and Keepalives for ARI and Media Outbound Websockets.
  Author: George Joseph
  Date:   2026-05-12

  See the notes below for high-level descriptions of the new features.

  * Proxies

  Outbound/forward HTTP proxies are now supported and configurable in
  websocket_client.conf. You can specify a host:port plus optional proxy_username
  and proxy_password. Because WebSockets aren't consistently supported among
  proxies (specifically passing through UPGRADEs), the CONNECT method is always
  used to establish a TCP tunnel through the proxy. This is required if a TLS
  session is to be established with the WebSocket server anyway.  It's important
  to understand that that negotiation with the proxy is ALWAYS unsecured. Once
  the proxy establishes the tunnel, the TLS session will be negotiated directly
  with the remote WebSocket server via the tunnel.

  * Keepalives

  Both TCP-level and WebSocket PING/PONG keepalives can be configured and are
  available with either the curl or tcptls client implementations. The TCP
  keepalives are handled entirely by the operating system and require no
  resources from Asterisk but by their very nature, they can't traverse proxies.
  WebSocket PING/PONGs are implemented in the Asterisk websocket code and require
  a scheduler thread to keep track of them so they're a bit more complicated but
  they do traverse proxies.  Which one is used is completely up to the admin.
  You could use both.

  * Other Changes

  A few changes were needed to res/ari/ari_websockets and
  res/res_aeap/transport_websocket to add explicit calls to ast_websocket_close.
  They had been assuming that the websocket session destructor would close the
  websocket when it unreffed it but the keepalive process now holds a reference
  so the destructor wouldn't actually run without the call to ast_websocket_close
  to stop the keepalives.

  A few new methods were added to tcptls.c to allow switching an existing
  connection from unsecured to TLS.  These were required because the initial
  connection and handshake with a proxy is always unsecured but then needs
  to be switched to TLS if required for the remote WebSocket server.

  There was a bug in sorcery.h where the ast_sorcery_register_uint macro
  was referencing _stringify (which doesn't exist) instead of _sorcery_stringify.

  Resolves: #1881
  Resolves: #1933

  UserNote: Forward/outbound proxies can now be specified for outbound websockets.
  See the websocket_client.conf.sample file for configuration information.

  UserNote: TCP-level or WebSocket PING/PONG keepalives can now be enabled on
  outbound websockets.  They can help detect network failures even when a
  persistent connection is idle. See the websocket_client.conf.sample
  file for configuration information.

  DeveloperNote: The addition of the proxy and keepalive configuration parameters
  pushed the websocket client parameter count over 32. This necessitated changing
  the size of the ast_ws_client_fields enum from a 32 bit bitfield to a 64-bit
  bitfield with a corresponding change to the ast_websocket_client structure.

#### chan_local: Update chan_local references for Local channels.
  Author: Naveen Albert
  Date:   2026-04-01

  chan_local no longer exists since Local channels are built into the
  core (core_local), but there are still comments which reference it,
  including in the configs. Update these to avoid confusion.

  Resolves: #1849

