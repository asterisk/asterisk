
## Change Log for Release asterisk-20.10.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.10.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.9.3...20.10.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.10.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 24
- Commit Authors: 9
- Issues Resolved: 18
- Security Advisories Resolved: 0

### User Notes:

- #### res_pjsip_notify: add dialplan application                                      
  A new dialplan application PJSIPNotify is now available
  which can send SIP NOTIFY requests from the dialplan.
  The pjsip send notify CLI command has also been enhanced to allow
  sending NOTIFY messages to a specific channel. Syntax:
  pjsip send notify <option> channel <channel>

- #### channel: Add multi-tenant identifier.                                           
  tenantid has been added to channels. It can be read in
  dialplan via CHANNEL(tenantid), and it can be set using
  Set(CHANNEL(tenantid)=My tenant ID). In pjsip.conf, it is recommended to
  use the new tenantid option for pjsip endpoints (e.g., tenantid=My
  tenant ID) so that it will show up in Newchannel events. You can set it
  like any other channel variable using set_var in pjsip.conf as well, but
  note that this will NOT show up in Newchannel events. Tenant ID is also
  available in CDR and can be accessed with CDR(tenantid). The peer tenant
  ID can also be accessed with CDR(peertenantid). CEL includes tenant ID
  as well if it has been set.

- #### res_pjsip_config_wizard.c: Refactor load process                                
  The res_pjsip_config_wizard.so module can now be reloaded.


### Upgrade Notes:

- #### channel: Add multi-tenant identifier.                                           
  A new versioned struct (ast_channel_initializers) has been
  added that gets passed to __ast_channel_alloc_ap. The new function
  ast_channel_alloc_with_initializers should be used when creating
  channels that require the use of this struct. Currently the only value
  in the struct is for tenantid, but now more fields can be added to the
  struct as necessary rather than the __ast_channel_alloc_ap function. A
  new option (tenantid) has been added to endpoints in pjsip.conf as well.
  CEL has had its version bumped to include tenant ID.


### Commit Authors:

- Alexei Gradinari: (2)
- Ben Ford: (1)
- Cade Parker: (1)
- George Joseph: (10)
- Jaco Kroon: (1)
- Jean-Denis Girard: (1)
- Mike Bradeen: (3)
- Sean Bright: (2)
- Tinet-Mucw: (3)

## Issue and Commit Detail:

### Closed Issues:

  - 740: [new-feature]: Add multi-tenant identifier to chan_pjsip
  - 763: [bug]: autoservice thread stuck in an endless sleep
  - 780: [bug]: Infinite loop of "Indicated Video Update", max CPU usage
  - 799: [improvement]: Add PJSIPNOTIFY dialplan application
  - 801: [bug]: res_stasis: Occasional 200ms delay adding channel to a bridge
  - 809: [bug]: CLI stir_shaken show verification kills asterisk
  - 816: [bug]: res_pjsip_config_wizard doesn't load properly if res_pjsip is loaded first
  - 831: [bug]: app_voicemail ODBC
  - 845: [bug]: Buffer overflow in handling of security mechanisms in res_pjsip 
  - 847: [bug]: Asterisk not using negotiated fall-back 8K digits
  - 854: [bug]:  wrong properties in stir_shaken.conf.sample
  - 856: [bug]: res_pjsip_sdp_rtp leaks astobj2 ast_format 
  - 861: [bug]: ChanSpy unable to read audiohook read direction frame when no packet lost on both side of the call
  - 876: [bug]: ChanSpy unable to write whisper_audiohook when set flag OPTION_READONLY
  - 879: [bug]: res_stir_shaken/verification.c: Getting verification errors when global_disable=yes
  - 884: [bug]: A ':' at the top of in stir_shaken.conf make Asterisk producing a core file when starting
  - 889: [bug]: res_stir_shaken/verification.c has a stale include for jansson.h that can cause compilation to fail
  - 904: [bug]: stir_shaken: attest_level isn't being propagated correctly from attestation to profile to tn

### Commits By Author:

- #### Alexei Gradinari (2):
  - res_pjsip_sdp_rtp fix leaking astobj2 ast_format
  - autoservice: Do not sleep if autoservice_stop is called within autoservice thr..

- #### Ben Ford (1):
  - channel: Add multi-tenant identifier.

- #### Cade Parker (1):
  - chan_mobile: decrease CHANNEL_FRAME_SIZE to prevent delay

