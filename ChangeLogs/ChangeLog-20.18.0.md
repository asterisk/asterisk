
## Change Log for Release asterisk-20.18.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.18.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.17.0...20.18.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.18.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 57
- Commit Authors: 20
- Issues Resolved: 40
- Security Advisories Resolved: 0

### User Notes:

- #### chan_websocket.conf.sample: Fix category name.
  The category name in the chan_websocket.conf.sample file was
  incorrect.  It should be "global" instead of "general".

- #### cli.c: Allow 'channel request hangup' to accept patterns.
  The 'channel request hangup' CLI command now accepts
  multiple channel names, POSIX Extended Regular Expressions, glob-like
  patterns, or a combination of all of them. See the CLI command 'core
  show help channel request hangup' for full details.

- #### res_sorcery_memory_cache: Reduce cache lock time for sorcery memory cache populate command
  The AMI command sorcery memory cache populate will now
  return an error if there is an internal error performing the populate.
  The CLI command will display an error in this case as well.

- #### res_geolocation:  Fix multiple issues with XML generation.
  Geolocation: Two new optional profile parameters have been added.
  * `pidf_element_id` which sets the value of the `id` attribute on the top-level
    PIDF-LO `device`, `person` or `tuple` elements.
  * `device_id` which sets the content of the `<deviceID>` element.
  Both parameters can include channel variables.

- #### res_pjsip_messaging: Add support for following 3xx redirects
  A new pjsip endpoint option follow_redirect_methods was added.
  This option is a comma-delimited, case-insensitive list of SIP methods
  for which SIP 3XX redirect responses are followed. An alembic upgrade
  script has been added for adding this new option to the Asterisk
  database.

- #### taskprocessors: Improve logging and add new cli options
  New CLI command has been added -
  core show taskprocessor name <taskprocessor-name>

- #### ccss:  Add option to ccss.conf to globally disable it.
  A new "enabled" parameter has been added to ccss.conf.  It defaults
  to "yes" to preserve backwards compatibility but CCSS is rarely used so
  setting "enabled = no" in the "general" section can save some unneeded channel
  locking operations and log message spam.  Disabling ccss will also prevent
  the func_callcompletion and chan_dahdi modules from loading.

- #### Makefile: Add module-list-* targets.
  Try "make module-list-deprecated" to see what modules
  are on their way out the door.

- #### app_mixmonitor: Add 's' (skip) option to delay recording.
  This change introduces a new 's(<seconds>)' (skip) option to the MixMonitor
  application. Example:
    MixMonitor(${UNIQUEID}.wav,s(3))
  This skips recording for the first 3 seconds before writing audio to the file.
  Existing MixMonitor behavior remains unchanged when the 's' option is not used.

- #### app_queue.c: Only announce to head caller if announce_to_first_user
  When announce_to_first_user is false, no announcements are played to the head caller


### Upgrade Notes:

- #### res_geolocation:  Fix multiple issues with XML generation.
  Geolocation: In order to correct bugs in both code and
  documentation, the following changes to the parameters for GML geolocation
  locations are now in effect:
  * The documented but unimplemented `crs` (coordinate reference system) element
    has been added to the location_info parameter that indicates whether the `2d`
    or `3d` reference system is to be used. If the crs isn't valid for the shape
    specified, an error will be generated. The default depends on the shape
    specified.
  * The Circle, Ellipse and ArcBand shapes MUST use a `2d` crs.  If crs isn't
    specified, it will default to `2d` for these shapes.
    The Sphere, Ellipsoid and Prism shapes MUST use a `3d` crs. If crs isn't
    specified, it will default to `3d` for these shapes.
    The Point and Polygon shapes may use either crs.  The default crs is `2d`
    however so if `3d` positions are used, the crs must be explicitly set to `3d`.
  * The `geoloc show gml_shape_defs` CLI command has been updated to show which
    coordinate reference systems are valid for each shape.
  * The `pos3d` element has been removed in favor of allowing the `pos` element
    to include altitude if the crs is `3d`.  The number of values in the `pos`
    element MUST be 2 if the crs is `2d` and 3 if the crs is `3d`.  An error
    will be generated for any other combination.
  * The angle unit-of-measure for shapes that use angles should now be included
    in the respective parameter.  The default is `degrees`. There were some
    inconsistent references to `orientation_uom` in some documentation but that
    parameter never worked and is now removed.  See examples below.
  Examples...
  ```
    location_info = shape="Sphere", pos="39.0 -105.0 1620", radius="20"
    location_info = shape="Point", crs="3d", pos="39.0 -105.0 1620"
    location_info = shape="Point", pos="39.0 -105.0"
    location_info = shape=Ellipsoid, pos="39.0 -105.0 1620", semiMajorAxis="20"
                  semiMinorAxis="10", verticalAxis="0", orientation="25 degrees"
    pidf_element_id = ${CHANNEL(name)}-${EXTEN}
    device_id = mac:001122334455
    Set(GEOLOC_PROFILE(pidf_element_id)=${CHANNEL(name)}/${EXTEN})
  ```

- #### pjsip: Move from threadpool to taskpool
  The threadpool_* options in pjsip.conf have now
  been deprecated though they continue to be read and used.
  They have been replaced with taskpool options that give greater
  control over the underlying taskpool used for PJSIP. An alembic
  upgrade script has been added to add these options to realtime
  as well.

- #### app_directed_pickup.c: Change some log messages from NOTICE to VERBOSE.
  In an effort to reduce log spam, two normal progress
  "pickup attempted" log messages from app_directed_pickup have been changed
  from NOTICE to VERBOSE(3).  This puts them on par with other normal
  dialplan progress messages.


