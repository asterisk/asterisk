
## Change Log for Release asterisk-20.9.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.9.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.8.1...20.9.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.9.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 20
- Commit Authors: 9
- Issues Resolved: 8
- Security Advisories Resolved: 0

### User Notes:

- #### app_voicemail_odbc: Allow audio to be kept on disk                              
  This commit adds a new voicemail.conf option
  'odbc_audio_on_disk' which when set causes the ODBC variant of
  app_voicemail_odbc to leave the message and greeting audio files
  on disk and only store the message metadata in the database.
  Much more information can be found in the voicemail.conf.sample
  file.

- #### app_queue:  Add option to not log Restricted Caller ID to queue_log             
  Add a Queue option log-restricted-caller-id to control whether the Restricted Caller ID
  will be stored in the queue log.
  If log-restricted-caller-id=no then the Caller ID will be stripped if the Caller ID is restricted.

- #### pbx.c: expand fields width of "core show hints"                                 
  The fields width of "core show hints" were increased.
  The width of "extension" field to 30 characters and
  the width of the "device state id" field to 60 characters.

- #### rtp_engine: add support for multirate RFC2833 digits                            
  No change in configuration is required in order to enable this
  feature. Endpoints configured to use RFC2833 will automatically have this
  enabled. If the endpoint does not support this, it should not include it in
  the SDP offer/response.
  Resolves: #699


### Upgrade Notes:

- #### app_queue:  Add option to not log Restricted Caller ID to queue_log             
  Add a new column to the queues table:
  queue_log_option_log_restricted ENUM('0','1','off','on','false','true','no','yes')
  to control whether the Restricted Caller ID will be stored in the queue log.


### Commit Authors:

- Alexei Gradinari: (2)
- Bastian Triller: (1)
- Chrsmj: (1)
- George Joseph: (4)
- Igor Goncharovsky: (1)
- Mike Bradeen: (2)
- Sean Bright: (7)
- Tinet-Mucw: (1)
- Walter Doekes: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 699: [improvement]: Add support for multi-rate DTMF
  - 736: [bug]: Seg fault on CLI after PostgreSQL CDR module fails to load for a second time
  - 765: [improvement]: Add option to not log Restricted Caller ID to queue_log
  - 770: [improvement]: pbx.c: expand fields width of "core show hints"
  - 776: [bug] DTMF broken after rtp_engine: add support for multirate RFC2833 digits commit
  - 783: [bug]: Under certain circumstances a channel snapshot can get orphaned in the cache
  - 789: [bug]: Mediasec headers aren't sent on outgoing INVITEs
  - 797: [bug]: 

### Commits By Author:

- ### Alexei Gradinari (2):
  - pbx.c: expand fields width of "core show hints"
  - app_queue:  Add option to not log Restricted Caller ID to queue_log

- ### Bastian Triller (1):
  - cli: Show configured cache dir

- ### George Joseph (4):
  - app_voicemail_odbc: Allow audio to be kept on disk
  - stasis_channels: Use uniqueid and name to delete old snapshots
  - security_agreement.c: Always add the Require and Proxy-Require headers
  - ast-db-manage: Remove duplicate enum creation

- ### Igor Goncharovsky (1):
  - res_pjsip_path.c: Fix path when dialing using PJSIP_DIAL_CONTACTS()

- ### Mike Bradeen (2):
  - rtp_engine: add support for multirate RFC2833 digits
  - res_pjsip_sdp_rtp: Add support for default/mismatched 8K RFC 4733/2833 digits

- ### Sean Bright (7):
  - file.h: Rename function argument to avoid C++ keyword clash.
  - bundled_pjproject: Disable UPnP support.
  - asterisk.c: Don't log an error if .asterisk_history does not exist.
  - xml.c: Update deprecated libxml2 API usage.
  - manager.c: Properly terminate `CoreShowChannelMap` event.
  - pjsip: Add PJSIP_PARSE_URI_FROM dialplan function.
  - logger.h: Include SCOPE_CALL_WITH_INT_RESULT() in non-dev-mode builds.

- ### Tinet-mucw (1):
  - bridge_basic.c: Make sure that ast_bridge_channel is not destroyed while itera..

- ### Walter Doekes (1):
  - chan_ooh323: Fix R/0 typo in docs

- ### chrsmj (1):
  - cdr_pgsql: Fix crash when the module fails to load multiple times.


