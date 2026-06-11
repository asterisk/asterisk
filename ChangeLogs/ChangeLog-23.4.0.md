
## Change Log for Release asterisk-23.4.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-23.4.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/23.3.0...23.4.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-23.4.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 53
- Commit Authors: 24
- Issues Resolved: 43
- Security Advisories Resolved: 0

### User Notes:

- #### res ari: Add attachable states to Channels and Bridges
  Bridge variables now can be set and retrieved via the following paths:
  `/bridges/{bridgeId}/variable`
  `/bridges/{bridgeId}/variables`
  Both Bridge and Channel variables can now be set with an optional 'report_events'
  boolean flag that will cause those variables to be included on all events on that
  object. The 'report_events' flag will default to False if not set to maintain
  backwards capability.
  To allow this, variables can now be either name value pairs (the current format):
  `<variable_name>: '<value_string>'`
   - or -
  `<variable_name>: {value: '<value_string>', report_events: [true|false]}`

- #### ARI: Added paths to get and set multiple channel variables.
  Added new ARI paths for getting and setting multiple channel
  variables at a time. For GET, this takes in a single string of
  comma-separated variable names, while POST takes in a dictionary of key
  value pairs. The behavior is the same as passing in variables when
  originating a channel.

- #### res_rtp_asterisk: Add option to control stun host resolution when TTL = 0
  A new `stunaddr_reresolve_ttl_0` parameter has been added to rtp.conf
  that allows control over what happens when a STUN server hostname lookup
  returns a TTL of 0.  The values can be set as follows:
  - 'no': This is the historical (and current default) behavior of not doing
  any further lookups and continuing to use the last successful result until
  Asterisk is restarted or rtp.conf is reloaded.
  - 'yes': Use the last cached result for the current call but trigger
  re-resolution in the background for the benefit of future calls.
  If the result of the background lookup is a ttl > 0, periodic resolution
  will be restarted otherwise the next call will use the new cached value
  and will trigger a background lookup again.
  A new CLI command `rtp resolve stun hostname` has been added
- #### app_dial: Properly handle callee hangup while sending digits.
  If a called channel sends progress or wink and the caller begins
  sending digits but the callee answers and then hangs up before digit
  sending can finish, the call is now answered before being disconnected.
  If the callee hangs up without answering, the call now continues in
  the dialplan.

- #### Upgrade bundled pjproject to 2.17.
  Bundled pjproject has been upgraded to 2.17. For more
  information about what is included in this release, see the
  pjproject Github page: https://github.com/pjsip/pjproject/releases/tag/2.17

- #### res_pjsip: Add per-endpoint RTP port range configuration
  PJSIP endpoints now support rtp_port_start and
  rtp_port_end options to configure a dedicated RTP port range per
  endpoint, overriding the global rtp.conf setting.

- #### stasis_broadcast: Add optional ARI broadcast with first-claim-wins
  New optional modules res_stasis_broadcast.so and
  app_stasis_broadcast.so enable broadcasting an incoming channel to multiple
  ARI applications. The first application to successfully claim (via
  POST /ari/events/claim) wins channel control. StasisBroadcast() dialplan
  application initiates broadcasts. CallBroadcast and CallClaimed events notify
  applications. When modules are not loaded, behavior is unchanged.

- #### chan_iax2: Add CHANNEL getter to retrieve auth method.
  CHANNEL(auth_method) can now be used to retrieve the
  auth method negotiated for a call on IAX2 channels.

- #### res_pjsip_maintenance: Add PJSIP endpoint maintenance mode
  New module res_pjsip_maintenance adds runtime maintenance
  mode for PJSIP endpoints. Use "pjsip set maintenance <on|off>
  <endpoint|all>" to enable or disable, and "pjsip show maintenance"
  to list affected endpoints. AMI actions PJSIPSetMaintenance and
  PJSIPShowMaintenance provide programmatic access. No configuration
  file changes required.


### Upgrade Notes:

- #### jansson: Upgrade version to jansson 2.15.0
  jansson has been upgraded to 2.15.0. For more
  information visit jansson Github page: https://github.com/akheron/jansson/releases/tag/v2.15.0

- #### res_pjsip: Add per-endpoint RTP port range configuration
  An alembic database migration has been added to add
  the rtp_port_start and rtp_port_end columns to the ps_endpoints
  table. Run "alembic upgrade head" to apply the schema change.


### Developer Notes:

- #### res_pjsip: Add per-endpoint RTP port range configuration
  New public API: ast_rtp_instance_new_with_port_range()
  creates an RTP instance with a per-instance port range.
  ast_rtp_instance_get_port_start() and ast_rtp_instance_get_port_end()
  allow RTP engines to query the override. Third-party RTP engines can
  use these getters to support per-instance port ranges.

- #### stasis_broadcast: Add optional ARI broadcast with first-claim-wins
  New public APIs in stasis_app_broadcast.h:
  stasis_app_broadcast_channel(), stasis_app_claim_channel(),
  stasis_app_broadcast_winner(), and stasis_app_broadcast_wait(). New ARI event
  types (CallBroadcast, CallClaimed) added to events.json. All code is isolated;
  no existing ABI modified.

- #### res_pjsip_maintenance: Add PJSIP endpoint maintenance mode
  ast_sip_session_supplement gains a new optional
  callback - int (*session_create)(struct ast_sip_endpoint *endpoint,
  const char *destination). It is called from the global supplement
  list (not per-session) at the start of ast_sip_session_create_outgoing()
  via ast_sip_session_check_supplement_create(). Returning non-zero
  blocks the outgoing session. Modules that need to gate outbound
  SIP session creation should register a supplement with this callback
  set rather than hooking into chan_pjsip directly.

- #### build: remove pjsua, pjsystest, Python bindings and asterisk_malloc_debug stubs from pjproject dev build
  The pjsua and pjsystest application binaries, the deprecated
  Python pjsua bindings (`_pjsua.so`), and the `asterisk_malloc_debug.c` stub
  implementations are no longer built or installed as part of the bundled
  pjproject dev mode build. The `PYTHONDEV` (python2.7-dev) build dependency
  is also removed. Developers who relied on the pjsua binary for Test Suite
  SIP simulation should use SIPp instead, which is the current Asterisk Test
  Suite standard.
  Fixes: #1840


### Commit Authors:

- Alexander Bakker: (1)
- Alexei Gradinari: (1)
- Ben Ford: (1)
- Bernd Kuhls: (2)
- Charles Langlois: (1)
- Daniel Donoghue: (2)
- George Joseph: (14)
- Jaco Kroon: (1)
- Joshua C. Colp: (1)
- Maximilian Fridrich: (1)
- Mike Bradeen: (3)
- Milan Kyselica: (1)
- Naveen Albert: (3)
- Peter Krall: (1)
- Sean Bright: (4)
- Sebastian Denz: (1)
- Sebastian Jennen: (2)
- Stanislav Abramenkov: (2)
- Sven Kube: (1)
- UpBeta: (1)
- mattia: (1)
- mikhail_grishak: (1)
- phoneben: (5)
- smtcbn: (2)

## Issue and Commit Detail:

