
## Change Log for Release asterisk-23.1.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-23.1.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/23.0.0...23.1.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-23.1.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 53
- Commit Authors: 17
- Issues Resolved: 37
- Security Advisories Resolved: 0

### User Notes:

- #### res_stir_shaken: Add STIR_SHAKEN_ATTESTATION dialplan function.
  The STIR_SHAKEN_ATTESTATION dialplan function has been added
  which will allow suppressing attestation on a call-by-call basis
  regardless of the profile attached to the outgoing endpoint.

- #### func_channel: Allow R/W of ADSI CPE capability setting.
  CHANNEL(adsicpe) can now be read or written to change
  the channels' ADSI CPE capability setting.

- #### func_hangupcause.c: Add access to Reason headers via HANGUPCAUSE()
  Added a new option to HANGUPCAUSE to access additional
  information about hangup reason. Reason headers from pjsip
  could be read using 'tech_extended' cause type.

- #### func_math: Add DIGIT_SUM function.
  The DIGIT_SUM function can be used to return the digit sum of
  a number.

- #### app_sf: Add post-digit timer option to ReceiveSF.
  The 't' option for ReceiveSF now allows for a timer since
  the last digit received, in addition to the number-wide timeout.

- #### app_dial: Allow fractional seconds for dial timeouts.
  The answer and progress dial timeouts now have millisecond
  precision, instead of having to be whole numbers.

- #### chan_dahdi: Add DAHDI_CHANNEL function.
  The DAHDI_CHANNEL function allows for getting/setting
  certain properties about DAHDI channels from the dialplan.


### Upgrade Notes:

- #### app_queue.c: Fix error in Queue parameter documentation.
  As part of Asterisk 21, macros were removed from Asterisk.
  This resulted in argument order changing for the Queue dialplan
  application since the macro argument was removed. Upgrade notice was
  missed when this was done, so this upgrade note has been added to
  provide a record of such and a notice to users who may have not upgraded
  yet.

- #### res_audiosocket: add message types for all slin sample rates
  New audiosocket message types 0x11 - 0x18 has been added
  for slin12, slin16, slin24, slin32, slin44, slin48, slin96, and
  slin192 audio. External applications using audiosocket may need to be
  updated to support these message types if the audiosocket channel is
  created with one of these audio formats.

- #### taskpool: Add taskpool API, switch Stasis to using it.
  The threadpool_* options in stasis.conf have now been deprecated
  though they continue to be read and used. They have been replaced with taskpool
  options that give greater control over the underlying taskpool used for stasis.


### Developer Notes:

- #### chan_pjsip: Add technology-specific off-nominal hangup cause to events.
  A "tech_cause" parameter has been added to the
  ChannelHangupRequest and ChannelDestroyed ARI event messages and a "TechCause"
  parameter has been added to the HangupRequest, SoftHangupRequest and Hangup
  AMI event messages.  For chan_pjsip, these will be set to the last SIP
  response status code for off-nominally terminated calls.  The parameter is
  suppressed for nominal termination.

- #### ARI: The bridges play and record APIs now handle sample rates > 8K correctly.
  The ARI /bridges/play and /bridges/record REST APIs have new
  parameters that allow the caller to specify the format to be used on the
  "Announcer" and "Recorder" channels respecitvely.

- #### taskpool: Add taskpool API, switch Stasis to using it.
  The taskpool API has been added for common usage of a
  pool of taskprocessors. It is suggested to use this API instead of the
  threadpool+taskprocessor approach.


### Commit Authors:

- Allan Nathanson: (1)
- Anthony Minessale: (1)
- Bastian Triller: (1)
- Ben Ford: (2)
- Christoph Moench-Tegeder: (1)
- George Joseph: (9)
- Igor Goncharovsky: (1)
- Joshua C. Colp: (6)
- Max Grobecker: (1)
- Nathan Monfils: (1)
- Naveen Albert: (18)
- Roman Pertsev: (1)
- Sean Bright: (3)
- Sven Kube: (3)
- Tinet-mucw: (1)
- gauravs456: (1)
- phoneben: (2)

## Issue and Commit Detail:

