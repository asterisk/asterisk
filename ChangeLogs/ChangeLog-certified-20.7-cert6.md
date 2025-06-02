
## Change Log for Release asterisk-certified-20.7-cert6

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-20.7-cert6.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-20.7-cert5...certified-20.7-cert6)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-20.7-cert6.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 31
- Commit Authors: 5
- Issues Resolved: 16
- Security Advisories Resolved: 0

### User Notes:

- #### res_stir_shaken: Allow sending Identity headers for unknown TNs                 
  You can now set the "unknown_tn_attest_level" option
  in the attestation and/or profile objects in stir_shaken.conf to
  enable sending Identity headers for callerid TNs not explicitly
  configured.

- #### res_pjsip: Add new endpoint option "suppress_moh_on_sendonly"                   
  The new "suppress_moh_on_sendonly" endpoint option
  can be used to prevent playing MOH back to a caller if the remote
  end sends "sendonly" or "inactive" (hold) to Asterisk in an SDP.

- #### app_mixmonitor: Add 'D' option for dual-channel audio.                          
  The MixMonitor application now has a new 'D' option which
  interleaves the recorded audio in the output frames. This allows for
  stereo recording output with one channel being the transmitted audio and
  the other being the received audio. The 't' and 't' options are
  compatible with this.

- #### db.c: Remove limit on family/key length                                         
  The `ast_db_*()` APIs have had the 253 byte limit on
  "/family/key" removed and will now accept families and keys with a
  total length of up to SQLITE_MAX_LENGTH (currently 1e9!).  This
  affects the `DB*` dialplan applications, dialplan functions,
  manager actions and `databse` CLI commands.  Since the
  media_cache also uses the `ast_db_*()` APIs, you can now store
  resources with URIs longer than 253 bytes.


### Upgrade Notes:


### Commit Authors:

- Ben Ford: (3)
- Chrsmj: (1)
- George Joseph: (22)
- Joshua C. Colp: (1)
- Sean Bright: (4)

## Issue and Commit Detail:

### Closed Issues:

  - 879: [bug]: res_stir_shaken/verification.c: Getting verification errors when global_disable=yes
  - 881: [bug]: Long URLs are being rejected by the media cache because of an astdb key length limit
  - 884: [bug]: A ':' at the top of in stir_shaken.conf make Asterisk producing a core file when starting
  - 889: [bug]: res_stir_shaken/verification.c has a stale include for jansson.h that can cause compilation to fail
  - 904: [bug]: stir_shaken: attest_level isn't being propagated correctly from attestation to profile to tn
  - 921: [bug]: Stir-Shaken doesn’t allow B or C attestation for unknown callerid which is allowed by ATIS-1000074.v003, §5.2.4
  - 937: [bug]: Wrong format for sample config file 'geolocation.conf.sample'
  - 938: [bug]: memory leak - CBAnn leaks a small amount format_cap related memory for every confbridge
  - 945: [improvement]: Add stereo recording support for app_mixmonitor
  - 974: [improvement]: change and/or remove some wiki mentions to docs mentions in the sample configs
  - 979: [improvement]: Add ability to suppress MOH when a remote endpoint sends "sendonly" or "inactive"
  - 982: [bug]: The addition of tenantid to the ast_sip_endpoint structure broke ABI compatibility
  - 995: [bug]: suppress_moh_on_sendonly should use AST_BOOL_VALUES instead of YESNO_VALUES in alembic script
  - 1112: [bug]: STIR/SHAKEN verification doesn't allow anonymous callerid to be passed to the dialplan.
  - 1122: Need status on CVE-2024-57520 claim.
  - 1131: [bug]: CHANGES link broken in README.md

### Commits By Author:

- #### Ben Ford (3):
  - app_mixmonitor: Add 'D' option for dual-channel audio.
  - Add res_pjsip_config_sangoma external module.
  - documentation: Update Gosub, Goto, and add new documentationtype.

