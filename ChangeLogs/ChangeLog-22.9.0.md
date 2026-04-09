
## Change Log for Release asterisk-22.9.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.9.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.8.2...22.9.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.9.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 50
- Commit Authors: 21
- Issues Resolved: 34
- Security Advisories Resolved: 0

### User Notes:

- #### acl: Add ACL support to http and ari
  A new section, type=restriction has been added to http.conf
  to allow an uri prefix based acl to be configured. See
  http.conf.sample for examples and more information.
  The user section of ari.conf can now contain an acl configuration
  to restrict users access. See ari.conf.sample for examples and more
  information

- #### res_rtp_asterisk.c: Fix DTLS packet drop when TURN loopback re-injection occurs before ICE candidate check
  WebRTC calls using TURN configured in rtp.conf (turnaddr,
  turnusername, turnpassword) will now correctly complete DTLS/SRTP
  negotiation. Previously all DTLS packets were silently dropped due to
  the loopback re-injection address not being in the ICE active candidate
  list.

- #### docs: Add "Provided-by" to doc XML and CLI output.
  The CLI help for applications, functions, manager commands and
  manager events now shows the module that provides its functionality.

- #### CDR/CEL Custom Performance Improvements
  Significant performance improvements have been made to the
  cdr_custom, cdr_sqlite3_custom, cel_custom and cel_sqlite3_custom modules.
  See the new sample config files for those modules to see how to benefit
  from them.

- #### chan_websocket: Add media direction.
  WebSocket now supports media direction, allowing for
  unidirectional media. This is done from the perspective of the
  application and can be set via channel origination, external media, or
  commands sent from the application. Check out
  https://docs.asterisk.org/Configuration/Channel-Drivers/WebSocket/ for
  more.

- #### app_queue: Add 'prio' setting to the 'force_longest_waiting_caller' option
  The 'force_longest_waiting_caller' option now supports a 'prio' setting.
  When set to 'prio', calls are offered by priority first, then by wait time.

- #### Upgrade bundled pjproject to 2.16.
  Bundled pjproject has been upgraded to 2.16. For more
  information on what all is included in this change, check out the
  pjproject Github page: https://github.com/pjsip/pjproject/releases

- #### res_pjsip_header_funcs: Add new PJSIP_INHERITABLE_HEADER dialplan function
  A new PJSIP_HEADER option has been added that allows
  inheriting pjsip headers from the inbound to the outbound bridged
  channel.
  Example- same => n,Set(PJSIP_INHERITABLE_HEADER(add,X-custom-1)=alpha)
  will add X-custom-1: alpha to the outbound pjsip channel INVITE
  upon Dial.

- #### app_queue: Fix rN raise_penalty ignoring min_penalty in calc_metric
  Fixes an issue where QUEUE_RAISE_PENALTY=rN could raise a member’s penalty below QUEUE_MIN_PENALTY during member selection. This could allow members intended to be excluded to be selected. The queue now consistently respects the minimum penalty when raising penalties, aligning member selection behavior with queue empty checks and documented rN semantics.


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- Alexei Gradinari: (1)
- Alexis Hadjisotiriou: (3)
- Arcadiy Ivanov: (1)
- Ben Ford: (2)
- George Joseph: (10)
- Jasper Hafkenscheid: (1)
- Joshua C. Colp: (2)
- Julian C. Dunn: (1)
- Michal Hajek: (1)
- Mike Bradeen: (5)
- Naveen Albert: (7)
- Prashant Srivastav: (1)
- Robert Wilson: (1)
- Sean Bright: (2)
- Sven Kube: (1)
- Talha Asghar: (1)
- Tinet-mucw: (2)
- hishamway: (1)
- nappsoft: (1)
- phoneben: (4)
- serfreeman1337: (2)

## Issue and Commit Detail:

### Closed Issues:

  - 449: [bug]:  PJSIP confuses media address after INVITE requiring authentication
  - 566: [bug]: core: SIGSEGV on DTMF when no timing modules loaded
  - 1356: [bug]: MESSAGE requests should not contain a Contact header
  - 1524: [bug]: PJSIP if sdp_session is blank the initial INVITE doesn't attach an SDP offer, worked in chan_sip
  - 1611: [bug]: asterisk deadlocked on start sometimes
  - 1612: [improvement]: pjsip: Upgrade bundled version to pjproject 2.16
  - 1637: [improvement]: force_longest_waiting_caller should also consider caller priority
  - 1641: [bug]: res_pjsip_config_wizard: Endpoints fail to update when Named ACLs change after reload
  - 1651: [bug]: Asterisk crashes with munmap_chunk() when using sorcery realtime for PJSIP registration objects
  - 1657: [bug]: Wrong dtmf payload is used when inbound invite contains 8K and 16K, and outgoing leg is using G722 and SRTP
  - 1670: [new-feature]: Add new option to PJSIP_HEADER to pass headers from the inbound to outbound channel.
  - 1691: [bug]: force_longest_waiting_caller stops offering calls if a call joins at the first position
  - 1703: [bug]: res_pjsip_pubsub: ao2 reference leak of subscription tree in ast_sip_subscription
  - 1707: [bug]: chan_iax2: Crash when processing video frames with negative length
  - 1716: [bug]: Ghost call when UAC didn't respond with 487 for a cancel request from server even after original call hangup.
  - 1724: [improvement]: say.c - added language support for pashto and dari
  - 1730: [bug]: CPP channel storage get_by_name_prefix does not check prefix match
  - 1755: [bug]: app_dial, utils.h: Compilation failure with -Wold-style-declaration and -Wdiscarded-qualifiers
  - 1781: [bug]: More discarded-qualifiers errors with gcc 15.2.1
  - 1783: [bug]: Several unused-but-set-variable warnings with gcc 16
  - 1785: [bug]: chan_websocket doesn’t work with genericplc and transcoding
  - 1786: [bug]: chan_dahdi: A few more discarded-qualifiers errors not caught previously
  - 1795: [bug]: DTLS packets dropped when TURN configured in rtp.conf due to loopback re-injection occurring before ICE candidate source check
  - 1797: [bug]: Potential logic issue in translated frame write loop (main/file.c)
  - 1802: [improvement]: app_dial: Channel name should be included in warnings during wait_for_answer
  - 1804: [new-feature]: dsp.c: Add support for R2 signaling
  - 1814: [bug]: A pjsip transport with an invalid config can cause issues with other transports
  - 1816: [bug]: ARI: RTPAUDIO channel vars aren't set if call hung up by ARI.
  - 1819: [bug]: When a 302 is received from a UAS, the cause and tech_cause codes set on the channel are incorrect.
  - 1831: [bug]:raise_exception() and EXCEPTION() read use channel datastores without holding ast_channel_lock
  - 1833: [bug]: Address security vulnerabilities in pjproject
  - 1844: [bug]: cdrel_custom isn't respecting the default time format for CEL records
  - 1845: [bug]:res_cdrel_custom produces wrong float timestamps
  - 1852: [bug]: res_cdrel_custom: connection to the sqlite3 database closes from time to time