### Developer Notes:

- #### ccss:  Add option to ccss.conf to globally disable it.
  A new API ast_is_cc_enabled() has been added.  It should be
  used to ensure that CCSS is enabled before making any other ast_cc_* calls.

- #### chan_websocket: Add ability to place a MARK in the media stream.
  Apps can now send a `MARK_MEDIA` command with an optional
  `correlation_id` parameter to chan_websocket which will be placed in the
  media frame queue. When that frame is dequeued after all intervening media
  has been played to the core, chan_websocket will send a
  `MEDIA_MARK_PROCESSED` event to the app with the same correlation_id
  (if any).

- #### chan_websocket: Add capability for JSON control messages and events.
  The chan_websocket plain-text control and event messages are now
  deprecated (but remain the default) in favor of JSON formatted messages.
  See https://docs.asterisk.org/Configuration/Channel-Drivers/WebSocket for
  more information.
  A "transport_data" parameter has been added to the

### Commit Authors:

- Alexei Gradinari: (1)
- C. Maj: (1)
- Daouda Taha: (1)
- Etienne Lessard: (1)
- George Joseph: (12)
- Joe Garlick: (2)
- Joshua C. Colp: (1)
- Justin T. Gibbs: (1)
- Kristian F. Høgh: (1)
- Maximilian Fridrich: (2)
- Michal Hajek: (1)
- Mike Bradeen: (2)
- Nathaniel Wesley Filardo: (1)
- Naveen Albert: (3)
- Peter Krall: (1)
- Sean Bright: (17)
- Sven Kube: (1)
- Tinet-mucw: (2)
- phoneben: (5)
- sarangr7: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 60: [bug]: Can't enter any of UTF-8 character in the CLI prompt
  - 1417: [bug]: static code analysis issues in abstract_jb
  - 1421: [bug]: static code analysis issues in apps/app_dtmfstore.c
  - 1427: [bug]: static code analysis issues in apps/app_stream_echo.c
  - 1430: [bug]: static code analysis issues in res/stasis/app.c
  - 1442: [bug]: static code analysis issues in main/bridge_basic.c
  - 1444: [bug]: static code analysis issues in bridges/bridge_simple.c
  - 1446: [bug]: static code analysis issues in bridges/bridge_softmix.c
  - 1531: [bug]: Memory corruption in manager.c due to double free of criteria variable.
  - 1546: [improvement]: Not able to pass the custom variables over the websockets using external Media with ari client library nodejs
  - 1552: [improvement]: chan_dahdi.conf.sample: Warnings for callgroup/pickupgroup in stock config
  - 1563: [bug]:  chan_websocket.c: Wrong variable used in ast_strings_equal() (payload instead of command)
  - 1566: [improvement]: Improve Taskprocessor logging
  - 1568: [improvement]: Queue is playing announcements when announce_to_first_user is false
  - 1572: [improvement]: List modules at various support levels
  - 1574: [improvement]: Add playback progress acknowledgment for WebSocket media (per-chunk or byte-level acknowledgment)
  - 1576: [improvement]: res_pjsip_messaging: Follow 3xx redirect messages if redirect_method=uri_pjsip
  - 1585: [bug]: cli 'stasis show topics' calls a read lock which freezes asterisk till the process is done
  - 1587: [bug]: chan_websocket terminates websocket on CNG/non-audio
  - 1590: [bug]: Fix: Use ast instead of p->chan to get the DIALSTATUS variable
  - 1597: [bug]: app_reload: Reload() without arguments doesn't work.
  - 1599: [bug]: pbx.c: Running "dialplan reload" shows wrong number of contexts
  - 1604: [bug]: asterisk crashes during dtmf input thru websocket -- fixed
  - 1609: [bug]: Crash: Double free in ast_channel_destructor leading to SIGABRT (Asterisk 20.17.0) with C++ channel storage
  - 1635: [bug]: Regression: Fix endpoint memory leak
  - 1638: [bug]: Channel drivers creating ephemeral channels create per-endpoint topics and cache when they shouldn't
  - 1643: [bug]: chan_websocket crash when channel hung up before read thread is started
  - 1645: [bug]: chan_websocket stuck channels
  - 1647: [bug]: "presencestate change" CLI command doesn't accept NOT_SET
  - 1648: [bug]: ARI announcer channel can cause crash in specific scenario due to unreffing of borrowed format
  - 1660: [bug]: missing hangup cause for ARI ChannelDestroyed when Originated channel times out
  - 1662: [improvement]: Include remote IP address in http.c “Requested URI has no handler” log entries
  - 1667: [bug]: Multiple geolocation issues with rendering XML
  - 1673: [bug]: A crash occurs during the call to mixmonitor_ds_remove_and_free
  - 1675: [bug]: res_pjsip_mwi: off-nominal endpoint ao2 reference leak in mwi_get_notify_data()
  - 1681: [bug]: stasis/control.c: Memory leak of hangup_time in set-timeout
  - 1683: [improvement]: chan_websocket: Use channel FD polling to read data from websocket instead of dedicated thread.
  - 1692: [improvement]:  Add comment to asterisk.conf.sample clarifying that template sections are ignored
  - 1700: [improvement]: Improve sorcery cache populate
  - 1711: [bug]: Missing Contact: header in 200 OK

### Commits By Author:

- #### Alexei Gradinari (1):

- #### C. Maj (1):

- #### Daouda Taha (1):

- #### Etienne Lessard (1):

- #### George Joseph (12):

- #### Joe Garlick (2):

- #### Joshua C. Colp (1):

- #### Justin T. Gibbs (1):

