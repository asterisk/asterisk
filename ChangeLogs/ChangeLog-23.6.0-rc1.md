
## Change Log for Release asterisk-23.6.0-rc1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-23.6.0-rc1.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/23.5.0...23.6.0-rc1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-23.6.0-rc1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 39
- Commit Authors: 20
- Issues Resolved: 29
- Security Advisories Resolved: 0

### User Notes:

- #### res_pjsip: Qualify all resolved contact targets
  DNS-backed PJSIP contacts are now considered reachable when any resolved
  SRV, A, or AAAA target responds to qualification.

- #### res_prometheus: add toggle config for metrics
  Add new config option 'channels_detail_metrics_enabled' to disable export of 'asterisk_channels_state' and 'asterisk_channels_duration_seconds' metrics,It defaults to 'yes' to preserve existing behavior
  Add new config option 'bridges_detail_metrics_enabled' to disable export of 'asterisk_bridges_channels_count' metric, It defaults to 'yes' to preserve existing behavior
  Explicitly enable bridges_detail_metrics_enabled for the bridge_to_string unit‑test.

- #### app_confbridge: Add announcements option to disable conference announcements
  A new ConfBridge bridge profile option, announcements, can be set to
  no to stop announcements from being played to the conference as a whole.  No
  announcer channel is then created, which saves a channel and a taskprocessor
  thread per conference.  Prompts played only to the participant that caused them
  are not affected.  It defaults to yes, which is the previous behavior.

- #### chan_websocket:  Add "unbuffered" mode and fix a few bugs.
  A new `unbuffered` chan_websocket mode has been added which
  sends frames to the core as they're received from the WebSocket and your
  app. It's similar to `passthrough` mode where your app is responsible
  for proper framing and timing except that, assuming you're not using an
  unbufferable codec (opus, speex, g729), in unbuffered mode, you can
  still use START_MEDIA_BUFFERING to send bulk media and have
  chan_websocket frame and time it. When you send STOP_MEDIA_BUFFERING,
  the channel will go back into unbuffered mode. Unbuffered mode is most
  useful when the media your app normally sends to Asterisk is already
  properly framed and timed but you may occasionally need to send bulk
  media. The mode is set with the `u` dialstring option.

- #### pjsip_wizard.conf.sample: Add IP authentication and TLS examples.
  pjsip_wizard.conf.sample now includes examples for an ITSP that
  authenticates by IP address and delivers calls from several hosts, and
  for a TLS trunk with SRTP.

- #### res_pjsip_config_wizard: Fix concurrent Named ACL reloads
  Fixes a PJSIP config wizard reload hang during overlapping reloads.

- #### res_pjsip_config_wizard: Fix documentation errors.
  The pjsip_wizard.conf sample documented the wrong option
  names for sends_auth and accepts_auth, and claimed a default for
  hint_application that does not exist.  Configurations written by
  following the sample would have failed to load.

- #### taskpool: count the in-flight task when selecting an executor
  Taskpool executors that are running a long task are no longer
  considered idle when work is distributed, so tasks are routed to free
  executors instead of queueing behind a busy one.


### Upgrade Notes:

- #### ARI, chan_websocket, res_websocket_client: Handle non-blocking and timeout correctly.
  A new "write_timeout" parameter has been added to
  websocket_client.conf that allows setting the maximum amount of time a
  write operation can take before returning an error.
  A new "write_timeout" parameter has been added to
- #### res_geolocation: Create alembic scripts and squash a SEGV.
  An Alembic script has been added for res_geolocation that creates
  the geoloc_location and geoloc_profile tables.

- #### alembic: Preserve sub-second precision in the queue_log time column.
  (schema) On MySQL and MariaDB the queue_log 'time' column is widened
  from DATETIME to DATETIME(6) so that the microsecond timestamps Asterisk
  already writes are stored rather than discarded. This ALTER rewrites the
  table, a datetime precision change is neither INSTANT nor INPLACE, so
  queue_log writes will block for its duration on a large table. PostgreSQL
  installations are unaffected. Rows already written at whole-second precision will remain as-is and won't be updated.


### Developer Notes:

- #### ARI, chan_websocket, res_websocket_client: Handle non-blocking and timeout correctly.
  A new "write_timeout" field has been added to the
  ast_websocket_client_options structure that allows setting the maximum
  amount of time a write operation can take before returning an error.
  The websocket session must be set to nonblocking for this to take effect.

- #### chan_websocket:  Add "unbuffered" mode and fix a few bugs.
  Two new fields have been added to the chan_websocket
  MEDIA_START event for both the plain text and JSON formats:
      `passthrough: true/false`
      `unbuffered: true/false`
  These indicate the mode the channel was created with and correspond
  to the `p` and `u` dialstring options.
  The MARK_MEDIA chan_websocket command can now be used

### Commit Authors:

- Alexandre Fournier: (1)
- Fran Vicente: (1)
- George Joseph: (5)
- Gian Diego Javes: (2)
- Jacky W J Li: (1)
- Jaco Kroon: (1)
- Jeremy Lainé: (2)
- Maksym Tushkov: (2)
- Mehrdad Seifzadeh: (2)
- Michal Hajek: (1)
- Mike Bradeen: (5)
- Naveen Albert: (4)
- OMAR.A: (1)
- Peter Lemenkov: (1)
- Sean Bright: (3)
- Tinet-mucw: (2)
- aabolfazl: (1)
- fsliwenjie: (1)
- seifzadeh: (1)
- whitetreebug: (2)

## Issue and Commit Detail:

### Closed Issues:

  - #1167: frame.h: ast_frame_adjust_volume* Incorrect documention related to 'adjustment' parameter
  - #1629: [bug]: `ast_streamfile` logs a misleading error based on errno
  - #1723: [improvement]: res_pjsip_endpoint_identifier_ip.c: misleading warning when duplicate IP is added to ACL via DNS resolved match
  - #1760: [bug]: queue incorrectly updates the pause time for realtime members
  - #1927: [bug]: PJSIP OPTIONS not adhering to SRV failover
  - #2044: [bug]: ARI channels.record fails with "Cannot record channel while in bridge" after channels.dial on created channel
  - #2063: [bug]: VM_INFO segfaults when requesting an email address from a mailbox that doesn't have an email address
  - #2069: [bug]: Passing HTML Through FastAGI Causes Asterisk Crash
  - #2072: [bug]: queue_log 'time' column discards the microseconds ast_queue_log() writes on MySQL/MariaDB
  - #2074: [bug]: Taskpool assigns work to an executor that is inside a long task while other executors in the pool are idle
  - #2091: [bug]: func_speex: use-after-free in speex_callback when disabling DENOISE/AGC while audiohook is active
  - #2095: [bug]: MES calculation skips the long-term jitter mean, using only the short-term RFC 3550 EWMA sample
  - #2097: [bug]: effective_latency uses full RTT instead of one-way delay and mixes seconds with milliseconds
  - #2099: [bug]: update_rtt_stats() stores RTT before validating it, exposing invalid values via AMI/ARI
  - #2101: [bug]: Reported packet loss feeds a cumulative counter into the MES mean, drifting the score toward its floor over a call’s duration
  - #2106: [bug]:  res_cdrel_custom JSON output is truncated at 1024 bytes
  - #2110: [bug]: res_resolver_unbound: libunbound API calls that fail should include reason
  - #2115: [bug]: chan_websocket: frame queue never drains, turning any backlog into permanent latency
  - #2121: [bug]: res_websocket_client: ast_websocket_client_connect() does not release lock_obj when retries are exhausted
  - #2123: [bug]: ChanSpy delays spyee channel destroy / Hangup by the spy wait timeout when the spy path has no reverse media
  - #2131: [improvement]: Add an option to disable the ConfBridge announcer channel
  - #2132: [bug]: Bundled pjproject aconfigure fails on PIE-default toolchains — ambient CFLAGS discarded
  - #2138: [improvement]: chan_dahdi: Device state is uncachable, preventing device state synchronization
  - #2140: [bug]: Cascading locks when chan websocket is connected to a server that stops reading packets.
  - #2150: [bug]: Asterisk crashes (SIGSEGV) when CHANNEL(channeltype) is evaluated on a dummy channel
  - #2152: [improvement]: chan_dahdi: Add macros to simplify checks for FXO signaling
  - #2154: [bug]: app_amd: AMD documentation does not match the implementation
  - #2159: [improvement]: chan_dahdi: Avoid INUSE -> RINGING device state transitions for analog lines
  - #2164: [bug]: func_env: Line mode doesn't work for CR LF (DOS) line endings

### Commits By Author:

- **Alexandre Fournier** (1):
  - func_channel: add NULL checks on channel tech for dummy channels

- **Fran Vicente** (1):
  - app_confbridge: Add announcements option to disable conference announcements

- **George Joseph** (5):
  - res_geolocation: Fix alembic scripts for MySQL.
  - ARI, chan_websocket, res_websocket_client: Handle non-blocking and timeout correctly.
  - chan_websocket:  Add "unbuffered" mode and fix a few bugs.
  - res_geolocation: Create alembic scripts and squash a SEGV.
  - res_cdrel_custom: Use extendable ast_str for JSON records.

- **Gian Diego Javes** (2):
  - res_websocket_client: Release lock_obj when retries are exhausted
  - taskpool: count the in-flight task when selecting an executor

- **Jacky W J Li** (1):
  - frame: Correct ast_frame_adjust_volume* documentation of 'adjustment'

- **Jaco Kroon** (1):
  - res_config_odbc: Missing SQLFreeHandle in error paths.

- **Jeremy Lainé** (2):
  - app_amd: Correct and clarify the AMD documentation.
  - main/file.c: Log the reason a file could not be opened.

- **Maksym Tushkov** (2):
  - pjsip_wizard.conf.sample: Add IP authentication and TLS examples.
  - res_pjsip_config_wizard: Fix documentation errors.

- **Mehrdad Seifzadeh** (2):
  - res_resolver_unbound: Include libunbound error reason
  - res_pjsip_endpoint_identifier_ip: Clarify identify DNS warning

- **Michal Hajek** (1):
  - res_pjsip_config_wizard: Fix concurrent Named ACL reloads

- **Mike Bradeen** (5):
  - res_pjsip: Qualify all resolved contact targets
  - res_rtp_asterisk: Fix reported packet loss to use current report's difference
  - res_rtp_asterisk: rtt stored before validation
  - res_rtp_asterisk: Correct effective latency calculation
  - res_rtp_asterisk: Change MES calculation to use long-term jitter mean

- **Naveen Albert** (4):
  - func_env: Fix line counting in FILE function for DOS (CR LF) endings.
  - chan_dahdi: Avoid transitioning device state from INUSE to RINGING.
  - chan_dahdi: Simplify checks for FXO signaling.
  - chan_dahdi: Make DAHDI device state cacheable to fix device state sync.

- **OMAR.A** (1):
  - alembic: Preserve sub-second precision in the queue_log time column.

- **Peter Lemenkov** (1):
  - Bundled pjproject: Do not discard the ambient CFLAGS

- **Sean Bright** (3):
  - res_geolocation: Don't crash on invalid sorcery configuration
  - res_geolocation: Load when explicitly configured in sorcery.conf
  - res_agi.c: Prevent out-of-bounds array access when parsing arguments