- #### George Joseph (22):
  - res_stir_shaken: Check for disabled before param validation
  - res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid
  - res_stir_shaken: Remove stale include for jansson.h in verification.c
  - db.c: Remove limit on family/key length
  - Fix application references to Background
  - stir_shaken: Fix propagation of attest_level and a few other values
  - manager.c: Add unit test for Originate app and appdata permissions
  - geolocation.sample.conf: Fix comment marker at end of file
  - core_unreal.c: Fix memory leak in ast_unreal_new_channels()
  - res_pjsip: Move tenantid to end of ast_sip_endpoint
  - res_pjsip: Add new endpoint option "suppress_moh_on_sendonly"
  - res_pjsip: Change suppress_moh_on_sendonly to OPT_BOOL_T
  - res_stir_shaken: Allow sending Identity headers for unknown TNs
  - Allow C++ source files (as extension .cc) in the main directory
  - gcc14: Fix issues caught by gcc 14
  - manager.c: Split XML docs into separate file
  - docs: Enable since/version handling for XML, CLI and ARI documentation
  - README.md, asterisk.c: Update Copyright Dates
  - res_stir_shaken: Allow missing or anonymous CID to continue to the dialplan.
  - swagger_model.py: Fix invalid escape sequence in get_list_parameter_type().
  - manager.c: Check for restricted file in action_createconfig.
  - README.md: Updates and Fixes

- #### Joshua C. Colp (1):
  - LICENSE: Update company name, email, and address.

- #### Sean Bright (4):
  - res_pjsip_pubsub: Persist subscription 'generator_data' in sorcery
  - alembic: Drop redundant voicemail_messages index.
  - manager.c: Rename restrictedFile to is_restricted_file.
  - manager: Add `<since>` tags for all AMI actions.

- #### chrsmj (1):
  - samples: remove and/or change some wiki mentions


### Commit List:

-  documentation: Update Gosub, Goto, and add new documentationtype.
-  README.md: Updates and Fixes
-  manager.c: Check for restricted file in action_createconfig.
-  swagger_model.py: Fix invalid escape sequence in get_list_parameter_type().
-  res_stir_shaken: Allow missing or anonymous CID to continue to the dialplan.
-  LICENSE: Update company name, email, and address.
-  README.md, asterisk.c: Update Copyright Dates
-  docs: Enable since/version handling for XML, CLI and ARI documentation
-  manager: Add `<since>` tags for all AMI actions.
-  manager.c: Split XML docs into separate file
-  manager.c: Rename restrictedFile to is_restricted_file.
-  gcc14: Fix issues caught by gcc 14
-  Allow C++ source files (as extension .cc) in the main directory
-  res_stir_shaken: Allow sending Identity headers for unknown TNs
-  res_pjsip: Change suppress_moh_on_sendonly to OPT_BOOL_T
-  res_pjsip: Add new endpoint option "suppress_moh_on_sendonly"
-  samples: remove and/or change some wiki mentions
-  res_pjsip: Move tenantid to end of ast_sip_endpoint
-  Add res_pjsip_config_sangoma external module.
-  app_mixmonitor: Add 'D' option for dual-channel audio.
-  core_unreal.c: Fix memory leak in ast_unreal_new_channels()
-  geolocation.sample.conf: Fix comment marker at end of file
-  manager.c: Add unit test for Originate app and appdata permissions
-  alembic: Drop redundant voicemail_messages index.
-  res_pjsip_pubsub: Persist subscription 'generator_data' in sorcery
-  stir_shaken: Fix propagation of attest_level and a few other values
-  Fix application references to Background
-  db.c: Remove limit on family/key length
-  res_stir_shaken: Remove stale include for jansson.h in verification.c
-  res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid
-  res_stir_shaken: Check for disabled before param validation

### Commit Details:

#### documentation: Update Gosub, Goto, and add new documentationtype.
  Author: Ben Ford
  Date:   2025-03-14

  Gosub and Goto were not displaying their syntax correctly on the docs
  site. This change adds a new way to specify an optional context, an
  optional extension, and a required priority that the xml stylesheet can
  parse without having to know which optional parameters come in which
  order. In Asterisk, it looks like this:

    parameter name="context" documentationtype="dialplan_context"
    parameter name="extension" documentationtype="dialplan_extension"
    parameter name="priority" documentationtype="dialplan_priority" required="true"

  The stylesheet will ignore the context and extension parameters, but for
  priority, it will automatically inject the following:

    [[context,]extension,]priority

  This is the correct oder for applications such as Gosub and Goto.