### Closed Issues:

  - 1217: [bug]: INSERT INTO cdr query prepare statement issue on cdr_adaptive_odbc to control statement preparation manually
  - 1357: [bug]: MessageSend WARNING “not a valid SIP/SIPS URI” when using endpoint not URI
  - 1653: [bug]: Asterisk ODBC Voicemail Crash Caused by Voicemail Re-entry Loop and Unsafe BLOB Retrieval
  - 1736: app_queue: update_queue() may double-increment member->calls with shared_lastcall=yes (regression observed after 20.17; impacts fewestcalls routing)
  - 1761: func_talkdetect.c: TALK_DETECT docs wording mistake
  - 1762: [bug]: 100% CPU usage when entering BridgeWait after JITTERBUFFER(disabled)=
  - 1807: [new-feature]: translate.c: implement different types of sample frame inputs
  - 1812: [new-feature]: add tests/test_codec_translations.c
  - 1818: [bug]: func_odbc: possible use-after-free crash during reload with active calls
  - 1839: Crash in MDMF Caller ID parser due to signed char length field on DAHDI channels
  - 1840: [bug]: Asterisk fails to compile with --enable-dev-mode=yes due to INIT_RETURN undeclared in bundled pjproject Python bindings
  - 1855: [bug]: core reload deadlocks Asterisk (pjsip, CLI, etc.)
  - 1858: [bug]: DNS records with a TTL of zero are permanently cached
  - 1859: [bug]: res_pjsip_outbound_registration: No expires header set when triggered via CLI
  - 1861: [bug]: Possible heap corruption in audiohook/translate write path during bridged media
  - 1862: [bug]: Build fails with Building Documentation: line 210: /tmp/xmldoc.tmp.xml: Permission denied
  - 1865: [bug]: chan_iax2: Another code path that causes crashes on negative data lengths
  - 1867: [bug]: Massive [eventpoll] file-descriptor leak (hundreds of epoll fds) when TURN is enabled in rtp.conf
  - 1872: [bug]:  Deadlock in chan_pjsip_new when endpoint set_var invokes PJSIP_HEADER
  - 1878: [new-feature]: chan_iax2: Allow retrieving the auth method using the CHANNEL function
  - 1883: [bug]: fix: stdatomic.h false positive on GCC 4.8
  - 1885: [bug]: cdrel_custom :SQLite version too old: sqlite3_prepare_v3 / SQLITE_PREPARE_PERSISTENT undeclared
  - 1888: [improvement]: pjsip: Upgrade bundled version to pjproject 2.17
  - 1892: [bug]: Build failure with bundled pjproject on OpenSSL 1.0.x: undefined reference to TLS_method and SSL_CTX_set_ciphersuites
  - 1894: [bug]: Outbound ARI websockets don't always clean up completely
  - 1896: [bug]: asterisk.c fails to compile when HAVE_LIBEDIT_IS_UNICODE isn't defined
  - 1901: [bug]: QUEUE_RAISE_PENALTY=rN ignored when set via queue rules
  - 1903: [bug]: g++ 16 no longer defines __STDC_VERSION__ causing channelstorage_cpp_map_name_id.cc to fail
  - 1907: [bug]: Deadlock between bridge and setting of RTP stats variables at hangup
  - 1910: [improvement]: Add attachable state variables to Channels and Bridges.
  - 1915: [bug]: app_dial: Channel not handled properly if callee disconnects while caller is sending it digits prior to answer
  - 1921: [bug]: Memory error in crypto_get_cert_subject when using malloc_debug
  - 1928: [bug]: Calling ast_softhangup with channel lock held can cause deadlock
  - 1931: [improvement]: jansson: Upgrade version to jansson 2.15.0
  - 1936: [bug]: Calling set_variable on PJSIP channel when originating with ARI with PJSIP_HEADER can result in deadlock
  - 1938: [bug]: res_rtp_asterisk: Copy/paste error in ast_rtp_get_stat()
  - 1941: [bug]: chan_websocket doesn't handle CONTINUATION websocket frames
  - 1947: [bug]: chan_dahdi fails to build with gcc-16 when openr2 is installed
  - 1950: [bug]: app_record does not detect channel hangup during beep playback
  - 1952: [bug]: OpenSSL 4.0.0
  - 1957: [bug]: Calendar module fails to build with libical 4.X
  - 1970: [bug]: Startup or shutdown segfault in res_ari_model under certain conditions with DEVMODE and persistent outbound websockets.

### Commits By Author:

- #### Alexander Bakker (1):
  - abstract_jb.c: Remove timerfd from channel when disabling jitter buffer

- #### Alexei Gradinari (1):
  - build: remove pjsua, pjsystest, Python bindings and asterisk_malloc_debug stubs from pjproject dev build

- #### Ben Ford (1):
  - ARI: Added paths to get and set multiple channel variables.

- #### Bernd Kuhls (2):
  - res_stir_shaken: avoid direct ASN1_STRING accesses
  - tcptls.c: fix build with OpenSSL 4

- #### Charles Langlois (1):
  - chan_pjsip: Fix deadlock when endpoint set_var uses PJSIP_HEADER

- #### Daniel Donoghue (2):
  - stasis_broadcast: Add optional ARI broadcast with first-claim-wins
  - res_pjsip_maintenance: Add PJSIP endpoint maintenance mode

- #### George Joseph (14):
  - res_ari: Add res_ari_model as an optional_module.
  - Ensure channel locks aren't held while calling ast_set_variables.
  - chan_dahdi: Fix set but not used in mfcr2_show_links_of().
  - chan_websocket: Handle incoming CONTINUATION frames.
  - res_rtp_asterisk: Fix incorrect reference in ast_rtp_get_stat().
  - channel.c: Move setting RTP stats from ast_softhangup to ast_ari_channels_hangup.
  - res_rtp_asterisk: Add option to control stun host resolution when TTL = 0
  - channel.c: Don't lock the channel in ast_softhangup while setting rtp instance vars
  - ari_websockets: Fix two issues in the cleanup of outbound websockets.
  - compat.h: Ensure check for `__STDC_VERSION__` is not attempted for c++.
  - asterisk.c: Fix #if HAVE_LIBEDIT_IS_UNICODE.
  - pbx_functions: Save module pointer before calling read and write callbacks.
  - res_rtp_asterisk: Destroy ioqueue in rtp_ioqueue_thread_destroy.
  - res_pjsip_config_wizard: Trigger reloads from a pjsip servant thread

- #### Jaco Kroon (1):
  - pjsip_configuration: Show actual dtls_verify config.

- #### Joshua C. Colp (1):
  - manager: Eliminate unnecessary code, simplify sessions in stasis callbacks

- #### Maximilian Fridrich (1):
  - res_pjsip_messaging: Update To URI only if it is a SIP(S) URI

- #### Mike Bradeen (3):
  - res ari: Add attachable states to Channels and Bridges
  - res_stir_shaken: fix memory free crash when Asterisk is built with malloc_debug
  - res_pjsip_outbound_registration: only update the Expires header if the value has changed

- #### Milan Kyselica (1):
  - callerid: fix signed char causing crash in MDMF parser

- #### Naveen Albert (3):
  - app_dial: Properly handle callee hangup while sending digits.
  - chan_iax2: Add CHANNEL getter to retrieve auth method.
  - chan_iax2: Add another check to abort frame handling if datalen < 0.

- #### Peter Krall (1):
  - res_stasis/resource_bridges: Split bridge playback control and wrapper cleanup