- #### Kristian F. Høgh (1):

- #### Maximilian Fridrich (2):

- #### Michal Hajek (1):

- #### Mike Bradeen (2):

- #### Nathaniel Wesley Filardo (1):

- #### Naveen Albert (3):

- #### Peter Krall (1):

- #### Sean Bright (17):

- #### Sven Kube (1):

- #### Tinet-mucw (2):

- #### phoneben (5):

- #### sarangr7 (1):

### Commit List:

-  chan_websocket.conf.sample: Fix category name.
-  chan_websocket: Fixed Ping/Pong messages hanging up the websocket channel
-  cli.c: Allow 'channel request hangup' to accept patterns.
-  chan_sip.c: Ensure Contact header is set on responses to INVITE.
-  res_sorcery_memory_cache: Reduce cache lock time for sorcery memory cache populate command
-  Add comment to asterisk.conf.sample clarifying that template sections are ignored
-  chan_websocket: Use the channel's ability to poll fds for the websocket read.
-  asterisk.c: Allow multi-byte characters on the Asterisk CLI.
-  func_presencestate.c: Allow `NOT_SET` to be set from CLI.
-  res/ari/resource_bridges.c: Normalize channel_format ref handling for bridge media
-  res_geolocation:  Fix multiple issues with XML generation.
-  stasis/control.c: Add destructor to timeout_datastore.
-  func_talkdetect.c: Remove reference to non-existent variables.
-  configure.ac: use AC_PATH_TOOL for nm
-  res_pjsip_mwi: Fix off-nominal endpoint ao2 ref leak in mwi_get_notify_data
-  res_pjsip_messaging: Add support for following 3xx redirects
-  res_pjsip: Introduce redirect module for handling 3xx responses
-  app_mixmonitor.c: Fix crash in mixmonitor_ds_remove_and_free when datastore is NULL
-  res_pjsip_refer: don't defer session termination for ari transfer
-  chan_dahdi.conf.sample: Avoid warnings with default configs.
-  main/dial.c: Set channel hangup cause on timeout in handle_timeout_trip
-  cel: Add missing manager documentation.
-  res_odbc: Use SQL_SUCCEEDED() macro where applicable.
-  rtp/rtcp: Configure dual-stack behavior via IPV6_V6ONLY
-  http.c: Include remote address in URI handler message.
-  pjsip: Move from threadpool to taskpool
-  Disable device state caching for ephemeral channels
-  chan_websocket: Add locking in send_event and check for NULL websocket handle.
-  Fix false null-deref warning in channel_state
-  endpoint.c: Plug a memory leak in ast_endpoint_shutdown().
-  Revert "func_hangupcause.c: Add access to Reason headers via HANGUPCAUSE()"
-  cel_manager.c: Correct manager event mask for CEL events.
-  app_queue.c: Update docs to correct QueueMemberPause event name.
-  taskprocessors: Improve logging and add new cli options
-  manager: fix double free of criteria variable when adding filter
-  app_stream_echo.c: Check that stream is non-NULL before dereferencing.
-  abstract_jb.c: Remove redundant timer check per static analysis.
-  channelstorage_cpp: Fix fallback return value in channelstorage callback
-  ccss:  Add option to ccss.conf to globally disable it.
-  app_directed_pickup.c: Change some log messages from NOTICE to VERBOSE.
-  chan_websocket: Fix crash on DTMF_END event.
-  chan_websocket.c: Tolerate other frame types
-  app_reload: Fix Reload() without arguments.
-  pbx.c: Print new context count when reloading dialplan.
-  Makefile: Add module-list-* targets.
-  core_unreal.c: Use ast instead of p->chan to get the DIALSTATUS variable
-  ast_coredumper: Fix multiple issues
-  app_mixmonitor: Add 's' (skip) option to delay recording.
-  stasis: switch stasis show topics temporary container from list - RBtree
-  app_dtmfstore: Avoid a potential buffer overflow.
-  main: Explicitly mark case statement fallthrough as such.
-  bridge_softmix: Return early on topology allocation failure.
-  bridge_simple: Increase code verbosity for clarity.
-  app_queue.c: Only announce to head caller if announce_to_first_user
-  chan_websocket: Add ability to place a MARK in the media stream.
-  chan_websocket: Add capability for JSON control messages and events.
-  build: Add menuselect options to facilitate code tracing and coverage

### Commit Details:

#### chan_websocket.conf.sample: Fix category name.
  Author: George Joseph
  Date:   2026-01-21

  UserNote: The category name in the chan_websocket.conf.sample file was
  incorrect.  It should be "global" instead of "general".

#### chan_websocket: Fixed Ping/Pong messages hanging up the websocket channel
  Author: Joe Garlick
  Date:   2026-01-15

  When chan_websocket received a Ping or a Pong opcode it would cause the channel to hangup. This change allows Ping/Pong opcodes and allows them to silently pass

#### cli.c: Allow 'channel request hangup' to accept patterns.
  Author: Sean Bright
  Date:   2026-01-05

  This extends 'channel request hangup' to accept multiple channel
  names, a POSIX Extended Regular Expression, a glob-like pattern, or a
  combination of all of them.

  UserNote: The 'channel request hangup' CLI command now accepts
  multiple channel names, POSIX Extended Regular Expressions, glob-like
  patterns, or a combination of all of them. See the CLI command 'core
  show help channel request hangup' for full details.