- **Tinet-mucw** (2):
  - apps/app_chanspy: don't delay spyee Hangup when spy channel has no reverse media
  - func_speex: Hold channel lock while updating speex state.

- **aabolfazl** (1):
  - app_voicemail: Fix crash when VM_INFO reads an unset email address.

- **fsliwenjie** (1):
  - res_stasis_recording: Allow recording a channel that is dialing via ARI

- **seifzadeh** (1):
  - app_queue: Update lastpause on realtime pause transition

- **whitetreebug** (2):
  - res_prometheus: add toggle config for metrics
  - chore: update .gitignore for clangd

### Commit List:

-  res_geolocation: Fix alembic scripts for MySQL.
-  res_pjsip: Qualify all resolved contact targets
-  func_env: Fix line counting in FILE function for DOS (CR LF) endings.
-  ARI, chan_websocket, res_websocket_client: Handle non-blocking and timeout correctly.
-  chan_dahdi: Avoid transitioning device state from INUSE to RINGING.
-  func_channel: add NULL checks on channel tech for dummy channels
-  chan_dahdi: Simplify checks for FXO signaling.
-  app_amd: Correct and clarify the AMD documentation.
-  apps/app_chanspy: don't delay spyee Hangup when spy channel has no reverse media
-  res_prometheus: add toggle config for metrics
-  chan_dahdi: Make DAHDI device state cacheable to fix device state sync.
-  app_confbridge: Add announcements option to disable conference announcements
-  Bundled pjproject: Do not discard the ambient CFLAGS
-  res_resolver_unbound: Include libunbound error reason
-  app_queue: Update lastpause on realtime pause transition
-  chan_websocket:  Add "unbuffered" mode and fix a few bugs.
-  res_geolocation: Create alembic scripts and squash a SEGV.
-  res_rtp_asterisk: Fix reported packet loss to use current report's difference
-  res_rtp_asterisk: rtt stored before validation
-  res_rtp_asterisk: Correct effective latency calculation
-  res_rtp_asterisk: Change MES calculation to use long-term jitter mean
-  res_websocket_client: Release lock_obj when retries are exhausted
-  frame: Correct ast_frame_adjust_volume* documentation of 'adjustment'
-  res_config_odbc: Missing SQLFreeHandle in error paths.
-  res_cdrel_custom: Use extendable ast_str for JSON records.
-  res_geolocation: Don't crash on invalid sorcery configuration
-  res_geolocation: Load when explicitly configured in sorcery.conf
-  res_stasis_recording: Allow recording a channel that is dialing via ARI
-  alembic: Preserve sub-second precision in the queue_log time column.
-  main/file.c: Log the reason a file could not be opened.
-  chore: update .gitignore for clangd
-  func_speex: Hold channel lock while updating speex state.
-  pjsip_wizard.conf.sample: Add IP authentication and TLS examples.
-  res_pjsip_config_wizard: Fix concurrent Named ACL reloads
-  res_pjsip_endpoint_identifier_ip: Clarify identify DNS warning
-  res_pjsip_config_wizard: Fix documentation errors.
-  taskpool: count the in-flight task when selecting an executor
-  app_voicemail: Fix crash when VM_INFO reads an unset email address.
-  res_agi.c: Prevent out-of-bounds array access when parsing arguments

### Commit Details:

__res_geolocation: Fix alembic scripts for MySQL.__
  Author: George Joseph
  Date:   2026-09-17

  Depending on the backend storage type, MySQL rows could have a maximum
  length of 65535 bytes including storage overhead. The number and size of
  the fixed-size string columns in the geoloc_profile table cause this to
  be exceeded. Unfortunately, this isn't detected until you try to
  actually create the table. To address this, the string columns types
  have been changed to TEXT, which both MySQL and PostgreSQL handle
  without counting to maximum row size. For long, variable-length strings,
  TEXT is also more storage efficient.

  MySQL also had an issue with the way the Enums were created.  These
  were also fixed.

__res_pjsip: Qualify all resolved contact targets__
  Author: Mike Bradeen
  Date:   2026-07-27

  Previously, PJSIP endpoint qualification was offloaded to pjproject which
  used a single, internally selected contact based on DNS results.
  This could result in unused SRV, A, or AAAA targets, leaving a contact
  marked as unreachable without probing the remaining targets.

  Now, we resolve the complete set of records before sending and creating
  OPTIONS transactions for every resolved target.  The contact will be marked
  as reachable as soon as any target succeeds and unreachable only after every
  target fails.

  Fixes: #1927

  UserNote: DNS-backed PJSIP contacts are now considered reachable when any resolved
  SRV, A, or AAAA target responds to qualification.

__func_env: Fix line counting in FILE function for DOS (CR LF) endings.__
  Author: Naveen Albert
  Date:   2026-09-14

  The DOS line counting mode was looking for LF CR, when it should have
  been looking for CR LF. As a result, line mode never worked properly
  for files with DOS (CR LF) line endings, instead erroneously
  triggering an error about the offset being negative.

  This bug has been present since line mode was introduced in
  commit 50d5f134c8d604081c4b9c208a23db9aa97cd560. LF CR is not
  a line ending sequence that exists in any line ending format.

  Swap the order around so that DOS mode works properly.
  Also clarify some of the documentation around FILE operation.

  Resolves: #2164

