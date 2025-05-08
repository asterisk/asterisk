
## Change Log for Release asterisk-22.4.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.4.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.3.0...22.4.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.4.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 24
- Commit Authors: 18
- Issues Resolved: 12
- Security Advisories Resolved: 0

### User Notes:

- #### stasis/control.c: Set Hangup Cause to No Answer on Dial timeout                 
  A Dial timeout on POST /channels/{channelId}/dial will now result in a
  CANCEL and ChannelDestroyed with cause 19 / User alerting, no answer.  Previously
  no explicit cause was set, resulting in a cause of 16 / Normal Call Clearing.

- #### contrib: Add systemd service and timer files for malloc trim.                   
  Service and timer files for systemd have been added to the
  contrib/systemd/ directory. If you are experiencing memory issues,
  install these files to have "malloc trim" periodically run on the
  system.

- #### Add log-caller-id-name option to log Caller ID Name in queue log                
  This patch adds a global configuration option, log-caller-id-name, to queues.conf
  to control whether the Caller ID name is logged as parameter 4 when a call enters a queue.
  When log-caller-id-name=yes, the Caller ID name is included in the queue log,
  Any '|' characters in the caller ID name will be replaced with '_'.
  (provided it’s allowed by the existing log_restricted_caller_id rules).
  When log-caller-id-name=no (the default), the Caller ID name is omitted.

- #### asterisk.c: Add "pre-init" and "pre-module" capability to cli.conf.             
  In cli.conf, you can now define startup commands that run before
  core initialization and before module initialization.

