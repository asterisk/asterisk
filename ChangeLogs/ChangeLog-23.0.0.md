
## Change Log for Release asterisk-23.0.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-23.0.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/23.0.0-pre1...23.0.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-23.0.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 45
- Commit Authors: 14
- Issues Resolved: 36
- Security Advisories Resolved: 1
  - [GHSA-64qc-9x89-rx5j](https://github.com/asterisk/asterisk/security/advisories/GHSA-64qc-9x89-rx5j): A specifically malformed Authorization header in an incoming SIP request can cause Asterisk to crash

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


### Upgrade Notes:

- #### config.c Make ast_variable_update update last match.                            
  Config variables, when set/updated, such as via AMI,
  will now have the corresponding setting updated, even if their
  sections inherit from template sections.

- #### config.c: Make ast_variable_retrieve return last match.                         
  Config variables retrieved explicitly by name now return
  the most recently overriding value as opposed to the base value (e.g.
  from a template). This is equivalent to retrieving a config setting
  using the -1 index to the AST_CONFIG function. The major implication of
  this is that modules processing configs by explicitly retrieving variables
  by name will now get the effective value of a variable as overridden in
  a config rather than the first-set value (from a template), which is
  consistent with how other modules load config settings.

- #### users.conf: Remove deprecated users.conf integration.                           
  users.conf has been removed and all channel drivers must
  be configured using their specific configuration files. The functionality
  previously in users.conf for res_phoneprov is now in phoneprov_users.conf.

- #### res_agi: Remove deprecated DeadAGI application.                                 
  The DeadAGI application, which was
  deprecated in Asterisk 15, has now been removed.
  The same functionality is available in the AGI app.

- #### res_musiconhold: Remove options that were deprecated in Asterisk 14.            
  The deprecated random and application=r options have
  been removed; use sort=random instead.

- #### app_voicemail: Remove deprecated options.                                       
  The deprecated maxmessage and minmessage options
  have been removed; use maxsecs and minsecs instead.
  The deprecated 'cz' language has also been removed; use 'cs' instead.

- #### app_queue: Remove redundant/deprecated function.                                
  The deprecated QUEUE_MEMBER_COUNT function
  has been removed; use QUEUE_MEMBER(<queue>,logged) instead.

- #### cli.c: Remove deprecated and redundant CLI command.                             
  The deprecated "no debug channel" command has
  now been removed; use "core set debug channel" instead.

- #### logger.c: Remove deprecated/redundant configuration option.                     
  The deprecated rotatetimestamp option has been removed.
  Use rotatestrategy instead.

- #### func_dialplan: Remove deprecated/redundant function.                            
  The deprecated VALID_EXTEN function has been removed.
  Use DIALPLAN_EXISTS instead.


### Developer Notes:

- #### ARI: Add command to indicate progress to a channel                              
  A new ARI endpoint is available at `/channels/{channelId}/progress` to indicate progress to a channel.


### Commit Authors:

- Alexei Gradinari: (1)
- Alexey Khabulyak: (1)
- Allan Nathanson: (1)
- Artem Umerov: (1)
- Ben Ford: (2)
- George Joseph: (7)
- Igor Goncharovsky: (2)
- Joe Garlick: (1)
- Jose Lopes: (1)
- Mike Bradeen: (1)
- Naveen Albert: (23)
- Sean Bright: (2)
- Stuart Henderson: (1)
- Sven Kube: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-64qc-9x89-rx5j: A specifically malformed Authorization header in an incoming SIP request can cause Asterisk to crash
  - 244: [bug]: config.c: Template inheritance is incorrect for ast_variable_retrieve
  - 258: [deprecation]: Remove deprecated DeadAGI application.
  - 401: [bug]: app_dial: Answer Gosub option passthrough regression
  - 960: [bug]: config.c: Template inheritance is not respected by ast_variable_update
  - 1147: [bug]: Commit 3cab4e7a to config.c causes segfaults and spinloops in UpdateConfig mgr action
  - 1222: [bug]: func_callerid: ANI2 is not always formatted as two digits
  - 1260: [bug]: Asterisk sends RTP audio stream before ICE/DTLS completes
  - 1289: [bug]: sorcery - duplicate objects from multiple backends and backend divergence on update
  - 1292: [improvement]: Remove deprecated users.conf
  - 1296: [improvement]: res_musiconhold: Remove deprecated options
  - 1298: [improvement]: app_voicemail: Remove deprecated options
  - 1327: [bug]: res_stasis_device_state: can't delete ARI Devicestate after asterisk restart
  - 1341: [improvement]: app_queue: Remove deprecated QUEUE_MEMBER_COUNT function
  - 1343: [improvement]: cli.c: Remove deprecated/redundant CLI command.
  - 1345: [improvement]: logger.c: Remove deprecated/redundant config option
  - 1347: [improvement]: func_dialplan: Remove deprecated/redundant function
  - 1352: [improvement]: Websocket channel with custom URI
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
  - 1451: [bug]: ast_config_text_file_save2(): incorrect handling of deep/wide template inheritance
  - 1457: [bug]: segmentation fault because of a wrong ari config
  - 1462: [bug]: chan_websocket isn't handling the "opus" codec correctly.
  - 1474: [bug]: Media doesn't flow for video conference after res_rtp_asterisk change to stop media flow before DTLS completes
  - ASTERISK-30370: config: Template inheritance is incorrect for ast_variable_retrieve

### Commits By Author:

- #### Alexei Gradinari (1):
  - sorcery: Prevent duplicate objects and ensure missing objects are created on u..

- #### Alexey Khabulyak (1):
  - pbx_lua.c: segfault when pass null data to term_color function

- #### Artem Umerov (1):
  - Fix missing ast_test_flag64 in extconf.c

- #### Ben Ford (1):
  - res_rtp_asterisk: Don't send RTP before DTLS has negotiated.

- #### George Joseph (7):
  - xmldoc.c: Fix rendering of CLI output.
  - chan_websocket: Fix buffer overrun when processing TEXT websocket frames.
  - chan_websocket: Allow additional URI parameters to be added to the outgoing URI.
  - res_pjsip_authenticator_digest: Fix SEGV if get_authorization_hdr returns NULL.
  - res_ari: Ensure outbound websocket config has a websocket_client_id.
  - chan_websocket: Fix codec validation and add passthrough option.
  - res_rtp_asterisk.c: Use rtp->dtls in __rtp_sendto when rtcp mux is used.

- #### Igor Goncharovsky (2):
  - app_waitforsilence.c: Use milliseconds to calculate timeout time
  - app_queue.c: Add new global 'log_unpause_on_reason_change'

- #### Joe Garlick (1):
  - chan_websocket.c: Add DTMF messages

- #### Jose Lopes (1):
  - res_stasis_device_state: Fix delete ARI Devicestates after asterisk restart.

- #### Naveen Albert (11):
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

- #### Stuart Henderson (1):
  - app_queue: fix comparison for announce-position-only-up

- #### Sven Kube (1):
  - ARI: Add command to indicate progress to a channel


### Commit List:

-  Prepare for Asterisk 23
-  config.c Make ast_variable_update update last match.
-  config.c: Make ast_variable_retrieve return last match.
-  utils: Remove libdb and astdb conversion scripts.
-  config.c: Fix inconsistent pointer logic in ast_variable_update.
-  channel: Deprecate `ast_moh_cleanup(...)`.
-  func_callerid: Always format ANI2 as two digits.
-  users.conf: Remove deprecated users.conf integration.
-  res_agi: Remove deprecated DeadAGI application.
-  res_musiconhold: Remove options that were deprecated in Asterisk 14.
-  app_voicemail: Remove deprecated options.
-  app_queue: Remove redundant/deprecated function.
-  cli.c: Remove deprecated and redundant CLI command.
-  logger.c: Remove deprecated/redundant configuration option.
-  func_dialplan: Remove deprecated/redundant function.
-  Update version for Asterisk 23
-  config.c: fix saving of deep/wide template configurations
-  res_rtp_asterisk.c: Use rtp->dtls in __rtp_sendto when rtcp mux is used.
-  chan_websocket: Fix codec validation and add passthrough option.
-  res_ari: Ensure outbound websocket config has a websocket_client_id.
-  chan_websocket.c: Add DTMF messages
-  app_queue.c: Add new global 'log_unpause_on_reason_change'
-  app_waitforsilence.c: Use milliseconds to calculate timeout time
-  res_rtp_asterisk: Don't send RTP before DTLS has negotiated.
-  Fix missing ast_test_flag64 in extconf.c
-  pbx_builtins: Allow custom tone for WaitExten.
-  res_tonedetect: Add option for TONE_DETECT detection to auto stop.
-  app_queue: fix comparison for announce-position-only-up
-  res_pjsip_authenticator_digest: Fix SEGV if get_authorization_hdr returns NULL.
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

### Commit Details:

#### Prepare for Asterisk 23
  Author: Mike Bradeen
  Date:   2024-08-14


#### config.c Make ast_variable_update update last match.
  Author: Naveen Albert
  Date:   2024-10-23

  ast_variable_update currently sets the first match for a variable, as
  opposed to the last one. This issue is complementary to that raised
  in #244.

  This is incorrect and results in the wrong (or no) action being taken
  in cases where a section inherits from a template section. When the
  traversal occurs to update the setting, the existing code erroneously
  would use the first of possibly multiple matches in its update logic,
  which is wrong. Now, explicitly use the last match in the traversal,
  which will ensure that the actual setting is updated properly, and
  not skipped or ignored because a template from which the setting's
  section inherits was used for comparison.

  Resolves: #960

  UpgradeNote: Config variables, when set/updated, such as via AMI,
  will now have the corresponding setting updated, even if their
  sections inherit from template sections.

#### config.c: Make ast_variable_retrieve return last match.
  Author: Naveen Albert
  Date:   2023-08-09

  ast_variable_retrieve currently returns the first match
  for a variable, as opposed to the last one. This is problematic
  because modules that load config settings by explicitly
  calling ast_variable_retrieve on a variable name (as opposed
  to iterating through all the directives as specified) will
  end up taking the first specified value, such as the default
  value from the template rather than the actual effective value
  in an individual config section, leading to the wrong config.

  This fixes this by making ast_variable_retrieve return the last
  match, or the most recently overridden one, as the effective setting.
  This is similar to what the -1 index in the AST_CONFIG function does.

  There is another function, ast_variable_find_last_in_list, that does
  something similar. However, it's a slightly different API, and it
  sees virtually no usage in Asterisk. ast_variable_retrieve is what
  most things use so this is currently the relevant point of breakage.

  In practice, this is unlikely to cause any breakage, since there
  would be no logical reason to use an inherited value rather than
  an explicitly overridden value when loading a config.

  ASTERISK-30370 #close

  Resolves: #244

  UpgradeNote: Config variables retrieved explicitly by name now return
  the most recently overriding value as opposed to the base value (e.g.
  from a template). This is equivalent to retrieving a config setting
  using the -1 index to the AST_CONFIG function. The major implication of
  this is that modules processing configs by explicitly retrieving variables
  by name will now get the effective value of a variable as overridden in
  a config rather than the first-set value (from a template), which is
  consistent with how other modules load config settings.

#### utils: Remove libdb and astdb conversion scripts.
  Author: Sean Bright
  Date:   2025-01-29

  These were included with Asterisk 10 when we switched astdb from libdb
  to sqlite3.

#### config.c: Fix inconsistent pointer logic in ast_variable_update.
  Author: Naveen Albert
  Date:   2025-03-06

  Commit 3cab4e7ab4a3ae483430d5f5e8fa167d02a8128c introduced a
  regression by causing the wrong pointers to be used in certain
  (more complex) cases. We now take care to ensure the exact
  same pointers are used as before that commit, and simplify
  by eliminating the unnecessary second for loop.

  Resolves: #1147

#### channel: Deprecate `ast_moh_cleanup(...)`.
  Author: Sean Bright
  Date:   2025-04-08

  We don't want anyone calling it but the channel destructor.

#### func_callerid: Always format ANI2 as two digits.
  Author: Naveen Albert
  Date:   2025-04-26

  ANI II is always supposed to be formatted as two digits,
  so zero pad when formatting it if necessary.

  Resolves: #1222

#### users.conf: Remove deprecated users.conf integration.
  Author: Naveen Albert
  Date:   2025-07-09

  users.conf was deprecated in Asterisk 21 and is now being removed
  for Asterisk 23, in accordance with the Asterisk deprecation policy.

  This consists of:
  * Removing integration with app_directory, app_voicemail, chan_dahdi,
    chan_iax2, and AMI.
  * users.conf was also partially used for res_phoneprov, and this remaining
    functionality is consolidated to a separate phoneprov_users.conf,
    used only by res_phoneprov.

  Resolves: #1292

  UpgradeNote: users.conf has been removed and all channel drivers must
  be configured using their specific configuration files. The functionality
  previously in users.conf for res_phoneprov is now in phoneprov_users.conf.

#### res_agi: Remove deprecated DeadAGI application.
  Author: Naveen Albert
  Date:   2023-08-11

  DeadAGI was deprecated 7 years ago, in Asterisk 15,
  as it duplicates functionality in the AGI app.
  This removes the application.

  Resolves: #258

  UpgradeNote: The DeadAGI application, which was
  deprecated in Asterisk 15, has now been removed.
  The same functionality is available in the AGI app.

#### res_musiconhold: Remove options that were deprecated in Asterisk 14.
  Author: Naveen Albert
  Date:   2025-07-09

  Commit 9c1f34c7e904b26bb550f426020635894cb805ac added dedicated options
  for random sorting functionality and deprecated older options that
  now duplicated these capabilities. Remove these deprecated options.

  Resolves: #1296

  UpgradeNote: The deprecated random and application=r options have
  been removed; use sort=random instead.

#### app_voicemail: Remove deprecated options.
  Author: Naveen Albert
  Date:   2025-07-10

  Remove the deprecated maxmessage and minmessage options,
  which have been superseded by maxsecs and minsecs since 1.6.
  Also remove the deprecated 'cz' language option (deprecated
  since 1.8.)

  Resolves: #1298

  UpgradeNote: The deprecated maxmessage and minmessage options
  have been removed; use maxsecs and minsecs instead.
  The deprecated 'cz' language has also been removed; use 'cs' instead.

#### app_queue: Remove redundant/deprecated function.
  Author: Naveen Albert
  Date:   2025-08-07

  QUEUE_MEMBER_COUNT has been deprecated since at least 1.6,
  for fully duplicating functionality available in the
  QUEUE_MEMBER function; remove it now.

  Resolves: #1341

  UpgradeNote: The deprecated QUEUE_MEMBER_COUNT function
  has been removed; use QUEUE_MEMBER(<queue>,logged) instead.

#### cli.c: Remove deprecated and redundant CLI command.
  Author: Naveen Albert
  Date:   2025-08-07

  The "no debug channel" command has been deprecated since
  1.6 (commit 691363656fbdc83edf04b125317aebae6525c9e7),
  as it is replaced by "core set debug channel", which also
  supports tab-completion on channels. Remove the redundant
  command.

  Resolves: #1343

  UpgradeNote: The deprecated "no debug channel" command has
  now been removed; use "core set debug channel" instead.

#### logger.c: Remove deprecated/redundant configuration option.
  Author: Naveen Albert
  Date:   2025-08-07

  Remove the deprecated 'rotatetimestamp' config option, as this
  was deprecated by 'rotatestrategy' in 1.6 by commit
  f5a14167f3ef090f8576da3070ed5c452fa01e44.

  Resolves: #1345

  UpgradeNote: The deprecated rotatetimestamp option has been removed.
  Use rotatestrategy instead.

#### func_dialplan: Remove deprecated/redundant function.
  Author: Naveen Albert
  Date:   2025-08-07

  Remove VALID_EXTEN, which was deprecated/superseded by DIALPLAN_EXISTS
  in Asterisk 11 (commit 8017b65bb97c4226ca7a3c7c944a9811484e0305),
  as DIALPLAN_EXISTS does the same thing and is more flexible.

  Resolves: #1347

  UpgradeNote: The deprecated VALID_EXTEN function has been removed.
  Use DIALPLAN_EXISTS instead.

#### Update version for Asterisk 23
  Author: Ben Ford
  Date:   2025-08-13


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

#### res_rtp_asterisk: Don't send RTP before DTLS has negotiated.
  Author: Ben Ford
  Date:   2025-08-04

  There was no check in __rtp_sendto that prevented Asterisk from sending
  RTP before DTLS had finished negotiating. This patch adds logic to do
  so.

  Fixes: #1260

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

#### res_pjsip_authenticator_digest: Fix SEGV if get_authorization_hdr returns NULL.
  Author: George Joseph
  Date:   2025-08-28

  In the highly-unlikely event that get_authorization_hdr() couldn't find an
  Authorization header in a request, trying to get the digest algorithm
  would cauase a SEGV.  We now check that we have an auth header that matches
  the realm before trying to get the algorithm from it.

  Resolves: #GHSA-64qc-9x89-rx5j

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