### Commits By Author:

- #### Alexei Gradinari (1):
  - res_pjsip_pubsub: Fix ao2 reference leak of subscription tree in ast_sip_subscription

- #### Alexis Hadjisotiriou (3):
  - channel: Prevent crash during DTMF emulation when no timing module is loaded
  - res_pjsip_messaging: Remove Contact header from out-of-dialog MESSAGE as per RFC3428
  - pjsip_configuration: Ensure s= and o= lines in SDP are never empty

- #### Arcadiy Ivanov (1):
  - res_pjsip_session: Make sure NAT hook runs when packet is retransmitted for whatever reason.

- #### Ben Ford (2):
  - chan_websocket_doc.xml: Add d(media_direction) option.
  - chan_websocket: Add media direction.

- #### George Joseph (10):
  - res_cdrel_custom: Resolve several formatting issues.
  - xmldoc.c: Fix memory leaks in handling of provided_by.
  - SECURITY.md: Update with additional instructions.
  - chan_pjsip: Set correct cause codes for non-2XX responses.
  - rtp: Set RTPAUDIOQOS variables when ast_softhangup is called.
  - res_pjsip: Remove temp transport state when a transport fails to load.
  - docs: Add "Provided-by" to doc XML and CLI output.
  - CDR/CEL Custom Performance Improvements
  - chan_websocket: Remove silence generation and frame padding.
  - app_amd: Remove errant space in documentation for totalAnalysisTime.

- #### Jasper Hafkenscheid (1):
  - channelstorage_cpp_map_name_id: Fix get_by_name_prefix prefix match

- #### Joshua C. Colp (2):
  - build: Fix GCC discarded-qualifiers const errors.
  - endpoints: Allow access to latest snapshot directly.

- #### Julian C. Dunn (1):
  - astconfigparser.py: Fix regex pattern error by properly escaping string

- #### Michal Hajek (1):
  - res_pjsip_config_wizard: Force reload on Named ACL change events

- #### Mike Bradeen (5):
  - res_pjsip: Address pjproject security vulnerabilities
  - acl: Add ACL support to http and ari
  - res_rtp_asterisk: use correct sample rate lookup to account for g722
  - Upgrade bundled pjproject to 2.16.
  - res_pjsip_header_funcs: Add new PJSIP_INHERITABLE_HEADER dialplan function

- #### Naveen Albert (7):
  - dsp.c: Add support for detecting R2 signaling tones.
  - app_dial: Include channel name in warnings during wait_for_answer.
  - chan_dahdi: Fix discarded-qualifiers errors.
  - build: Fix unused-but-set-variable warnings with gcc 16.
  - build: Fix another GCC discarded-qualifiers const error.
  - chan_iax2: Fix crash due to negative length frame lengths.
  - app_dial, utils.h: Avoid old style declaration and discarded qualifier.

- #### Prashant Srivastav (1):
  - fix: Add macOS (Darwin) compatibility for building Asterisk

- #### Robert Wilson (1):
  - res_rtp_asterisk.c: Fix DTLS packet drop when TURN loopback re-injection occurs before ICE candidate check

- #### Sean Bright (2):
  - resource_channels.c: Fix validation response for externalMedia with AudioSockets
  - res_pjsip_outbound_registration.c: Prevent crash if load_module() fails

- #### Sven Kube (1):
  - res_audiosocket: Fix header read loop to use correct buffer offset

- #### Talha Asghar (1):
  - say.c: added language support for pashto and dari

- #### Tinet-mucw (2):
  - pbx: Hold channel lock for exception datastore access
  - main/file: fix translated-frame write loop to use current frame

- #### hishamway (1):
  - res_pjsip_session.c: Prevent INVITE failover when session is cancelled

- #### nappsoft (1):
  - res_cdrel_custom: do not free config when no new config was loaded

- #### phoneben (4):
  - manager.c : Fix CLI event display
  - app_queue: Queue Timing Parity with Dial() and Accurate Wait Metrics
  - stasis.c: Fix deadlock in stasis_topic_pool_get_topic during module load
  - app_queue: Fix rN raise_penalty ignoring min_penalty in calc_metric

- #### serfreeman1337 (2):
  - app_queue: Add 'prio' setting to the 'force_longest_waiting_caller' option
  - app_queue: Only compare calls at 1st position across queues when forcing longest waiting caller.

### Commit List:

-  res_cdrel_custom: do not free config when no new config was loaded
-  res_cdrel_custom: Resolve several formatting issues.
-  res_pjsip: Address pjproject security vulnerabilities
-  pbx: Hold channel lock for exception datastore access
-  xmldoc.c: Fix memory leaks in handling of provided_by.
-  SECURITY.md: Update with additional instructions.
-  res_audiosocket: Fix header read loop to use correct buffer offset
-  manager.c : Fix CLI event display
-  chan_pjsip: Set correct cause codes for non-2XX responses.
-  res_pjsip_config_wizard: Force reload on Named ACL change events
-  rtp: Set RTPAUDIOQOS variables when ast_softhangup is called.
-  channel: Prevent crash during DTMF emulation when no timing module is loaded
-  res_pjsip: Remove temp transport state when a transport fails to load.
-  res_pjsip_messaging: Remove Contact header from out-of-dialog MESSAGE as per RFC3428
-  acl: Add ACL support to http and ari
-  res_rtp_asterisk.c: Fix DTLS packet drop when TURN loopback re-injection occurs before ICE candidate check
-  dsp.c: Add support for detecting R2 signaling tones.
-  app_dial: Include channel name in warnings during wait_for_answer.
-  main/file: fix translated-frame write loop to use current frame
-  docs: Add "Provided-by" to doc XML and CLI output.
-  chan_websocket_doc.xml: Add d(media_direction) option.
-  resource_channels.c: Fix validation response for externalMedia with AudioSockets
-  CDR/CEL Custom Performance Improvements
-  chan_websocket: Remove silence generation and frame padding.
-  chan_websocket: Add media direction.
-  fix: Add macOS (Darwin) compatibility for building Asterisk
-  astconfigparser.py: Fix regex pattern error by properly escaping string
-  res_rtp_asterisk: use correct sample rate lookup to account for g722
-  res_pjsip_outbound_registration.c: Prevent crash if load_module() fails
-  pjsip_configuration: Ensure s= and o= lines in SDP are never empty
-  res_pjsip_session: Make sure NAT hook runs when packet is retransmitted for whatever reason.
-  chan_dahdi: Fix discarded-qualifiers errors.
-  build: Fix unused-but-set-variable warnings with gcc 16.
-  build: Fix another GCC discarded-qualifiers const error.
-  chan_iax2: Fix crash due to negative length frame lengths.
-  build: Fix GCC discarded-qualifiers const errors.
-  endpoints: Allow access to latest snapshot directly.
-  app_dial, utils.h: Avoid old style declaration and discarded qualifier.
-  app_queue: Add 'prio' setting to the 'force_longest_waiting_caller' option
-  Upgrade bundled pjproject to 2.16.
-  res_pjsip_header_funcs: Add new PJSIP_INHERITABLE_HEADER dialplan function
-  app_queue: Queue Timing Parity with Dial() and Accurate Wait Metrics
-  stasis.c: Fix deadlock in stasis_topic_pool_get_topic during module load
-  app_queue: Fix rN raise_penalty ignoring min_penalty in calc_metric
-  app_queue: Only compare calls at 1st position across queues when forcing longest waiting caller.
-  channelstorage_cpp_map_name_id: Fix get_by_name_prefix prefix match
-  app_amd: Remove errant space in documentation for totalAnalysisTime.
-  say.c: added language support for pashto and dari
-  res_pjsip_session.c: Prevent INVITE failover when session is cancelled
-  res_pjsip_pubsub: Fix ao2 reference leak of subscription tree in ast_sip_subscription

### Commit Details:

#### res_cdrel_custom: do not free config when no new config was loaded
  Author: nappsoft
  Date:   2026-04-02

  When the res_cdrel_custom modules is reloaded and the config has not been changed asterisk should not free the old config. Otherwise the connection to the database will be closed and no new connection will be opened.

  Resolves: #1852

#### res_cdrel_custom: Resolve several formatting issues.
  Author: George Joseph
  Date:   2026-03-31

  Several issues are resolved:

  * Internally, floats were used for timestamp values but this could result
  in wrapping so they've been changed to doubles.

  * Historically, the default CEL eventtime format is `<seconds>.<microseconds>`
  with `<microseconds>` always being 6 digits.  This should have continued to be
  the case but res_cdrel_custom wasn't checking the `dateformat` setting in
  cel.conf and was defaulting to `%F %T`.  res_cdrel_custom now gets the default
  date format from cel.conf, which will be whatever the `dateformat` parameter
  is set to or `<seconds>.<microseconds>` if not set.

  * The timeval field formatter for both CDR and CEL wasn't handling custom
  strftime format strings correctly.  This is now fixed so you should be able
  to specifiy custom strftime format strings for the CEL `eventtime` and CDR
  `start`, `answer` and `end` fields.  For example: `eventtime(%FT%T%z)`.

  Resolves: #1844
  Resolves: #1845