- #### audiosocket: added support for DTMF frames                                      
  The AudioSocket protocol now forwards DTMF frames with
  payload type 0x03. The payload is a 1-byte ascii representing the DTMF
  digit (0-9,*,#...).


### Upgrade Notes:

- #### ARI: REST over Websocket                                                        
  This commit adds the ability to make ARI REST requests over the same
  websocket used to receive events.
  See https://docs.asterisk.org/Configuration/Interfaces/Asterisk-REST-Interface-ARI/ARI-REST-over-WebSocket/


### Commit Authors:

- Albrecht Oster: (1)
- Alexei Gradinari: (1)
- Allan Nathanson: (1)
- Andreas Wehrmann: (1)
- Ben Ford: (1)
- Florent CHAUVEAU: (1)
- George Joseph: (4)
- Joshua C. Colp: (1)
- Luz Paz: (1)
- Mark Murawski: (1)
- Mike Bradeen: (1)
- Mkmer: (1)
- Naveen Albert: (3)
- Norm Harrison: (2)
- Peter Jannesen: (1)
- Phoneben: (1)
- Sean Bright: (1)
- Zhai Liangliang: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 505: [bug]: res_pjproject: ast_sockaddr_cmp() always fails on sockaddrs created by ast_sockaddr_from_pj_sockaddr()
  - 643: [new-feature]: pjsip show contact -- show all details same as AMI PJSIPShowContacts
  - 963: [bug]: missing hangup cause for ARI ChannelDestroyed when Dial times out
  - 1091: [improvement]: app queue :add to  queue log callerid name
  - 1144: [bug]: action_redirect don't remove bridge_after_goto data
  - 1171: [improvement]: Need the capability in audiohook.c for fractional (float) type volume adjustments.
  - 1181: [bug]: Incorrect PJSIP Endpoint Device States on Multiple Channels
  - 1190: [bug]: Crash when starting ConfBridge recording over CLI and AMI
  - 1197: [bug]: ChannelHangupRequest does not show cause code in all cases
  - 1206: [improvement]: chan_iax2: Minor improvements to documentation and warning messages.
  - 1220: [bug]: res_pjsip_caller_id: OLI is not parsed if contained in a URI parameter
  - 1224: [improvement]: app_meetme: Removal version is incorrect

### Commits By Author:

- #### Albrecht Oster (1):
  - res_pjproject: Fix DTLS client check failing on some platforms

- #### Alexei Gradinari (1):
  - chan_pjsip: set correct Endpoint Device State on multiple channels

- #### Allan Nathanson (1):
  - file.c: missing "custom" sound files should not generate warning logs

- #### Andreas Wehrmann (1):
  - pbx_ael: unregister AELSub application and CLI commands on module load failure

- #### Ben Ford (1):
  - contrib: Add systemd service and timer files for malloc trim.

- #### Florent CHAUVEAU (1):
  - audiosocket: added support for DTMF frames

- #### George Joseph (4):
  - ARI: REST over Websocket
  - ari_websockets: Fix frack if ARI config fails to load.
  - asterisk.c: Add "pre-init" and "pre-module" capability to cli.conf.
  - Prequisites for ARI Outbound Websockets

- #### Joshua C. Colp (1):
  - channel: Always provide cause code in ChannelHangupRequest.

- #### Luz Paz (1):
  - docs: Fix typos in apps/

- #### Mark Murawski (1):
  - chan_pjsip:  Add the same details as PJSIPShowContacts to the CLI via 'pjsip s..

- #### Mike Bradeen (1):
  - stasis/control.c: Set Hangup Cause to No Answer on Dial timeout

- #### Naveen Albert (3):
  - chan_iax2: Minor improvements to documentation and warning messages.
  - app_meetme: Remove inaccurate removal version from xmldocs.
  - res_pjsip_caller_id: Also parse URI parameters for ANI2.

- #### Norm Harrison (2):
  - audiosocket: fix timeout, fix dialplan app exit, server address in logs
  - asterisk/channel.h: fix documentation for 'ast_waitfor_nandfds()'

- #### Peter Jannesen (1):
  - action_redirect: remove after_bridge_goto_info

- #### Sean Bright (1):
  - app_confbridge: Prevent crash when publishing channel-less event.

- #### Zhai Liangliang (1):
  - Update config.guess and config.sub

- #### mkmer (1):
  - audiohook.c: Add ability to adjust volume with float

- #### phoneben (1):
  - Add log-caller-id-name option to log Caller ID Name in queue log


### Commit List:

-  res_pjsip_caller_id: Also parse URI parameters for ANI2.
-  app_meetme: Remove inaccurate removal version from xmldocs.
-  docs: Fix typos in apps/
-  stasis/control.c: Set Hangup Cause to No Answer on Dial timeout
-  chan_iax2: Minor improvements to documentation and warning messages.
-  pbx_ael: unregister AELSub application and CLI commands on module load failure
-  res_pjproject: Fix DTLS client check failing on some platforms
-  Prequisites for ARI Outbound Websockets
-  contrib: Add systemd service and timer files for malloc trim.
-  action_redirect: remove after_bridge_goto_info
-  channel: Always provide cause code in ChannelHangupRequest.
-  Add log-caller-id-name option to log Caller ID Name in queue log
-  asterisk.c: Add "pre-init" and "pre-module" capability to cli.conf.
-  app_confbridge: Prevent crash when publishing channel-less event.
-  ari_websockets: Fix frack if ARI config fails to load.
-  ARI: REST over Websocket
-  audiohook.c: Add ability to adjust volume with float
-  audiosocket: added support for DTMF frames
-  asterisk/channel.h: fix documentation for 'ast_waitfor_nandfds()'
-  audiosocket: fix timeout, fix dialplan app exit, server address in logs
-  Update config.guess and config.sub
-  chan_pjsip: set correct Endpoint Device State on multiple channels
-  file.c: missing "custom" sound files should not generate warning logs

### Commit Details:

#### res_pjsip_caller_id: Also parse URI parameters for ANI2.
  Author: Naveen Albert
  Date:   2025-04-26

  If the isup-oli was sent as a URI parameter, rather than a header
  parameter, it was not being parsed. Make sure we parse both if
  needed so the ANI2 is set regardless of which type of parameter
  the isup-oli is sent as.

  Resolves: #1220

#### app_meetme: Remove inaccurate removal version from xmldocs.
  Author: Naveen Albert
  Date:   2025-04-26

  app_meetme is deprecated but wasn't removed as planned in 21,
  so remove the inaccurate removal version.

  Resolves: #1224

#### docs: Fix typos in apps/
  Author: Luz Paz
  Date:   2025-04-09

  Found via codespell


#### stasis/control.c: Set Hangup Cause to No Answer on Dial timeout
  Author: Mike Bradeen
  Date:   2025-04-17

  Other Dial operations (dial, app_dial) use Q.850 cause 19 when a dial timeout occurs,
  but the Dial command via ARI did not set an explicit reason. This resulted in a
  CANCEL with Normal Call Clearing and corresponding ChannelDestroyed.

  This change sets the hangup cause to AST_CAUSE_NO_ANSWER to be consistent with the
  other operations.

  Fixes: #963

  UserNote:  A Dial timeout on POST /channels/{channelId}/dial will now result in a
  CANCEL and ChannelDestroyed with cause 19 / User alerting, no answer.  Previously
  no explicit cause was set, resulting in a cause of 16 / Normal Call Clearing.


#### chan_iax2: Minor improvements to documentation and warning messages.
  Author: Naveen Albert
  Date:   2025-04-18

  * Update Dial() documentation for IAX2 to include syntax for RSA
    public key names.
  * Add additional details to a couple warnings to provide more context
    when an undecodable frame is received.

  Resolves: #1206

#### pbx_ael: unregister AELSub application and CLI commands on module load failure
  Author: Andreas Wehrmann
  Date:   2025-04-18

  This fixes crashes/hangs I noticed with Asterisk 20.3.0 and 20.13.0 and quickly found out,
  that the AEL module doesn't do proper cleanup when it fails to load.
  This happens for example when there are syntax errors and AEL fails to compile in which case pbx_load_module()
  returns an error but load_module() doesn't then unregister CLI cmds and the application.


#### res_pjproject: Fix DTLS client check failing on some platforms
  Author: Albrecht Oster
  Date:   2025-04-10

  Certain platforms (mainly BSD derivatives) have an additional length
  field in `sockaddr_in6` and `sockaddr_in`.
  `ast_sockaddr_from_pj_sockaddr()` does not take this field into account
  when copying over values from the `pj_sockaddr` into the `ast_sockaddr`.
  The resulting `ast_sockaddr` will have an uninitialized value for
  `sin6_len`/`sin_len` while the other `ast_sockaddr` (not converted from
  a `pj_sockaddr`) to check against in `ast_sockaddr_pj_sockaddr_cmp()`
  has the correct length value set.

  This has the effect that `ast_sockaddr_cmp()` will always indicate
  an address mismatch, because it does a bitwise comparison, and all DTLS
  packets are dropped even if addresses and ports match.

  `ast_sockaddr_from_pj_sockaddr()` now checks whether the length fields
  are available on the current platform and sets the values accordingly.

  Resolves: #505

#### Prequisites for ARI Outbound Websockets
  Author: George Joseph
  Date:   2025-04-16

  stasis:
  * Added stasis_app_is_registered().
  * Added stasis_app_control_mark_failed().
  * Added stasis_app_control_is_failed().
  * Fixed res_stasis_device_state so unsubscribe all works properly.
  * Modified stasis_app_unregister() to unsubscribe from all event sources.
  * Modified stasis_app_exec to return -1 if stasis_app_control_is_failed()
    returns true.

  http:
  * Added ast_http_create_basic_auth_header().

  md5:
  * Added define for MD5_DIGEST_LENGTH.

  tcptls:
  * Added flag to ast_tcptls_session_args to suppress connection log messages
    to give callers more control over logging.

  http_websocket:
  * Add flag to ast_websocket_client_options to suppress connection log messages
    to give callers more control over logging.
  * Added username and password to ast_websocket_client_options to support
    outbound basic authentication.
  * Added ast_websocket_result_to_str().


#### contrib: Add systemd service and timer files for malloc trim.
  Author: Ben Ford
  Date:   2025-04-16

  Adds two files to the contrib/systemd/ directory that can be installed
  to periodically run "malloc trim" on Asterisk. These files do nothing
  unless they are explicitly moved to the correct location on the system.
  Users who are experiencing Asterisk memory issues can use this service
  to potentially help combat the problem. These files can also be
  configured to change the start time and interval. See systemd.timer(5)
  and systemd.time(7) for more information.

  UserNote: Service and timer files for systemd have been added to the
  contrib/systemd/ directory. If you are experiencing memory issues,
  install these files to have "malloc trim" periodically run on the
  system.


#### action_redirect: remove after_bridge_goto_info
  Author: Peter Jannesen
  Date:   2025-03-13

  Under certain circumstances the context/extens/prio are stored in the
  after_bridge_goto_info. This info is used when the bridge is broken by
  for hangup of the other party. In the situation that the bridge is
  broken by an AMI Redirect this info is not used but also not removed.
  With the result that when the channel is put back in a bridge and the
  bridge is broken the execution continues at the wrong
  context/extens/prio.

  Resolves: #1144

#### channel: Always provide cause code in ChannelHangupRequest.
  Author: Joshua C. Colp
  Date:   2025-04-16

  When queueing a channel to be hung up a cause code can be
  specified in one of two ways:

  1. ast_queue_hangup_with_cause
  This function takes in a cause code and queues it as part
  of the hangup request, which ultimately results in it being
  set on the channel.

  2. ast_channel_hangupcause_set + ast_queue_hangup
  This combination sets the hangup cause on the channel before
  queueing the hangup instead of as part of that process.

  In the #2 case the ChannelHangupRequest event would not contain
  the cause code. For consistency if a cause code has been set
  on the channel it will now be added to the event.

  Resolves: #1197

#### Add log-caller-id-name option to log Caller ID Name in queue log
  Author: phoneben
  Date:   2025-02-28

  Add log-caller-id-name option to log Caller ID Name in queue log

  This patch introduces a new global configuration option, log-caller-id-name,
  to queues.conf to control whether the Caller ID name is logged when a call enters a queue.

  When log-caller-id-name=yes, the Caller ID name is logged
  as parameter 4 in the queue log, provided it’s allowed by the
  existing log_restricted_caller_id rules. If log-caller-id-name=no (the default),
  the Caller ID name is omitted from the logs.

  Fixes: #1091

  UserNote: This patch adds a global configuration option, log-caller-id-name, to queues.conf
  to control whether the Caller ID name is logged as parameter 4 when a call enters a queue.
  When log-caller-id-name=yes, the Caller ID name is included in the queue log,
  Any '|' characters in the caller ID name will be replaced with '_'.
  (provided it’s allowed by the existing log_restricted_caller_id rules).
  When log-caller-id-name=no (the default), the Caller ID name is omitted.


#### asterisk.c: Add "pre-init" and "pre-module" capability to cli.conf.
  Author: George Joseph
  Date:   2025-04-10

  Commands in the "[startup_commands]" section of cli.conf have historically run
  after all core and module initialization has been completed and just before
  "Asterisk Ready" is printed on the console. This meant that if you
  wanted to debug initialization of a specific module, your only option
  was to turn on debug for everything by setting "debug" in asterisk.conf.

  This commit introduces options to allow you to run CLI commands earlier in
  the asterisk startup process.

  A command with a value of "pre-init" will run just after logger initialization
  but before most core, and all module, initialization.

  A command with a value of "pre-module" will run just after all core
  initialization but before all module initialization.

  A command with a value of "fully-booted" (or "yes" for backwards
  compatibility) will run as they always have been...after all
  initialization and just before "Asterisk Ready" is printed on the console.

  This means you could do this...

  ```
  [startup_commands]
  core set debug 3 res_pjsip.so = pre-module
  core set debug 0 res_pjsip.so = fully-booted
  ```

  This would turn debugging on for res_pjsip.so to catch any module
  initialization debug messages then turn it off again after the module is
  loaded.

  UserNote: In cli.conf, you can now define startup commands that run before
  core initialization and before module initialization.


#### app_confbridge: Prevent crash when publishing channel-less event.
  Author: Sean Bright
  Date:   2025-04-07

  Resolves: #1190

#### ari_websockets: Fix frack if ARI config fails to load.
  Author: George Joseph
  Date:   2025-04-02

  ari_ws_session_registry_dtor() wasn't checking that the container was valid
  before running ao2_callback on it to shutdown registered sessions.


#### ARI: REST over Websocket
  Author: George Joseph
  Date:   2025-03-12

  This commit adds the ability to make ARI REST requests over the same
  websocket used to receive events.

  For full details on how to use the new capability, visit...

  https://docs.asterisk.org/Configuration/Interfaces/Asterisk-REST-Interface-ARI/ARI-REST-over-WebSocket/

  Changes:

  * Added utilities to http.c:
    * ast_get_http_method_from_string().
    * ast_http_parse_post_form().
  * Added utilities to json.c:
    * ast_json_nvp_array_to_ast_variables().
    * ast_variables_to_json_nvp_array().
  * Added definitions for new events to carry REST responses.
  * Created res/ari/ari_websocket_requests.c to house the new request handlers.
  * Moved non-event specific code out of res/ari/resource_events.c into
    res/ari/ari_websockets.c
  * Refactored res/res_ari.c to move non-http code out of ast_ari_callback()
    (which is http specific) and into ast_ari_invoke() so it can be shared
    between both the http and websocket transports.

  UpgradeNote: This commit adds the ability to make ARI REST requests over the same
  websocket used to receive events.
  See https://docs.asterisk.org/Configuration/Interfaces/Asterisk-REST-Interface-ARI/ARI-REST-over-WebSocket/


#### audiohook.c: Add ability to adjust volume with float
  Author: mkmer
  Date:   2025-03-18

  Add the capability to audiohook for float type volume adjustments.  This allows for adjustments to volume smaller than 6dB.  With INT adjustments, the first step is 2 which converts to ~6dB (or 1/2 volume / double volume depending on adjustment sign). 3dB is a typical adjustment level which can now be accommodated with an adjustment value of 1.41.

  This is accomplished by the following:
    Convert internal variables to type float.
    Always use ast_frame_adjust_volume_float() for adjustments.
    Cast int to float in original functions ast_audiohook_volume_set(), and ast_volume_adjust().
    Cast float to int in ast_audiohook_volume_get()
    Add functions ast_audiohook_volume_get_float, ast_audiohook_volume_set_float, and ast_audiohook_volume_adjust_float.

  This update maintains 100% backward compatibility.

  Resolves: #1171

#### audiosocket: added support for DTMF frames
  Author: Florent CHAUVEAU
  Date:   2025-02-28

  Updated the AudioSocket protocol to allow sending DTMF frames.
  AST_FRAME_DTMF frames are now forwarded to the server, in addition to
  AST_FRAME_AUDIO frames. A new payload type AST_AUDIOSOCKET_KIND_DTMF
  with value 0x03 was added to the protocol. The payload is a 1-byte
  ascii representing the DTMF digit (0-9,*,#...).

  UserNote: The AudioSocket protocol now forwards DTMF frames with
  payload type 0x03. The payload is a 1-byte ascii representing the DTMF
  digit (0-9,*,#...).


#### asterisk/channel.h: fix documentation for 'ast_waitfor_nandfds()'
  Author: Norm Harrison
  Date:   2023-04-03

  Co-authored-by: Florent CHAUVEAU <florentch@pm.me>

#### audiosocket: fix timeout, fix dialplan app exit, server address in logs
  Author: Norm Harrison
  Date:   2023-04-03

  - Correct wait timeout logic in the dialplan application.
  - Include server address in log messages for better traceability.
  - Allow dialplan app to exit gracefully on hangup messages and socket closure.
  - Optimize I/O by reducing redundant read()/write() operations.

  Co-authored-by: Florent CHAUVEAU <florentch@pm.me>

#### chan_pjsip:  Add the same details as PJSIPShowContacts to the CLI via 'pjsip s..
  Author: Mark Murawski
  Date:   2025-03-23

  CLI 'pjsip show contact' does not show enough information.
  One must telnet to AMI or write a script to ask Asterisk for example what the User-Agent is on a Contact
  This feature adds the same details as PJSIPShowContacts to the CLI

  Resolves: #643

#### Update config.guess and config.sub
  Author: Zhai Liangliang
  Date:   2025-03-26


#### chan_pjsip: set correct Endpoint Device State on multiple channels
  Author: Alexei Gradinari
  Date:   2025-03-25

  1. When one channel is placed on hold, the device state is set to ONHOLD
  without checking other channels states.
  In case of AST_CONTROL_HOLD set the device state as AST_DEVICE_UNKNOWN
  to calculate aggregate device state of all active channels.

  2. The current implementation incorrectly classifies channels in use.
  The only channels that has the states: UP, RING and BUSY are considered as "in use".
  A channel should be considered "in use" if its state is anything other than
  DOWN or RESERVED.

  3. Currently, if the number of channels "in use" is greater than device_state_busy_at,
  the system does not set the state to BUSY. Instead, it incorrectly assigns an aggregate
  device state.
  The endpoint device state should be BUSY if the number of channels "in use" is greater
  than or equal to device_state_busy_at.

  Fixes: #1181

#### file.c: missing "custom" sound files should not generate warning logs
  Author: Allan Nathanson
  Date:   2025-03-18

  With `sounds_search_custom_dir = yes` we first look to see if a sound file
  is present in the "custom" sound directory before looking in the standard
  sound directories.  We should not be issuing a WARNING log message if a
  sound cannot be found in the "custom" directory.

  Resolves: https://github.com/asterisk/asterisk/issues/1170