#### chan_sip.c: Ensure Contact header is set on responses to INVITE.
  Author: Etienne Lessard
  Date:   2026-01-09

  From the original report* on ASTERISK-24915:

    > The problem occurs because the handle_incoming function updates
    p->method to req->method (p being a struct sip_pvt *) before
    checking if the CSeq makes sense, and if the CSeq is unexpected, it
    does not reset p->method to its old value before returning. Then,
    when asterisk sends the 200 OK response for the original INVITE,
    since p->method is now equal to SIP_ACK (instead of SIP_INVITE), the
    resp_need_contact function (called from respprep) says "its a SIP
    ACK, no need to add a Contact header for the response", which is
    wrong, since it's not a SIP ACK but a SIP INVITE dialog.

  I have confirmed that the analysis is correct and that the patch fixes
  the behavior.

  *: https://issues-archive.asterisk.org/ASTERISK-24915

  Resolves: #1711

#### res_sorcery_memory_cache: Reduce cache lock time for sorcery memory cache populate command
  Author: Mike Bradeen
  Date:   2026-01-06

  Reduce cache lock time for AMI and CLI sorcery memory cache populate
  commands by adding a new populate_lock to the sorcery_memory_cache
  struct which is locked separately from the existing cache lock so that
  the cache lock can be maintained for a reduced time, locking only when
  the cache objects are removed and re-populated.

  Resolves: #1700

  UserNote: The AMI command sorcery memory cache populate will now
  return an error if there is an internal error performing the populate.
  The CLI command will display an error in this case as well.

#### Add comment to asterisk.conf.sample clarifying that template sections are ignored
  Author: phoneben
  Date:   2026-01-05

  Add comment to asterisk.conf.sample clarifying that template sections are ignored.

  Resolves: #1692

#### chan_websocket: Use the channel's ability to poll fds for the websocket read.
  Author: George Joseph
  Date:   2025-12-30

  We now add the websocket's file descriptor to the channel's fd array and let
  it poll for data availability instead if having a dedicated thread that
  does the polling. This eliminates the thread and allows removal of most
  explicit locking since the core channel code will lock the channel to prevent
  simultaneous calls to webchan_read, webchan_hangup, etc.

  While we were here, the hangup code was refactored to use ast_hangup_with_cause
  instead of directly queueing an AST_CONTROL_HANGUP frame.  This allows us
  to set hangup causes and generate snapshots.

  For a bit of extra debugging, a table of websocket close codes was added
  to http_websocket.h with an accompanying "to string" function added to
  res_http_websocket.c

  Resolves: #1683

#### asterisk.c: Allow multi-byte characters on the Asterisk CLI.
  Author: Sean Bright
  Date:   2025-12-13

  Versions of libedit that support Unicode expect that the
  EL_GETCFN (the function that does character I/O) will fill in a
  `wchar_t` with a character, which may be multi-byte. The built-in
  function that libedit provides, but does not expose with a public API,
  does properly handle multi-byte sequences.

  Due to the design of Asterisk's console processing loop, Asterisk
  provides its own implementation which does not handle multi-byte
  characters. Changing Asterisk to use libedit's built-in function would
  be ideal, but would also require changing some fundamental things
  about console processing which could be fairly disruptive.

  Instead, we bring in libedit's `read_char` implementation and modify
  it to suit our specific needs.

  Resolves: #60

#### func_presencestate.c: Allow `NOT_SET` to be set from CLI.
  Author: Sean Bright
  Date:   2026-01-01

  Resolves: #1647

#### res/ari/resource_bridges.c: Normalize channel_format ref handling for bridge media
  Author: Peter Krall
  Date:   2025-12-17

  Always take an explicit reference on the format used for bridge playback
  and recording channels, regardless of where it was sourced, and release
  it after prepare_bridge_media_channel. This aligns the code paths and
  avoids mixing borrowed and owned references while preserving behavior.

  Fixes: #1648

#### res_geolocation:  Fix multiple issues with XML generation.
  Author: George Joseph
  Date:   2025-12-17

  * 3d positions were being rendered without an enclosing `<gml:pos>`
    element resulting in invalid XML.
  * There was no way to set the `id` attribute on the enclosing `tuple`, `device`
    and `person` elements.
  * There was no way to set the value of the `deviceID` element.
  * Parsing of degree and radian UOMs was broken resulting in them appearing
    outside an XML element.
  * The UOM schemas for degrees and radians were reversed.
  * The Ellipsoid shape was missing and the Ellipse shape was defined multiple
    times.
  * The `crs` location_info parameter, although documented, didn't work.
  * The `pos3d` location_info parameter appears in some documentation but
    wasn't being parsed correctly.
  * The retransmission-allowed and retention-expiry sub-elements of usage-rules
    were using the `gp` namespace instead of the `gbp` namespace.

  In addition to fixing the above, several other code refactorings were
  performed and the unit test enhanced to include a round trip
  XML -> eprofile -> XML validation.

  Resolves: #1667

  UserNote: Geolocation: Two new optional profile parameters have been added.
  * `pidf_element_id` which sets the value of the `id` attribute on the top-level
    PIDF-LO `device`, `person` or `tuple` elements.
  * `device_id` which sets the content of the `<deviceID>` element.
  Both parameters can include channel variables.

  UpgradeNote: Geolocation: In order to correct bugs in both code and
  documentation, the following changes to the parameters for GML geolocation
  locations are now in effect:
  * The documented but unimplemented `crs` (coordinate reference system) element
    has been added to the location_info parameter that indicates whether the `2d`
    or `3d` reference system is to be used. If the crs isn't valid for the shape
    specified, an error will be generated. The default depends on the shape
    specified.
  * The Circle, Ellipse and ArcBand shapes MUST use a `2d` crs.  If crs isn't
    specified, it will default to `2d` for these shapes.
    The Sphere, Ellipsoid and Prism shapes MUST use a `3d` crs. If crs isn't
    specified, it will default to `3d` for these shapes.
    The Point and Polygon shapes may use either crs.  The default crs is `2d`
    however so if `3d` positions are used, the crs must be explicitly set to `3d`.
  * The `geoloc show gml_shape_defs` CLI command has been updated to show which
    coordinate reference systems are valid for each shape.
  * The `pos3d` element has been removed in favor of allowing the `pos` element
    to include altitude if the crs is `3d`.  The number of values in the `pos`
    element MUST be 2 if the crs is `2d` and 3 if the crs is `3d`.  An error
    will be generated for any other combination.
  * The angle unit-of-measure for shapes that use angles should now be included
    in the respective parameter.  The default is `degrees`. There were some
    inconsistent references to `orientation_uom` in some documentation but that
    parameter never worked and is now removed.  See examples below.
  Examples...
  ```
    location_info = shape="Sphere", pos="39.0 -105.0 1620", radius="20"
    location_info = shape="Point", crs="3d", pos="39.0 -105.0 1620"
    location_info = shape="Point", pos="39.0 -105.0"
    location_info = shape=Ellipsoid, pos="39.0 -105.0 1620", semiMajorAxis="20"
                  semiMinorAxis="10", verticalAxis="0", orientation="25 degrees"
    pidf_element_id = ${CHANNEL(name)}-${EXTEN}
    device_id = mac:001122334455
    Set(GEOLOC_PROFILE(pidf_element_id)=${CHANNEL(name)}/${EXTEN})
  ```

