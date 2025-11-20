
## Change Log for Release asterisk-21.12.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-21.12.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/21.11.0...21.12.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21.12.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 20
- Commit Authors: 10
- Issues Resolved: 13
- Security Advisories Resolved: 0

### User Notes:

- #### func_hangupcause.c: Add access to Reason headers via HANGUPCAUSE()
  Added a new option to HANGUPCAUSE to access additional
  information about hangup reason. Reason headers from pjsip
  could be read using 'tech_extended' cause type.

- #### chan_dahdi: Add DAHDI_CHANNEL function.
  The DAHDI_CHANNEL function allows for getting/setting
  certain properties about DAHDI channels from the dialplan.


### Upgrade Notes:

- #### res_audiosocket: add message types for all slin sample rates
  New audiosocket message types 0x11 - 0x18 has been added
  for slin12, slin16, slin24, slin32, slin44, slin48, slin96, and
  slin192 audio. External applications using audiosocket may need to be
  updated to support these message types if the audiosocket channel is
  created with one of these audio formats.


### Developer Notes:


### Commit Authors:

- Bastian Triller: (1)
- Ben Ford: (1)
- George Joseph: (4)
- Igor Goncharovsky: (1)
- Max Grobecker: (1)
- Nathan Monfils: (1)
- Naveen Albert: (4)
- Sean Bright: (3)
- Sven Kube: (3)
- phoneben: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 1340: [bug]: comfort noise packet corrupted
  - 1419: [bug]: static code analysis issues in app_adsiprog.c
  - 1422: [bug]: static code analysis issues in apps/app_externalivr.c
  - 1425: [bug]: static code analysis issues in apps/app_queue.c
  - 1434: [improvement]: pbx_variables: Create real channel for dialplan eval CLI command
  - 1436: [improvement]: res_cliexec: Avoid unnecessary cast to char*
  - 1455: [new-feature]: chan_dahdi: Add DAHDI_CHANNEL function
  - 1467: [bug]: Crash in res_pjsip_refer during REFER progress teardown with PJSIP_TRANSFER_HANDLING(ari-only)
  - 1491: [bug]: Segfault: `channelstorage_cpp` fast lookup without lock (`get_by_name_exact`/`get_by_uniqueid`) leads to UAF during hangup
  - 1525: [bug]: chan_websocket: fix use of raw payload variable for string comparison in process_text_message
  - 1539: [bug]: safe_asterisk without TTY doesn't log to file
  - 1554: [bug]: safe_asterisk recurses into subdirectories of startup.d after f97361
  - 1578: [bug]: Deadlock with externalMedia custom channel id and cpp map channel backend

### Commits By Author:

- #### Bastian Triller (1):

- #### Ben Ford (1):

- #### George Joseph (4):

- #### Igor Goncharovsky (1):

- #### Max Grobecker (1):

- #### Nathan Monfils (1):

- #### Naveen Albert (4):

- #### Sean Bright (3):

- #### Sven Kube (3):

- #### phoneben (1):

### Commit List:

-  channelstorage:  Allow storage driver read locking to be skipped.
-  safe_asterisk: Resolve a POSIX sh problem and restore globbing behavior.
-  safe_asterisk:  Fix logging and sorting issue.
-  res_audiosocket: add message types for all slin sample rates
-  chan_websocket.c: Change payload references to command instead.
-  func_hangupcause.c: Add access to Reason headers via HANGUPCAUSE()
-  channelstorage_cpp_map_name_id: Add read locking around retrievals.
-  res_pjsip_geolocation: Add support for Geolocation loc-src parameter
-  stasis_channels.c: Make protocol_id optional to enable blind transfer via ari
-  Fix some doxygen, typos and whitespace
-  stasis_channels.c: Add null check for referred_by in ast_ari_transfer_message_create
-  app_queue: Add NULL pointer checks in app_queue
-  app_externalivr: Prevent out-of-bounds read during argument processing.
-  chan_dahdi: Add DAHDI_CHANNEL function.
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

#### safe_asterisk: Resolve a POSIX sh problem and restore globbing behavior.
  Author: Sean Bright
  Date:   2025-10-22

  * Using `==` with the POSIX sh `test` utility is UB.
  * Switch back to using globs instead of using `$(find … | sort)`.
  * Fix a missing redirect when checking for the OS type.

  Resolves: #1554

#### safe_asterisk:  Fix logging and sorting issue.
  Author: George Joseph
  Date:   2025-10-17

  Re-enabled "TTY=9" which was erroneously disabled as part of a recent
  security fix and removed another logging "fix" that was added.

  Also added a sort to the "find" that enumerates the scripts to be sourced so
  they're sourced in the correct order.

  Resolves: #1539

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

#### chan_websocket.c: Change payload references to command instead.
  Author: George Joseph
  Date:   2025-10-08

  Some of the tests in process_text_message() were still comparing to the
  websocket message payload instead of the "command" string.

  Resolves: #1525

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

#### res_pjsip_geolocation: Add support for Geolocation loc-src parameter
  Author: Max Grobecker
  Date:   2025-09-21

  This adds support for the Geolocation 'loc-src' parameter to res_pjsip_geolocation.
  The already existing config option 'location_source` in res_geolocation is documented to add a 'loc-src' parameter containing a user-defined FQDN to the 'Geolocation:' header,
  but that option had no effect as it was not implemented by res_pjsip_geolocation.

  If the `location_source` configuration option is not set or invalid, that parameter will not be added (this is already checked by res_geolocation).

  This commits adds already documented functionality.

#### stasis_channels.c: Make protocol_id optional to enable blind transfer via ari
  Author: Sven Kube
  Date:   2025-09-22

  When handling SIP transfers via ARI, there is no protocol_id in case of
  a blind transfer.

  Resolves: #1467

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