- #### Sean Bright (4):
  - res_pjsip: Don't allow a leading period when wildcard matching
  - install_prereq: Add a 'minimal' mode for basic build dependencies
  - func_talkdetect.c: Clarify dsp_talking_threshold documentation.
  - make_xml_documentation: Remove temporary file on script exit.

- #### Sebastian Denz (1):
  - res_pjsip_outbound_publish.c: Add more verbose documentation for outbound_proxy usage

- #### Sebastian Jennen (2):
  - tests: add tests/test_codec_translations.c
  - translate.c: implement different sample_types for translation computation.

- #### Stanislav Abramenkov (2):
  - jansson: Upgrade version to jansson 2.15.0
  - Upgrade bundled pjproject to 2.17.

- #### Sven Kube (1):
  - res_audiosocket: Tolerate non-audio frame types

- #### UpBeta (1):
  - app_record: Fix hangup handling during beep playback

- #### mattia (1):
  - res_pjsip: Add per-endpoint RTP port range configuration

- #### mikhail_grishak (1):
  - res_calendar: Fix build with libical 4.X

- #### phoneben (5):
  - app_queue: Fix raise_respect_min lost in copy_rules() breaking rN queue rules
  - app_voicemail_odbc: fix msgnum race and crash on failed STORE
  - pjproject: Backport fix for OpenSSL < 1.1.0 build failure in ssl_sock_ossl.c
  - cdrel_custom: fix SQLite compatibility for versions < 3.20.0
  - fix: backport pjproject stdatomic.h GCC 4.8 build failure patch

- #### smtcbn (2):
  - odbc: Don't use prepared statements for distinct SQL statements
  - app_queue: fix double increment of member->calls with shared_lastcall

### Commit List:

-  res_ari: Add res_ari_model as an optional_module.
-  res ari: Add attachable states to Channels and Bridges
-  ARI: Added paths to get and set multiple channel variables.
-  res_stir_shaken: avoid direct ASN1_STRING accesses
-  tcptls.c: fix build with OpenSSL 4
-  res_calendar: Fix build with libical 4.X
-  app_record: Fix hangup handling during beep playback
-  odbc: Don't use prepared statements for distinct SQL statements
-  abstract_jb.c: Remove timerfd from channel when disabling jitter buffer
-  res_pjsip: Don't allow a leading period when wildcard matching
-  Ensure channel locks aren't held while calling ast_set_variables.
-  app_queue: fix double increment of member->calls with shared_lastcall
-  chan_dahdi: Fix set but not used in mfcr2_show_links_of().
-  tests: add tests/test_codec_translations.c
-  install_prereq: Add a 'minimal' mode for basic build dependencies
-  chan_websocket: Handle incoming CONTINUATION frames.
-  res_rtp_asterisk: Fix incorrect reference in ast_rtp_get_stat().
-  jansson: Upgrade version to jansson 2.15.0
-  channel.c: Move setting RTP stats from ast_softhangup to ast_ari_channels_hangup.
-  res_rtp_asterisk: Add option to control stun host resolution when TTL = 0
-  pjsip_configuration: Show actual dtls_verify config.
-  app_dial: Properly handle callee hangup while sending digits.
-  res_pjsip_messaging: Update To URI only if it is a SIP(S) URI
-  Upgrade bundled pjproject to 2.17.
-  res_stir_shaken: fix memory free crash when Asterisk is built with malloc_debug
-  manager: Eliminate unnecessary code, simplify sessions in stasis callbacks
-  res_stasis/resource_bridges: Split bridge playback control and wrapper cleanup
-  res_pjsip_outbound_publish.c: Add more verbose documentation for outbound_proxy usage
-  channel.c: Don't lock the channel in ast_softhangup while setting rtp instance vars
-  chan_pjsip: Fix deadlock when endpoint set_var uses PJSIP_HEADER
-  res_pjsip: Add per-endpoint RTP port range configuration
-  app_queue: Fix raise_respect_min lost in copy_rules() breaking rN queue rules
-  app_voicemail_odbc: fix msgnum race and crash on failed STORE
-  ari_websockets: Fix two issues in the cleanup of outbound websockets.
-  compat.h: Ensure check for `__STDC_VERSION__` is not attempted for c++.
-  pjproject: Backport fix for OpenSSL < 1.1.0 build failure in ssl_sock_ossl.c
-  asterisk.c: Fix #if HAVE_LIBEDIT_IS_UNICODE.
-  cdrel_custom: fix SQLite compatibility for versions < 3.20.0
-  translate.c: implement different sample_types for translation computation.
-  stasis_broadcast: Add optional ARI broadcast with first-claim-wins
-  res_audiosocket: Tolerate non-audio frame types
-  pbx_functions: Save module pointer before calling read and write callbacks.
-  chan_iax2: Add CHANNEL getter to retrieve auth method.
-  fix: backport pjproject stdatomic.h GCC 4.8 build failure patch
-  res_rtp_asterisk: Destroy ioqueue in rtp_ioqueue_thread_destroy.
-  res_pjsip_maintenance: Add PJSIP endpoint maintenance mode
-  chan_iax2: Add another check to abort frame handling if datalen < 0.
-  res_pjsip_outbound_registration: only update the Expires header if the value has changed
-  func_talkdetect.c: Clarify dsp_talking_threshold documentation.
-  make_xml_documentation: Remove temporary file on script exit.
-  res_pjsip_config_wizard: Trigger reloads from a pjsip servant thread
-  build: remove pjsua, pjsystest, Python bindings and asterisk_malloc_debug stubs from pjproject dev build
-  callerid: fix signed char causing crash in MDMF parser

### Commit Details:

#### res_ari: Add res_ari_model as an optional_module.
  Author: George Joseph
  Date:   2026-06-03

  Under certain timing/load conditions, res_ari_model may not load until after
  res_ari on startup or it might unload before res_ari on shutdown. This can
  cause a segfault when DEVMODE is enabled and there are persistent outbound
  websocket connections because DEVMODE forces validation of outgoing events
  against the models.  To prevent this, res_ari_model has been added as an
  "optional_module" to res_ari's NODULE_INFO.  This will enforce load/unload
  order but not make res_ari dependent on res_ari_model.  However, if
  Asterisk is configured with --enable-dev-mode, res_ari will fail to
  load if res_ari_model isn't available.

  Resolves: #1970

#### res ari: Add attachable states to Channels and Bridges
  Author: Mike Bradeen
  Date:   2026-03-31

  Adds the ability to attach multiple states to both Channels and Bridges in the form
  of variables that are included in all events on the associated object.

  First, this adds an optional boolean field to channel variables 'report_events'
  that causes the variable to automatically be included in all events on that channel.

  To allow this, variables can now be either name value pairs (the current format):
  `<variable_name>: '<value_string>'`
   - or -
  `<variable_name>: {value: '<value_string>', report_events: [true|false]}`

  If the old format is used or 'report_events' is not included, it will default to
  false and retain current behavior.

  Second, this extends both reported and unreported variables to Bridges so they too
  may have stateful information.

  Resolves: #1910

  UserNote: Bridge variables now can be set and retrieved via the following paths:
  `/bridges/{bridgeId}/variable`
  `/bridges/{bridgeId}/variables`
  Both Bridge and Channel variables can now be set with an optional 'report_events'
  boolean flag that will cause those variables to be included on all events on that
  object. The 'report_events' flag will default to False if not set to maintain
  backwards capability.
  To allow this, variables can now be either name value pairs (the current format):
  `<variable_name>: '<value_string>'`
   - or -
  `<variable_name>: {value: '<value_string>', report_events: [true|false]}`