#### README.md: Updates and Fixes
  Author: George Joseph
  Date:   2025-03-05

  * Outdated information has been removed.
  * New links added.
  * Placeholder added for link to change logs.

  Going forward, the release process will create HTML versions of the README
  and change log and will update the link in the README to the current
  change log for the branch...

  * In the development branches, the link will always point to the current
    release on GitHub.
  * In the "releases/*" branches and the tarballs, the link will point to the
    ChangeLogs/ChangeLog-<version>.html file in the source directory.
  * On the downloads website, the link will point to the
    ChangeLog-<version>.html file in the same directory.

  Resolves: #1131

#### manager.c: Check for restricted file in action_createconfig.
  Author: George Joseph
  Date:   2025-03-03

  The `CreateConfig` manager action now ensures that a config file can
  only be created in the AST_CONFIG_DIR unless `live_dangerously` is set.

  Resolves: #1122

#### swagger_model.py: Fix invalid escape sequence in get_list_parameter_type().
  Author: George Joseph
  Date:   2025-03-04

  Recent python versions complain when backslashes in strings create invalid
  escape sequences.  This causes issues for strings used as regex patterns like
  `'^List\[(.*)\]$'` where you want the regex parser to treat `[` and `]`
  as literals.  Double-backslashing is one way to fix it but simply converting
  the string to a raw string `re.match(r'^List\[(.*)\]$', text)` is easier
  and less error prone.


#### res_stir_shaken: Allow missing or anonymous CID to continue to the dialplan.
  Author: George Joseph
  Date:   2025-02-05

  The verification check for missing or anonymous callerid was happening before
  the endpoint's profile was retrieved which meant that the failure_action
  parameter wasn't available.  Therefore, if verification was enabled and there
  was no callerid or it was "anonymous", the call was immediately terminated
  instead of giving the dialplan the ability to decide what to do with the call.

  * The callerid check now happens after the verification context is created and
    the endpoint's stir_shaken_profile is available.

  * The check now processes the callerid failure just as it does for other
    verification failures and respects the failure_action parameter.  If set
    to "continue" or "continue_return_reason", `STIR_SHAKEN(0,verify_result)`
    in the dialplan will return "invalid_or_no_callerid".

  * If the endpoint's failure_action is "reject_request", the call will be
    rejected with `433 "Anonymity Disallowed"`.

  * If the endpoint's failure_action is "continue_return_reason", the call will
    continue but a `Reason: STIR; cause=433; text="Anonymity Disallowed"`
    header will be added to the next provisional or final response.

  Resolves: #1112

#### LICENSE: Update company name, email, and address.
  Author: Joshua C. Colp
  Date:   2025-01-21


#### README.md, asterisk.c: Update Copyright Dates
  Author: George Joseph
  Date:   2025-01-20


#### docs: Enable since/version handling for XML, CLI and ARI documentation
  Author: George Joseph
  Date:   2025-01-09

  * Added the "since" element to the XML configObject and configOption elements
    in appdocsxml.dtd.

  * Added the "Since" section to the following CLI output:
    ```
    config show help <module> <object>
    config show help <module> <object> <option>
    core show application <app>
    core show function <func>
    manager show command <command>
    manager show event <event>
    agi show commands topic <topic>
    ```

  * Refactored the commands above to output their sections in the same order:
    Synopsis, Since, Description, Syntax, Arguments, SeeAlso

  * Refactored the commands above so they all use the same pattern for writing
    the output to the CLI.

  * Fixed several memory leaks caused by failure to free temporary output
    buffers.

  * Added a "since" array to the mustache template for the top-level resources
    (Channel, Endpoint, etc.) and to the paths/methods underneath them. These
    will be added to the generated markdown if present.
    Example:
    ```
      "resourcePath": "/api-docs/channels.{format}",
      "requiresModules": [
          "res_stasis_answer",
          "res_stasis_playback",
          "res_stasis_recording",
          "res_stasis_snoop"
      ],
      "since": [
          "18.0.0",
          "21.0.0"
      ],
      "apis": [
          {
              "path": "/channels",
              "description": "Active channels",
              "operations": [
                  {
                      "httpMethod": "GET",
                      "since": [
                          "18.6.0",
                          "21.8.0"
                      ],
                      "summary": "List all active channels in Asterisk.",
                      "nickname": "list",
                      "responseClass": "List[Channel]"
                  },

    ```

  NOTE:  No versioning information is actually added in this commit.
  Those will be added separately and instructions for adding and maintaining
  them will be published on the documentation site at a later date.