### Commit List:

-  res_pjsip_path.c: Fix path when dialing using PJSIP_DIAL_CONTACTS()
-  res_pjsip_sdp_rtp: Add support for default/mismatched 8K RFC 4733/2833 digits
-  ast-db-manage: Remove duplicate enum creation
-  security_agreement.c: Always add the Require and Proxy-Require headers
-  logger.h: Include SCOPE_CALL_WITH_INT_RESULT() in non-dev-mode builds.
-  stasis_channels: Use uniqueid and name to delete old snapshots
-  app_voicemail_odbc: Allow audio to be kept on disk
-  app_queue:  Add option to not log Restricted Caller ID to queue_log
-  pbx.c: expand fields width of "core show hints"
-  pjsip: Add PJSIP_PARSE_URI_FROM dialplan function.
-  manager.c: Properly terminate `CoreShowChannelMap` event.
-  cli: Show configured cache dir
-  xml.c: Update deprecated libxml2 API usage.
-  cdr_pgsql: Fix crash when the module fails to load multiple times.
-  asterisk.c: Don't log an error if .asterisk_history does not exist.
-  chan_ooh323: Fix R/0 typo in docs
-  bundled_pjproject: Disable UPnP support.
-  file.h: Rename function argument to avoid C++ keyword clash.
-  rtp_engine: add support for multirate RFC2833 digits

### Commit Details:

#### res_pjsip_path.c: Fix path when dialing using PJSIP_DIAL_CONTACTS()
  Author: Igor Goncharovsky
  Date:   2024-05-12

  When using the PJSIP_DIAL_CONTACTS() function for use in the Dial()
  command, the contacts are returned in text form, so the input to
  the path_outgoing_request() function is a contact value of NULL.
  The issue was reported in ASTERISK-28211, but was not actually fixed
  in ASTERISK-30100. This fix brings back the code that was previously
  removed and adds code to search for a contact to extract the path
  value from it.


#### res_pjsip_sdp_rtp: Add support for default/mismatched 8K RFC 4733/2833 digits
  Author: Mike Bradeen
  Date:   2024-06-21

  After change made in 624f509 to add support for non 8K RFC 4733/2833 digits,
  Asterisk would only accept RFC 4733/2833 offers that matched the sample rate of
  the negotiated codec(s).

  This change allows Asterisk to accept 8K RFC 4733/2833 offers if the UAC
  offfers 8K RFC 4733/2833 but negotiates for a non 8K bitrate codec.

  A number of corresponding tests in tests/channels/pjsip/dtmf_sdp also needed to
  be re-written to allow for these scenarios.

  Fixes: #776

#### ast-db-manage: Remove duplicate enum creation
  Author: George Joseph
  Date:   2024-07-08

  Remove duplicate creation of ast_bool_values from
  2b7c507d7d12_add_queue_log_option_log_restricted_.py.  This was
  causing alembic upgrades to fail since the enum was already created
  in fe6592859b85_fix_mwi_subscribe_replaces_.py back in 2018.

  Resolves: #797

#### security_agreement.c: Always add the Require and Proxy-Require headers
  Author: George Joseph
  Date:   2024-07-03

  The `Require: mediasec` and `Proxy-Require: mediasec` headers need
  to be sent whenever we send `Security-Client` or `Security-Verify`
  headers but the logic to do that was only in add_security_headers()
  in res_pjsip_outbound_register.  So while we were sending them on
  REGISTER requests, we weren't sending them on INVITE requests.

  This commit moves the logic to send the two headers out of
  res_pjsip_outbound_register:add_security_headers() and into
  security_agreement:ast_sip_add_security_headers().  This way
  they're always sent when we send `Security-Client` or
  `Security-Verify`.

  Resolves: #789

#### logger.h: Include SCOPE_CALL_WITH_INT_RESULT() in non-dev-mode builds.
  Author: Sean Bright
  Date:   2024-06-29

  Fixes #785