#### ARI: Added paths to get and set multiple channel variables.
  Author: Ben Ford
  Date:   2026-04-15

  Two new paths exist for ARI to get and set multiple channel variables at
  the same time. This is done via GET and POST like the single get and set
  variable equivalents. Leading and trailing whitespace will be stripped
  from the variable names for both paths. When setting variables, the
  values will be read as-is, whitespace included. GET takes in a single
  string with comma-separated values, while POST takes in a dictionary of
  key value pairs. The code follows the same paths as when setting
  multiple variables when originating a channel via ARI.

  UserNote: Added new ARI paths for getting and setting multiple channel
  variables at a time. For GET, this takes in a single string of
  comma-separated variable names, while POST takes in a dictionary of key
  value pairs. The behavior is the same as passing in variables when
  originating a channel.

#### res_stir_shaken: avoid direct ASN1_STRING accesses
  Author: Bernd Kuhls
  Date:   2026-05-02

  https://github.com/openssl/openssl/issues/29117

  Signed-off-by: Bernd Kuhls <bernd@kuhls.net>

  Resolves: #1952

#### tcptls.c: fix build with OpenSSL 4
  Author: Bernd Kuhls
  Date:   2026-05-02

  tcptls.c: In function '__ssl_setup':
  tcptls.c:417:52: error: implicit declaration of function 'SSLv3_client_method';
   did you mean 'SSLv23_client_method'? [-Wimplicit-function-declaration]
    417 |                         cfg->ssl_ctx = SSL_CTX_new(SSLv3_client_method());

  SSLv3_client_method was removed from OpenSSL 4.0.0:
  https://github.com/openssl/openssl/blob/openssl-4.0.0/doc/man7/ossl-removed-api.pod?plain=1#L440

  Signed-off-by: Bernd Kuhls <bernd@kuhls.net>

  Resolves: #1952

#### res_calendar: Fix build with libical 4.X
  Author: mikhail_grishak
  Date:   2026-05-26

  libical 4.0 removed the icaltime_add() function in favor of icaltime_adjust(). Additionally, the callback signature for icalcomponent_foreach_recurrence() was updated to use a const pointer for the icaltime_span argument.

  This commit adds conditional compilation using ICAL_MAJOR_VERSION to support both libical 3.X and the new 4.X API, ensuring backward compatibility.

  Fixes: #1957

#### app_record: Fix hangup handling during beep playback
  Author: UpBeta
  Date:   2026-05-23

  When a hangup occurs while app_record is playing the initial beep,
  the application does not detect the hangup and continues running
  until the maxduration timeout expires.

  Replace the manual ast_streamfile() + ast_waitstream() sequence with
  ast_stream_and_wait(), which properly detects hangup and returns
  non-zero, allowing the application to exit immediately with
  RECORD_STATUS set to HANGUP.

  Resolves: #1950

#### odbc: Don't use prepared statements for distinct SQL statements
  Author: smtcbn
  Date:   2025-04-25

  Avoids unnecessary prepare for simple INSERT statements that cause
  issues with ProxySQL (prepared statement counter overflow).

  Resolves: #1217

#### abstract_jb.c: Remove timerfd from channel when disabling jitter buffer
  Author: Alexander Bakker
  Date:   2026-05-20

  Previously, the lingering timerfd would cause a tight loop if the
  channel enters a BridgeWait after the jitter buffer was disabled.

  Fixes: #1762

#### res_pjsip: Don't allow a leading period when wildcard matching
  Author: Sean Bright
  Date:   2026-05-26

  The reference identifier (what the client provides - in this case a
  hostname) must start with a domain label, not a `.`.

  The current implementation will match `.seanbright.com` against
  `*.seanbright.com` which is incorrect.

#### Ensure channel locks aren't held while calling ast_set_variables.
  Author: George Joseph
  Date:   2026-05-20

  If the channel is locked when calling ast_set_variables and any of the
  variables contained dialplan functions, there's a possiblilty of a deadlock.
  To prevent this, either the explicit locks were removed or the call to
  ast_set_variables moved out of the lock scope.  A warning to not hold
  channel locks is also added to the documentation for ast_set_variables.

  Resolves: #1936

#### app_queue: fix double increment of member->calls with shared_lastcall
  Author: smtcbn
  Date:   2026-01-23

  Under high concurrency, update_queue() may be invoked multiple times
  for the same call, causing member->calls and queue-level counters to
  be incremented more than once.

  The existing starttime check is not atomic and allows concurrent
  execution paths to pass. Treat member->starttime as a single-use token
  and consume it via CAS to ensure the call is counted exactly once.

  This also prevents incorrect call distribution when using strategies
  such as fewestcalls.

  Observed as a regression after upgrading to 20.17.

  Resolves: #1736

#### chan_dahdi: Fix set but not used in mfcr2_show_links_of().
  Author: George Joseph
  Date:   2026-05-21

  When openr2 is installed mfcr2_show_links_of() is no longer ifdeffed out
  which makes gcc-16 complain with 'variable ‘x’ set but not used'.

  Resolves: #1947

#### tests: add tests/test_codec_translations.c
  Author: Sebastian Jennen
  Date:   2026-03-06

  This tests checks [slin -> codec -> slin] and then compares slin in vs out
  regarding signal noise ratio and delay.

  Near-lossless codecs (ulaw, alaw) are checked with a maximum per-sample
  error bound.  Lossy codecs are checked with a per-codec SNR threshold.
  Cross-correlation alignment compensates for algorithmic delay in codecs
  like speex and opus.

  Covered codecs: ulaw, alaw, adpcm, g726, g726aal2, gsm, speex,
  speex16, speex32, ilbc, codec2, lpc10, g722, opus.

  Resolves: #1812

#### install_prereq: Add a 'minimal' mode for basic build dependencies
  Author: Sean Bright
  Date:   2026-05-20


#### chan_websocket: Handle incoming CONTINUATION frames.
  Author: George Joseph
  Date:   2026-05-20

  chan_websocket now tells res_http_websocket to accumulate incoming CONTINUATION
  frames into 1024 byte TEXT or BINARY frames.

  Resolves: #1941

#### res_rtp_asterisk: Fix incorrect reference in ast_rtp_get_stat().
  Author: George Joseph
  Date:   2026-05-19

  ```
  AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_STDEVMES, \
  AST_RTP_INSTANCE_STAT_COMBINED_MES, stats->local_stdevmes, \
  rtp->rtcp->stdev_rxjitter);
  ```

  Should have been

  ```
  AST_RTP_STAT_SET(AST_RTP_INSTANCE_STAT_LOCAL_STDEVMES, \
  AST_RTP_INSTANCE_STAT_COMBINED_MES, stats->local_stdevmes, \
  rtp->rtcp->stdev_rxmes);
  ```

  Note the last macro parameter name.

  Resolves: #1938

#### jansson: Upgrade version to jansson 2.15.0
  Author: Stanislav Abramenkov
  Date:   2026-05-13

  UpgradeNote: jansson has been upgraded to 2.15.0. For more
  information visit jansson Github page: https://github.com/akheron/jansson/releases/tag/v2.15.0

  Resolves: #1931

