
## Change Log for Release asterisk-22.0.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.0.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.0.0-pre1...22.0.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.0.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 22
- Commit Authors: 8
- Issues Resolved: 13
- Security Advisories Resolved: 1
  - [GHSA-v428-g3cw-7hv9](https://github.com/asterisk/asterisk/security/advisories/GHSA-v428-g3cw-7hv9): A malformed Contact or Record-Route URI in an incoming SIP request can cause Asterisk to crash when res_resolver_unbound is used

### User Notes:

- #### feat: ARI "ChannelToneDetected" event                                           
  Setting the TONE_DETECT dialplan function on a channel
  in ARI will now cause a ChannelToneDetected ARI event to be raised
  when the specified tone is detected.


### Upgrade Notes:

- #### app_record: Add RECORD_TIME output variable.                                    
  The RECORD_TIME variable now contains
  the duration of Record() recordings in milliseconds.


### Commit Authors:

- Alexei Gradinari: (2)
- George Joseph: (10)
- Gibbz00: (1)
- Joshua C. Colp: (2)
- Mike Bradeen: (2)
- Naveen Albert: (1)
- Sean Bright: (2)
- Tinet-Mucw: (2)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-v428-g3cw-7hv9: A malformed Contact or Record-Route URI in an incoming SIP request can cause Asterisk to crash when res_resolver_unbound is used
  - 548: [improvement]: Get Record() audio duration/length
  - 763: [bug]: autoservice thread stuck in an endless sleep
  - 811: [new-feature]: ARI channel tone detect events.
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

- #### George Joseph (8):
  - stir_shaken.conf.sample: Fix bad references to private_key_path
  - security_agreements.c: Refactor the to_str functions and fix a few other bugs
  - app_voicemail: Use ast_asprintf to create mailbox SQL query
  - res_resolver_unbound: Test for NULL ub_result in unbound_resolver_callback
  - res_stir_shaken: Check for disabled before param validation
  - res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid
  - res_stir_shaken: Remove stale include for jansson.h in verification.c
  - stir_shaken: Fix propagation of attest_level and a few other values

- #### Mike Bradeen (1):
  - res_pjsip_sdp_rtp: Use negotiated DTMF Payload types on bitrate mismatch

- #### Sean Bright (2):
  - alembic: Make 'revises' header comment match reality.
  - res_pjsip_logger.c: Fix 'OPTIONS' tab completion.

- #### Tinet-mucw (2):
  - app_chanspy.c: resolving the issue with audiohook direction read
  - app_chanspy.c: resolving the issue writing frame to whisper audiohook.


### Commit List:

-  Prepare master for Asterisk 22
-  Update issue guidelines link for bug reports.
-  .github: NightlyAdmin now calls external CloseStaleIssuesAndPRs
-  app_record: Add RECORD_TIME output variable.
-  Revert "app_record: Add RECORD_TIME output variable."
-  feat: ARI "ChannelToneDetected" event
-  Update version for Asterisk 22
-  stir_shaken: Fix propagation of attest_level and a few other values
-  res_stir_shaken: Remove stale include for jansson.h in verification.c
-  res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid
-  res_stir_shaken: Check for disabled before param validation
-  app_chanspy.c: resolving the issue writing frame to whisper audiohook.
-  res_resolver_unbound: Test for NULL ub_result in unbound_resolver_callback
-  app_voicemail: Use ast_asprintf to create mailbox SQL query
-  res_pjsip_sdp_rtp: Use negotiated DTMF Payload types on bitrate mismatch
-  app_chanspy.c: resolving the issue with audiohook direction read
-  security_agreements.c: Refactor the to_str functions and fix a few other bugs
-  res_pjsip_sdp_rtp fix leaking astobj2 ast_format
-  stir_shaken.conf.sample: Fix bad references to private_key_path
-  res_pjsip_logger.c: Fix 'OPTIONS' tab completion.
-  alembic: Make 'revises' header comment match reality.

### Commit Details:

#### Prepare master for Asterisk 22
  Author: George Joseph
  Date:   2023-08-09


#### Update issue guidelines link for bug reports.
  Author: Joshua C. Colp
  Date:   2023-10-27


#### .github: NightlyAdmin now calls external CloseStaleIssuesAndPRs
  Author: George Joseph
  Date:   2024-03-20


#### app_record: Add RECORD_TIME output variable.
  Author: Naveen Albert
  Date:   2024-01-22

  This adds the RECORD_TIME variable to Record(),
  which is set to the recording duration before
  the application returns.

  Resolves: #548

  UpgradeNote: The RECORD_TIME variable now contains
  the duration of Record() recordings in milliseconds.

#### Revert "app_record: Add RECORD_TIME output variable."
  Author: Joshua C. Colp
  Date:   2024-04-30

  This reverts commit 6e8dccdbbf896bcc99046ae249db360698ede0b2.

#### feat: ARI "ChannelToneDetected" event
  Author: gibbz00
  Date:   2024-07-18

  A stasis event is now produced when using the TONE_DETECT dialplan
  function. This event is published over ARI using the ChannelToneDetected
  event. This change does not make it available over AMI.

  Fixes: #811

  UserNote: Setting the TONE_DETECT dialplan function on a channel
  in ARI will now cause a ChannelToneDetected ARI event to be raised
  when the specified tone is detected.

#### Update version for Asterisk 22
  Author: Mike Bradeen
  Date:   2024-08-14


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

#### res_resolver_unbound: Test for NULL ub_result in unbound_resolver_callback
  Author: George Joseph
  Date:   2024-08-12

  The ub_result pointer passed to unbound_resolver_callback by
  libunbound can be NULL if the query was for something malformed
  like `.1` or `[.1]`.  If it is, we now set a 'ns_r_formerr' result
  and return instead of crashing with a SEGV.  This causes pjproject
  to simply cancel the transaction with a "No answer record in the DNS
  response" error.  The existing "off nominal" unit test was also
  updated to check this condition.

  Although not necessary for this fix, we also made
  ast_dns_resolver_completed() tolerant of a NULL result.

  Resolves: GHSA-v428-g3cw-7hv9

#### app_voicemail: Use ast_asprintf to create mailbox SQL query
  Author: George Joseph
  Date:   2024-09-03

  ...instead of trying to calculate the length of the buffer needed
  manually.


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