#### stasis_channels: Use uniqueid and name to delete old snapshots
  Author: George Joseph
  Date:   2024-05-08

  Whenver a new channel snapshot is created or when a channel is
  destroyed, we need to delete any existing channel snapshot from
  the snapshot cache.  Historically, we used the channel->snapshot
  pointer to delete any existing snapshots but this has two issues.

  First, if something (possibly ast_channel_internal_swap_snapshots)
  sets channel->snapshot to NULL while there's still a snapshot in
  the cache, we wouldn't be able to delete it and it would be orphaned
  when the channel is destroyed.  Since we use the cache to list
  channels from the CLI, AMI and ARI, it would appear as though the
  channel was still there when it wasn't.

  Second, since there are actually two caches, one indexed by the
  channel's uniqueid, and another indexed by the channel's name,
  deleting from the caches by pointer requires a sequential search of
  all of the hash table buckets in BOTH caches to find the matching
  snapshots.  Not very efficient.

  So, we now delete from the caches using the channel's uniqueid
  and name.  This solves both issues.

  This doesn't address how channel->snapshot might have been set
  to NULL in the first place because although we have concrete
  evidence that it's happening, we haven't been able to reproduce it.

  Resolves: #783

#### app_voicemail_odbc: Allow audio to be kept on disk
  Author: George Joseph
  Date:   2024-04-09

  This commit adds a new voicemail.conf option 'odbc_audio_on_disk'
  which when set causes the ODBC variant of app_voicemail to leave
  the message and greeting audio files on disk and only store the
  message metadata in the database.  This option came from a concern
  that the database could grow to large and cause remote access
  and/or replication to become slow.  In a clustering situation
  with this option, all asterisk instances would share the same
  database for the metadata and either use a shared filesystem
  or other filesystem replication service much more suitable
  for synchronizing files.

  The changes to app_voicemail to implement this feature were actually
  quite small but due to the complexity of the module, the actual
  source code changes were greater.  They fall into the following
  categories:

  * Tracing.  The module is so complex that it was impossible to
  figure out the path taken for various scenarios without the addition
  of many SCOPE_ENTER, SCOPE_EXIT and ast_trace statements, even in
  code that's not related to the functional change.  Making this worse
  was the fact that many "if" statements in this module didn't use
  braces.  Since the tracing macros add multiple statements, many "if"
  statements had to be converted to use braces.

  * Excessive use of PATH_MAX.  Previous maintainers of this module
  used PATH_MAX to allocate character arrays for filesystem paths
  and SQL statements as though they cost nothing.  In fact, PATH_MAX
  is defined as 4096 bytes!  Some functions had (and still have)
  multiples of these.  One function has 7.  Given that the vast
  majority of installations use the default spool directory path
  `/var/spool/asterisk/voicemail`, the actual path length is usually
  less than 80 bytes.  That's over 4000 bytes wasted.  It was the
  same for SQL statement buffers.  A 4K buffer for statement that
  only needed 60 bytes.  All of these PATH_MAX allocations in the
  ODBC related code were changed to dynamically allocated buffers.
  The rest will have to be addressed separately.

  * Bug fixes.  During the development of this feature, several
  pre-existing ODBC related bugs were discovered and fixed.  They
  had to do with leaving orphaned files on disk, not preserving
  original message ids when moving messages between folders,
  not honoring the "formats" config parameter in certain circumstances,
  etc.

  UserNote: This commit adds a new voicemail.conf option
  'odbc_audio_on_disk' which when set causes the ODBC variant of
  app_voicemail_odbc to leave the message and greeting audio files
  on disk and only store the message metadata in the database.
  Much more information can be found in the voicemail.conf.sample
  file.


#### bridge_basic.c: Make sure that ast_bridge_channel is not destroyed while itera..
  Author: Tinet-mucw
  Date:   2024-06-13

  Resolves: https://github.com/asterisk/asterisk/issues/768

#### app_queue:  Add option to not log Restricted Caller ID to queue_log
  Author: Alexei Gradinari
  Date:   2024-06-12

  Add a queue option log-restricted-caller-id to strip the Caller ID when storing the ENTERQUEUE event
  in the queue log if the Caller ID is restricted.

  Resolves: #765

  UpgradeNote: Add a new column to the queues table:
  queue_log_option_log_restricted ENUM('0','1','off','on','false','true','no','yes')
  to control whether the Restricted Caller ID will be stored in the queue log.

  UserNote: Add a Queue option log-restricted-caller-id to control whether the Restricted Caller ID
  will be stored in the queue log.
  If log-restricted-caller-id=no then the Caller ID will be stripped if the Caller ID is restricted.