#### channel.c: Move setting RTP stats from ast_softhangup to ast_ari_channels_hangup.
  Author: George Joseph
  Date:   2026-05-12

  The original trigger for setting the RTP stats in ast_softhangup() came from
  an ARI issue where stats weren't being set in time to be reported on STASIS_END
  events. The thought was that setting them in a common place like ast_softhangup()
  would ensure the stats were set in possibly other scenarios.  Unfortunately,
  setting the RTP stats variables in ast_softhangup() broke ABI as it required
  that no channel locks be held which was not the case earlier.

  Given that the original issue was ARI, we can move setting the stats to
  ast_ari_channels_hangup() in resource_channels just before it calls
  ast_softhangup().  This might not catch all cases of the stats not being set,
  but it won't break ABI or deadlock either.

  Resolves: #1928

#### res_rtp_asterisk: Add option to control stun host resolution when TTL = 0
  Author: George Joseph
  Date:   2026-05-05

  If a hostname is specified for stunaddr in rtp.conf, periodic DNS resolution
  is enabled based on the TTL returned in the DNS results.  If the TTL returned
  is 0, it means that the next time the IP address is needed, it must be
  looked up again.  I.E.  Don't cache.  Historically (and incorrectly) however,
  res_rtp_asterisk stopped the periodic resolution and never re-resolved the
  hostname again.

  Besides what's mentioned in the user notes...
  * Additional debugging was added in various STUN/DNS functions.
  * The `rtp show settings` CLI command shows more detailed STUN info.
  * Some debugging was added to dns_core.c and dns_recurring.c.

  UserNote: A new `stunaddr_reresolve_ttl_0` parameter has been added to rtp.conf
  that allows control over what happens when a STUN server hostname lookup
  returns a TTL of 0.  The values can be set as follows:
  - 'no': This is the historical (and current default) behavior of not doing
  any further lookups and continuing to use the last successful result until
  Asterisk is restarted or rtp.conf is reloaded.
  - 'yes': Use the last cached result for the current call but trigger
  re-resolution in the background for the benefit of future calls.
  If the result of the background lookup is a ttl > 0, periodic resolution
  will be restarted otherwise the next call will use the new cached value
  and will trigger a background lookup again.

  UserNote: A new CLI command `rtp resolve stun hostname` has been added
  that will force a resolution of the STUN hostname and (re)start periodic
  resolution if the result has a TTL > 0.

  Resolves: #1858

#### pjsip_configuration: Show actual dtls_verify config.
  Author: Jaco Kroon
  Date:   2026-05-07

  Rather than merely showing

  dtls_verify : Yes/No

  in pjsip show endpoint xxx it will now be shown what exactly is being
  checked, ie, one of:

  dtls_verify : No
  dtls_verify : Fingerprint
  dtls_verify : Certificate
  dtls_verify : Yes

  Where Yes implies both Fingerprint and Certificate.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

#### app_dial: Properly handle callee hangup while sending digits.
  Author: Naveen Albert
  Date:   2026-05-05

  If we are sending digits (either DTMF, MF, or SF) to the called channel
  after receiving progress or a wink, and the callee hangs up before we
  have finished sending it digits, there are several problems that can ensue:

  * If the callee hung up without answering, the calling channel would
    hang up and not continue in the dialplan.
  * If the callee *did* answer before hanging up, the answer was never
    passed through to the caller, since this gets "eaten" by the various
    digit streaming functions and is never processed by app_dial.

  This is generally an edge case that occurs due to some kind of signaling
  failure, but to better handle this:

  * Set to_answer to 0 to prevent hangup on the exit path, just like other
    parts of wait_for_answer.
  * Better document this usage of to_answer.
  * If the channel did answer while it was receiving digits, manually
    answer the calling channel before we abort. The call would not continue
    in the dialplan anyways (either before or after this fix), but technically
    the call was answered, so the CDRs should probably reflect that, and this
    mirrors the behavior of calls which normally do not continue.

  Resolves: #1915

  UserNote: If a called channel sends progress or wink and the caller begins
  sending digits but the callee answers and then hangs up before digit
  sending can finish, the call is now answered before being disconnected.
  If the callee hangs up without answering, the call now continues in
  the dialplan.

#### res_pjsip_messaging: Update To URI only if it is a SIP(S) URI
  Author: Maximilian Fridrich
  Date:   2026-05-07

  When a message is sent via ARI, the ARI endpoint only provides a To
  field which is also used as destination field. This means that the To
  field might not necessarily contain a SIP URI but might instead specify
  an Asterisk endpoint (in MessageDestinationInfo format). This led to
  many warnings even though the message was sent correctly.

  The fix is to only call `ast_sip_update_to_uri` if the To field starts
  with the sip: or sips: scheme.

  Resolves: #1357

#### Upgrade bundled pjproject to 2.17.
  Author: Stanislav Abramenkov
  Date:   2026-04-27

  Resolves: #1888

  UserNote: Bundled pjproject has been upgraded to 2.17. For more
  information about what is included in this release, see the
  pjproject Github page: https://github.com/pjsip/pjproject/releases/tag/2.17

#### res_stir_shaken: fix memory free crash when Asterisk is built with malloc_debug
  Author: Mike Bradeen
  Date:   2026-05-06

  crypto_utils uses ast_asprintf to allocate the search string when checking the
  certificate subject, but was not using ast_free to free it. This caused a crash
  when Asterisk was built with malloc_debug

  Resolves: #1921

#### manager: Eliminate unnecessary code, simplify sessions in stasis callbacks
  Author: Joshua C. Colp
  Date:   2026-05-04

  Due to stasis filtering the stasis callback for AMI type messages is
  guaranteed to only receive messages that can be turned into AMI events,
  so remove the check done in the callback.

  The sessions container usage for the stasis callbacks has also been
  simplified by having a reference on the message router subscription
  instead of having to acquire the sessions from the global object each
  time.

#### res_stasis/resource_bridges: Split bridge playback control and wrapper cleanup
  Author: Peter Krall
  Date:   2026-04-17

  Modified the bridge playback teardown so the worker thread removes only the
  playback control, while the after-bridge callback removes the playback
  wrapper once the announcer has actually left the bridge.

  This avoids a stale window where a new playback request could create a
  replacement announcer before the old announcer had fully exited the holding
  bridge.

  Also replaced the flexible trailing bridge_id storage in the shared worker
  thread data with an optional bridge_id pointer, since recording paths use the
  same structure without a bridge id.

  Fixes: #1861

#### res_pjsip_outbound_publish.c: Add more verbose documentation for outbound_proxy usage
  Author: Sebastian Denz
  Date:   2026-03-26


#### channel.c: Don't lock the channel in ast_softhangup while setting rtp instance vars
  Author: George Joseph
  Date:   2026-05-05

  ast_softhangup() was locking the channel before calling ast_rtp_instance_set_stats_vars()
  which, if the channel was in a bridge, then locked the bridge peer channel.  If another
  thread attempted to set bridge variables on the peer, it would lock that channel first,
  then this channel causing a lock inversion.  ast_softhangup() now holds the channel lock
  while retrieving the rtp instance, then unlocks it before calling
  ast_rtp_instance_set_stats_vars(), then locks it again after it returns.

  Resolves: #1907