#### stasis/control.c: Add destructor to timeout_datastore.
  Author: George Joseph
  Date:   2025-12-31

  The timeout_datastore was missing a destructor resulting in a leak
  of 16 bytes for every outgoing ARI call.

  Resolves: #1681

#### func_talkdetect.c: Remove reference to non-existent variables.
  Author: Sean Bright
  Date:   2025-12-30


#### configure.ac: use AC_PATH_TOOL for nm
  Author: Nathaniel Wesley Filardo
  Date:   2025-11-27

  `nm` might, especially in cross-compilation scenarios, be available but prefixed with the target triple. So: use `AC_PATH_TOOL` rather than `AC_PATH_PROG` to find it. (See https://www.gnu.org/software/autoconf/manual/autoconf-2.68/html_node/Generic-Programs.html .)

  Found and proposed fix tested by cross-compiling Asterisk using Nixpkgs on x86_64 targeting aarch64. :)

#### res_pjsip_mwi: Fix off-nominal endpoint ao2 ref leak in mwi_get_notify_data
  Author: Alexei Gradinari
  Date:   2025-12-29

  Delay acquisition of the ast_sip_endpoint reference in mwi_get_notify_data()
  to avoid an ao2 ref leak on early-return error paths.

  Move ast_sip_subscription_get_endpoint() to just before first use so all
  acquired references are properly cleaned up.

  Fixes: #1675

#### res_pjsip_messaging: Add support for following 3xx redirects
  Author: Maximilian Fridrich
  Date:   2025-11-07

  This commit integrates the redirect module into res_pjsip_messaging
  to enable following 3xx redirect responses for outgoing SIP MESSAGEs.

  When follow_redirect_methods contains 'message' on an endpoint, Asterisk
  will now follow 3xx redirect responses for MESSAGEs, similar to how
  it behaves for INVITE responses.

  Resolves: #1576

  UserNote: A new pjsip endpoint option follow_redirect_methods was added.
  This option is a comma-delimited, case-insensitive list of SIP methods
  for which SIP 3XX redirect responses are followed. An alembic upgrade
  script has been added for adding this new option to the Asterisk
  database.

#### res_pjsip: Introduce redirect module for handling 3xx responses
  Author: Maximilian Fridrich
  Date:   2025-11-07

  This commit introduces a new redirect handling module that provides
  infrastructure for following SIP 3xx redirect responses. The redirect
  functionality respects the endpoint's redirect_method setting and only
  follows redirects when set to 'uri_pjsip'. This infrastructure can be
  used by any PJSIP module that needs to handle 3xx redirect responses.

#### app_mixmonitor.c: Fix crash in mixmonitor_ds_remove_and_free when datastore is NULL
  Author: Tinet-mucw
  Date:   2025-12-25

  The datastore may be NULL, so a null pointer check needs to be added.

  Resolves: #1673

#### res_pjsip_refer: don't defer session termination for ari transfer
  Author: Sven Kube
  Date:   2025-10-23

  Allow session termination during an in progress ari handled transfer.

#### chan_dahdi.conf.sample: Avoid warnings with default configs.
  Author: Naveen Albert
  Date:   2025-10-23

  callgroup and pickupgroup may only be specified for FXO-signaled channels;
  however, the chan_dahdi sample config had these options uncommented in
  the [channels] section, thus applying these settings to all channels,
  resulting in warnings. Comment these out so there are no warnings with
  an unmodified sample config.

  Resolves: #1552

#### main/dial.c: Set channel hangup cause on timeout in handle_timeout_trip
  Author: sarangr7
  Date:   2025-12-18

  When dial attempts timeout in the core dialing API, the channel's hangup
  cause was not being set before hanging up. Only the ast_dial_channel
  structure's internal cause field was updated, but the actual ast_channel
  hangup cause remained unset.

  This resulted in incorrect or missing hangup cause information being
  reported through CDRs, AMI events, and other mechanisms that read the
  channel's hangup cause when dial timeouts occurred via applications
  using the dialing API (FollowMe, Page, etc.).

  The fix adds proper channel locking and sets AST_CAUSE_NO_ANSWER on
  the channel before calling ast_hangup(), ensuring consistent hangup
  cause reporting across all interfaces.

  Resolves: #1660