#### res_pjsip: Address pjproject security vulnerabilities
  Author: Mike Bradeen
  Date:   2026-03-23

  Address the following pjproject security vulnerabilities

  [GHSA-j29p-pvh2-pvqp - Buffer overflow in ICE with long username](https://github.com/pjsip/pjproject/security/advisories/GHSA-j29p-pvh2-pvqp)
  [GHSA-8fj4-fv9f-hjpc - Heap use-after-free in PJSIP presense subscription termination header](https://github.com/pjsip/pjproject/security/advisories/GHSA-8fj4-fv9f-hjpc)
  [GHSA-g88q-c2hm-q7p7 - ICE session use-after-free race conditions](https://github.com/pjsip/pjproject/security/advisories/GHSA-g88q-c2hm-q7p7)
  [GHSA-x5pq-qrp4-fmrj - Out-of-bounds read in SIP multipart parsing](https://github.com/pjsip/pjproject/security/advisories/GHSA-x5pq-qrp4-fmrj)

  Resolves: #1833

#### pbx: Hold channel lock for exception datastore access
  Author: Tinet-mucw
  Date:   2026-03-20

  ast_channel_datastore_find() and ast_channel_datastore_add() must only be
  called while the channel is locked (see channel.h). raise_exception() and the
  EXCEPTION dialplan function read path accessed the exception datastore without
  holding ast_channel_lock, which could corrupt the per-channel datastore list
  under concurrency and lead to crashes during teardown (e.g. double free in
  ast_datastore_free).

  Resolves: #1831

#### xmldoc.c: Fix memory leaks in handling of provided_by.
  Author: George Joseph
  Date:   2026-03-17

  Added a few calls to ast_xml_free_attr() to squash memory leaks when handling
  "provided_by".

#### SECURITY.md: Update with additional instructions.
  Author: George Joseph
  Date:   2026-03-19

  Also added line breaks for people reading this file directly
  from the code base.

#### res_audiosocket: Fix header read loop to use correct buffer offset
  Author: Sven Kube
  Date:   2026-03-17

  The PR #1522 introduced the header read loop for audiosocket packets
  which does not handle partial header reads correctly. This commit
  adds the missing buffer offsets.

#### manager.c : Fix CLI event display
  Author: phoneben
  Date:   2026-03-16

  manager.c: Fix CLI event display

  - `manager show events`: fix event names being truncated at 20 characters, widen column to 28 to accommodate the longest registered event name
  - `manager show events`: skip duplicate entries caused by multiple modules registering the same event name, list is already sorted so adjacent name comparison is sufficient

#### chan_pjsip: Set correct cause codes for non-2XX responses.
  Author: George Joseph
  Date:   2026-03-10

  Redirects initiated by 302 response codes weren't handled correctly
  when setting the hangup cause code and tech cause code on the responding
  channel.  They're now set to 23 (REDIRECTED_TO_NEW_DESTINATION) and
  302 (Moved permanently).  Other non-2XX response codes also had issues.

  A new API ast_channel_dialed_causes_iterator() was added to retrieve
  the hangup cause codes for a channel.

  chan_pjsip_session_end() in chan_pjsip has been refactored to set the
  correct cause codes on a channel based on the cause codes added by
  chan_pjsip_incoming_response_update_cause().  Copious amounts of
  debugging and comments were also added.

  Resolves: #1819

#### res_pjsip_config_wizard: Force reload on Named ACL change events
  Author: Michal Hajek
  Date:   2025-12-10

  Currently, endpoints created via the PJSIP Config Wizard do not update
  their ACL rules if the underlying Named ACL (in acl.conf) changes.
  This occurs because the wizard relies on file timestamp and content
  caching of pjsip_wizard.conf, which remains unchanged during an external
  ACL update. As a result, endpoints retain stale ACL rules even after
  a reload.

  This patch updates res_pjsip_config_wizard to subscribe to the
  ast_named_acl_change_type Stasis event. A local generation counter is
  incremented whenever an ACL change event is received.

  During a reload, the wizard compares the current local generation against
  the generation stored in the wizard object. If a change is detected:
  1. The file cache optimization (CONFIG_FLAG_FILEUNCHANGED) is bypassed.
  2. Wizard objects utilizing 'acl' or 'contact_acl' are forced to update,
     ensuring they pick up the new IP rules.

  Signed-off-by: Michal Hajek michal.hajek@daktela.com

  Fixes: #1641

#### rtp: Set RTPAUDIOQOS variables when ast_softhangup is called.
  Author: George Joseph
  Date:   2026-03-06

  If a channel in Stasis/ARI is hung up by the channel driver, the RTPAUDIOQOS
  variables are set before the channel leaves Stasis and are therefore
  available to the ARI app via ChannelVarset events.  If the channel is hung up
  by ARI however, the channel leaves Stasis before the RTPAUDIOQOS variables
  are set so the app may not get the ChannelVarset events.

  We now set the RTPAUDIOQOS variables when ast_softhangup() is called as well
  as when the channel driver hangs up a channel.  Since ARI hangups call
  ast_softhangup(), the variables will be set before the channel leaves Stasis
  and the app should get the ChannelVarset events.
  ast_rtp_instance_set_stats_vars(), which actually sets the variables, now
  checks to see if the variables are already set before attempting to set them.
  This prevents double messages from being generated.

  Resolves: #1816

#### channel: Prevent crash during DTMF emulation when no timing module is loaded
  Author: Alexis Hadjisotiriou
  Date:   2026-02-26

  Description:
  When Asterisk is running without a timing module, attempting to process DTMF
  triggers a segmentation fault. This occurs because the system
  attempts to access a null timing file descriptor when setting up the
  DTMF emulation timer.

  This fix ensures that the system checks for a valid timing source before
  attempting to start the DTMF emulation timer. If no timing module is
  present, it logs a warning and skips the emulation instead of crashing
  the process.

  Changes:
  - Modified main/channel.c to add a safety check within the __ast_read function.
  - Implemented a graceful return path when no timing source is available
  - Added a LOG_WARNING to inform the administrator that DTMF emulation
    was skipped due to missing timing modules.

  Testing:
  - Disabled all timing_ modules in modules.conf and confirmed with
    'timing test'.
  - Reproduced the crash by modifying the dialplan with:
   exten => 707,1,NoOp(Starting DTMF - No Timing Mode)
   same => n,Answer()
   same => n,Background(demo-congrats)
   same => n,WaitExten(10)
   same => n,Hangup()
    And calling 707 followed by 1
  - Verified that with the fix applied, the system logs "No timing module
    loaded; skipping DTMF timer" and continues dialplan
    execution without crashing.
  - Confirmed stability during concurrent media sessions and DTMF input.

  Fixes: #566

#### res_pjsip: Remove temp transport state when a transport fails to load.
  Author: George Joseph
  Date:   2026-03-06

  If a pjsip transport (A) fails to load, its temporary state gets left behind
  causing the next transport to load (B) to pick up some of its parameters,
  including its name. This can cause B to have the correct name (B) in its
  transport object but the wrong name (A) in its internal state object. When a
  transport state is searched for later on, transport state B is returned but a
  retrieval of the actual transport object will fail because B's transport
  state id is actually "A" and transport "A" doesn't exist because it failed
  to load.

  remove_temporary_state() is now being called in all error paths in
  config_transport.c functions that call find_or_create_temporary_state().

  A bit of extra debugging was also added to res_pjsip_nat.c.

  Resolves: #1814

#### res_pjsip_messaging: Remove Contact header from out-of-dialog MESSAGE as per RFC3428
  Author: Alexis Hadjisotiriou
  Date:   2026-01-19

  According to RFC 3428 (Section 5), a Contact header is not required in a
  MESSAGE request unless the sender wants to establish a session. This
  patch ensures that the Contact header is removed from out-of-dialog
  MESSAGE requests within res_pjsip_messaging.c.

  Fixes: #1356

#### acl: Add ACL support to http and ari
  Author: Mike Bradeen
  Date:   2026-02-27

  Add uri prefix based acl support to the built in http server.
  This allows an acl to be added per uri prefix (ie '/metrics'
  or '/ws') to restrict access.

  Add user based acl support for ARI. This adds new acl options
  to the user section of ari.conf to restrict access on a per
  user basis.

  resolves: #1799

  UserNote: A new section, type=restriction has been added to http.conf
  to allow an uri prefix based acl to be configured. See
  http.conf.sample for examples and more information.
  The user section of ari.conf can now contain an acl configuration
  to restrict users access. See ari.conf.sample for examples and more
  information

#### res_rtp_asterisk.c: Fix DTLS packet drop when TURN loopback re-injection occurs before ICE candidate check
  Author: Robert Wilson
  Date:   2026-03-03

  When TURN is configured in rtp.conf, pjproject re-injects TURN packets
  via 127.0.0.1 (the loopback address). The DTLS packet handler checks the
  source address against the ICE active candidate list before the loopback
  address substitution runs, causing the packet to be silently dropped as
  the source 127.0.0.1 is not in the candidate list.

  Fix by performing the loopback address substitution before the ICE
  candidate source check in the DTLS path, mirroring the logic already
  present in the non-DTLS RTP path.

  Fixes: #1795

  UserNote: WebRTC calls using TURN configured in rtp.conf (turnaddr,
  turnusername, turnpassword) will now correctly complete DTLS/SRTP
  negotiation. Previously all DTLS packets were silently dropped due to
  the loopback re-injection address not being in the ICE active candidate
  list.

#### dsp.c: Add support for detecting R2 signaling tones.
  Author: Naveen Albert
  Date:   2026-03-01

  Extend the existing DTMF/MF tone detection support by adding support
  for R2 tones, another variant of MF (R1) signaling. Both forward
  and backward signaling are supported.

  Resolves: #1804

#### app_dial: Include channel name in warnings during wait_for_answer.
  Author: Naveen Albert
  Date:   2026-02-28

  Include the channel name in warnings during wait_for_answer to make
  them more useful and allow problematic channels to be easily identified.

  Resolves: #1802

#### main/file: fix translated-frame write loop to use current frame
  Author: Tinet-mucw
  Date:   2026-02-27

  write each translated frame from translator output.

  Resolves: #1797

#### docs: Add "Provided-by" to doc XML and CLI output.
  Author: George Joseph
  Date:   2026-02-24

  For application, function, manager, managerEvent, managerEventInstance
  and info XML documentation nodes, the make_xml_documentation script will
  add a "module" attribute if not already present.  For XML in separate
  "*_doc.xml" files, the script figures out the correct module name.  For
  documentation in the "main" directory, the module name is set to "builtin".

  The CLI handlers for "core show application", "core show function",
  "manager show command" and "manager show event", have been updated to
  show the following after the Synopsis...

  ```
  [Provided By]
  <modulename>
  ```

  For modules that provide additional "info" elements (like the technologies
  do for Dial), the providing module has also been added.

  ```
  Technology: WebSocket  Provided by: chan_websocket
  WebSocket Dial Strings:
  ...
  ```

  UserNote: The CLI help for applications, functions, manager commands and
  manager events now shows the module that provides its functionality.

#### chan_websocket_doc.xml: Add d(media_direction) option.
  Author: Ben Ford
  Date:   2026-02-27

  Adds documentation for the 'd' option to set media direction for
  chan_websocket.

#### resource_channels.c: Fix validation response for externalMedia with AudioSockets
  Author: Sean Bright
  Date:   2026-03-01

  The AudioSocket encapsulation for externalMedia requires a UUID to be
  provided in the `data` parameter of the ARI call. If not provided, we
  should return a 400 Bad Request instead of a 500 Internal Server
  Error.

  Pointed out by AVT in the community forum[1].

  1: https://community.asterisk.org/t/externalmedia-audiosocket-on-asterisk-22/112149

#### CDR/CEL Custom Performance Improvements
  Author: George Joseph
  Date:   2026-01-27

  There is a LOT of work in this commit but the TL;DR is that it takes
  CEL processing from using 38% of the CPU instructions used by a call,
  which is more than that used by the call processing itself, down to less
  than 10% of the instructions.

  So here's the deal...  cdr_custom, cdr_sqlite3_custom, cel_custom
  and cel_sqlite3_custom all shared one ugly trait...they all used
  ast_str_substitute_variables() or pbx_substitute_variables_helper()
  to resolve the dialplan functions used in their config files.  Not only
  are they both extraordinarily expensive, they both require a dummy
  channel to be allocated and destroyed for each record written.  For CDRs,
  that's not too bad because we only write one CDR per call.  For CELs however,
  it's a disaster.

  As far as source code goes, the modules basically all did the same thing.
  Unfortunately, they did it badly.  The config files simply contained long
  opaque strings which were intepreted by ast_str_substitute_variables() or
  pbx_substitute_variables_helper(), the very functions that ate all the
  instructions.  This meant introducing a new "advanced" config format much
  like the advanced manager event filtering added to manager.conf in 2024.
  Fortunately however, if the legacy config was recognizable, we were able to
  parse it as an advanced config and gain the benefit.  If not, then it
  goes the legacy, and very expensive, route.

  Given the commonality among the modules, instead of making the same
  improvements to 4 modules then trying to maintain them over time, a single
  module "res_cdrel_custom" was created that contains all of the common code.
  A few bonuses became possible in the process...
  * The cdr_custom and cel_custom modules now support JSON formatted output.
  * The cdr_sqlite_custom and cel_sqlite3_custom modules no longer have
    to share an Sqlite3 database.

  Summary of changes:

  A new module "res/res_cdrel_custom.c" has been created and the existing
  cdr_custom, cdr_sqlite3_custom, cel_custom and cel_sqlite3_custom modules
  are now just stubs that call the code in res_cdrel_custom.

  res_cdrel_custom contains:
  * A common configuration facility.
  * Getters for both CDR and CEL fields that share the same abstraction.
  * Formatters for all data types found in the ast_cdr and ast_event
    structures that share the same abstraction.
  * Common writers for the text file and database backends that, you guessed it,
    share the same abstraction.

  The result is that while there is certainly a net increase in the number of
  lines in the code base, most of it is in the configuration handling at
  load-time.  The run-time instruction path length is now significanty shorter.

  ```
  Scenario                   Instructions     Latency
  =====================================================
  CEL pre changes                  38.49%     37.51%
  CEL Advanced                      9.68%      6.06%
  CEL Legacy (auto-conv to adv)     9.95%      6.13%

  CEL Sqlite3 pre changes          39.41%     39.90%
  CEL Sqlite3 Advanced             25.68%     24.24%
  CEL Sqlite3 Legacy (auto conv)   25.88%     24.53%

  CDR pre changes                   4.79%      2.95%
  CDR Advanced                      0.79%      0.47%
  CDR Legacy (auto conv to adv)     0.86%      0.51%

  CDR Sqlite3 pre changes           4.47%      2.89%
  CEL Sqlite3 Advanced              2.16%      1.29%
  CEL Sqlite3 Legacy (auto conv)    2.19%      1.30%
  ```

  Notes:
  * We only write one CDR per call but every little bit helps.
  * Sqlite3 still takes a fair amount of resources but the new config
    makes a decent improvement.
  * Legacy configs that we can't auto convert will still take the
    "pre changes" path.

  If you're interested in more implementation details, see the comments
  at the top of the res_cdrel_custom.c file.

  One minor fix to CEL is also included...Although TenantID was added to the
  ast_event structure, it was always rendered as an empty string.  It's now
  properly rendered.

  UserNote: Significant performance improvements have been made to the
  cdr_custom, cdr_sqlite3_custom, cel_custom and cel_sqlite3_custom modules.
  See the new sample config files for those modules to see how to benefit
  from them.

#### chan_websocket: Remove silence generation and frame padding.
  Author: George Joseph
  Date:   2026-02-19

  The original chan_websocket implementation attempted to improve the
  call quality experience by generating silence frames to send to the core
  when no media was being read from the websocket and padding frames with
  silence when short frames were read from the websocket.  Both of these
  required switching the formats on the channel to slin for short periods
  of time then switching them back to whatever format the websocket channel
  was configured for.  Unfortunately, the format switching caused issues
  when transcoding was required and the transcode_via_sln option was enabled
  in asterisk.conf (which it is by default).  The switch would cause the
  transcoding path to be incorrectly set resulting in malformed RTP packets
  being sent back out to the caller on the other end which the caller heard
  as loud noise.

  After looking through the code and performing multiple listening tests,
  the best solution to this problem was to just remove the code that was
  attempting to generate the silence.  There was no decrease in call quality
  whatsoever and the code is a bit simpler.  None of the other channel drivers
  nor res_rtp_asterisk generate silence frames or pad short frames which
  backed up decision.

  Resolves: #1785

#### chan_websocket: Add media direction.
  Author: Ben Ford
  Date:   2026-02-10

  Currently, WebSockets both accept and send media without the option to
  disable one or the other. This commit adds the ability to set the media
  direction for a WebSocket, making it unidirectional or bidirectional
  (the default). Direction is done from the point of view of the
  application, NOT Asterisk. The allowed values are 'both', 'in', and
  'out'. If media direction is 'both' (the default), Asterisk accepts and
  sends media to the application. If it is 'in', Asterisk will drop any
  media received from the application. If it is 'out', Asterisk will not
  write any media frames to the application.

  UserNote: WebSocket now supports media direction, allowing for
  unidirectional media. This is done from the perspective of the
  application and can be set via channel origination, external media, or
  commands sent from the application. Check out
  https://docs.asterisk.org/Configuration/Channel-Drivers/WebSocket/ for
  more.

#### fix: Add macOS (Darwin) compatibility for building Asterisk
  Author: Prashant Srivastav
  Date:   2026-02-05

  - Makefile: Skip /usr/lib/bundle1.o on modern macOS (doesn't exist)
  - Makefile.rules: Skip -fno-partial-inlining for clang (gcc-only flag)
  - include/asterisk/utils.h: Use asterisk/endian.h instead of <endian.h>
  - main/Makefile: Add Darwin-specific pjproject linking with -force_load
  - main/strcompat.c: Include poll-compat.h, use ast_poll()
  - main/xml.c: Add ASTMM_LIBC ASTMM_IGNORE for libxml2 compatibility
  - res/res_pjsip/config_transport.c: Define TCP keepalive constants for macOS

  Tested on macOS Darwin 25.2.0 (Apple Silicon ARM64)

#### astconfigparser.py: Fix regex pattern error by properly escaping string
  Author: Julian C. Dunn
  Date:   2026-02-17

  "SyntaxWarning: invalid escape sequence '\s'" occurs when using the pjsip
  migration script because '\' is an escape character in Python. Instead,
  use a raw string for the regex.

#### res_rtp_asterisk: use correct sample rate lookup to account for g722
  Author: Mike Bradeen
  Date:   2026-02-23

  Swap out ast_rtp_get_rate for ast_format_get_sample_rate when looking
  at the paired audio codec rate to account for g722 oddness.

  Resolves: #1657

#### res_pjsip_outbound_registration.c: Prevent crash if load_module() fails
  Author: Sean Bright
  Date:   2026-02-12

  `ast_cli_unregister_multiple()` expects internal data members to be heap
  allocated which happens during a successful call to
  `ast_cli_register_multiple()`. CLI handlers defined traditionally - those whose
  handler responds to the CLI_INIT message - take care of this automatically. But
  when we statically provide a `command` or `usage` member, we _must_ initialize
  them with `ast_cli_register_multiple()` before attempting to destroy them.

  Resolves: #1651

#### pjsip_configuration: Ensure s= and o= lines in SDP are never empty
  Author: Alexis Hadjisotiriou
  Date:   2026-01-30

  According to RFC 8866 (Section 5.2), the Session Name (s=) field and
  the username part of origin (o=) are both mandatory and cannot be
  empty. If a session has no name, or no username part of origin, the
  RFC recommends using a single dash (-) as a placeholder.

  This fix ensures that if the session name or the username part of
  origin length is zero , it defaults to -.

  Fixes: #1524

#### res_pjsip_session: Make sure NAT hook runs when packet is retransmitted for whatever reason.
  Author: Arcadiy Ivanov
  Date:   2026-01-14

  This hook may not be necessary when we do a retransmit, but when there are two
  INVITEs, one *initial* and one with auth digest, the second INVITE contains wrong (unmodified) media address
  due to the commented line below.
  The NAT hook needs to run due to filters potentially reverting previously modified packets.

  Fixes: #449

#### chan_dahdi: Fix discarded-qualifiers errors.
  Author: Naveen Albert
  Date:   2026-02-18

  Fix discarded-qualifiers errors to compile successfully with gcc 15.2.1.

  Associated changes have also been made to libss7; however, for compatibility
  we cast const char* values to char*. In the future, these casts could be
  removed.

  Resolves: #1786

#### build: Fix unused-but-set-variable warnings with gcc 16.
  Author: Naveen Albert
  Date:   2026-02-18

  Fix or remove a few variables that were being set but not actually
  used anywhere, causing warnings with gcc 16.

  Resolves: #1783

#### build: Fix another GCC discarded-qualifiers const error.
  Author: Naveen Albert
  Date:   2026-02-18

  Follow on commit to 27a39cba7e6832cb30cb64edaf879f447b669628
  to fix compilation with BETTER_BACKTRACES with gcc 15.2.1.

  Resolves: #1781

#### chan_iax2: Fix crash due to negative length frame lengths.
  Author: Naveen Albert
  Date:   2026-01-08

  chan_iax2 has several code paths where a frame's data length
  is calculated by subtraction. On some paths, there is a check
  for negative length. One of these paths is missing this check,
  and on this path, it is possible for the result to be negative,
  leading to a crash as a result of memory operations using the
  bogus length.

  Add a check to capture this off-nominal case. This will log
  the appropriate warnings as in other cases and prevent a crash.
  Also update the log messages to be clearer.

  Resolves: #1707

#### build: Fix GCC discarded-qualifiers const errors.
  Author: Joshua C. Colp
  Date:   2026-02-12

  GCC 15.2.1 pays attention to the discarding of the const
  qualifier when strchr, strrchr, memchr, or memrchr are now
  used. This change fixes numerous errors with this throughout
  the tree. The fixes can be broken down into the following:

  1. The return value should be considered const.
  2. The value passed to strchr or strrchr can be cast as it is
     expected and allowed to be modified.
  3. The pointer passed to strchr or strrchr is not meant to be
     modified and so the contents must be duplicated.
  4. It was declared const and never should have been.

#### endpoints: Allow access to latest snapshot directly.
  Author: Joshua C. Colp
  Date:   2026-02-10

  This change adds an API call to allow direct access to the latest
  snapshot of an ast_endpoint. This is then used by chan_pjsip when
  calculating device state, eliminating the need to access the cache
  which would incur a container find and access.

#### app_dial, utils.h: Avoid old style declaration and discarded qualifier.
  Author: Naveen Albert
  Date:   2026-02-04

  * app_dial: Use const char* for fixed strings.
  * utils.h: inline should come before return type.

  Resolves: #1755

#### app_queue: Add 'prio' setting to the 'force_longest_waiting_caller' option
  Author: serfreeman1337
  Date:   2026-01-29

  This adds a 'prio' setting to ensure that call priority is respected across multiple queues.
  Using 'yes' could cause high-priority callers to be skipped if a caller
  in another queue had a longer wait time, regardless of priority.

  Resolves: #1637

  UserNote: The 'force_longest_waiting_caller' option now supports a 'prio' setting.
  When set to 'prio', calls are offered by priority first, then by wait time.

#### Upgrade bundled pjproject to 2.16.
  Author: Mike Bradeen
  Date:   2026-01-27

  Resolves: #1612

  UserNote: Bundled pjproject has been upgraded to 2.16. For more
  information on what all is included in this change, check out the
  pjproject Github page: https://github.com/pjsip/pjproject/releases

#### res_pjsip_header_funcs: Add new PJSIP_INHERITABLE_HEADER dialplan function
  Author: Mike Bradeen
  Date:   2025-11-19

  Adds a new PJSIP_INHERITABLE_HEADER dialplan function to add
  inheritable headers from the inbound channel to an outbound
  bridged channel.  This works similarly to the existing
  PJSIP_HEADER function, but will set the header on the bridged
  outbound channel's INVITE upon Dial.

  Inheritable headers can be updated or removed from the inbound
  channel as well as from a pre-dial handler

  Resolves: #1670

  UserNote: A new PJSIP_HEADER option has been added that allows
  inheriting pjsip headers from the inbound to the outbound bridged
  channel.
  Example- same => n,Set(PJSIP_INHERITABLE_HEADER(add,X-custom-1)=alpha)
  will add X-custom-1: alpha to the outbound pjsip channel INVITE
  upon Dial.

#### app_queue: Queue Timing Parity with Dial() and Accurate Wait Metrics
  Author: phoneben
  Date:   2026-01-18

  app_queue: Set Dial-compatible timing variables

  Extends Queue() to set Dial-compatible timing variables (ANSWEREDTIME, DIALEDTIME) and introduces a precise QUEUEWAIT metric calculated at agent connect time, with proper initialization to prevent stale or misleading values.

#### stasis.c: Fix deadlock in stasis_topic_pool_get_topic during module load
  Author: phoneben
  Date:   2025-11-29

  stasis.c: Fix deadlock in stasis_topic_pool_get_topic during module load.

  Deadlock occurs when res_manager_devicestate loads concurrently with
  device state operations due to lock ordering violation:

  Thread 1: Holds pool lock → needs topic lock (in stasis_forward_all)
  Thread 2: Holds topic lock → needs pool lock (in stasis_topic_pool_get_topic)

  Fix: Release pool lock before calling stasis_topic_create() and
  stasis_forward_all(). Re-acquire only for insertion with race check.

  Preserves borrowed reference semantics while breaking the deadlock cycle.

  Fixes: #1611

#### app_queue: Fix rN raise_penalty ignoring min_penalty in calc_metric
  Author: phoneben
  Date:   2026-01-06

  QUEUE_RAISE_PENALTY=rN was not respected during member selection. calc_metric() raised penalties below QUEUE_MIN_PENALTY, allowing excluded members to be selected.

  This change makes calc_metric() honor raise_respect_min, keeping behavior consistent with queue empty checks and expected rN semantics

  UserNote: Fixes an issue where QUEUE_RAISE_PENALTY=rN could raise a member’s penalty below QUEUE_MIN_PENALTY during member selection. This could allow members intended to be excluded to be selected. The queue now consistently respects the minimum penalty when raising penalties, aligning member selection behavior with queue empty checks and documented rN semantics.

#### app_queue: Only compare calls at 1st position across queues when forcing longest waiting caller.
  Author: serfreeman1337
  Date:   2026-01-05

  This prevents a situation where a call joining at 1st position to a queue with calls
  leads to a state where no callers are considered the longest waiting,
  causing queues to stop offering calls.

  Resolves: #1691

#### channelstorage_cpp_map_name_id: Fix get_by_name_prefix prefix match
  Author: Jasper Hafkenscheid
  Date:   2026-01-21

  Lower bound filter did not ensure prefix match.

  Resolves: #1730

#### app_amd: Remove errant space in documentation for totalAnalysisTime.
  Author: George Joseph
  Date:   2026-01-22


#### say.c: added language support for pashto and dari
  Author: Talha Asghar
  Date:   2026-01-22

  With this new feature, users who speak these languages can now benefit from the
  text-to-speech functionality provided by asterisk. This will make the platform
  more accessible and useful to a wider range of users, particularly those in
  regions where Pashto and Dari are spoken. This contribution will help to improve
  the overall usability and inclusivity of the asterisk platform.

  Fixes: #1724

#### res_pjsip_session.c: Prevent INVITE failover when session is cancelled
  Author: hishamway
  Date:   2026-01-15

  When an outbound INVITE transaction times out (408) or receives a 503 error,
  check_request_status() attempts to failover to the next available address by
  restarting the INVITE session. However, the function did not check if the
  inv_session was already cancelled before attempting the failover.

  This caused unexpected behavior when a caller hung up during a ring group
  scenario: after CANCEL was sent but the remote endpoint failed to respond
  with 487 (e.g., due to network disconnection), the transaction timeout
  would trigger a NEW outbound INVITE to the next address, even though the
  session was already terminated.

  This violates RFC 3261 Section 9.1 which states that if no final response
  is received after CANCEL within 64*T1 seconds, the client should consider
  the transaction cancelled and destroy it, not retry to another address.

  The fix adds a check for both PJSIP_INV_STATE_DISCONNECTED and inv->cancelling
  at the beginning of check_request_status(). This ensures that:
  - Failover is blocked when the user explicitly cancelled the call (CANCEL sent)
  - Failover is still allowed for legitimate timeout/503 scenarios where no
    CANCEL was initiated (e.g., SRV failover when first server is unreachable)

  Resolves: #1716

#### res_pjsip_pubsub: Fix ao2 reference leak of subscription tree in ast_sip_subscription
  Author: Alexei Gradinari
  Date:   2026-01-07

  allocate_subscription() increments the ao2 reference count of the subscription tree,
  but the reference was not consistently released during subscription destruction,
  resulting in leaked sip_subscription_tree objects.

  This patch makes destroy_subscription() responsible for releasing sub->tree,
  removes ad-hoc cleanup in error paths,
  and guards tree cleanup to ensure refcount symmetry and correct ownership.

  Fixes: #1703