#### chan_pjsip: Fix deadlock when endpoint set_var uses PJSIP_HEADER
  Author: Charles Langlois
  Date:   2026-04-16

  When a PJSIP endpoint is configured with set_var invoking a dialplan
  function (e.g. PJSIP_HEADER(add,...)), chan_pjsip_new() calls
  pbx_builtin_setvar_helper() while holding the channel lock.
  For function-style variables, this dispatches to ast_func_write()
  which, in the case of PJSIP_HEADER, calls
  ast_sip_push_task_wait_serializer() -- blocking synchronously while
  the channel lock is held.

  If a concurrent operation (ARI, AMI, rtp_check_timeout) traverses
  the channels container via ast_channel_get_by_name(), it acquires
  the container lock then tries to lock individual channels in the
  iteration callback (by_uniqueid_cb/by_name_cb). When the serializer
  thread also needs the container lock, a circular dependency forms:

    channel_lock -> serializer_wait -> container_lock -> channel_lock

  This causes a complete Asterisk freeze. In the observed case, 36
  threads were blocked on the container lock until res_freeze_check
  triggered SIGABRT after its 30-second timeout.

  Unlock the channel before iterating endpoint channel_vars so that
  dialplan functions can block without holding the channel lock. Re-lock
  the channel for ast_channel_stage_snapshot_done() so the batched
  snapshot is published under lock and captures the full channel state
  including the variables set during the loop.

  Fixes: #1872

#### res_pjsip: Add per-endpoint RTP port range configuration
  Author: mattia
  Date:   2026-04-01

  Add rtp_port_start and rtp_port_end options to PJSIP endpoint
  configuration, allowing each endpoint to use a dedicated RTP port
  range instead of the global rtp.conf setting.

  This is useful for scenarios where different endpoints need isolated
  port ranges, such as firewall rules per trunk, multi-tenant systems,
  or network QoS policies tied to port ranges.

  The implementation adds ast_rtp_instance_new_with_port_range() to the
  RTP engine API, which sets the port range on the instance before the
  engine allocates the transport. The default RTP engine
  (res_rtp_asterisk) checks for per-instance overrides in
  rtp_allocate_transport() and falls back to the global range when
  none is set.

  Both options must be set together, with values >= 1024 and
  rtp_port_end > rtp_port_start. Setting both to 0 (the default)
  preserves existing behavior.

  Resolves: https://github.com/asterisk/asterisk-feature-requests/issues/71

  UserNote: PJSIP endpoints now support rtp_port_start and
  rtp_port_end options to configure a dedicated RTP port range per
  endpoint, overriding the global rtp.conf setting.

  UpgradeNote: An alembic database migration has been added to add
  the rtp_port_start and rtp_port_end columns to the ps_endpoints
  table. Run "alembic upgrade head" to apply the schema change.

  DeveloperNote: New public API: ast_rtp_instance_new_with_port_range()
  creates an RTP instance with a per-instance port range.
  ast_rtp_instance_get_port_start() and ast_rtp_instance_get_port_end()
  allow RTP engines to query the override. Third-party RTP engines can
  use these getters to support per-instance port ranges.

#### app_queue: Fix raise_respect_min lost in copy_rules() breaking rN queue rules
  Author: phoneben
  Date:   2026-04-26

  app_queue: Fix raise_respect_min not copied in copy_rules() causing rN rules to be ignored.

  `copy_rules()` never copied `raise_respect_min` into the per-call rule list, so the flag was always 0 when a timed penaltychange rule fired, making `rN` behave like plain `N` and raising members below `min_penalty` that should have been excluded.

  Also fixes `update_qe_rule()` not propagating the flag from `qe->pr` to `qe`, and dropping the `r` prefix when saving back to `QUEUE_RAISE_PENALTY`.

  Resolves: #1901

#### app_voicemail_odbc: fix msgnum race and crash on failed STORE
  Author: phoneben
  Date:   2026-04-09

  app_voicemail_odbc: fix msgnum race and crash on failed STORE

  Two concurrent callers leaving voicemail to the same mailbox could be
  assigned the same msgnum because ast_unlock_path() was called before
  STORE(), allowing a second thread to read the same LAST_MSG_INDEX()
  before the first INSERT committed. The losing thread got a duplicate
  key error, but execution continued into notify_new_message() ->
  RETRIEVE() because the STORE() return value was not checked.
  RETRIEVE() then fetched the winning thread's DB row, mmap'd its blob
  size against the locally truncated file, and crashed with SIGBUS.

  Hold the path lock through STORE() and bail out on failure.

  Fixes: #1653

#### ari_websockets: Fix two issues in the cleanup of outbound websockets.
  Author: George Joseph
  Date:   2026-04-22

  1.  session_cleanup() now saves the websocket type before unlinking the
  session from the session registry.  This prevents a FRACK when cleaning
  up per-call websockets when MALLOC_DEBUG is used.

  2.  session_shutdown_cb() and outbound_sessions_load() now call
  pthread_cancel() to cancel the session handler thread to prevent the
  thread from continually trying to connect to a server after the
  connection config has been removed by a reload.  This required the
  thread to use pthread_cleanup_push() to clean up its reference to the
  session instead of RAII because RAII destructors don't get run when
  pthread_cancel() is used.

  Resolves: #1894

#### compat.h: Ensure check for `__STDC_VERSION__` is not attempted for c++.
  Author: George Joseph
  Date:   2026-04-27

  `__STDC_VERSION__` is specific to C but up until gcc 16, the g++ compiler
  also defined it.  With g++ 16.0 it's no longer defined (which is the correct
  behavior) so compiling channelstorage_cpp_map_name_id.cc fails.  The
  check for `__STDC_VERSION__` in compat.h is now skipped if we're compiling
  a C++ source file.

  Resolves: #1903

#### pjproject: Backport fix for OpenSSL < 1.1.0 build failure in ssl_sock_ossl.c
  Author: phoneben
  Date:   2026-04-22

  Backport pjsip/pjproject#4941 which fixes a build/link failure when
  compiling against OpenSSL < 1.1.0 (e.g. OpenSSL 1.0.2k on CentOS 7).

  Two symbols introduced in OpenSSL 1.1.x were called unconditionally
  in ssl_sock_ossl.c without version guards:

  - `TLS_method()` in `init_ossl_ctx()` is now guarded with
    `OPENSSL_VERSION_NUMBER < 0x10100000L`, falling back to
    `SSLv23_method()` on older OpenSSL.

  - `SSL_CTX_set_ciphersuites()` is now guarded with
    `OPENSSL_VERSION_NUMBER >= 0x1010100fL` since this function
    was introduced in OpenSSL 1.1.1 and is absent in 1.0.x.

  Without this fix, linking fails with:
    undefined reference to `TLS_method'
    undefined reference to `SSL_CTX_set_ciphersuites'

  when building Asterisk with bundled pjproject on systems such as
  CentOS 7 with OpenSSL 1.0.2k.

  Resolves: #1892

#### asterisk.c: Fix #if HAVE_LIBEDIT_IS_UNICODE.
  Author: George Joseph
  Date:   2026-04-22

  Line 2729 has `#if HAVE_LIBEDIT_IS_UNICODE` instead if `#ifdef`.  Since
  macros defined by autoconf are either set to `1` or not set at all,
  older distros where libedit isn't unicode won't have that macro defined
  and will fail to compile.

  Resolves: #1896