### Closed Issues:

  - 781: [improvement]: Allow call by call disabling Stir/Shaken header inclusion 
  - 1340: [bug]: comfort noise packet corrupted
  - 1419: [bug]: static code analysis issues in app_adsiprog.c
  - 1422: [bug]: static code analysis issues in apps/app_externalivr.c
  - 1425: [bug]: static code analysis issues in apps/app_queue.c
  - 1434: [improvement]: pbx_variables: Create real channel for dialplan eval CLI command
  - 1436: [improvement]: res_cliexec: Avoid unnecessary cast to char*
  - 1451: [bug]: ast_config_text_file_save2(): incorrect handling of deep/wide template inheritance
  - 1455: [new-feature]: chan_dahdi: Add DAHDI_CHANNEL function
  - 1467: [bug]: Crash in res_pjsip_refer during REFER progress teardown with PJSIP_TRANSFER_HANDLING(ari-only)
  - 1478: [improvement]: Stasis threadpool -> taskpool
  - 1479: [bug]: The ARI bridge play and record APIs limit audio bandwidth by forcing the slin8 format.
  - 1483: [improvement]: sig_analog: Eliminate possible timeout for Last Number Redial
  - 1485: [improvement]: func_scramble: Add example to XML documentation.
  - 1487: [improvement]: app_dial: Allow partial seconds to be used for dial timeouts
  - 1489: [improvement]: config_options.c: Improve misleading error message
  - 1491: [bug]: Segfault: `channelstorage_cpp` fast lookup without lock (`get_by_name_exact`/`get_by_uniqueid`) leads to UAF during hangup
  - 1493: [new-feature]: app_sf: Add post-digit timer option
  - 1496: [improvement]: dsp.c: Minor fixes to debug log messages
  - 1499: [new-feature]: func_math: Add function to return the digit sum
  - 1501: [improvement]: codec_builtin: Fix some inaccurate quality weights.
  - 1505: [improvement]: res_fax: Add XML documentation for channel variables
  - 1507: [improvement]: res_tonedetect: Minor formatting issue in documentation
  - 1509: [improvement]: res_fax.c — log debug error as debug, not regular log
  - 1510: [new-feature]: sig_analog: Allow '#' to end the inter-digit timeout when dialing.
  - 1514: [improvement]: func_channel: Allow R/W of ADSI CPE capability setting.
  - 1517: [improvement]: core_unreal: Preserve ADSI capability when dialing Local channels
  - 1519: [improvement]: app_dial / func_callerid: DNIS information is not propagated by Dial
  - 1525: [bug]: chan_websocket: fix use of raw payload variable for string comparison in process_text_message
  - 1534: [bug]: app_queue when using gosub breaks dialplan when going from 20 to 21, What's new in 21 doesn't mention it's a breaking change,
  - 1535: [bug]: chan_pjsip changes SSRC on WebRTC channels, which is unsupported by some browsers
  - 1536: [bug]: asterisk -rx connects to console instead of executing a command
  - 1539: [bug]: safe_asterisk without TTY doesn't log to file
  - 1544: [improvement]: While Receiving the MediaConnect Message Using External Media Over websocket ChannelID is  Details are missing
  - 1554: [bug]: safe_asterisk recurses into subdirectories of startup.d after f97361
  - 1559: [improvement]: Handle TLS handshake attacks in order to resolve the issue of exceeding the maximum number of HTTPS sessions.
  - 1578: [bug]: Deadlock with externalMedia custom channel id and cpp map channel backend

### Commits By Author:

- #### Allan Nathanson (1):
  - config.c: fix saving of deep/wide template configurations

- #### Anthony Minessale (1):
  - Update contact information for anthm

- #### Bastian Triller (1):
  - Fix some doxygen, typos and whitespace

- #### Ben Ford (2):
  - app_queue.c: Fix error in Queue parameter documentation.
  - rtp_engine.c: Add exception for comfort noise payload.

- #### Christoph Moench-Tegeder (1):
  - Fix Endianness detection in utils.h for non-Linux

- #### George Joseph (9):
  - channelstorage:  Allow storage driver read locking to be skipped.
  - res_stir_shaken: Add STIR_SHAKEN_ATTESTATION dialplan function.
  - chan_pjsip: Disable SSRC change for WebRTC endpoints.
  - safe_asterisk:  Fix logging and sorting issue.
  - chan_pjsip: Add technology-specific off-nominal hangup cause to events.
  - taskpool:  Fix some references to threadpool that should be taskpool.
  - chan_websocket.c: Change payload references to command instead.
  - channelstorage_cpp_map_name_id: Add read locking around retrievals.
  - ARI: The bridges play and record APIs now handle sample rates > 8K correctly.

- #### Igor Goncharovsky (1):
  - func_hangupcause.c: Add access to Reason headers via HANGUPCAUSE()

- #### Joshua C. Colp (6):
  - devicestate: Don't publish redundant device state messages.
  - endpoints: Remove need for stasis subscription.
  - app_queue: Allow stasis message filtering to work.
  - sorcery: Move from threadpool to taskpool.
  - taskpool: Update versions for taskpool stasis options.
  - taskpool: Add taskpool API, switch Stasis to using it.

- #### Max Grobecker (1):
  - res_pjsip_geolocation: Add support for Geolocation loc-src parameter

- #### Nathan Monfils (1):
  - manager.c: Fix presencestate object leak

- #### Naveen Albert (18):
  - func_callerid: Document limitation of DNID fields.
  - func_channel: Allow R/W of ADSI CPE capability setting.
  - core_unreal: Preserve ADSI capability when dialing Local channels.
  - sig_analog: Allow '#' to end the inter-digit timeout when dialing.
  - func_math: Add DIGIT_SUM function.
  - app_sf: Add post-digit timer option to ReceiveSF.
  - codec_builtin.c: Adjust some of the quality scores to reflect reality.
  - res_tonedetect: Fix formatting of XML documentation.
  - res_fax: Add XML documentation for channel variables.
  - app_dial: Allow fractional seconds for dial timeouts.
  - dsp.c: Make minor fixes to debug log messages.
  - config_options.c: Improve misleading warning.
  - func_scramble: Add example to XML documentation.
  - sig_analog: Eliminate potential timeout with Last Number Redial.
  - chan_dahdi: Add DAHDI_CHANNEL function.
  - app_adsiprog: Fix possible NULL dereference.
  - res_cliexec: Remove unnecessary casts to char*.
  - pbx_variables.c: Create real channel for "dialplan eval function".