- #### George Joseph (10):
  - bridge_softmix: Fix queueing VIDUPDATE control frames
  - res_pjsip_config_wizard.c: Refactor load process
  - stir_shaken: CRL fixes and a new CLI command
  - manager.c: Fix FRACK when doing CoreShowChannelMap in DEVMODE
  - stir_shaken.conf.sample: Fix bad references to private_key_path
  - security_agreements.c: Refactor the to_str functions and fix a few other bugs
  - res_stir_shaken: Check for disabled before param validation
  - res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid
  - res_stir_shaken: Remove stale include for jansson.h in verification.c
  - stir_shaken: Fix propagation of attest_level and a few other values

- #### Jaco Kroon (1):
  - configure:  Use . file rather than source file.

- #### Jean-Denis Girard (1):
  - app_voicemail: Fix sql insert mismatch caused by cherry-pick

- #### Mike Bradeen (3):
  - res_stasis: fix intermittent delays on adding channel to bridge
  - res_pjsip_notify: add dialplan application
  - res_pjsip_sdp_rtp: Use negotiated DTMF Payload types on bitrate mismatch

- #### Sean Bright (2):
  - alembic: Make 'revises' header comment match reality.
  - res_pjsip_logger.c: Fix 'OPTIONS' tab completion.

- #### Tinet-mucw (3):
  - res_pjsip_sdp_rtp.c: Fix DTMF Handling in Re-INVITE with dtmf_mode set to auto
  - app_chanspy.c: resolving the issue with audiohook direction read
  - app_chanspy.c: resolving the issue writing frame to whisper audiohook.


### Commit List:

-  stir_shaken: Fix propagation of attest_level and a few other values
-  res_stir_shaken: Remove stale include for jansson.h in verification.c
-  res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid
-  res_stir_shaken: Check for disabled before param validation
-  app_chanspy.c: resolving the issue writing frame to whisper audiohook.
-  app_voicemail: Fix sql insert mismatch caused by cherry-pick
-  res_pjsip_sdp_rtp: Use negotiated DTMF Payload types on bitrate mismatch
-  app_chanspy.c: resolving the issue with audiohook direction read
-  security_agreements.c: Refactor the to_str functions and fix a few other bugs
-  res_pjsip_sdp_rtp fix leaking astobj2 ast_format
-  stir_shaken.conf.sample: Fix bad references to private_key_path
-  res_pjsip_logger.c: Fix 'OPTIONS' tab completion.
-  alembic: Make 'revises' header comment match reality.
-  chan_mobile: decrease CHANNEL_FRAME_SIZE to prevent delay
-  res_pjsip_notify: add dialplan application
-  manager.c: Fix FRACK when doing CoreShowChannelMap in DEVMODE
-  channel: Add multi-tenant identifier.
-  configure:  Use . file rather than source file.
-  res_stasis: fix intermittent delays on adding channel to bridge
-  res_pjsip_sdp_rtp.c: Fix DTMF Handling in Re-INVITE with dtmf_mode set to auto
-  stir_shaken: CRL fixes and a new CLI command
-  res_pjsip_config_wizard.c: Refactor load process
-  bridge_softmix: Fix queueing VIDUPDATE control frames

### Commit Details:

#### stir_shaken: Fix propagation of attest_level and a few other values
  Author: George Joseph
  Date:   2024-09-24

  attest_level, send_mky and check_tn_cert_public_url weren't
  propagating correctly from the attestation object to the profile
  and tn.

  * In the case of attest_level, the enum needed to be changed
  so the "0" value (the default) was "NOT_SET" instead of "A".  This
  now allows the merging of the attestation object, profile and tn
  to detect when a value isn't set and use the higher level value.

  * For send_mky and check_tn_cert_public_url, the tn default was
  forced to "NO" which always overrode the profile and attestation
  objects.  Their defaults are now "NOT_SET" so the propagation
  happens correctly.

  * Just to remove some redundant code in tn_config.c, a bunch of calls to
  generate_sorcery_enum_from_str() and generate_sorcery_enum_to_str() were
  replaced with a single call to generate_acfg_common_sorcery_handlers().

  Resolves: #904

#### res_stir_shaken: Remove stale include for jansson.h in verification.c
  Author: George Joseph
  Date:   2024-09-17

  verification.c had an include for jansson.h left over from previous
  versions of the module.  Since res_stir_shaken no longer has a
  dependency on jansson, the bundled version wasn't added to GCC's
  include path so if you didn't also have a jansson development package
  installed, the compile would fail.  Removing the stale include
  was the only thing needed.

  Resolves: #889