#### pbx.c: expand fields width of "core show hints"
  Author: Alexei Gradinari
  Date:   2024-06-13

  The current width for "extension" is 20 and "device state id" is 20, which is too small.
  The "extension" field contains "ext"@"context", so 20 characters is not enough.
  The "device state id" field, for example for Queue pause state contains Queue:"queue_name"_pause_PSJIP/"endpoint", so the 20 characters is not enough.

  Increase the width of "extension" field to 30 characters and the width of the "device state id" field to 60 characters.

  Resolves: #770

  UserNote: The fields width of "core show hints" were increased.
  The width of "extension" field to 30 characters and
  the width of the "device state id" field to 60 characters.


#### pjsip: Add PJSIP_PARSE_URI_FROM dialplan function.
  Author: Sean Bright
  Date:   2024-06-02

  Various SIP headers permit a URI to be prefaced with a `display-name`
  production that can include characters (like commas and parentheses)
  that are problematic for Asterisk's dialplan parser and, specifically
  in the case of this patch, the PJSIP_PARSE_URI function.

  This patch introduces a new function - `PJSIP_PARSE_URI_FROM` - that
  behaves identically to `PJSIP_PARSE_URI` except that the first
  argument is now a variable name and not a literal URI.

  Fixes #756


#### manager.c: Properly terminate `CoreShowChannelMap` event.
  Author: Sean Bright
  Date:   2024-06-10

  Fixes #761


#### cli: Show configured cache dir
  Author: Bastian Triller
  Date:   2024-06-07

  Since Asterisk 19 it is possible to cache recorded files into another
  directory [1] [2].
  Show configured location of cache dir in CLI's core show settings.

  [1] ASTERISK-29143
  [2] b08427134fd51bb549f198e9f60685f2680c68d7


#### xml.c: Update deprecated libxml2 API usage.
  Author: Sean Bright
  Date:   2024-05-23

  Two functions are deprecated as of libxml2 2.12:

    * xmlSubstituteEntitiesDefault
    * xmlParseMemory

  So we update those with supported API.

  Additionally, `res_calendar_caldav` has been updated to use libxml2's
  xmlreader API instead of the SAX2 API which has always felt a little
  hacky (see deleted comment block in `res_calendar_caldav.c`).

  The xmlreader API has been around since libxml2 2.5.0 which was
  released in 2003.

  Fixes #725


#### cdr_pgsql: Fix crash when the module fails to load multiple times.
  Author: chrsmj
  Date:   2024-05-16

  Missing or corrupt cdr_pgsql.conf configuration file can cause the
  second attempt to load the PostgreSQL CDR module to crash Asterisk via
  the Command Line Interface because a null CLI command is registered on
  the first failed attempt to load the module.

  Resolves: #736

#### asterisk.c: Don't log an error if .asterisk_history does not exist.
  Author: Sean Bright
  Date:   2024-05-27

  Fixes #751


#### chan_ooh323: Fix R/0 typo in docs
  Author: Walter Doekes
  Date:   2024-05-27


#### bundled_pjproject: Disable UPnP support.
  Author: Sean Bright
  Date:   2024-05-24

  Fixes #747


#### file.h: Rename function argument to avoid C++ keyword clash.
  Author: Sean Bright
  Date:   2024-05-24

  Fixes #744


#### rtp_engine: add support for multirate RFC2833 digits
  Author: Mike Bradeen
  Date:   2024-04-08

  Add RFC2833 DTMF support for 16K, 24K, and 32K bitrate codecs.

  Asterisk currently treats RFC2833 Digits as a single rtp payload type
  with a fixed bitrate of 8K.  This change would expand that to 8, 16,
  24 and 32K.

  This requires checking the offered rtp types for any of these bitrates
  and then adding an offer for each (if configured for RFC2833.)  DTMF
  generation must also be changed in order to look at the current outbound
  codec in order to generate appropriately timed rtp.

  For cases where no outgoing audio has yet been sent prior to digit
  generation, Asterisk now has a concept of a 'preferred' codec based on
  offer order.

  On inbound calls Asterisk will mimic the payload types of the RFC2833
  digits.

  On outbound calls Asterisk will choose the next free payload types starting
  with 101.

  UserNote: No change in configuration is required in order to enable this
  feature. Endpoints configured to use RFC2833 will automatically have this
  enabled. If the endpoint does not support this, it should not include it in
  the SDP offer/response.

  Resolves: #699