- #### Roman Pertsev (1):
  - res_audiosocket: fix temporarily unavailable

- #### Sean Bright (3):
  - safe_asterisk: Resolve a POSIX sh problem and restore globbing behavior.
  - app_externalivr: Prevent out-of-bounds read during argument processing.
  - audiohook.c: Ensure correct AO2 reference is dereffed.

- #### Sven Kube (3):
  - res_audiosocket: add message types for all slin sample rates
  - stasis_channels.c: Make protocol_id optional to enable blind transfer via ari
  - stasis_channels.c: Add null check for referred_by in ast_ari_transfer_message_create

- #### Tinet-mucw (1):
  - iostream.c: Handle TLS handshake attacks in order to resolve the issue of exceeding the maximum number of HTTPS sessions.

- #### gauravs456 (1):
  - chan_websocket: Add channel_id to MEDIA_START, DRIVER_STATUS and DTMF_END events.

- #### phoneben (2):
  - res_fax.c: lower FAXOPT read warning to debug level
  - app_queue: Add NULL pointer checks in app_queue

### Commit List:

-  channelstorage:  Allow storage driver read locking to be skipped.
-  res_audiosocket: fix temporarily unavailable
-  safe_asterisk: Resolve a POSIX sh problem and restore globbing behavior.
-  res_stir_shaken: Add STIR_SHAKEN_ATTESTATION dialplan function.
-  iostream.c: Handle TLS handshake attacks in order to resolve the issue of exceeding the maximum number of HTTPS sessions.
-  chan_pjsip: Disable SSRC change for WebRTC endpoints.
-  chan_websocket: Add channel_id to MEDIA_START, DRIVER_STATUS and DTMF_END events.
-  safe_asterisk:  Fix logging and sorting issue.
-  Fix Endianness detection in utils.h for non-Linux
-  app_queue.c: Fix error in Queue parameter documentation.
-  devicestate: Don't publish redundant device state messages.
-  chan_pjsip: Add technology-specific off-nominal hangup cause to events.
-  res_audiosocket: add message types for all slin sample rates
-  res_fax.c: lower FAXOPT read warning to debug level
-  endpoints: Remove need for stasis subscription.
-  app_queue: Allow stasis message filtering to work.
-  taskpool:  Fix some references to threadpool that should be taskpool.
-  Update contact information for anthm
-  chan_websocket.c: Change payload references to command instead.
-  func_callerid: Document limitation of DNID fields.
-  func_channel: Allow R/W of ADSI CPE capability setting.
-  core_unreal: Preserve ADSI capability when dialing Local channels.
-  func_hangupcause.c: Add access to Reason headers via HANGUPCAUSE()
-  sig_analog: Allow '#' to end the inter-digit timeout when dialing.
-  func_math: Add DIGIT_SUM function.
-  app_sf: Add post-digit timer option to ReceiveSF.
-  codec_builtin.c: Adjust some of the quality scores to reflect reality.
-  res_tonedetect: Fix formatting of XML documentation.
-  res_fax: Add XML documentation for channel variables.
-  channelstorage_cpp_map_name_id: Add read locking around retrievals.
-  app_dial: Allow fractional seconds for dial timeouts.
-  dsp.c: Make minor fixes to debug log messages.
-  config_options.c: Improve misleading warning.
-  func_scramble: Add example to XML documentation.
-  sig_analog: Eliminate potential timeout with Last Number Redial.
-  ARI: The bridges play and record APIs now handle sample rates > 8K correctly.
-  res_pjsip_geolocation: Add support for Geolocation loc-src parameter
-  sorcery: Move from threadpool to taskpool.
-  stasis_channels.c: Make protocol_id optional to enable blind transfer via ari
-  config.c: fix saving of deep/wide template configurations
-  Fix some doxygen, typos and whitespace
-  stasis_channels.c: Add null check for referred_by in ast_ari_transfer_message_create
-  app_queue: Add NULL pointer checks in app_queue
-  app_externalivr: Prevent out-of-bounds read during argument processing.
-  chan_dahdi: Add DAHDI_CHANNEL function.
-  taskpool: Update versions for taskpool stasis options.
-  taskpool: Add taskpool API, switch Stasis to using it.
-  app_adsiprog: Fix possible NULL dereference.
-  manager.c: Fix presencestate object leak
-  audiohook.c: Ensure correct AO2 reference is dereffed.
-  res_cliexec: Remove unnecessary casts to char*.
-  rtp_engine.c: Add exception for comfort noise payload.
-  pbx_variables.c: Create real channel for "dialplan eval function".

### Commit Details:

#### channelstorage:  Allow storage driver read locking to be skipped.
  Author: George Joseph
  Date:   2025-11-06

  After PR #1498 added read locking to channelstorage_cpp_map_name_id, if ARI
  channels/externalMedia was called with a custom channel id AND the
  cpp_map_name_id channel storage backend is in use, a deadlock can occur when
  hanging up the channel. It's actually triggered in
  channel.c:__ast_channel_alloc_ap() when it gets a write lock on the
  channelstorage driver then subsequently does a lookup for channel uniqueid
  which now does a read lock. This is an invalid operation and causes the lock
  state to get "bad". When the channels try to hang up, a write lock is
  attempted again which hangs and causes the deadlock.

  Now instead of the cpp_map_name_id channelstorage driver "get" APIs
  automatically performing a read lock, they take a "lock" parameter which
  allows a caller who already has a write lock to indicate that the "get" API
  must not attempt its own lock.  This prevents the state from getting mesed up.

  The ao2_legacy driver uses the ao2 container's recursive mutex so doesn't
  have this issue but since it also implements the common channelstorage API,
  it needed its "get" implementations updated to take the lock parameter. They
  just don't use it.

  Resolves: #1578

#### res_audiosocket: fix temporarily unavailable
  Author: Roman Pertsev
  Date:   2025-10-07

  Operations on non-blocking sockets may return a resource temporarily unavailable error (EAGAIN or EWOULDBLOCK). This is not a fatal error but a normal condition indicating that the operation would block.

  This patch corrects the handling of this case. Instead of incorrectly treating it as a reason to terminate the connection, the code now waits for data to arrive on the socket.

#### safe_asterisk: Resolve a POSIX sh problem and restore globbing behavior.
  Author: Sean Bright
  Date:   2025-10-22

  * Using `==` with the POSIX sh `test` utility is UB.
  * Switch back to using globs instead of using `$(find … | sort)`.
  * Fix a missing redirect when checking for the OS type.

  Resolves: #1554

#### res_stir_shaken: Add STIR_SHAKEN_ATTESTATION dialplan function.
  Author: George Joseph
  Date:   2025-10-24

  Also...

  * Refactored the verification datastore process so instead of having
  a separate channel datastore for each verification result, there's only
  one channel datastore with a vector of results.

  * Refactored some log messages to include channel name and removed
  some that would be redundant if a memory allocation failed.

  Resolves: #781

  UserNote: The STIR_SHAKEN_ATTESTATION dialplan function has been added
  which will allow suppressing attestation on a call-by-call basis
  regardless of the profile attached to the outgoing endpoint.

#### iostream.c: Handle TLS handshake attacks in order to resolve the issue of exceeding the maximum number of HTTPS sessions.
  Author: Tinet-mucw
  Date:   2025-10-26

  The TCP three-way handshake completes, but if the server is under a TLS handshake attack, asterisk will get stuck at SSL_do_handshake().
  In this case, a timeout mechanism should be set for the SSL/TLS handshake process to prevent indefinite waiting during the SSL handshake.

  Resolves: #1559

#### chan_pjsip: Disable SSRC change for WebRTC endpoints.
  Author: George Joseph
  Date:   2025-10-21

  Commit b333ee3b introduced a fix to chan_pjsip that addressed RTP issues with
  blind transfers and some SBCs.  Unfortunately, the fix broke some WebRTC
  clients that are sensitive to SSRC changes and non-monotonic timestamps so
  the fix is now disabled for endpoints with the "bundle" parameter set to true.

  Resolves: #1535

#### chan_websocket: Add channel_id to MEDIA_START, DRIVER_STATUS and DTMF_END events.
  Author: gauravs456
  Date:   2025-10-21

  Resolves: #1544

#### safe_asterisk:  Fix logging and sorting issue.
  Author: George Joseph
  Date:   2025-10-17

  Re-enabled "TTY=9" which was erroneously disabled as part of a recent
  security fix and removed another logging "fix" that was added.

  Also added a sort to the "find" that enumerates the scripts to be sourced so
  they're sourced in the correct order.

  Resolves: #1539