#### cel: Add missing manager documentation.
  Author: Sean Bright
  Date:   2025-12-12

  The LOCAL_OPTIMIZE_BEGIN, STREAM_BEGIN, STREAM_END, and DTMF CEL
  events were not all documented in the CEL configuration file or the
  manager documentation for the CEL event.

#### res_odbc: Use SQL_SUCCEEDED() macro where applicable.
  Author: Sean Bright
  Date:   2025-12-17

  This is just a cleanup of some repetitive code.

#### rtp/rtcp: Configure dual-stack behavior via IPV6_V6ONLY
  Author: Justin T. Gibbs
  Date:   2025-12-21

  Dual-stack behavior (simultaneous listening for IPV4 and IPV6
  connections on a single socket) is required by Asterisk's ICE
  implementation.  On systems with the IPV6_V6ONLY sockopt, set
  the option to 0 (dual-stack enabled) when binding to the IPV6
  any address. This ensures correct behavior regardless of the
  system's default dual-stack configuration.

#### http.c: Include remote address in URI handler message.
  Author: Sean Bright
  Date:   2025-12-22

  Resolves: #1662

#### pjsip: Move from threadpool to taskpool
  Author: Joshua C. Colp
  Date:   2025-12-04

  This change moves the PJSIP module from the threadpool API
  to the taskpool API. PJSIP-specific implementations for
  task usage have been removed and replaced with calls to
  the optimized taskpool implementations instead. The need
  for a pool of serializers has also been removed as
  taskpool inherently provides this. The default settings
  have also been changed to be more realistic for common
  usage.

  UpgradeNote: The threadpool_* options in pjsip.conf have now
  been deprecated though they continue to be read and used.
  They have been replaced with taskpool options that give greater
  control over the underlying taskpool used for PJSIP. An alembic
  upgrade script has been added to add these options to realtime
  as well.

#### Disable device state caching for ephemeral channels
  Author: phoneben
  Date:   2025-12-09

  chan_audiosocket/chan_rtp/res_stasis_snoop: Disable device state caching for ephemeral channels

  Resolves: #1638

#### chan_websocket: Add locking in send_event and check for NULL websocket handle.
  Author: George Joseph
  Date:   2025-12-10

  On an outbound websocket connection, when the triggering caller hangs up,
  webchan_hangup() closes the outbound websocket session and sets the websocket
  session handle to NULL.  If the hangup happened in the tiny window between
  opening the outbound websocket connection and before read_thread_handler()
  was able to send the MEDIA_START message, it could segfault because the
  websocket session handle was NULL.  If it didn't actually segfault, there was
  also the possibility that the websocket instance wouldn't get cleaned up which
  could also cause the channel snapshot to not get cleaned up.  That could
  cause memory leaks and `core show channels` to list phantom WebSocket
  channels.

  To prevent the race, the send_event() macro now locks the websocket_pvt
  instance and checks the websocket session handle before attempting to send
  the MEDIA_START message.

  Resolves: #1643
  Resolves: #1645

#### Fix false null-deref warning in channel_state
  Author: phoneben
  Date:   2025-12-08

  Resolve analyzer warning in channel_state by checking AST_FLAG_DEAD on snapshot, which is guaranteed non-NULL.

  Resolves: #1430

#### endpoint.c: Plug a memory leak in ast_endpoint_shutdown().
  Author: George Joseph
  Date:   2025-12-08

  Commit 26795be introduced a memory leak of ast_endpoint when
  ast_endpoint_shutdown() was called. The leak occurs only if a configuration
  change removes an endpoint and isn't related to call volume or the length of
  time asterisk has been running.  An ao2_ref(-1) has been added to
  ast_endpoint_shutdown() to plug the leak.

  Resolves: #1635

#### Revert "func_hangupcause.c: Add access to Reason headers via HANGUPCAUSE()"
  Author: Sean Bright
  Date:   2025-12-03

  This reverts commit 517766299093d7a9798af68b39951ed8b2469836.

  For rationale, see #1621 and #1606

#### cel_manager.c: Correct manager event mask for CEL events.
  Author: Sean Bright
  Date:   2025-12-05

  There is no EVENT_FLAG_CEL and these events are raised with as
  EVENT_FLAG_CALL.

#### app_queue.c: Update docs to correct QueueMemberPause event name.
  Author: Sean Bright
  Date:   2025-12-04


#### taskprocessors: Improve logging and add new cli options
  Author: Mike Bradeen
  Date:   2025-10-28

  This change makes some small changes to improve log readability in
  addition to the following changes:

  Modified 'core show taskprocessors' to now show Low time and High time
  for task execution.

  New command 'core show taskprocessor name <taskprocessor-name>' to dump
  taskprocessor info and current queue.

  Addionally, a new test was added to demonstrate the 'show taskprocessor
  name' functionality:
  test execute category /main/taskprocessor/ name taskprocessor_cli_show

  Setting 'core set debug 3 taskprocessor.c' will now log pushed tasks.
  (Warning this is will cause extremely high levels of logging at even
  low traffic levels.)

  Resolves: #1566

  UserNote: New CLI command has been added -
  core show taskprocessor name <taskprocessor-name>

#### manager: fix double free of criteria variable when adding filter
  Author: Michal Hajek
  Date:   2025-10-13

  Signed-off-by: Michal Hajek <michal.hajek@daktela.com>

  Fixes: #1531