__ARI, chan_websocket, res_websocket_client: Handle non-blocking and timeout correctly.__
  Author: George Joseph
  Date:   2026-09-03

  This change was prompted by an issue where if the websocket peer to
  which ARI or chan_websocket is connected stops consuming TCP packets,
  its TCP receive buffer will begin to fill and ultimately cause the
  client's TCP send buffer to start filling. When the send buffer is
  completely filled, further writes to the websocket will block. This can
  cause a cascading lock situation with chan_websocket because it sends
  large amounts of data very quickly but can also affect ARI.

  * Added "write_timeout" parameters to websocket_client.conf and
    chan_websocket.conf. ari.conf already had the
    "websocket_write_timeout" parameter. See notes below.

  * Updated chan_websocket to use the write_timeout set in
    chan_websocket.conf for incoming/server connections if set and to use
    AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT (100ms) as the default if not set.

  * Updated chan_websocket to use the write_timeout set in
    websocket_client.conf for outgoing/client connections if set and to use
    write_timeout from chan_websocket.conf if not set.  If neither is set,
    AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT (100ms) is used as the default.

  * Set non-blocking mode on the websocket in chan_websocket for both
    client and server connections. ARI already sets it.  This is required
    for the timeouts to operate correctly.

  * Updated sample config files and XML documentation for all 3 areas.

  Resolves: #2140

  UpgradeNote: A new "write_timeout" parameter has been added to
  websocket_client.conf that allows setting the maximum amount of time a
  write operation can take before returning an error.

  UpgradeNote: A new "write_timeout" parameter has been added to
  chan_websocket.conf setting the maximum amount of time a write operation
  can take before returning an error. Outgoing/client connections can
  override this with the new "write_timeout" parameter that was also added
  to websocket_client.conf.

  DeveloperNote: A new "write_timeout" field has been added to the
  ast_websocket_client_options structure that allows setting the maximum
  amount of time a write operation can take before returning an error.
  The websocket session must be set to nonblocking for this to take effect.

__chan_dahdi: Avoid transitioning device state from INUSE to RINGING.__
  Author: Naveen Albert
  Date:   2026-09-11

  By default, DAHDI relies on the Asterisk code to provide most device
  state, especially for analog lines, in which case it relies entirely
  on the core. When an incoming call arrives to a DAHDI line, it
  immediately asks the core to update device state for the channel.

  This can sometimes lead to unoptimal outcomes; for example, when
  ringing an FXS line (station), the device state is momentarily
  "INUSE" and then almost immediately transitions to "RINGING". However,
  this is incompatible with the state machines of some CPE. For example,
  Polycom IP phones' BLFs will ignore any "RINGING" received after an
  "INUSE". This makes sense because a line can't ring after being in use
  without first being made idle. (The reverse case, RINGING followed by
  INUSE, is allowed, since one can answer a ringing phone.)

  Thus, to ensure that BLFs accurately display the status of DAHDI
  channels, we need to avoid the momentary "INUSE" device state
  for ringing channels.

  Resolves: #2159

__func_channel: add NULL checks on channel tech for dummy channels__
  Author: Alexandre Fournier
  Date:   2026-07-23

  A dummy channel allocated with ast_dummy_channel_alloc() never gets a
  channel technology, so ast_channel_tech() returns NULL for it.

  func_channel_read() dereferences ast_channel_tech(chan)->type for
  CHANNEL(channeltype) without checking the tech for NULL, and
  func_channel_write_real() dereferences it the same way in its fallback
  branch. Both crash with a SIGSEGV on a dummy channel.

  This happens when CHANNEL(channeltype) is present in channelvars of
  ari.conf and the variable is evaluated on a dummy channel, e.g. when
  a voicemail is deposited for a mailbox that has an email address
  configured.

  Guard both dereferences. A dummy channel has no technology, so
  CHANNEL(channeltype) now reads as an empty string and a write falls
  through to the "Unknown or unavailable item requested" warning.

  This is very similar to the issue fixed by
  https://github.com/asterisk/asterisk/pull/1993

  Resolves: #2150

  AI disclaimer: Claude Opus 4.8 was used to find the cause of the crash
  and to find a solution.

  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>

__chan_dahdi: Simplify checks for FXO signaling.__
  Author: Naveen Albert
  Date:   2026-09-09

  Many parts of chan_dahdi and sig_analog check if a channel is
  FXO-signaled (i.e. an FXS channel) by enumerating all 3 types
  of FXO-signaled channels. Add macros to simplify these checks
  and improve code readability (sig_analog already has one for
  FXS-signaled channels).

  Resolves: #2152

__app_amd: Correct and clarify the AMD documentation.__
  Author: Jeremy Lainé
  Date:   2026-09-10

  Fix the miniumWordLength and betweenWordSilence parameter names, which
  did not match the arguments AMD() accepts.

  Document the NOAUDIODATA cause, explain how values are appended to
  AMDCAUSE, and name the parameter each cause reports. MAXWORDLENGTH
  appends a single value, so its phantom second field is removed. Also
  note that both variables are set to the empty string when detection
  cannot be performed, state the millisecond unit once, and describe
  maximumWordLength as continuous voice, which is what the check
  measures.

  Resolves: #2154

__apps/app_chanspy: don't delay spyee Hangup when spy channel has no reverse media__
  Author: Tinet-mucw
  Date:   2026-08-26

  channel_spy() waited indefinitely on the spy channel, so after the spyee
  hung up the spy audiohook leaving RUNNING was not noticed until waitfor
  timed out. That kept an autochan reference on the spyee and delayed its
  destructor / Hangup.

  Poll with a short timeout instead. On timeout, only re-check hook status;
  call ast_read() only when the spy channel is readable.

  Resolves: #2123

