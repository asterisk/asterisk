
## Change Log for Release asterisk-22.5.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.5.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.4.1...22.5.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.5.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 29
- Commit Authors: 14
- Issues Resolved: 19
- Security Advisories Resolved: 1
  - [GHSA-c7p6-7mvq-8jq2](https://github.com/asterisk/asterisk/security/advisories/GHSA-c7p6-7mvq-8jq2): cli_permissions.conf: deny option does not work for disallowing shell commands

### User Notes:

- #### res_stir_shaken.so: Handle X5U certificate chains.                              
  The STIR/SHAKEN verification process will now load a full
  certificate chain retrieved via the X5U URL instead of loading only
  the end user cert.

- #### res_stir_shaken: Add "ignore_sip_date_header" config option.                    
  A new STIR/SHAKEN verification option "ignore_sip_date_header" has
  been added that when set to true, will cause the verification process to
  not consider a missing or invalid SIP "Date" header to be a failure.  This
  will make the IAT the sole "truth" for Date in the verification process.
  The option can be set in the "verification" and "profile" sections of
  stir_shaken.conf.
  Also fixed a bug in the port match logic.
  Resolves: #1251
  Resolves: #1271

- #### app_record: Add RECORDING_INFO function.                                        
  The RECORDING_INFO function can now be used
  to retrieve the duration of a recording.

- #### app_queue: queue rules – Add support for QUEUE_RAISE_PENALTY=rN to raise penal..
  This change introduces QUEUE_RAISE_PENALTY=rN, allowing selective penalty raises
  only for members whose current penalty is within the [min_penalty, max_penalty] range.
  Members with lower or higher penalties are unaffected.
  This behavior is backward-compatible with existing queue rule configurations.

- #### res_odbc: cache_size option to limit the cached connections.                    
  New cache_size option for res_odbc to on a per class basis limit the
  number of cached connections. Please reference the sample configuration
  for details.

- #### res_odbc: cache_type option for res_odbc.                                       
  When using res_odbc it should be noted that back-end
  connections to the underlying database can now be configured to re-use
  the cached connections in a round-robin manner rather than repeatedly
  re-using the same connection.  This helps to keep connections alive, and
  to purge dead connections from the system, thus more dynamically
  adjusting to actual load.  The downside is that one could keep too many
  connections active for a longer time resulting in resource also begin
  consumed on the database side.

- #### ARI Outbound Websockets                                                         
  Asterisk can now establish websocket sessions _to_ your ARI applications
  as well as accepting websocket sessions _from_ them.
  Full details: http://s.asterisk.net/ari-outbound-ws

- #### res_websocket_client: Create common utilities for websocket clients.            
  A new module "res_websocket_client" and config file
  "websocket_client.conf" have been added to support several upcoming new
  capabilities that need common websocket client configuration.

- #### asterisk.c: Add option to restrict shell access from remote consoles.           
  A new asterisk.conf option 'disable_remote_console_shell' has
  been added that, when set, will prevent remote consoles from executing
  shell commands using the '!' prefix.
  Resolves: #GHSA-c7p6-7mvq-8jq2

- #### sig_analog: Add Call Waiting Deluxe support.                                    
  Call Waiting Deluxe can now be enabled for FXS channels
  by enabling its corresponding option.


### Upgrade Notes:

- #### jansson: Upgrade version to jansson 2.14.1                                      
  jansson has been upgraded to 2.14.1. For more
  information visit jansson Github page: https://github.com/akheron/jansson/releases/tag/v2.14.1
  Resolves: #1178

- #### Alternate Channel Storage Backends                                              
  With this release, you can now select an alternate channel
  storage backend based on C++ Maps.  Using the new backend may increase
  performance and reduce the chances of deadlocks on heavily loaded systems.
  For more information, see http://s.asterisk.net/dc679ec3


### Commit Authors:

- George Joseph: (10)
- Itzanh: (1)
- Jaco Kroon: (2)
- Joe Searle: (1)
- Michal Hajek: (1)
- Mike Bradeen: (2)
- Mkmer: (1)
- Nathan Monfils: (1)
- Naveen Albert: (3)
- Phoneben: (1)
- Sean Bright: (2)
- Stanislav Abramenkov: (1)
- Sven Kube: (2)
- Thomas B. Clark: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-c7p6-7mvq-8jq2: cli_permissions.conf: deny option does not work for disallowing shell commands
  - 271: [new-feature]: sig_analog: Add Call Waiting Deluxe support.
  - 548: [improvement]: Get Record() audio duration/length
  - 1088: [bug]: app_sms: Compilation failure in DEVMODE due to stringop-overflow error in GCC 15 pre-release
  - 1141: [bug]: res_pjsip: Contact header set incorrectly for call redirect (302 Moved temp.) when external_* set
  - 1178: [improvement]: jansson: Upgrade version to jansson 2.14.1
  - 1230: [bug]: ast_frame_adjust_volume and ast_frame_adjust_volume_float crash on interpolated frames
  - 1234: [bug]: Set CalllerID lost on DTMF attended transfer
  - 1240: [bug]: WebRTC invites failing on Chrome 136
  - 1243: [bug]: make menuconfig fails due to changes in GTK callbacks
  - 1251: [improvement]: PJSIP shouldn't require SIP Date header to process full shaken passport which includes iat
  - 1254: [bug]: ActiveChannels not reported when using AMI command PJSIPShowEndpoint
  - 1271: [bug]: STIR/SHAKEN not accepting port 8443 in certificate URLs
  - 1272: [improvement]: STIR/SHAKEN handle X5U certificate chains
  - 1276: MixMonitor produces broken recordings in bridged calls with asymmetric codecs (e.g., alaw vs G.722)
  - 1279: [bug]: regression: 20.12.0 downgrades quality of wav16 recordings
  - 1282: [bug]: Alternate Channel Storage Backends menuselect not enabling it
  - 1287: [bug]: channelstorage.c: Compilation failure with DEBUG_FD_LEAKS
  - 1288: [bug]: Crash when destroying channel with C++ alternative storage backend enabled
  - ASTERISK-30373: sig_analog: Add Call Waiting Deluxe options

### Commits By Author:

- #### George Joseph (10):
  - Alternate Channel Storage Backends
  - lock.h: Add include for string.h when DEBUG_THREADS is defined.
  - asterisk.c: Add option to restrict shell access from remote consoles.
  - res_websocket_client: Create common utilities for websocket clients.
  - ARI Outbound Websockets
  - res_websocket_client:  Add more info to the XML documentation.
  - res_stir_shaken: Add "ignore_sip_date_header" config option.
  - res_stir_shaken.so: Handle X5U certificate chains.
  - channelstorage_cpp_map_name_id: Fix callback returning non-matching channels.
  - channelstorage: Rename callbacks that conflict with DEBUG_FD_LEAKS.

- #### Itzanh (1):
  - app_sms.c: Fix sending and receiving SMS messages in protocol 2

- #### Jaco Kroon (2):
  - res_odbc: cache_type option for res_odbc.
  - res_odbc: cache_size option to limit the cached connections.

- #### Joe Searle (1):
  - pjproject: Increase maximum SDP formats and attribute limits

- #### Michal Hajek (1):
  - audiohook.c: Improve frame pairing logic to avoid MixMonitor breakage with mix..

- #### Mike Bradeen (2):
  - chan_pjsip: Serialize INVITE creation on DTMF attended transfer
  - res_pjsip_nat.c: Do not overwrite transfer host

- #### Nathan Monfils (1):
  - manager.c: Invalid ref-counting when purging events

- #### Naveen Albert (3):
  - app_sms: Ignore false positive vectorization warning.
  - sig_analog: Add Call Waiting Deluxe support.
  - app_record: Add RECORDING_INFO function.

- #### Sean Bright (2):
  - res_pjsip: Fix empty `ActiveChannels` property in AMI responses.
  - channelstorage_makeopts.xml: Remove errant XML character.

- #### Stanislav Abramenkov (1):
  - jansson: Upgrade version to jansson 2.14.1

- #### Sven Kube (2):
  - res_audiosocket.c: Set the TCP_NODELAY socket option
  - res_audiosocket.c: Add retry mechanism for reading data from AudioSocket

- #### Thomas B. Clark (1):
  - menuselect: Fix GTK menu callbacks for Fedora 42 compatibility

- #### mkmer (1):
  - frame.c: validate frame data length is less than samples when adjusting volume

- #### phoneben (1):
  - app_queue: queue rules – Add support for QUEUE_RAISE_PENALTY=rN to raise penal..


### Commit List:

-  channelstorage: Rename callbacks that conflict with DEBUG_FD_LEAKS.
-  channelstorage_cpp_map_name_id: Fix callback returning non-matching channels.
-  channelstorage_makeopts.xml: Remove errant XML character.
-  res_stir_shaken.so: Handle X5U certificate chains.
-  res_stir_shaken: Add "ignore_sip_date_header" config option.
-  app_record: Add RECORDING_INFO function.
-  app_sms.c: Fix sending and receiving SMS messages in protocol 2
-  res_websocket_client:  Add more info to the XML documentation.
-  res_odbc: cache_size option to limit the cached connections.
-  res_odbc: cache_type option for res_odbc.
-  res_pjsip: Fix empty `ActiveChannels` property in AMI responses.
-  ARI Outbound Websockets
-  res_websocket_client: Create common utilities for websocket clients.
-  asterisk.c: Add option to restrict shell access from remote consoles.
-  frame.c: validate frame data length is less than samples when adjusting volume
-  res_audiosocket.c: Add retry mechanism for reading data from AudioSocket
-  res_audiosocket.c: Set the TCP_NODELAY socket option
-  menuselect: Fix GTK menu callbacks for Fedora 42 compatibility
-  jansson: Upgrade version to jansson 2.14.1
-  pjproject: Increase maximum SDP formats and attribute limits
-  manager.c: Invalid ref-counting when purging events
-  res_pjsip_nat.c: Do not overwrite transfer host
-  chan_pjsip: Serialize INVITE creation on DTMF attended transfer
-  sig_analog: Add Call Waiting Deluxe support.
-  app_sms: Ignore false positive vectorization warning.
-  lock.h: Add include for string.h when DEBUG_THREADS is defined.
-  Alternate Channel Storage Backends

### Commit Details:

#### channelstorage: Rename callbacks that conflict with DEBUG_FD_LEAKS.
  Author: George Joseph
  Date:   2025-07-08

  DEBUG_FD_LEAKS replaces calls to "open" and "close" with functions that keep
  track of file descriptors, even when those calls are actually callbacks
  defined in structures like ast_channelstorage_instance->open and don't touch
  file descriptors.  This causes compilation failures.  Those callbacks
  have been renamed to "open_instance" and "close_instance" respectively.

  Resolves: #1287

#### channelstorage_cpp_map_name_id: Fix callback returning non-matching channels.
  Author: George Joseph
  Date:   2025-07-09

  When the callback() API was invoked but no channel passed the test, callback
  would return the last channel tested instead of NULL.  It now correctly
  returns NULL when no channel matches.

  Resolves: #1288

#### audiohook.c: Improve frame pairing logic to avoid MixMonitor breakage with mix..
  Author: Michal Hajek
  Date:   2025-05-21

  This patch adjusts the read/write synchronization logic in audiohook_read_frame_both()
  to better handle calls where participants use different codecs or sample sizes
  (e.g., alaw vs G.722). The previous hard threshold of 2 * samples caused MixMonitor
  recordings to break or stutter when frames were not aligned between both directions.

  The new logic uses a more tolerant limit (1.5 * samples), which prevents audio tearing
  without causing excessive buffer overruns. This fix specifically addresses issues
  with MixMonitor when recording directly on a channel in a bridge using mixed codecs.

  Reported-by: Michal Hajek <michal.hajek@daktela.com>

  Resolves: #1276
  Resolves: #1279

#### channelstorage_makeopts.xml: Remove errant XML character.
  Author: Sean Bright
  Date:   2025-06-30

  Resolves: #1282

#### res_stir_shaken.so: Handle X5U certificate chains.
  Author: George Joseph
  Date:   2025-06-18

  The verification process will now load a full certificate chain retrieved
  via the X5U URL instead of loading only the end user cert.

  * Renamed crypto_load_cert_from_file() and crypto_load_cert_from_memory()
  to crypto_load_cert_chain_from_file() and crypto_load_cert_chain_from_memory()
  respectively.

  * The two load functions now continue to load certs from the file or memory
  PEMs and store them in a separate stack of untrusted certs specific to the
  current verification context.

  * crypto_is_cert_trusted() now uses the stack of untrusted certs that were
  extracted from the PEM in addition to any untrusted certs that were passed
  in from the configuration (and any CA certs passed in from the config of
  course).

  Resolves: #1272

  UserNote: The STIR/SHAKEN verification process will now load a full
  certificate chain retrieved via the X5U URL instead of loading only
  the end user cert.


#### res_stir_shaken: Add "ignore_sip_date_header" config option.
  Author: George Joseph
  Date:   2025-06-15

  UserNote: A new STIR/SHAKEN verification option "ignore_sip_date_header" has
  been added that when set to true, will cause the verification process to
  not consider a missing or invalid SIP "Date" header to be a failure.  This
  will make the IAT the sole "truth" for Date in the verification process.
  The option can be set in the "verification" and "profile" sections of
  stir_shaken.conf.

  Also fixed a bug in the port match logic.

  Resolves: #1251
  Resolves: #1271

#### app_record: Add RECORDING_INFO function.
  Author: Naveen Albert
  Date:   2024-01-22

  Add a function that can be used to retrieve info
  about a previous recording, such as its duration.

  This is being added as a function to avoid possibly
  trampling on dialplan variables, and could be extended
  to provide other information in the future.

  Resolves: #548

  UserNote: The RECORDING_INFO function can now be used
  to retrieve the duration of a recording.


#### app_sms.c: Fix sending and receiving SMS messages in protocol 2
  Author: Itzanh
  Date:   2025-04-06

  This fixes bugs in SMS messaging to SMS-capable analog phones that prevented app_sms.c from talking to phones using SMS protocol 2.

  - Fix MORX message reception (from phone to Asterisk) in SMS protocol 2
  - Fix MTTX message transmission (from Asterisk to phone) in SMS protocol 2

  One of the bugs caused messages to have random characters and junk appended at the end up to the character limit. Another bug prevented Asterisk from sending messages from Asterisk to the phone at all. A final bug caused the transmission from Asterisk to the phone to take a long time because app_sms.c did not hang up after correctly sending the message, causing the phone to have to time out and hang up in order to complete the message transmission.

  This was tested with a Linksys PAP2T and with a GrandStream HT814, sending and receiving messages with Telefónica DOMO Mensajes phones from Telefónica Spain. I had to play with both the network jitter buffer and the dB gain to get it to work. One of my phones required the gain to be set to +3dB for it to work, while another required it to be set to +6dB.

  Only MORX and MTTX were tested, I did not test sending and receiving messages to a TelCo SMSC.


#### app_queue: queue rules – Add support for QUEUE_RAISE_PENALTY=rN to raise penal..
  Author: phoneben
  Date:   2025-05-26

  This update adds support for a new QUEUE_RAISE_PENALTY format: rN

  When QUEUE_RAISE_PENALTY is set to rN (e.g., r4), only members whose current penalty
  is greater than or equal to the defined min_penalty and less than or equal to max_penalty
  will have their penalty raised to N.

  Members with penalties outside the min/max range remain unchanged.

  Example behaviors:

  QUEUE_RAISE_PENALTY=4     → Raise all members with penalty < 4 (existing behavior)
  QUEUE_RAISE_PENALTY=r4    → Raise only members with penalty in [min_penalty, max_penalty] to 4

  Implementation details:

  Adds parsing logic to detect the r prefix and sets the raise_respect_min flag

  Modifies the raise logic to skip members outside the defined penalty range when the flag is active

  UserNote: This change introduces QUEUE_RAISE_PENALTY=rN, allowing selective penalty raises
  only for members whose current penalty is within the [min_penalty, max_penalty] range.
  Members with lower or higher penalties are unaffected.
  This behavior is backward-compatible with existing queue rule configurations.


#### res_websocket_client:  Add more info to the XML documentation.
  Author: George Joseph
  Date:   2025-06-05

  Added "see-also" links to chan_websocket and ARI Outbound WebSocket and
  added an example configuration for each.


#### res_odbc: cache_size option to limit the cached connections.
  Author: Jaco Kroon
  Date:   2024-12-13

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

  UserNote: New cache_size option for res_odbc to on a per class basis limit the
  number of cached connections. Please reference the sample configuration
  for details.


#### res_odbc: cache_type option for res_odbc.
  Author: Jaco Kroon
  Date:   2024-12-10

  This enables setting cache_type classes to a round-robin queueing system
  rather than the historic stack mechanism.

  This should result in lower risk of connection drops due to shorter idle
  times (the first connection to go onto the stack could in theory never
  be used again, ever, but sit there consuming resources, there could be
  multiple of these).

  And with a queue rather than a stack, dead connections are guaranteed to
  be detected and purged eventually.

  This should end up better balancing connection_cnt with actual load
  over time, assuming the database doesn't keep connections open
  excessively long from it's side.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

  UserNote: When using res_odbc it should be noted that back-end
  connections to the underlying database can now be configured to re-use
  the cached connections in a round-robin manner rather than repeatedly
  re-using the same connection.  This helps to keep connections alive, and
  to purge dead connections from the system, thus more dynamically
  adjusting to actual load.  The downside is that one could keep too many
  connections active for a longer time resulting in resource also begin
  consumed on the database side.


#### res_pjsip: Fix empty `ActiveChannels` property in AMI responses.
  Author: Sean Bright
  Date:   2025-05-27

  The logic appears to have been reversed since it was introduced in
  05cbf8df.

  Resolves: #1254

#### ARI Outbound Websockets
  Author: George Joseph
  Date:   2025-03-28

  Asterisk can now establish websocket sessions _to_ your ARI applications
  as well as accepting websocket sessions _from_ them.
  Full details: http://s.asterisk.net/ari-outbound-ws

  Code change summary:
  * Added an ast_vector_string_join() function,
  * Added ApplicationRegistered and ApplicationUnregistered ARI events.
  * Converted res/ari/config.c to use sorcery to process ari.conf.
  * Added the "outbound-websocket" ARI config object.
  * Refactored res/ari/ari_websockets.c to handle outbound websockets.
  * Refactored res/ari/cli.c for the sorcery changeover.
  * Updated res/res_stasis.c for the sorcery changeover.
  * Updated apps/app_stasis.c to allow initiating per-call outbound websockets.
  * Added CLI commands to manage ARI websockets.
  * Added the new "outbound-websocket" object to ari.conf.sample.
  * Moved the ARI XML documentation out of res_ari.c into res/ari/ari_doc.xml

  UserNote: Asterisk can now establish websocket sessions _to_ your ARI applications
  as well as accepting websocket sessions _from_ them.
  Full details: http://s.asterisk.net/ari-outbound-ws


#### res_websocket_client: Create common utilities for websocket clients.
  Author: George Joseph
  Date:   2025-05-02

  Since multiple Asterisk capabilities now need to create websocket clients
  it makes sense to create a common set of utilities rather than making
  each of those capabilities implement their own.

  * A new configuration file "websocket_client.conf" is used to store common
  client parameters in named configuration sections.
  * APIs are provided to list and retrieve ast_websocket_client objects created
  from the named configurations.
  * An API is provided that accepts an ast_websocket_client object, connects
  to the remote server with retries and returns an ast_websocket object. TLS is
  supported as is basic authentication.
  * An observer can be registered to receive notification of loaded or reloaded
  client objects.
  * An API is provided to compare an existing client object to one just
  reloaded and return the fields that were changed. The caller can then decide
  what action to take based on which fields changed.

  Also as part of thie commit, several sorcery convenience macros were created
  to make registering common object fields easier.

  UserNote: A new module "res_websocket_client" and config file
  "websocket_client.conf" have been added to support several upcoming new
  capabilities that need common websocket client configuration.


#### asterisk.c: Add option to restrict shell access from remote consoles.
  Author: George Joseph
  Date:   2025-05-19

  UserNote: A new asterisk.conf option 'disable_remote_console_shell' has
  been added that, when set, will prevent remote consoles from executing
  shell commands using the '!' prefix.

  Resolves: #GHSA-c7p6-7mvq-8jq2

#### frame.c: validate frame data length is less than samples when adjusting volume
  Author: mkmer
  Date:   2025-05-12

  Resolves: #1230

#### res_audiosocket.c: Add retry mechanism for reading data from AudioSocket
  Author: Sven Kube
  Date:   2025-05-13

  The added retry mechanism addresses an issue that arises when fragmented TCP
  packets are received, each containing only a portion of an AudioSocket packet.
  This situation can occur if the external service sending the AudioSocket data
  has Nagle's algorithm enabled.


#### res_audiosocket.c: Set the TCP_NODELAY socket option
  Author: Sven Kube
  Date:   2025-05-13

  Disable Nagle's algorithm by setting the TCP_NODELAY socket option.
  This reduces latency by preventing delays caused by packet buffering.


#### menuselect: Fix GTK menu callbacks for Fedora 42 compatibility
  Author: Thomas B. Clark
  Date:   2025-05-12

  This patch resolves a build failure in `menuselect_gtk.c` when running
  `make menuconfig` on Fedora 42. The new version of GTK introduced stricter
  type checking for callback signatures.

  Changes include:
  - Add wrapper functions to match the expected `void (*)(void)` signature.
  - Update `menu_items` array to use these wrappers.

  Fixes: #1243

#### jansson: Upgrade version to jansson 2.14.1
  Author: Stanislav Abramenkov
  Date:   2025-03-24

  UpgradeNote: jansson has been upgraded to 2.14.1. For more
  information visit jansson Github page: https://github.com/akheron/jansson/releases/tag/v2.14.1

  Resolves: #1178

#### pjproject: Increase maximum SDP formats and attribute limits
  Author: Joe Searle
  Date:   2025-05-15

  Since Chrome 136, using Windows, when initiating a video call the INVITE SDP exceeds the maximum number of allowed attributes, resulting in the INVITE being rejected. This increases the attribute limit and the number of formats allowed when using bundled pjproject.

  Fixes: #1240

#### manager.c: Invalid ref-counting when purging events
  Author: Nathan Monfils
  Date:   2025-05-05

  We have a use-case where we generate a *lot* of events on the AMI, and
  then when doing `manager show eventq` we would see some events which
  would linger for hours or days in there. Obviously something was leaking.
  Testing allowed us to track down this logic bug in the ref-counting on
  the event purge.

  Reproducing the bug was not super trivial, we managed to do it in a
  production-like load testing environment with multiple AMI consumers.

  The race condition itself:

  1. something allocates and links `session`
  2. `purge_sessions` iterates over that `session` (takes ref)
  3. `purge_session` correctly de-referencess that session
  4. `purge_session` re-evaluates the while() loop, taking a reference
  5. `purge_session` exits (`n_max > 0` is false)
  6. whatever allocated the `session` deallocates it, but a reference is
     now lost since we exited the `while` loop before de-referencing.
  7. since the destructor is never called, the session->last_ev->usecount
     is never decremented, leading to events lingering in the queue

  The impact of this bug does not seem major. The events are small and do
  not seem, from our testing, to be causing meaningful additional CPU
  usage. Mainly we wanted to fix this issue because we are internally
  adding prometheus metrics to the eventq and those leaked events were
  causing the metrics to show garbage data.


#### res_pjsip_nat.c: Do not overwrite transfer host
  Author: Mike Bradeen
  Date:   2025-05-08

  When a call is transfered via dialplan behind a NAT, the
  host portion of the Contact header in the 302 will no longer
  be over-written with the external NAT IP and will retain the
  hostname.

  Fixes: #1141

#### chan_pjsip: Serialize INVITE creation on DTMF attended transfer
  Author: Mike Bradeen
  Date:   2025-05-05

  When a call is transfered via DTMF feature code, the Transfer Target and
  Transferer are bridged immediately.  This opens the possibilty of a race
  condition between the creation of an INVITE and the bridge induced colp
  update that can result in the set caller ID being over-written with the
  transferer's default info.

  Fixes: #1234

#### sig_analog: Add Call Waiting Deluxe support.
  Author: Naveen Albert
  Date:   2023-08-24

  Adds support for Call Waiting Deluxe options to enhance
  the current call waiting feature.

  As part of this change, a mechanism is also added that
  allows a channel driver to queue an audio file for Dial()
  to play, which is necessary for the announcement function.

  ASTERISK-30373 #close

  Resolves: #271

  UserNote: Call Waiting Deluxe can now be enabled for FXS channels
  by enabling its corresponding option.


#### app_sms: Ignore false positive vectorization warning.
  Author: Naveen Albert
  Date:   2025-01-24

  Ignore gcc warning about writing 32 bytes into a region of size 6,
  since we check that we don't go out of bounds for each byte.
  This is due to a vectorization bug in gcc 15, stemming from
  gcc commit 68326d5d1a593dc0bf098c03aac25916168bc5a9.

  Resolves: #1088

#### lock.h: Add include for string.h when DEBUG_THREADS is defined.
  Author: George Joseph
  Date:   2025-05-02

  When DEBUG_THREADS is defined, lock.h uses strerror(), which is defined
  in the libc string.h file, to print warning messages. If the including
  source file doesn't include string.h then strerror() won't be found and
  and compile errors will be thrown. Since lock.h depends on this, string.h
  is now included from there if DEBUG_THREADS is defined.  This way, including
  source files don't have to worry about it.


#### Alternate Channel Storage Backends
  Author: George Joseph
  Date:   2024-12-31

  Full details: http://s.asterisk.net/dc679ec3

  The previous proof-of-concept showed that the cpp_map_name_id alternate
  storage backed performed better than all the others so this final PR
  adds only that option.  You still need to enable it in menuselect under
  the "Alternate Channel Storage Backends" category.

  To select which one is used at runtime, set the "channel_storage_backend"
  option in asterisk.conf to one of the values described in
  asterisk.conf.sample.  The default remains "ao2_legacy".

  UpgradeNote: With this release, you can now select an alternate channel
  storage backend based on C++ Maps.  Using the new backend may increase
  performance and reduce the chances of deadlocks on heavily loaded systems.
  For more information, see http://s.asterisk.net/dc679ec3