#### cdrel_custom: fix SQLite compatibility for versions < 3.20.0
  Author: phoneben
  Date:   2026-04-21

  cdrel_custom: fix SQLite compatibility for versions < 3.20.0

  Replace sqlite3_prepare_v3 + SQLITE_PREPARE_PERSISTENT with a version-guarded fallback to sqlite3_prepare_v2 for older SQLite builds.

  Resolves: #1885

#### translate.c: implement different sample_types for translation computation.
  Author: Sebastian Jennen
  Date:   2026-04-02

  The default (codec) still uses the codec provided samples. Additionally
  different sample_types can be used with eg: `translate sampletype speech`
  and then running `core show translation comp 10` to measure performance
  of different audio scenarios.

  Resolves: #1807

#### stasis_broadcast: Add optional ARI broadcast with first-claim-wins
  Author: Daniel Donoghue
  Date:   2026-02-25

  Adds two optional modules:
  res_stasis_broadcast.so: Infrastructure for broadcasting a single incoming
  channel to multiple ARI applications with atomic first-claim-wins semantics.

  app_stasis_broadcast.so: Provides the StasisBroadcast() dialplan application
  which invokes the broadcast infrastructure.

  Both modules are self-contained; if neither is loaded there is zero runtime
  impact. Loading them does not alter existing Stasis or ARI behavior unless
  explicitly used.

  Key Features (only active when modules are loaded):
  Fisher-Yates shuffled broadcast dispatch for fair claim races
  Atomic claim operations using mutex + condition variable signaling
  Configurable broadcast timeouts
  Safe regex application filtering with validation to mitigate ReDoS risk
  Thread-safe channel variable snapshotting (channel locked during reads)
  Late-claim safety: broadcast context kept alive until after the Stasis
  session ends so concurrent claimants always receive 409 Conflict rather
  than 404 Not Found
  Memory safety via RAII_VAR, ast_json_ref/unref, and ao2 reference counting

  Components Added:
  res/res_stasis_broadcast.c: Core broadcast + claim logic
  apps/app_stasis_broadcast.c: StasisBroadcast() dialplan application
  include/asterisk/stasis_app_broadcast.h: Public API header
  res/ari/resource_events.c: Integrates POST /ari/events/claim endpoint
  rest-api/api-docs/events.json: New CallBroadcast and CallClaimed events

  Implementation Notes:
  Broadcast contexts reside in an ao2 hash container keyed by channel id. Each
  context holds atomic claim state, winner application name, timeout metadata,
  and a condition variable for waiters. Broadcast contexts are kept alive until
  after stasis_app_exec() returns so that concurrent claimants racing against
  the timeout always receive 409 Conflict. Broadcast dispatch calls
  stasis_app_send() directly for each matching application in shuffled order.
  Regex filters are validated with bounded length, group depth, quantified
  group count, and alternation limits to reduce pathological backtracking.
  Timeout calculation uses timespec arithmetic with overflow-safe millisecond
  remainder handling. Event JSON follows existing Stasis/ARI conventions;
  references are managed correctly to avoid leaks or double frees.

  Optional Nature / Impact:
  No changes to existing APIs, events, or applications when absent.
  Clean fallback: systems ignoring the modules behave identically to prior
  versions.

  Development was assisted by Claude (Anthropic). All generated code has been
  reviewed, tested, and is understood by the author.

  UserNote: New optional modules res_stasis_broadcast.so and
  app_stasis_broadcast.so enable broadcasting an incoming channel to multiple
  ARI applications. The first application to successfully claim (via
  POST /ari/events/claim) wins channel control. StasisBroadcast() dialplan
  application initiates broadcasts. CallBroadcast and CallClaimed events notify
  applications. When modules are not loaded, behavior is unchanged.

  DeveloperNote: New public APIs in stasis_app_broadcast.h:
  stasis_app_broadcast_channel(), stasis_app_claim_channel(),
  stasis_app_broadcast_winner(), and stasis_app_broadcast_wait(). New ARI event
  types (CallBroadcast, CallClaimed) added to events.json. All code is isolated;
  no existing ABI modified.

#### res_audiosocket: Tolerate non-audio frame types
  Author: Sven Kube
  Date:   2026-04-22

  This commit implements the handling of non-voice or DTMF frames like the
  chan_websocket handling added in #1588. Rather than treating unsupported
  frames as fatal errors, silently ignore CNG frames and log a warning for
  other unsupported types.

#### pbx_functions: Save module pointer before calling read and write callbacks.
  Author: George Joseph
  Date:   2026-04-21

  Before ast_func_read and ast_func_write call their respective read and write
  callbacks for registered dialplan functions, they use the module pointer in
  the registered ast_custom_function structure to increment the module use
  count.  They then decrement the usecount when the callback returns.  This
  prevents the providing module from being unloaded while there's a call using
  the function.

  Some modules, notably func_odbc, create and destroy dialplan functions based
  on the contents of a config file.  Since the ast_custom_function structure is
  dynamically allocated, it could be destroyed on reload which means when the
  module's read or write callback returns to the ast_func calls it would try to
  decrement the usecount using the module pointer from an ast_custom_function
  structure that has already been freed.  Proper locking or reference counting
  by the module can reduce the possibility of this happening but it can't
  prevent it because it doesn't have control after its read or write callback
  has returned to ast_func_read or ast_func_write.

  To address this, ast_func_read, ast_func_read2 and ast_func_write save the
  module pointer to a local variable before calling the module's callback,
  then use the saved pointer to decrement the use count.  The module
  pointer will always be valid if the module is loaded regardless of the
  state of the ast_custom_function structure.

  Resolves: #1818

#### chan_iax2: Add CHANNEL getter to retrieve auth method.
  Author: Naveen Albert
  Date:   2026-04-18

  Add a property to the CHANNEL method to retrieve the auth method,
  which can be used to retrieve the specific auth method actually
  negotiated for a call (e.g. RSA, MD5, etc.).

  Also clean up some of the documentation for the secure properties
  to clarify how these relate to call encryption.

  Resolves: #1878

  UserNote: CHANNEL(auth_method) can now be used to retrieve the
  auth method negotiated for a call on IAX2 channels.

#### fix: backport pjproject stdatomic.h GCC 4.8 build failure patch
  Author: phoneben
  Date:   2026-04-21

  pjproject 2.16 (bundled) fails to build on GCC 4.8 (CentOS/RHEL 7)
  due to a false positive C11 atomics detection introduced in pjproject
  commit #4570. A fix has been submitted upstream to pjproject (#4933).

  Adding a local patch to third-party/pjproject/patches/ until a fixed
  version of pjproject is bundled in Asterisk.

  Fixes build error:
  ../src/pj/os_core_unix.c:52:27: fatal error: stdatomic.h: No such file or directory

  Resolves: #1883

#### res_rtp_asterisk: Destroy ioqueue in rtp_ioqueue_thread_destroy.
  Author: George Joseph
  Date:   2026-04-16

  The rtp_ioqueue_thread_destroy() function was destroying the the ioqueue
  thread and releasing its pool but not destroying the ioqueue itself.  This
  was causing the ioqueue's epoll file descriptor to leak.

  Resolves: #1867