#### app_stream_echo.c: Check that stream is non-NULL before dereferencing.
  Author: Sean Bright
  Date:   2025-12-01

  Also re-order and rename the arguments of `stream_echo_write_error` to
  match those of `ast_write_stream` for consistency.

  Resolves: #1427

#### abstract_jb.c: Remove redundant timer check per static analysis.
  Author: Sean Bright
  Date:   2025-12-01

  While this check is technically unnecessary, it also was not harmful.

  The 2 other items mentioned in the linked issue are false positives
  and require no action.

  Resolves: #1417

#### channelstorage_cpp: Fix fallback return value in channelstorage callback
  Author: phoneben
  Date:   2025-11-26

  callback returned the last iterated channel when no match existed, causing invalid channel references and potential double frees. Updated to correctly return NULL when there is no match.

  Resolves: #1609

#### ccss:  Add option to ccss.conf to globally disable it.
  Author: George Joseph
  Date:   2025-11-19

  The Call Completion Supplementary Service feature is rarely used but many of
  it's functions are called by app_dial and channel.c "just in case".  These
  functions lock and unlock the channel just to see if CCSS is enabled on it,
  which it isn't 99.99% of the time.

  UserNote: A new "enabled" parameter has been added to ccss.conf.  It defaults
  to "yes" to preserve backwards compatibility but CCSS is rarely used so
  setting "enabled = no" in the "general" section can save some unneeded channel
  locking operations and log message spam.  Disabling ccss will also prevent
  the func_callcompletion and chan_dahdi modules from loading.

  DeveloperNote: A new API ast_is_cc_enabled() has been added.  It should be
  used to ensure that CCSS is enabled before making any other ast_cc_* calls.

#### app_directed_pickup.c: Change some log messages from NOTICE to VERBOSE.
  Author: George Joseph
  Date:   2025-11-20

  UpgradeNote: In an effort to reduce log spam, two normal progress
  "pickup attempted" log messages from app_directed_pickup have been changed
  from NOTICE to VERBOSE(3).  This puts them on par with other normal
  dialplan progress messages.

#### chan_websocket: Fix crash on DTMF_END event.
  Author: Sean Bright
  Date:   2025-11-20

  Resolves: #1604
#### chan_websocket.c: Tolerate other frame types
  Author: Joe Garlick
  Date:   2025-11-12

  Currently, if chan_websocket receives an un supported frame like comfort noise it will exit the websocket. The proposed change is to tolerate the other frames by not sending them down the websocket but instead just ignoring them.

  Resolves: #1587

#### app_reload: Fix Reload() without arguments.
  Author: Naveen Albert
  Date:   2025-11-17

  Calling Reload() without any arguments is supposed to reload
  everything (equivalent to a 'core reload'), but actually does
  nothing. This is because it was calling ast_module_reload with
  an empty string, and the argument needs to explicitly be NULL.

  Resolves: #1597

#### pbx.c: Print new context count when reloading dialplan.
  Author: Naveen Albert
  Date:   2025-11-17

  When running "dialplan reload", the number of contexts reported
  is initially wrong, as it is the old context count. Running
  "dialplan reload" a second time returns the correct number of
  contexts that are loaded. This can confuse users into thinking
  that the reload didn't work successfully the first time.

  This counter is currently only incremented when iterating the
  old contexts prior to the context merge; at the very end, get
  the current number of elements in the context hash table and
  report that instead. This way, the count is correct immediately
  whenever a reload occurs.

  Resolves: #1599

#### Makefile: Add module-list-* targets.
  Author: C. Maj
  Date:   2025-11-17

  Convenience wrappers for showing modules at various support levels.

  * module-list-core
  * module-list-extended
  * module-list-deprecated

  Resolves: #1572

  UserNote: Try "make module-list-deprecated" to see what modules
  are on their way out the door.

#### core_unreal.c: Use ast instead of p->chan to get the DIALSTATUS variable
  Author: Tinet-mucw
  Date:   2025-11-13

  After p->chan = NULL, ast still points to the valid channel object,
  using ast safely accesses the channel's DIALSTATUS variable before it's fully destroyed

  Resolves: #1590

#### ast_coredumper: Fix multiple issues
  Author: George Joseph
  Date:   2025-11-07

  * Fixed an issue with tarball-coredumps when asterisk was invoked without an
  absolute path.

  * Fixed an issue with gdb itself segfaulting when trying to get symbols from
  separate debuginfo files.  The command line arguments needed to be altered
  such that the gdbinit files is loaded before anything else but the
  `dump-asterisk` command is run after full initialization.

  In the embedded gdbinit script:

  * The extract_string_symbol function needed a `char *` cast to work properly.

  * The s_strip function needed to be updated to continue to work with the
  cpp_map_name_id channel storage backend.

  * A new function was added to dump the channels when cpp_map_name_id was
  used.

  * The Channel object was updated to account for the new channel storage
  backends

  * The show_locks function was refactored to work correctly.

#### app_mixmonitor: Add 's' (skip) option to delay recording.
  Author: Daouda Taha
  Date:   2025-10-28

  The 's' (skip) option delays MixMonitor recording until the specified number of seconds
  (can be fractional) have elapsed since MixMonitor was invoked.

  No audio is written to the recording file during this time. If the call ends before this
  period, no audio will be saved. This is useful for avoiding early audio such as
  announcements, ringback tones, or other non-essential sounds.

  UserNote: This change introduces a new 's(<seconds>)' (skip) option to the MixMonitor
  application. Example:
    MixMonitor(${UNIQUEID}.wav,s(3))

  This skips recording for the first 3 seconds before writing audio to the file.
  Existing MixMonitor behavior remains unchanged when the 's' option is not used.