#### Fix Endianness detection in utils.h for non-Linux
  Author: Christoph Moench-Tegeder
  Date:   2025-10-19

  Commit 43bf8a4ded7a65203b766b91eaf8331a600e9d8d introduced endian
  dependend byte-swapping code in include/asterisk/utils.h, where the
  endianness was detected using the __BYTE_ORDER macro. This macro
  lives in endian.h, which on Linux is included implicitely (by the
  network-related headers, I think), but on FreeBSD the headers are
  laid out differently and we do not get __BYTE_ORDER the implicit way.

  Instead, this makes the usage of endian.h explicit by including it
  where we need it, and switches the BYTE_ORDER/*ENDIAN macros to the
  POSIX-defined ones (see
  https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/endian.h.html
  for standard compliance). Additionally, this adds a compile-time check
  for the endianness-logic: compilation will fail if neither big nor
  little endian can be detected.

  Fixes: #1536

#### app_queue.c: Fix error in Queue parameter documentation.
  Author: Ben Ford
  Date:   2025-10-20

  When macro was removed in Asterisk 21, the parameter documentation in
  code was not updated to reflect the correct numerization for gosub. It
  still stated that it was the seventh parameter, but got shifted to the
  sixth due to the removal of macro. This has been updated to correctly
  reflect the parameter order, and a note has been added to the XML that
  states this was done after the initial commit.

  Fixes: #1534

  UpgradeNote: As part of Asterisk 21, macros were removed from Asterisk.
  This resulted in argument order changing for the Queue dialplan
  application since the macro argument was removed. Upgrade notice was
  missed when this was done, so this upgrade note has been added to
  provide a record of such and a notice to users who may have not upgraded
  yet.

#### devicestate: Don't publish redundant device state messages.
  Author: Joshua C. Colp
  Date:   2025-10-17

  When publishing device state check the local cache for the
  existing device state. If the new device state is unchanged
  from the prior one, don't bother publishing the update. This
  can reduce the work done by consumers of device state, such
  as hints and app_queue, by not publishing a message to them.

  These messages would most often occur with devices that are
  seeing numerous simultaneous channels. The underlying device
  state would remain as in use throughout, but an update would
  be published as channels are created and hung up.

#### chan_pjsip: Add technology-specific off-nominal hangup cause to events.
  Author: George Joseph
  Date:   2025-10-14

  Although the ISDN/Q.850/Q.931 hangup cause code is already part of the ARI
  and AMI hangup and channel destroyed events, it can be helpful to know what
  the actual channel technology code was if the call was unsuccessful.
  For PJSIP, it's the SIP response code.

  * A new "tech_hangupcause" field was added to the ast_channel structure along
  with ast_channel_tech_hangupcause() and ast_channel_tech_hangupcause_set()
  functions.  It should only be set for off-nominal terminations.

  * chan_pjsip was modified to set the tech hangup cause in the
  chan_pjsip_hangup() and chan_pjsip_session_end() functions.  This is a bit
  tricky because these two functions aren't always called in the same order.
  The channel that hangs up first will get chan_pjsip_session_end() called
  first which will trigger the core to call chan_pjsip_hangup() on itself,
  then call chan_pjsip_hangup() on the other channel.  The other channel's
  chan_pjsip_session_end() function will get called last.  Unfortunately,
  the other channel's HangupRequest events are sent before chan_pjsip has had a
  chance to set the tech hangupcause code so the HangupRequest events for that
  channel won't have the cause code set.  The ChannelDestroyed and Hangup
  events however will have the code set for both channels.

  * A new "tech_cause" field was added to the ast_channel_snapshot_hangup
  structure. This is a public structure so a bit of refactoring was needed to
  preserve ABI compatibility.

  * The ARI ChannelHangupRequest and ChannelDestroyed events were modified to
  include the "tech_cause" parameter in the JSON for off-nominal terminations.
  The parameter is suppressed for nominal termination.

  * The AMI SoftHangupRequest, HangupRequest and Hangup events were modified to
  include the "TechCause" parameter for off-nominal terminations. Like their ARI
  counterparts, the parameter is suppressed for nominal termination.

  DeveloperNote: A "tech_cause" parameter has been added to the
  ChannelHangupRequest and ChannelDestroyed ARI event messages and a "TechCause"
  parameter has been added to the HangupRequest, SoftHangupRequest and Hangup
  AMI event messages.  For chan_pjsip, these will be set to the last SIP
  response status code for off-nominally terminated calls.  The parameter is
  suppressed for nominal termination.

#### res_audiosocket: add message types for all slin sample rates
  Author: Sven Kube
  Date:   2025-10-10

  Extend audiosocket messages with types 0x11 - 0x18 to create asterisk
  frames in slin12, slin16, slin24, slin32, slin44, slin48, slin96, and
  slin192 format, enabling the transmission of audio at a higher sample
  rates. For audiosocket messages sent by Asterisk, the message kind is
  determined by the format of the originating asterisk frame.

  UpgradeNote: New audiosocket message types 0x11 - 0x18 has been added
  for slin12, slin16, slin24, slin32, slin44, slin48, slin96, and
  slin192 audio. External applications using audiosocket may need to be
  updated to support these message types if the audiosocket channel is
  created with one of these audio formats.

#### res_fax.c: lower FAXOPT read warning to debug level
  Author: phoneben
  Date:   2025-10-03

  Reading ${FAXOPT()} before a fax session is common in dialplans to check fax state.
  Currently this logs an error even when no fax datastore exists, creating excessive noise.
  Change these messages to ast_debug(3, …) so they appear only with debug enabled.

  Resolves: #1509

#### endpoints: Remove need for stasis subscription.
  Author: Joshua C. Colp
  Date:   2025-10-10

  When an endpoint is created in the core of Asterisk a subscription
  was previously created alongside it to monitor any channels being
  destroyed that were related to it. This was done by receiving all
  channel snapshot updates for every channel and only reacting when
  it was indicated that the channel was dead.

  This change removes this logic and instead provides an API call
  for directly removing a channel from an endpoint. This is called
  when channels are destroyed. This operation is fast, so blocking
  the calling thread for a short period of time doesn't have any
  noticeable impact.

#### app_queue: Allow stasis message filtering to work.
  Author: Joshua C. Colp
  Date:   2025-10-10

  The app_queue module subscribes on a per-dialed agent basis to both
  the bridge all and channel all topics to keep apprised of things going
  on involving them. This subscription has associated state that must
  be cleaned up when the subscription ends. This was done by setting
  a default router callback that only had logic to handle the case
  where the subscription ends. By using the default router callback
  all filtering for the subscription was disabled, causing unrelated
  messages to get published and handled by it.

  This change makes it so that an explicit route is added for the
  message type used for the message indicating the subscription has
  ended and removes the default router callback. This allows message
  filtering to occur on publishing reducing the messages to app_queue
  to only those it is interested in.

#### taskpool:  Fix some references to threadpool that should be taskpool.
  Author: George Joseph
  Date:   2025-10-10

  Resolves: #1478

#### Update contact information for anthm
  Author: Anthony Minessale
  Date:   2025-10-10


#### chan_websocket.c: Change payload references to command instead.
  Author: George Joseph
  Date:   2025-10-08

  Some of the tests in process_text_message() were still comparing to the
  websocket message payload instead of the "command" string.

  Resolves: #1525

#### func_callerid: Document limitation of DNID fields.
  Author: Naveen Albert
  Date:   2025-10-06

  The Dial() application does not propagate DNID fields, which is counter
  to the behavior of the other Caller ID fields. This behavior is likely
  intentional since the use of Dial theoretically suggests a new dialed
  number, but document this caveat to inform users of it.

  Resolves: #1519

#### func_channel: Allow R/W of ADSI CPE capability setting.
  Author: Naveen Albert
  Date:   2025-10-06

  Allow retrieving and setting the channel's ADSI capability from the
  dialplan.

  Resolves: #1514

  UserNote: CHANNEL(adsicpe) can now be read or written to change
  the channels' ADSI CPE capability setting.

#### core_unreal: Preserve ADSI capability when dialing Local channels.
  Author: Naveen Albert
  Date:   2025-10-06

  Dial() already preserves the ADSI capability by copying it to the new
  channel, but since Local channel pairs consist of two channels, we
  also need to copy the capability to the second channel.

  Resolves: #1517

#### func_hangupcause.c: Add access to Reason headers via HANGUPCAUSE()
  Author: Igor Goncharovsky
  Date:   2025-09-04

  As soon as SIP call may end with several Reason headers, we
  want to make all of them available through the HAGUPCAUSE() function.
  This implementation uses the same ao2 hash for cause codes storage
  and adds a flag to make difference between last processed sip
  message and content of reason headers.

  UserNote: Added a new option to HANGUPCAUSE to access additional
  information about hangup reason. Reason headers from pjsip
  could be read using 'tech_extended' cause type.

#### sig_analog: Allow '#' to end the inter-digit timeout when dialing.
  Author: Naveen Albert
  Date:   2025-10-03

  It is customary to allow # to terminate digit collection immediately
  when there would normally be a timeout. However, currently, users are
  forced to wait for the timeout to expire when dialing numbers that
  are prefixes of other valid matches, and there is no way to end the
  timeout early. Customarily, # terminates the timeout, but at the moment,
  this is just rejected unless there happens to be a matching extension
  ending in #.

  Allow # to terminate the timeout in cases where there is no dialplan
  match. This ensures that the dialplan is always respected, but if a
  valid extension has been dialed that happens to prefix other valid
  matches, # can be used to dial it immediately.

  Resolves: #1510

#### func_math: Add DIGIT_SUM function.
  Author: Naveen Albert
  Date:   2025-10-01

  Add a function (DIGIT_SUM) which returns the digit sum of a number.

  Resolves: #1499

  UserNote: The DIGIT_SUM function can be used to return the digit sum of
  a number.

#### app_sf: Add post-digit timer option to ReceiveSF.
  Author: Naveen Albert
  Date:   2025-10-01

  Add a sorely needed option to set a timeout between digits, rather than
  for receiving the entire number. This is needed if the number of digits
  being sent is unknown by the receiver in advance. Previously, we had
  to wait for the entire timer to expire.

  Resolves: #1493

  UserNote: The 't' option for ReceiveSF now allows for a timer since
  the last digit received, in addition to the number-wide timeout.

#### codec_builtin.c: Adjust some of the quality scores to reflect reality.
  Author: Naveen Albert
  Date:   2025-10-02

  Among the lower-quality voice codecs, some of the quality scores did
  not make sense relative to each other.

  For instance, quality-wise, G.729 > G.723 > PLC10.
  However, current scores do not uphold these relationships.

  Tweak the scores slightly to reflect more accurate relationships.

  Resolves: #1501

#### res_tonedetect: Fix formatting of XML documentation.
  Author: Naveen Albert
  Date:   2025-10-02

  Fix the indentation in the documentation for the variable list.

  Resolves: #1507

#### res_fax: Add XML documentation for channel variables.
  Author: Naveen Albert
  Date:   2025-10-02

  Document the channel variables currently set by SendFAX and ReceiveFAX.

  Resolves: #1505

#### channelstorage_cpp_map_name_id: Add read locking around retrievals.
  Author: George Joseph
  Date:   2025-10-01

  When we retrieve a channel from a C++ map, we actually get back a wrapper
  object that points to the channel then right after we retrieve it, we bump its
  reference count.  There's a tiny chance however that between those two
  statements a delete and/or unref might happen which would cause the wrapper
  object or the channel itself to become invalid resulting in a SEGV.  To avoid
  this we now perform a read lock on the driver around those statements.

  Resolves: #1491

#### app_dial: Allow fractional seconds for dial timeouts.
  Author: Naveen Albert
  Date:   2025-09-30

  Even though Dial() internally uses milliseconds for its dial timeouts,
  this capability has been mostly obscured from users as the argument is
  only parsed as an integer, thus forcing the use of whole seconds for
  timeouts.

  Parse it as a decimal instead so that timeouts can now truly have
  millisecond precision.

  Resolves: #1487

  UserNote: The answer and progress dial timeouts now have millisecond
  precision, instead of having to be whole numbers.

#### dsp.c: Make minor fixes to debug log messages.
  Author: Naveen Albert
  Date:   2025-10-01

  Commit dc8e3eeaaf094a3d16991289934093d5e7127680 improved the debug log
  messages in dsp.c. This makes two minor corrections to it:

  * Properly guard an added log statement in a conditional.
  * Don't add one to the hit count if there was no hit (however, we do
    still want to do this for the case where this is one).

  Resolves: #1496

#### config_options.c: Improve misleading warning.
  Author: Naveen Albert
  Date:   2025-09-30

  When running "config show help <module>", if no XML documentation exists
  for the specified module, "Module <module> not found." is returned,
  which is misleading if the module is loaded but simply has no XML
  documentation for its config. Improve the message to clarify that the
  module may simply have no config documentation.

  Resolves: #1489

#### func_scramble: Add example to XML documentation.
  Author: Naveen Albert
  Date:   2025-09-29

  The previous lack of an example made it ambiguous if the arguments went
  inside the function arguments or were part of the right-hand value.

  Resolves: #1485

#### sig_analog: Eliminate potential timeout with Last Number Redial.
  Author: Naveen Albert
  Date:   2025-09-29

  If Last Number Redial is used to redial, ensure that we do not wait
  for further digits. This was possible if the number that was last
  dialed is a prefix of another possible dialplan match. Since all we
  did is copy the number into the extension buffer, if other matches
  are now possible, there would thus be a timeout before the call went
  through. We now complete redialed calls immediaetly in all cases.

  Resolves: #1483

#### ARI: The bridges play and record APIs now handle sample rates > 8K correctly.
  Author: George Joseph
  Date:   2025-09-25

  The bridge play and record APIs were forcing the Announcer/Recorder channel
  to slin8 which meant that if you played or recorded audio with a sample
  rate > 8K, it was downsampled to 8K limiting the bandwidth.

  * The /bridges/play REST APIs have a new "announcer_format" parameter that
    allows the caller to explicitly set the format on the "Announcer" channel
    through which the audio is played into the bridge.  If not specified, the
    default depends on how many channels are currently in the bridge.  If
    a single channel is in the bridge, then the Announcer channel's format
    will be set to the same as that channel's.  If multiple channels are in the
    bridge, the channels will be scanned to find the one with the highest
    sample rate and the Announcer channel's format will be set to the slin
    format that has an equal to or greater than sample rate.

  * The /bridges/record REST API has a new "recorder_format" parameter that
    allows the caller to explicitly set the format on the "Recorder" channel
    from which audio is retrieved to write to the file.  If not specified,
    the Recorder channel's format will be set to the format that was requested
    to save the audio in.

  Resolves: #1479

  DeveloperNote: The ARI /bridges/play and /bridges/record REST APIs have new
  parameters that allow the caller to specify the format to be used on the
  "Announcer" and "Recorder" channels respecitvely.

#### res_pjsip_geolocation: Add support for Geolocation loc-src parameter
  Author: Max Grobecker
  Date:   2025-09-21

  This adds support for the Geolocation 'loc-src' parameter to res_pjsip_geolocation.
  The already existing config option 'location_source` in res_geolocation is documented to add a 'loc-src' parameter containing a user-defined FQDN to the 'Geolocation:' header,
  but that option had no effect as it was not implemented by res_pjsip_geolocation.

  If the `location_source` configuration option is not set or invalid, that parameter will not be added (this is already checked by res_geolocation).

  This commits adds already documented functionality.

#### sorcery: Move from threadpool to taskpool.
  Author: Joshua C. Colp
  Date:   2025-09-23

  This change moves observer invocation from the use of
  a threadpool to a taskpool. The taskpool options have also
  been adjusted to ensure that at least one taskprocessor
  remains available at all times.

#### stasis_channels.c: Make protocol_id optional to enable blind transfer via ari
  Author: Sven Kube
  Date:   2025-09-22

  When handling SIP transfers via ARI, there is no protocol_id in case of
  a blind transfer.

  Resolves: #1467

#### config.c: fix saving of deep/wide template configurations
  Author: Allan Nathanson
  Date:   2025-09-10

  Follow-on to #244 and #960 regarding how the ast_config_XXX APIs
  handle template inheritance.

  ast_config_text_file_save2() incorrectly suppressed variables if they
  matched any ancestor template.  This broke deep chains (dropping values
  based on distant parents) and wide inheritance (ignoring last-wins order
  across multiple parents).

  The function now inspects the full template hierarchy to find the nearest
  effective parent (last occurrence wins).  Earlier inherited duplicates are
  collapsed, explicit overrides are kept unless they exactly match the parent,
  and PreserveEffectiveContext avoids writing redundant lines.

  Resolves: #1451

#### Fix some doxygen, typos and whitespace
  Author: Bastian Triller
  Date:   2025-09-21


#### stasis_channels.c: Add null check for referred_by in ast_ari_transfer_message_create
  Author: Sven Kube
  Date:   2025-09-18

  When handling SIP transfers via ARI, the `referred_by` field in
  `transfer_ari_state` may be null, since SIP REFER requests are not
  required to include a `Referred-By` header. Without this check, a null
  value caused the transfer to fail and triggered a NOTIFY with a 500
  Internal Server Error.

#### app_queue: Add NULL pointer checks in app_queue
  Author: phoneben
  Date:   2025-09-11

  Add NULL check for word_list before calling word_in_list()
  Add NULL checks for channel snapshots from ast_multi_channel_blob_get_channel()

  Resolves: #1425

#### app_externalivr: Prevent out-of-bounds read during argument processing.
  Author: Sean Bright
  Date:   2025-09-17

  Resolves: #1422

#### chan_dahdi: Add DAHDI_CHANNEL function.
  Author: Naveen Albert
  Date:   2025-09-11

  Add a dialplan function that can be used to get/set properties of
  DAHDI channels (as opposed to Asterisk channels). This exposes
  properties that were not previously available, allowing for certain
  operations to now be performed in the dialplan.

  Resolves: #1455

  UserNote: The DAHDI_CHANNEL function allows for getting/setting
  certain properties about DAHDI channels from the dialplan.

#### taskpool: Update versions for taskpool stasis options.
  Author: Joshua C. Colp
  Date:   2025-09-16


#### taskpool: Add taskpool API, switch Stasis to using it.
  Author: Joshua C. Colp
  Date:   2025-08-06

  This change introduces a new API called taskpool. This is a pool
  of taskprocessors. It provides the following functionality:

  1. Task pushing to a pool of taskprocessors
  2. Synchronous tasks
  3. Serializers for execution ordering of tasks
  4. Growing/shrinking of number of taskprocessors in pool

  This functionality already exists through the combination of
  threadpool+taskprocessors but through investigating I determined
  that this carries substantial overhead for short to medium duration
  tasks. The threadpool uses a single queue of work, and for management
  of threads it involves additional tasks.

  I wrote taskpool to eliminate the extra overhead and management
  as much as possible. Instead of a single queue of work each
  taskprocessor has its own queue and at push time a selector chooses
  the taskprocessor to queue the task to. Each taskprocessor also
  has its own thread like normal. This spreads out the tasks immediately
  and reduces contention on shared resources.

  Using the included efficiency tests the number of tasks that can be
  executed per second in a taskpool is 6-12 times more than an equivalent
  threadpool+taskprocessor setup.

  Stasis has been moved over to using this new API as it is a heavy consumer
  of threadpool+taskprocessors and produces a lot of tasks.

  UpgradeNote: The threadpool_* options in stasis.conf have now been deprecated
  though they continue to be read and used. They have been replaced with taskpool
  options that give greater control over the underlying taskpool used for stasis.

  DeveloperNote: The taskpool API has been added for common usage of a
  pool of taskprocessors. It is suggested to use this API instead of the
  threadpool+taskprocessor approach.

#### app_adsiprog: Fix possible NULL dereference.
  Author: Naveen Albert
  Date:   2025-09-10

  get_token can return NULL, but process_token uses this result without
  checking for NULL; as elsewhere, check for a NULL result to avoid
  possible NULL dereference.

  Resolves: #1419

#### manager.c: Fix presencestate object leak
  Author: Nathan Monfils
  Date:   2025-09-08

  ast_presence_state allocates subtype and message. We straightforwardly
  need to clean those up.

#### audiohook.c: Ensure correct AO2 reference is dereffed.
  Author: Sean Bright
  Date:   2025-09-10

  Part of #1440.

#### res_cliexec: Remove unnecessary casts to char*.
  Author: Naveen Albert
  Date:   2025-09-09

  Resolves: #1436

#### rtp_engine.c: Add exception for comfort noise payload.
  Author: Ben Ford
  Date:   2025-09-09

  In a previous commit, a change was made to
  ast_rtp_codecs_payload_code_tx_sample_rate to check for differing sample
  rates. This ended up returning an invalid payload int for comfort noise.
  A check has been added that returns early if the payload is in fact
  supposed to be comfort noise.

  Fixes: #1340

#### pbx_variables.c: Create real channel for "dialplan eval function".
  Author: Naveen Albert
  Date:   2025-09-09

  "dialplan eval function" has been using a dummy channel for function
  evaluation, much like many of the unit tests. However, sometimes, this
  can cause issues for functions that are not expecting dummy channels.
  As an example, ast_channel_tech(chan) is NULL on such channels, and
  ast_channel_tech(chan)->type consequently results in a NULL dereference.
  Normally, functions do not worry about this since channels executing
  dialplan aren't dummy channels.

  While some functions are better about checking for these sorts of edge
  cases, use a real channel with a dummy technology to make this CLI
  command inherently safe for any dialplan function that could be evaluated
  from the CLI.

  Resolves: #1434