#### res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid
  Author: George Joseph
  Date:   2024-09-13

  * If the call to ast_config_load() returns CONFIG_STATUS_FILEINVALID,
  check_for_old_config() now returns LOAD_DECLINE instead of continuing
  on with a bad pointer.

  * If CONFIG_STATUS_FILEMISSING is returned, check_for_old_config()
  assumes the config is being loaded from realtime and now returns
  LOAD_SUCCESS.  If it's actually not being loaded from realtime,
  sorcery will catch that later on.

  * Also refactored the error handling in load_module() a bit.

  Resolves: #884

#### res_stir_shaken: Check for disabled before param validation
  Author: George Joseph
  Date:   2024-09-11

  For both attestation and verification, we now check whether they've
  been disabled either globally or by the profile before validating
  things like callerid, orig_tn, dest_tn, etc.  This prevents useless
  error messages.

  Resolves: #879

#### app_chanspy.c: resolving the issue writing frame to whisper audiohook.
  Author: Tinet-mucw
  Date:   2024-09-10

  ChanSpy(${channel}, qEoSw): because flags set o, ast_audiohook_set_frame_feed_direction(audiohook, AST_AUDIOHOOK_DIRECTION_READ); this will effect whisper audiohook and spy audiohook, this makes writing frame to whisper audiohook impossible. So add function start_whispering to starting whisper audiohook.

  Resolves: #876

#### autoservice: Do not sleep if autoservice_stop is called within autoservice thr..
  Author: Alexei Gradinari
  Date:   2024-09-04

  It's possible that ast_autoservice_stop is called within the autoservice thread.
  In this case the autoservice thread is stuck in an endless sleep.

  To avoid endless sleep ast_autoservice_stop must check that it's not called
  within the autoservice thread.

  Fixes: #763

#### app_voicemail: Fix sql insert mismatch caused by cherry-pick
  Author: Jean-Denis Girard
  Date:   2024-08-07

  When commit e8c9cb80 was cherry-picked in from master, the
  fact that the 20 and 18 branches still had the old "macrocontext"
  column wasn't taken into account so the number of named parameters
  didn't match the number of '?' placeholders.  They do now.

  We also now use ast_asprintf to create the full mailbox query SQL
  statement instead of trying to calculate the proper length ourselves.

  Resolves: #831

#### res_pjsip_sdp_rtp: Use negotiated DTMF Payload types on bitrate mismatch
  Author: Mike Bradeen
  Date:   2024-08-21

  When Asterisk sends an offer to Bob that includes 48K and 8K codecs with
  matching 4733 offers, Bob may want to use the 48K audio codec but can not
  accept 48K digits and so negotiates for a mixed set.

  Asterisk will now check Bob's offer to make sure Bob has indicated this is
  acceptible and if not, will use Bob's preference.

  Fixes: #847

#### app_chanspy.c: resolving the issue with audiohook direction read
  Author: Tinet-mucw
  Date:   2024-08-30

  ChanSpy(${channel}, qEoS): When chanspy spy the direction read, reading frame is often failed when reading direction read audiohook. because chanspy only read audiohook direction read; write_factory_ms will greater than 100ms soon, then ast_slinfactory_flush will being called, then direction read will fail.

  Resolves: #861

#### security_agreements.c: Refactor the to_str functions and fix a few other bugs
  Author: George Joseph
  Date:   2024-08-17

  * A static array of security mechanism type names was created.

  * ast_sip_str_to_security_mechanism_type() was refactored to do
    a lookup in the new array instead of using fixed "if/else if"
    statments.

  * security_mechanism_to_str() and ast_sip_security_mechanisms_to_str()
    were refactored to use ast_str instead of a fixed length buffer
    to store the result.

  * ast_sip_security_mechanism_type_to_str was removed in favor of
    just referencing the new type name array.  Despite starting with
    "ast_sip_", it was a static function so removing it doesn't affect
    ABI.

  * Speaking of "ast_sip_", several other static functions that
    started with "ast_sip_" were renamed to avoid confusion about
    their public availability.

  * A few VECTOR free loops were replaced with AST_VECTOR_RESET().

  * Fixed a meomry leak in pjsip_configuration.c endpoint_destructor
    caused by not calling ast_sip_security_mechanisms_vector_destroy().

  * Fixed a memory leak in res_pjsip_outbound_registration.c
    add_security_headers() caused by not specifying OBJ_NODATA in
    an ao2_callback.

  * Fixed a few ao2_callback return code misuses.

  Resolves: #845