#### manager: Add `<since>` tags for all AMI actions.
  Author: Sean Bright
  Date:   2025-01-02


#### manager.c: Split XML docs into separate file
  Author: George Joseph
  Date:   2025-01-13

  To keep the source tree somewhat compatible with the base 20 branch
  the XML documentation from manager.c has been extracted into manager_doc.xml.
  This will give future cherry-picks a better channce of succeeding without
  manual intervention.


#### manager.c: Rename restrictedFile to is_restricted_file.
  Author: Sean Bright
  Date:   2025-01-09

  Also correct the spelling of 'privileges.'


#### gcc14: Fix issues caught by gcc 14
  Author: George Joseph
  Date:   2025-01-03

  * reqresp_parser.c: Fix misuse of "static" with linked list definitions
  * test_message.c: Fix segfaults caused by passing NULL as an sprintf fmt


#### Allow C++ source files (as extension .cc) in the main directory
  Author: George Joseph
  Date:   2024-12-09

  Although C++ files (as extension .cc) have been handled in the module
  directories for many years, the main directory was missing one line in its
  Makefile that prevented C++ files from being recognised there.


#### res_stir_shaken: Allow sending Identity headers for unknown TNs
  Author: George Joseph
  Date:   2024-11-08

  Added a new option "unknown_tn_attest_level" to allow Identity
  headers to be sent when a callerid TN isn't explicitly configured
  in stir_shaken.conf.  Since there's no TN object, a private_key_file
  and public_cert_url must be configured in the attestation or profile
  objects.

  Since "unknown_tn_attest_level" uses the same enum as attest_level,
  some of the sorcery macros had to be refactored to allow sharing
  the enum and to/from string conversion functions.

  Also fixed a memory leak in crypto_utils:pem_file_cb().

  Resolves: #921

  UserNote: You can now set the "unknown_tn_attest_level" option
  in the attestation and/or profile objects in stir_shaken.conf to
  enable sending Identity headers for callerid TNs not explicitly
  configured.


#### res_pjsip: Change suppress_moh_on_sendonly to OPT_BOOL_T
  Author: George Joseph
  Date:   2024-11-15

  The suppress_moh_on_sendonly endpoint option should have been
  defined as OPT_BOOL_T in pjsip_configuration.c and AST_BOOL_VALUES
  in the alembic script instead of OPT_YESNO_T and YESNO_VALUES.

  Also updated contrib/ast-db-manage/README.md to indicate that
  AST_BOOL_VALUES should always be used and provided an example.

  Resolves: #995

#### res_pjsip: Add new endpoint option "suppress_moh_on_sendonly"
  Author: George Joseph
  Date:   2024-11-05

  Normally, when one party in a call sends Asterisk an SDP with
  a "sendonly" or "inactive" attribute it means "hold" and causes
  Asterisk to start playing MOH back to the other party. This can be
  problematic if it happens at certain times, such as in a 183
  Progress message, because the MOH will replace any early media you
  may be playing to the calling party. If you set this option
  to "yes" on an endpoint and the endpoint receives an SDP
  with "sendonly" or "inactive", Asterisk will NOT play MOH back to
  the other party.

  Resolves: #979

  UserNote: The new "suppress_moh_on_sendonly" endpoint option
  can be used to prevent playing MOH back to a caller if the remote
  end sends "sendonly" or "inactive" (hold) to Asterisk in an SDP.


#### samples: remove and/or change some wiki mentions
  Author: chrsmj
  Date:   2024-11-01

  Cleaned some dead links. Replaced word wiki with
  either docs or link to https://docs.asterisk.org/

  Resolves: #974