__res_prometheus: add toggle config for metrics__
  Author: whitetreebug
  Date:   2026-08-28

  In high‑volume call scenarios, Prometheus exports a large number of channels‑related and bridges-related metrics.Not all users require channels_state, channels_duration and bridges_channels.Add conf options to enable or disable channel_details(covering channels_state, channels_duration) and bridge_details(covering bridges_channels).

  UserNote: Add new config option 'channels_detail_metrics_enabled' to disable export of 'asterisk_channels_state' and 'asterisk_channels_duration_seconds' metrics,It defaults to 'yes' to preserve existing behavior

  Add new config option 'bridges_detail_metrics_enabled' to disable export of 'asterisk_bridges_channels_count' metric, It defaults to 'yes' to preserve existing behavior

  Explicitly enable bridges_detail_metrics_enabled for the bridge_to_string unit‑test.

__chan_dahdi: Make DAHDI device state cacheable to fix device state sync.__
  Author: Naveen Albert
  Date:   2026-09-03

  Commit 8fb5bdce9ab9f7f3758545753cbc787653920753 disabled caching of
  certain device states that weren't associated with real devices in
  order to prevent a resource exhaustion attack from guest/unauthenticated
  connections.

  In most cases, code was converted to remain cacheable; the few exceptions
  had mainly to do with certain paths in channel drivers for guest or
  unauthenticated access. However, everything in chan_dahdi was made
  uncachable for no apparent reason. This is not harmless, since it
  prevents device state synchronization from working (e.g. with
  res_pjsip_publish_asterisk) since device state updates that are
  marked uncachable result in hints on remote systems always showing
  "Unavailable" since uncached device state updates are simply discarded.

  Since DAHDI devices very much correspond to real devices (perhaps more
  so than in any other channel driver), we can safely cache device state
  for DAHDI channels, which also ensures that device state
  synchronization now works properly.

  Resolves: #2138

__app_confbridge: Add announcements option to disable conference announcements__
  Author: Fran Vicente
  Date:   2026-09-02

  Every conference unconditionally created an announcer channel (CBAnn) and a
  taskprocessor thread to play announcements to the conference as a whole, even on
  systems that never play any.  That costs a channel, a thread and a bridge member
  per conference.

  A new bridge profile option, announcements, defaults to yes and preserves the
  previous behavior.  When set to no, no announcer channel is created and nothing
  is played to the conference as a whole: the join and leave sounds, recorded name
  intros, the participant count announced to everyone, sound_begin and
  sound_leader_has_left are all suppressed, and users are never prompted to record
  their name regardless of the announce_join_leave user profile option.

  Prompts played only to the participant that caused them are unaffected, since
  they are played on that participant's own channel.  sound_kicked, sound_muted,
  sound_unmuted, the join sound played back by hear_own_join_sound and the
  participant count requested from the DTMF menu all still play.

  The announcer channel exists for the lifetime of the conference, so the value
  that takes effect is the one from the bridge profile of the channel that creates
  the conference.

  Resolves: #2131

  UserNote: A new ConfBridge bridge profile option, announcements, can be set to
  no to stop announcements from being played to the conference as a whole.  No
  announcer channel is then created, which saves a channel and a taskprocessor
  thread per conference.  Prompts played only to the participant that caused them
  are not affected.  It defaults to yes, which is the previous behavior.