#### res_pjsip_sdp_rtp fix leaking astobj2 ast_format
  Author: Alexei Gradinari
  Date:   2024-08-23

  PR #700 added a preferred_format for the struct ast_rtp_codecs,
  but when set the preferred_format it leaks an astobj2 ast_format.
  In the next code
  ast_rtp_codecs_set_preferred_format(&codecs, ast_format_cap_get_format(joint, 0));
  both functions ast_rtp_codecs_set_preferred_format
  and ast_format_cap_get_format increases the ao2 reference count.

  Fixes: #856

#### stir_shaken.conf.sample: Fix bad references to private_key_path
  Author: George Joseph
  Date:   2024-08-22

  They should be private_key_file.

  Resolves: #854

#### res_pjsip_logger.c: Fix 'OPTIONS' tab completion.
  Author: Sean Bright
  Date:   2024-08-19

  Fixes #843


#### alembic: Make 'revises' header comment match reality.
  Author: Sean Bright
  Date:   2024-08-17


#### chan_mobile: decrease CHANNEL_FRAME_SIZE to prevent delay
  Author: Cade Parker
  Date:   2024-08-07

  On modern Bluetooth devices or lower-powered asterisk servers, decreasing the channel frame size significantly improves latency and delay on outbound calls with only a mild sacrifice to the quality of the call (the frame size before was massive overkill to begin with)


#### res_pjsip_notify: add dialplan application
  Author: Mike Bradeen
  Date:   2024-07-09

  Add dialplan application PJSIPNOTIFY to send either pre-configured
  NOTIFY messages from pjsip_notify.conf or with headers defined in
  dialplan.

  Also adds the ability to send pre-configured NOTIFY commands to a
  channel via the CLI.

  Resolves: #799

  UserNote: A new dialplan application PJSIPNotify is now available
  which can send SIP NOTIFY requests from the dialplan.

  The pjsip send notify CLI command has also been enhanced to allow
  sending NOTIFY messages to a specific channel. Syntax:

  pjsip send notify <option> channel <channel>


#### manager.c: Fix FRACK when doing CoreShowChannelMap in DEVMODE
  Author: George Joseph
  Date:   2024-08-08

  If you run an AMI CoreShowChannelMap on a channel that isn't in a
  bridge and you're in DEVMODE, you can get a FRACK because the
  bridge id is empty.  We now simply return an empty list for that
  request.


#### channel: Add multi-tenant identifier.
  Author: Ben Ford
  Date:   2024-05-21

  This patch introduces a new identifier for channels: tenantid. It's
  a stringfield on the channel that can be used for general purposes. It
  will be inherited by other channels the same way that linkedid is.

  You can set tenantid in a few ways. The first is to set it in the
  dialplan with the Set and CHANNEL functions:

  exten => example,1,Set(CHANNEL(tenantid)=My tenant ID)

  It can also be accessed via CHANNEL:

  exten => example,2,NoOp(CHANNEL(tenantid))

  Another method is to use the new tenantid option for pjsip endpoints in
  pjsip.conf:

  [my_endpoint]
  type=endpoint
  tenantid=My tenant ID

  This is considered the best approach since you will be able to see the
  tenant ID as early as the Newchannel event.

  It can also be set using set_var in pjsip.conf on the endpoint like
  setting other channel variable:

  set_var=CHANNEL(tenantid)=My tenant ID

  Note that set_var will not show tenant ID on the Newchannel event,
  however.

  Tenant ID has also been added to CDR. It's read-only and can be accessed
  via CDR(tenantid). You can also get the tenant ID of the last channel
  communicated with via CDR(peertenantid).

  Tenant ID will also show up in CEL records if it has been set, and the
  version number has been bumped accordingly.

  Fixes: #740

  UserNote: tenantid has been added to channels. It can be read in
  dialplan via CHANNEL(tenantid), and it can be set using
  Set(CHANNEL(tenantid)=My tenant ID). In pjsip.conf, it is recommended to
  use the new tenantid option for pjsip endpoints (e.g., tenantid=My
  tenant ID) so that it will show up in Newchannel events. You can set it
  like any other channel variable using set_var in pjsip.conf as well, but
  note that this will NOT show up in Newchannel events. Tenant ID is also
  available in CDR and can be accessed with CDR(tenantid). The peer tenant
  ID can also be accessed with CDR(peertenantid). CEL includes tenant ID
  as well if it has been set.

  UpgradeNote: A new versioned struct (ast_channel_initializers) has been
  added that gets passed to __ast_channel_alloc_ap. The new function
  ast_channel_alloc_with_initializers should be used when creating
  channels that require the use of this struct. Currently the only value
  in the struct is for tenantid, but now more fields can be added to the
  struct as necessary rather than the __ast_channel_alloc_ap function. A
  new option (tenantid) has been added to endpoints in pjsip.conf as well.
  CEL has had its version bumped to include tenant ID.