#### res_pjsip: Move tenantid to end of ast_sip_endpoint
  Author: George Joseph
  Date:   2024-11-06

  The tenantid field was originally added to the ast_sip_endpoint
  structure at the end of the AST_DECLARE_STRING_FIELDS block.  This
  caused everything after it in the structure to move down in memory
  and break ABI compatibility.  It's now at the end of the structure
  as an AST_STRING_FIELD_EXTENDED.  Given the number of string fields
  in the structure now, the initial string field allocation was
  also increased from 64 to 128 bytes.

  Resolves: #982

#### Add res_pjsip_config_sangoma external module.
  Author: Ben Ford
  Date:   2024-11-01

  Adds res_pjsip_config_sangoma as an external module that can be
  downloaded via menuselect. It lives under the Resource Modules section.


#### app_mixmonitor: Add 'D' option for dual-channel audio.
  Author: Ben Ford
  Date:   2024-10-28

  Adds the 'D' option to app_mixmonitor that interleaves the input and
  output frames of the channel being recorded in the monitor output frame.
  This allows for two streams in the recording: the transmitted audio and
  the received audio. The 't' and 'r' options are compatible with this.

  Fixes: #945

  UserNote: The MixMonitor application now has a new 'D' option which
  interleaves the recorded audio in the output frames. This allows for
  stereo recording output with one channel being the transmitted audio and
  the other being the received audio. The 't' and 't' options are
  compatible with this.


#### core_unreal.c: Fix memory leak in ast_unreal_new_channels()
  Author: George Joseph
  Date:   2024-10-15

  When the channel tech is multistream capable, the reference to
  chan_topology was passed to the new channel.  When the channel tech
  isn't multistream capable, the reference to chan_topology was never
  released.  "Local" channels are multistream capable so it didn't
  affect them but the confbridge "CBAnn" and the bridge_media
  "Recorder" channels are not so they caused a leak every time one
  of them was created.

  Also added tracing to ast_stream_topology_alloc() and
  stream_topology_destroy() to assist with debugging.

  Resolves: #938

#### geolocation.sample.conf: Fix comment marker at end of file
  Author: George Joseph
  Date:   2024-10-08

  Resolves: #937

#### manager.c: Add unit test for Originate app and appdata permissions
  Author: George Joseph
  Date:   2024-10-03

  This unit test checks that dialplan apps and app data specified
  as parameters for the Originate action are allowed with the
  permissions the user has.


#### alembic: Drop redundant voicemail_messages index.
  Author: Sean Bright
  Date:   2024-09-26

  The `voicemail_messages_dir` index is a left prefix of the table's
  primary key and therefore unnecessary.


#### res_pjsip_pubsub: Persist subscription 'generator_data' in sorcery
  Author: Sean Bright
  Date:   2024-09-23

  Fixes #895


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

#### Fix application references to Background
  Author: George Joseph
  Date:   2024-09-20

  The app is actually named "BackGround" but several references
  in XML documentation were spelled "Background" with the lower
  case "g".  This was causing documentation links to return
  "not found" messages.


#### db.c: Remove limit on family/key length
  Author: George Joseph
  Date:   2024-09-11

  Consumers like media_cache have been running into issues with
  the previous astdb "/family/key" limit of 253 bytes when needing
  to store things like long URIs.  An Amazon S3 URI is a good example
  of this.  Now, instead of using a static 256 byte buffer for
  "/family/key", we use ast_asprintf() to dynamically create it.

  Both test_db.c and test_media_cache.c were also updated to use
  keys/URIs over the old 253 character limit.

  Resolves: #881

  UserNote: The `ast_db_*()` APIs have had the 253 byte limit on
  "/family/key" removed and will now accept families and keys with a
  total length of up to SQLITE_MAX_LENGTH (currently 1e9!).  This
  affects the `DB*` dialplan applications, dialplan functions,
  manager actions and `databse` CLI commands.  Since the
  media_cache also uses the `ast_db_*()` APIs, you can now store
  resources with URIs longer than 253 bytes.


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