#### stasis: switch stasis show topics temporary container from list - RBtree
  Author: phoneben
  Date:   2025-11-11

  switch stasis show topics temporary container from list to RB-tree
  minimizing lock time

  Resolves: #1585

#### app_dtmfstore: Avoid a potential buffer overflow.
  Author: Sean Bright
  Date:   2025-11-07

  Prefer snprintf() so we can readily detect if our output was
  truncated.

  Resolves: #1421

#### main: Explicitly mark case statement fallthrough as such.
  Author: Sean Bright
  Date:   2025-11-07

  Resolves: #1442

#### bridge_softmix: Return early on topology allocation failure.
  Author: Sean Bright
  Date:   2025-11-07

  Resolves: #1446

#### bridge_simple: Increase code verbosity for clarity.
  Author: Sean Bright
  Date:   2025-11-07

  There's no actual problem here, but I can see how it might by
  confusing.

  Resolves: #1444

#### app_queue.c: Only announce to head caller if announce_to_first_user
  Author: Kristian F. Høgh
  Date:   2025-10-30

  Only make announcements to head caller if announce_to_first_user is true

  Fixes: #1568

  UserNote: When announce_to_first_user is false, no announcements are played to the head caller

#### chan_websocket: Add ability to place a MARK in the media stream.
  Author: George Joseph
  Date:   2025-11-05

  Also cleaned up a few unused #if blocks, and started sending a few ERROR
  events back to the apps.

  Resolves: #1574

  DeveloperNote: Apps can now send a `MARK_MEDIA` command with an optional
  `correlation_id` parameter to chan_websocket which will be placed in the
  media frame queue. When that frame is dequeued after all intervening media
  has been played to the core, chan_websocket will send a
  `MEDIA_MARK_PROCESSED` event to the app with the same correlation_id
  (if any).

#### chan_websocket: Add capability for JSON control messages and events.
  Author: George Joseph
  Date:   2025-10-22

  With recent enhancements to chan_websocket, the original plain-text
  implementation of control messages and events is now too limiting.  We
  probably should have used JSON initially but better late than never.  Going
  forward, enhancements that require control message or event changes will
  only be done to the JSON variants and the plain-text variants are now
  deprecated but not yet removed.

  * Added the chan_websocket.conf config file that allows setting which control
  message format to use globally: "json" or "plain-text".  "plain-text" is the
  default for now to preserve existing behavior.

  * Added a dialstring option `f(json|plain-text)` to allow the format to be
  overridden on a call-by-call basis.  Again, 'plain-text' is the default for
  now to preserve existing behavior.

  The JSON for commands sent by the app to Asterisk must be...
  `{ "command": "<command>" ... }` where `<command>` is one of `ANSWER`, `HANGUP`,
  `START_MEDIA_BUFFERING`, etc.  The `STOP_MEDIA_BUFFERING` command takes an
  additional, optional parameter to be returned in the corresponding
  `MEDIA_BUFFERING_COMPLETED` event:
  `{ "command": "STOP_MEDIA_BUFFERING", "correlation_id": "<correlation id>" }`.

  The JSON for events sent from Asterisk to the app will be...
  `{ "event": "<event>", "channel_id": "<channel_id>" ... }`.
  The `MEDIA_START` event will now look like...

  ```
  {
    "event": "MEDIA_START",
    "connection_id": "media_connection1",
    "channel": "WebSocket/media_connection1/0x5140001a0040",
    "channel_id": "1761245643.1",
    "format": "ulaw",
    "optimal_frame_size": 160,
    "ptime": 20,
    "channel_variables": {
      "DIALEDPEERNUMBER": "media_connection1/c(ulaw)",
      "MEDIA_WEBSOCKET_CONNECTION_ID": "media_connection1",
      "MEDIA_WEBSOCKET_OPTIMAL_FRAME_SIZE": "160"
    }
  }
  ```

  Note the addition of the channel variables which can't be supported
  with the plain-text formatting.

  The documentation will be updated with the exact formats for all commands
  and events.

  Resolves: #1546
  Resolves: #1563

  DeveloperNote: The chan_websocket plain-text control and event messages are now
  deprecated (but remain the default) in favor of JSON formatted messages.
  See https://docs.asterisk.org/Configuration/Channel-Drivers/WebSocket for
  more information.

  DeveloperNote: A "transport_data" parameter has been added to the
  channels/externalMedia ARI endpoint which, for websocket, allows the caller
  to specify parameters to be added to the dialstring for the channel.  For
  instance, `"transport_data": "f(json)"`.

#### build: Add menuselect options to facilitate code tracing and coverage
  Author: George Joseph
  Date:   2025-10-30

  The following options have been added to the menuselect "Compiler Flags"
  section...

  CODE_COVERAGE: The ability to enable code coverage via the `--enable-coverage`
  configure flag has existed for many years but changing it requires
  re-running ./configure which is painfully slow.  With this commit, you can
  now enable and disable it via menuselect. Setting this option adds the
  `-ftest-coverage` and `-fprofile-arcs` flags on the gcc and ld command lines.
  It also sets DONT_OPTIMIZE. Note: If you use the `--enable-coverage` configure
  flag, you can't turn it off via menuselect so choose one method and stick to
  it.

  KEEP_FRAME_POINTERS: This option sets `-fno-omit-frame-pointers` on the gcc
  command line which can facilitate debugging with 'gdb' and tracing with 'perf'.
  Unlike CODE_COVERAGE, this option doesn't depend on optimization being
  disabled.  It does however conflict with COMPILE_DOUBLE.