__Bundled pjproject: Do not discard the ambient CFLAGS__
  Author: Peter Lemenkov
  Date:   2026-08-31

  Commit f82393da0dbf125bd96e3c4a0dc080f88d6a90e4 ("Bundled pjproject: Make it
  easier to override options in config_site.h") added

      PJPROJECT_CONFIG_OPTS += CFLAGS="$(PJPROJECT_CFLAGS)"

  Previously CFLAGS was never passed on the aconfigure command line, so the
  bundled pjproject inherited the exported CFLAGS from the environment. Passing
  it explicitly overrides that, and PJPROJECT_CFLAGS contains only the options
  this file adds (-DPJ_HAS_LINUX_EPOLL=1), so every distribution flag is lost.

  Appending to the ambient CFLAGS rather than replacing them keeps the new
  PJPROJECT_CFLAGS override working while restoring inheritance.

  Resolves: #2132

  Signed-off-by: Peter Lemenkov <lemenkov@gmail.com>

__res_resolver_unbound: Include libunbound error reason__
  Author: Mehrdad Seifzadeh
  Date:   2026-09-06

  Failures returned by ub_resolve_async() are currently logged without the
  reason provided by libunbound, making resolver configuration failures
  difficult to diagnose.

  Include ub_strerror() in the error message so the underlying libunbound
  failure reason is available in the Asterisk log.

  Resolves: #2110

__app_queue: Update lastpause on realtime pause transition__
  Author: seifzadeh
  Date:   2026-07-30

  Realtime queue members used lastpause to determine whether a member
  had transitioned from unpaused to paused. However, lastpause also
  stores the timestamp of the member's most recent pause and should not
  be cleared when the member is unpaused.

  Compare the existing paused state with the new realtime value instead,
  and update lastpause only when the member transitions from unpaused to
  paused.

  Resolves: #1760

__chan_websocket:  Add "unbuffered" mode and fix a few bugs.__
  Author: George Joseph
  Date:   2026-08-25

  Besides the User and Developer notes below, checks have been added to
  reject a START_MEDIA_BUFFERING buffering command if it's already
  active and to reject a STOP_MEDIA_BUFFERING command if a corresponding
  START_MEDIA_BUFFERING command was never issued.

  Resolves: #2115

  UserNote: A new `unbuffered` chan_websocket mode has been added which
  sends frames to the core as they're received from the WebSocket and your
  app. It's similar to `passthrough` mode where your app is responsible
  for proper framing and timing except that, assuming you're not using an
  unbufferable codec (opus, speex, g729), in unbuffered mode, you can
  still use START_MEDIA_BUFFERING to send bulk media and have
  chan_websocket frame and time it. When you send STOP_MEDIA_BUFFERING,
  the channel will go back into unbuffered mode. Unbuffered mode is most
  useful when the media your app normally sends to Asterisk is already
  properly framed and timed but you may occasionally need to send bulk
  media. The mode is set with the `u` dialstring option.

  DeveloperNote: Two new fields have been added to the chan_websocket
  MEDIA_START event for both the plain text and JSON formats:
      `passthrough: true/false`
      `unbuffered: true/false`
  These indicate the mode the channel was created with and correspond
  to the `p` and `u` dialstring options.

  DeveloperNote: The MARK_MEDIA chan_websocket command can now be used
  by an app when in "passthrough" mode.

__res_geolocation: Create alembic scripts and squash a SEGV.__
  Author: George Joseph
  Date:   2026-08-31

  If a profile contained a location_reference that pointed to a location that
  didn't exist, a segfault would occur when running the `geoloc show profiles`
  command. The command now prints a notice that there was an issue retrieving
  the profile as well as an ERROR log message with the explanation.

  UpgradeNote: An Alembic script has been added for res_geolocation that creates
  the geoloc_location and geoloc_profile tables.

__res_rtp_asterisk: Fix reported packet loss to use current report's difference__
  Author: Mike Bradeen
  Date:   2026-08-18

  The lost packet calculation was using the cumulative number of lost packets instead
  of the current interval's lost packets.  This lead to the MES caculation skewing
  over time.  We now keep track of the last reported value so we can determine the
  current value vs the cumulative value.

  Fixes: #2101

__res_rtp_asterisk: rtt stored before validation__
  Author: Mike Bradeen
  Date:   2026-08-18

  The rtcp rtt value was updated before being validated.  The calculating function
  was correctly returning an error, but only after setting the value.  This could
  result in AMI and ARI events being generated with an invalid value.

  Fixes: #2099

__res_rtp_asterisk: Correct effective latency calculation__
  Author: Mike Bradeen
  Date:   2026-08-18

  The effective latency calculation was using the full round trip time instead of
  the one-way, which causes the MES score to be worse than it would otherwise.

  Additionally, the rx jitter calculation was not scaling the normdev and stdev
  values to milliseconds, essentially disregarding their values in the calculation

  Fixes: #2097

__res_rtp_asterisk: Change MES calculation to use long-term jitter mean__
  Author: Mike Bradeen
  Date:   2026-08-18

  The MES score used the reported jitter value instead of the smoothed statistic
  value, which could cause a single high anomalous RTCP sample to unduly skew
  the overall score.

  Fixes: #2095

__res_websocket_client: Release lock_obj when retries are exhausted__
  Author: Gian Diego Javes
  Date:   2026-08-25

  ast_websocket_client_connect() takes lock_obj at the top of every pass
  through the retry loop.  When the attempts run out it leaves through the
  break and returns NULL without releasing it, so the calling thread ends
  up holding it.  The other two exits, the ast_calloc failure and the
  success path, do release it.

  The retry path also takes the lock again instead of releasing it before
  sleeping.  The header describes lock_obj as "an ao2 object to lock while
  the connection is being attempted", and the usleep is the wait between
  attempts rather than an attempt, so that is where it should be released.

  chan_websocket.c passes the per-call instance as lock_obj, allocated
  with ao2_alloc and no flags, so it gets the default
  AO2_ALLOC_OPT_LOCK_MUTEX.  When the object is freed, __ao2_ref() reaches
  ast_mutex_destroy() with the mutex still held and frees the memory
  anyway.  There is no deadlock, Asterisk mutexes are recursive, but every
  call that fails to connect logs two ERROR lines.

  With each pass balanced, the lock is held exactly once when the loop
  breaks, so a single unlock before the return is enough.

  Fixes: #2121

__frame: Correct ast_frame_adjust_volume* documentation of 'adjustment'__
  Author: Jacky W J Li
  Date:   2026-08-25

  The documentation for ast_frame_adjust_volume() and
  ast_frame_adjust_volume_float() described the 'adjustment' parameter as a
  dB value. In reality the parameter is a linear gain factor applied
  directly to the audio samples: a positive value multiplies each sample,
  a negative value divides each sample, and 0 leaves the audio unchanged.

  The documentation block has also been reformatted to use the standard
  ' *' Doxygen style (asterisk in the second column) for consistency with
  the rest of the file.

  To convert a dB change to this factor use adjustment = 10^(dB/20)
  (for example, 2 ~= +6 dB and 10 ~= +20 dB).

  Resolves: #1167

__res_config_odbc: Missing SQLFreeHandle in error paths.__
  Author: Jaco Kroon
  Date:   2026-06-18

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

__res_cdrel_custom: Use extendable ast_str for JSON records.__
  Author: George Joseph
  Date:   2026-08-20

  Since JSON records include the field names, the actual record size can be quite
  lengthy and since the thread-local backed ast_str used by the DSV writer can't be
  extended, we need to use a regular, extendable ast_str.

  Resolves: #2106

__res_geolocation: Don't crash on invalid sorcery configuration__
  Author: Sean Bright
  Date:   2026-08-21

  A crash will occur when you've configured object mappings using the
  realtime wizard but neglect to set up your realtime configuration in
  `extconfig.conf`. For example, adding the following to `sorcery.conf`:

      [res_geolocation]
      location = realtime,geo_location
      profile = realtime,geo_profile

  But not defining `geo_location` or `geo_profile` in `extconfig.conf`
  will cause Asterisk to crash on startup.

__res_geolocation: Load when explicitly configured in sorcery.conf__
  Author: Sean Bright
  Date:   2026-08-20

  If you've explicitly configured your configuration objects in
  sorcery.conf, it is normal for `ast_sorcery_apply_default` to return
  something other than
  `AST_SORCERY_APPLY_SUCCESS` (e.g. `AST_SORCERY_APPLY_DEFAULT_UNNECESSARY`).

  This is one of the few places (outside of unit tests) that we actual
  pay attention to the return value of `ast_sorcery_apply_default` at all.

__res_stasis_recording: Allow recording a channel that is dialing via ARI__
  Author: fsliwenjie
  Date:   2026-08-13

  When an ARI application dials a channel with channels.dial, the channel
  is placed into an internal, invisible holding bridge (the dial bridge)
  while dialing. record_file() rejected recording for any channel that was
  in a bridge, so channels.record failed with "Cannot record channel while
  in bridge" even though the application never joined the channel to a
  bridge of its own.

  Narrow the check so it only rejects recording when the channel is in a
  bridge that is NOT flagged invisible. The internal dial bridge is flagged
  AST_BRIDGE_FLAG_INVISIBLE (it is created by ARI internally, not by the
  application), so recording is still permitted there. A "real" bridge
  created by the application - including a user-created ARI holding bridge
  - is not invisible, so recording stays rejected. This keeps the fix
  targeted at the internal dial bridge only, rather than allowing recording
  in every holding bridge.

  Resolves: #2044

__alembic: Preserve sub-second precision in the queue_log time column.__
  Author: OMAR.A
  Date:   2026-08-07

  ast_queue_log() formats realtime queue_log timestamps with microseconds
  ("%F %T.%6q" in main/logger.c), but the queue_log table declares 'time' as
  a generic SQLAlchemy DateTime.  That renders on MySQL and MariaDB as bare
  DATETIME, which has a fractional-seconds precision of 0, so MariaDB
  truncates the fraction and MySQL rounds it.  Every event of a single queue
  entry can collapse onto one second, or move to the next one, and neither
  server warns, not even in strict mode.

  Once that happens, queue_log rows can no longer be ordered against each
  other, or against the CEL records that cel_odbc and cel_pgsql do stamp
  with microseconds.

  This adds a revision that widens the queue_log column to DATETIME(6) on
  MySQL and MariaDB.  PostgreSQL is unaffected and left untouched: DateTime
  already renders as TIMESTAMP, which keeps microseconds by default.

  Fixes: #2072

  UpgradeNote: (schema) On MySQL and MariaDB the queue_log 'time' column is widened
  from DATETIME to DATETIME(6) so that the microsecond timestamps Asterisk
  already writes are stored rather than discarded. This ALTER rewrites the
  table, a datetime precision change is neither INSTANT nor INPLACE, so
  queue_log writes will block for its duration on a large table. PostgreSQL
  installations are unaffected. Rows already written at whole-second precision will remain as-is and won't be updated.

__main/file.c: Log the reason a file could not be opened.__
  Author: Jeremy Lainé
  Date:   2026-08-18

  When ast_streamfile() failed to open a file it reported strerror(errno),
  but errno was not set by anything on that path.  The value came from
  whatever syscall ran last, so the message routinely blamed an unrelated
  error.

  Drop errno from that message and log at the point of failure instead:
  filehelper() now reports the fopen() error, which does carry a meaningful
  errno, and an allocation failure from get_filestream().  Candidate files
  whose format does not match the channel are logged at debug level only,
  since filehelper() walks every registered format and skipping the ones
  that do not match is normal: a sound installed in several formats would
  otherwise warn on every successful playback.

  Failures inside the format module's open callback stay unlogged here,
  as those callbacks already report their own reason.

  Resolves: #1629

__chore: update .gitignore for clangd__
  Author: whitetreebug
  Date:   2026-08-18

  Ignore clangd generated artifacts: compile_commands.json, .cache

  Ignores for .clangd, .clang-format, .clang-tidy tooling files

__func_speex: Hold channel lock while updating speex state.__
  Author: Tinet-mucw
  Date:   2026-08-17

  speex_write() previously unlocked the channel immediately after
  looking up the speex datastore, then continued to create, modify,
  or destroy SpeexPreprocessState and related direction data without
  the lock. speex_callback() runs from the media path with the channel
  already locked, so concurrent Set(DENOISE)/Set(AGC) could free or
  mutate that state while preprocess was running and crash inside
  speex_preprocess_run().
  Keep the channel locked for the full configuration update, unlock
  before ast_audiohook_attach()/detach(), and hold the lock across
  speex_read() while copying values out of the datastore.

  Fixes: #2091

__pjsip_wizard.conf.sample: Add IP authentication and TLS examples.__
  Author: Maksym Tushkov
  Date:   2026-08-12

  The only ITSP example in the file shows a single trunk that registers and
  authenticates.  Many providers, including DIDWW which motivated this
  change, instead authenticate by IP address, deliver inbound calls from
  several regional POPs, and terminate outbound calls through a separate
  gateway that does require digest authentication.  Several things about
  configuring that with the wizard are not obvious from the option
  reference:

  * remote_hosts alone generates the per-host identify matches, and is
    mandatory when registrations are not accepted.
  * Inbound and outbound need one wizard each, because a wizard has a
    single remote_hosts list.
  * A template carrying "type = wizard" is not turned into objects itself,
    so the two can share one.

  TLS has a pitfall of its own: the wizard cannot create transport objects,
  so the transport has to be defined in pjsip.conf and referenced here by
  name, while media_encryption belongs to the endpoint and needs the
  endpoint/ prefix.

  Add commented-out examples for an IP authenticated trunk pair sharing a
  template, and for a TLS trunk with SRTP.

  No functional change.

  UserNote: pjsip_wizard.conf.sample now includes examples for an ITSP that
  authenticates by IP address and delivers calls from several hosts, and
  for a TLS trunk with SRTP.

__res_pjsip_config_wizard: Fix concurrent Named ACL reloads__
  Author: Michal Hajek
  Date:   2026-06-17

  Named ACL change notifications reloaded PJSIP sorcery directly from the
  Stasis callback. Under concurrent ACL, core, and config wizard reloads, that
  could overlap with config wizard object type observers that compare, prune,
  destroy, and replace the per-object-type last_config cache.

  Route the ACL-triggered reload through the PJSIP servant path and track when
  a Named ACL reload is pending or running so the observer still bypasses the
  unchanged-file optimization. Also serialize the observer cache update to
  prevent overlapping reload paths from mutating last_config at the same time.

  UserNote: Fixes a PJSIP config wizard reload hang during overlapping reloads.

__res_pjsip_endpoint_identifier_ip: Clarify identify DNS warning__
  Author: Mehrdad Seifzadeh
  Date:   2026-06-04

  When two DNS-based identify matches resolve to the same address, the first
  match makes the resolved address available for identify matching and the
  second match adds no new match address. The existing warning treated this as
  a DNS resolution failure and reported that the hostname did not resolve.

  Clarify the warning path so a hostname that resolves but only returns
  addresses already present for matching is not reported as unresolved.

  Fixes: #1723

__res_pjsip_config_wizard: Fix documentation errors.__
  Author: Maksym Tushkov
  Date:   2026-08-10

  The sample configuration and the module's XML documentation contained
  several errors, two of which actively misled users:

  * The sample said that sends_auth and accepts_auth require
    "outbound/username" and "inbound/username".  The wizard looks for
    "outbound_auth/username" and "inbound_auth/username", so a
    configuration written by following the sample fails to load with
    "Wizard 'xxx' must have 'outbound_auth/username' if it sends
    authentication."

  * The sample claimed that hint_application defaults to "Dial(${HINT})".
    There is no such default.  When hint_application is not specified the
    wizard removes the priority 1 extension it manages rather than
    creating one.

  The remaining corrections are a reversed object/field reference
  (match/identify), a leftover line in the hint_exten description, an
  option name (send_registrations), a stray space (remote _hosts), an
  unbalanced quote in a synopsis, a misspelling (nneds), an unbalanced
  bracket in the CLI usage text, and a copy/paste error in a log message
  that referred to the "sangoma wizard" instead of pjsip_wizard.  Two
  "(default = ...)" annotations became "(default: ...)" to match the
  other fourteen in the same file.

  No functional change.

  UserNote: The pjsip_wizard.conf sample documented the wrong option
  names for sends_auth and accepts_auth, and claimed a default for
  hint_application that does not exist.  Configurations written by
  following the sample would have failed to load.

__taskpool: count the in-flight task when selecting an executor__
  Author: Gian Diego Javes
  Date:   2026-08-07

  The taskpool selectors measure load with ast_taskprocessor_size(), which
  only reports queued tasks. A taskprocessor that is executing a long
  running task has an empty queue, so it looks identical to an idle one and
  the least full selector hands it more work while other executors sit free.

  Add ast_taskprocessor_is_executing() and include the in-flight task in the
  load the selectors compare.

  This is not a new semantic. taskprocessor_push() already counts the
  in-flight task as outstanding work when it notifies the listener:

  	/* The currently executing task counts as still in queue */
  	was_empty = tps->executing ? 0 : previous_size == 0;

  The taskpool selectors are the one place that still measures only the
  queue. This change applies the same rule there.

  The accessor reads tps->executing without the object lock, the way the CLI
  already does in main/taskprocessor.c, because taking the lock on every
  push cost roughly six times the throughput: taskpool push efficiency went
  from 324438 to 52966 tasks per second and the serializer variant from
  217467 to 38313. Without the lock both are back to baseline. A stale read
  only means one push is routed as if the taskprocessor had just changed
  state, which the selector already tolerates.

  Also add a unit test that holds one executor of a four executor pool
  inside a long task and checks that a task pushed to a different
  serializer runs on one of the three free executors. It fails on every run
  without this change.

  Fixes: #2074

  UserNote: Taskpool executors that are running a long task are no longer
  considered idle when work is distributed, so tasks are routed to free
  executors instead of queueing behind a busy one.

__app_voicemail: Fix crash when VM_INFO reads an unset email address.__
  Author: aabolfazl
  Date:   2026-08-09

  The email member of struct ast_vm_user is a pointer rather than a fixed
  array, and populate_defaults() leaves it NULL when a mailbox has no
  email address configured. Every other attribute VM_INFO reads is a fixed
  array, so only the email attribute is affected.

  VM_INFO passed vmu->email straight to ast_copy_string(), which
  dereferences its source unconditionally. Reading the email attribute of
  a mailbox that has no email address therefore crashed Asterisk from the
  dialplan.

  Guard the copy with S_OR() so an unset email address yields an empty
  string, matching how the language attribute already handles its
  fallback. The make_email_file() call sites were already guarded and are
  left alone.

  Add a regression test for the unset case, and restore the voicemail
  configuration when the VM_INFO test finishes. That test was the only one
  in app_voicemail that did not do so, which left its test mailbox in the
  users list and made the test fail if it ran a second time.

  Fixes: #2063

__res_agi.c: Prevent out-of-bounds array access when parsing arguments__
  Author: Sean Bright
  Date:   2026-08-08

  The `parse_args(...)` function writes to the `argv` array in 2
  locations but was only bounds checking in one of them.

  Moved the bounds check so that it is encountered on each iteration
  through the parsing loop.

  Resolves: #2069