#### res_pjsip_maintenance: Add PJSIP endpoint maintenance mode
  Author: Daniel Donoghue
  Date:   2026-03-10

  Introduces res_pjsip_maintenance, a loadable module that allows
  operators to place individual PJSIP endpoints into maintenance mode
  at runtime without unregistering or disabling them.

  While an endpoint is in maintenance mode:
   * New inbound INVITE and SUBSCRIBE dialogs are rejected with
     503 Service Unavailable and a Retry-After: 300 header.
   * In-progress dialogs (re-INVITE, UPDATE, BYE, etc.) are
     unaffected and complete normally.
   * Outbound originations via Dial() or ARI originate are refused
     before any SIP session is created.

  State is held in-memory only and is cleared on module unload
  or Asterisk restart.

  This module was developed with AI assistance (Claude).  All code
  has been reviewed and tested by the author, who takes full
  responsibility for the submission.

  CLI interface:
    pjsip set maintenance <on|off> <endpoint|all>
    pjsip show maintenance [endpoint]

  AMI interface:
    Action: PJSIPSetMaintenance
    Endpoint: <name>|all
    State: on|off

    Action: PJSIPShowMaintenance
    Endpoint: <name>  (optional; omit to list all)

    Emits PJSIPMaintenanceStatus events per result, followed by
    PJSIPMaintenanceStatusComplete. State changes also emit an
    unsolicited PJSIPMaintenanceStatus event.

  To support outbound blocking, a new session_create callback is
  added to ast_sip_session_supplement. Supplements that set this
  callback are invoked at the start of ast_sip_session_create_outgoing()
  in res_pjsip_session, before any dialog or invite session resources
  are allocated. res_pjsip_maintenance registers itself as a session
  supplement and uses this callback to gate outbound session creation
  on a per-endpoint basis.

  MODULEINFO:
    <depend>pjproject</depend>
    <depend>res_pjsip</depend>
    <depend>res_pjsip_session</depend>

  UserNote: New module res_pjsip_maintenance adds runtime maintenance
  mode for PJSIP endpoints. Use "pjsip set maintenance <on|off>
  <endpoint|all>" to enable or disable, and "pjsip show maintenance"
  to list affected endpoints. AMI actions PJSIPSetMaintenance and
  PJSIPShowMaintenance provide programmatic access. No configuration
  file changes required.

  DeveloperNote: ast_sip_session_supplement gains a new optional
  callback - int (*session_create)(struct ast_sip_endpoint *endpoint,
  const char *destination). It is called from the global supplement
  list (not per-session) at the start of ast_sip_session_create_outgoing()
  via ast_sip_session_check_supplement_create(). Returning non-zero
  blocks the outgoing session. Modules that need to gate outbound
  SIP session creation should register a supplement with this callback
  set rather than hooking into chan_pjsip directly.

#### chan_iax2: Add another check to abort frame handling if datalen < 0.
  Author: Naveen Albert
  Date:   2026-04-11

  Commit 2da221e217cbff957af928e8df43ee25583232d1 added a missing abort
  if datalen < 0 check on a code path and an assertion inside
  iax_frame_wrap if we ever encountered a frame with a negative frame
  length (which will eventually cause a crash).

  Add another missing abort check for negative datalen, exposed by this
  assertion. (Similar to the previous commit, this is a video frame with
  a datalen of -1).

  Resolves: #1865

#### res_pjsip_outbound_registration: only update the Expires header if the value has changed
  Author: Mike Bradeen
  Date:   2026-04-08

  The PJSIP outbound registration API has undocumented behavior when reconfiguring
  the outbound registration if the expires value being set is the same as what was
  previously set.

  In this case PJSIP will remove the Expires header entirely from subsequent
  outbound REGISTER requests. To eliminate this as an issue we now check the current
  expires value against the configured expires value and only apply it if it differs.

  This ensures that outbound REGISTER requests always contain an Expires header.

  Resolves: #1859

#### func_talkdetect.c: Clarify dsp_talking_threshold documentation.
  Author: Sean Bright
  Date:   2026-04-08

  Fixes: #1761

#### make_xml_documentation: Remove temporary file on script exit.
  Author: Sean Bright
  Date:   2026-04-09

  Fixes: #1862

#### res_pjsip_config_wizard: Trigger reloads from a pjsip servant thread
  Author: George Joseph
  Date:   2026-04-07

  When res_pjsip is reloaded directly, it does the sorcery reload in a pjsip
  servant thread as it's supposed to.  res_pjsip_config_wizard however
  was not which was leading to occasional deadlocks.  It now does the reload
  in a servant thread just like res_pjsip.

  Resolves: #1855

#### build: remove pjsua, pjsystest, Python bindings and asterisk_malloc_debug stubs from pjproject dev build
  Author: Alexei Gradinari
  Date:   2026-04-06

  The pjsua Python module and the pjsua/pjsystest apps were used by the
  Asterisk Test Suite for SIP simulation in dev mode builds. They are now
  fully obsolete for three independent reasons:

  1. **pjsua Python bindings officially deprecated upstream.** The pjproject
     maintainers added `pjsip-apps/src/python/DEPRECATED.txt` directing
     users to the PJSUA2 SWIG binding instead. A build-fix PR
     (https://github.com/pjsip/pjproject/pull/4892) was closed by the
     maintainer explicitly citing this deprecation.

  2. **Removed from the Asterisk Test Suite.** As confirmed by @mbradeen:
     > *"We had to get rid of pjsua when we went to Python3 because it would
     > hang due to a conflict between async calls within pjsua and twisted.
     > There are still some old references to tests we couldn't fully convert
     > to sipp, but those are skipped."*

  3. **Broken and unmaintained.** Building with Python 2.7 (the only version
     `configure.ac` searched for) fails with:
     ```
     _pjsua.c: error: 'INIT_RETURN' undeclared (first use in this function)
     ```
     due to a bug in pjproject 2.16's `python3_compat.h` that upstream
     declined to fix.

  This PR removes all pjsua-related build artifacts from Asterisk's bundled
  pjproject build: the pjsua and pjsystest application binaries, the deprecated
  Python (`_pjsua.so`) bindings, the `asterisk_malloc_debug.c` stubs, and the
  `PYTHONDEV` detection from `configure.ac`. Also removes `libpjsua` from
  Asterisk's main linker flags.

  DeveloperNote: The pjsua and pjsystest application binaries, the deprecated
  Python pjsua bindings (`_pjsua.so`), and the `asterisk_malloc_debug.c` stub
  implementations are no longer built or installed as part of the bundled
  pjproject dev mode build. The `PYTHONDEV` (python2.7-dev) build dependency
  is also removed. Developers who relied on the pjsua binary for Test Suite
  SIP simulation should use SIPp instead, which is the current Asterisk Test
  Suite standard.

  Fixes: #1840

#### callerid: fix signed char causing crash in MDMF parser
  Author: Milan Kyselica
  Date:   2026-03-25

  Change rawdata buffer from char to unsigned char to prevent
  sign-extension of TLV length bytes >= 0x80. On signed-char
  platforms (all Asterisk builds due to -fsigned-char in
  configure.ac), these values become negative when assigned to
  int, bypass the `if (res > 32)` bounds check, and reach
  memcpy as size_t producing a ~18 EB read that immediately
  crashes with SIGSEGV.

  Affects DAHDI analog (FXO) channels only. Not reachable
  via SIP, PRI/BRI, or DTMF-based Caller ID.

  Fixes: #1839