#### configure:  Use . file rather than source file.
  Author: Jaco Kroon
  Date:   2024-08-05

  source is a bash concept, so when /bin/sh points to another shell the
  existing construct won't work.

  Reference: https://bugs.gentoo.org/927055
  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

#### res_stasis: fix intermittent delays on adding channel to bridge
  Author: Mike Bradeen
  Date:   2024-07-10

  Previously, on command execution, the control thread was awoken by
  sending a SIGURG. It was found that this still resulted in some
  instances where the thread was not immediately awoken.

  This change instead sends a null frame to awaken the control thread,
  which awakens the thread more consistently.

  Resolves: #801

#### res_pjsip_sdp_rtp.c: Fix DTMF Handling in Re-INVITE with dtmf_mode set to auto
  Author: Tinet-mucw
  Date:   2024-08-02

  When the endpoint dtmf_mode is set to auto, a SIP request is sent to the UAC, and the SIP SDP from the UAC does not include the telephone-event. Later, the UAC sends an INVITE, and the SIP SDP includes the telephone-event. In this case, DTMF should be sent by RFC2833 rather than using inband signaling.

  Resolves: asterisk#826

#### stir_shaken: CRL fixes and a new CLI command
  Author: George Joseph
  Date:   2024-07-19

  * Fixed a bug in crypto_show_cli_store that was causing asterisk
  to crash if there were certificate revocation lists in the
  verification certificate store.  We're also now prefixing
  certificates with "Cert:" and CRLs with "CRL:" to distinguish them
  in the list.

  * Added 'untrusted_cert_file' and 'untrusted_cert_path' options
  to both verification and profile objects.  If you have CRLs that
  are signed by a different CA than the incoming X5U certificate
  (indirect CRL), you'll need to provide the certificate of the
  CRL signer here.  Thse will show up as 'Untrusted" when showing
  the verification or profile objects.

  * Fixed loading of crl_path.  The OpenSSL API we were using to
  load CRLs won't actually load them from a directory, only a file.
  We now scan the directory ourselves and load the files one-by-one.

  * Fixed the verification flags being set on the certificate store.
    - Removed the CRL_CHECK_ALL flag as this was causing all certificates
      to be checked for CRL extensions and failing to verify the cert if
      there was none.  This basically caused all certs to fail when a CRL
      was provided via crl_file or crl_path.
    - Added the EXTENDED_CRL_SUPPORT flag as it is required to handle
      indirect CRLs.

  * Added a new CLI command...
  `stir_shaken verify certificate_file <certificate_file> [ <profile> ]`
  which will assist troubleshooting certificate problems by allowing
  the user to manually verify a certificate file against either the
  global verification certificate store or the store for a specific
  profile.

  * Updated the XML documentation and the sample config file.

  Resolves: #809

#### res_pjsip_config_wizard.c: Refactor load process
  Author: George Joseph
  Date:   2024-07-23

  The way we have been initializing the config wizard prevented it
  from registering its objects if res_pjsip happened to load
  before it.

  * We now use the object_type_registered sorcery observer to kick
  things off instead of the wizard_mapped observer.

  * The load_module function now checks if res_pjsip has been loaded
  already and if it was it fires the proper observers so the objects
  load correctly.

  Resolves: #816

  UserNote: The res_pjsip_config_wizard.so module can now be reloaded.

#### bridge_softmix: Fix queueing VIDUPDATE control frames
  Author: George Joseph
  Date:   2024-07-17

  softmix_bridge_write_control() now calls ast_bridge_queue_everyone_else()
  with the bridge_channel so the VIDUPDATE control frame isn't echoed back.

  softmix_bridge_write_control() was setting bridge_channel to NULL
  when calling ast_bridge_queue_everyone_else() for VIDUPDATE control
  frames.  This was causing the frame to be echoed back to the
  channel it came from.  In certain cases, like when two channels or
  bridges are being recorded, this can cause a ping-pong effect that
  floods the system with VIDUPDATE control frames.

  Resolves: #780

