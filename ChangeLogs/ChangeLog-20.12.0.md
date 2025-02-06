
## Change Log for Release asterisk-20.12.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.12.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.11.1...20.12.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.12.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 53
- Commit Authors: 20
- Issues Resolved: 19
- Security Advisories Resolved: 0

### User Notes:

- #### sig_analog: Add Last Number Redial feature.                                     
  Users can now redial the last number
  called if the lastnumredial setting is set to yes.
  Resolves: #437

- #### Add SHA-256 and SHA-512-256 as authentication digest algorithms                 
  The SHA-256 and SHA-512-256 algorithms are now available
  for authentication as both a UAS and a UAC.

- #### Upgrade bundled pjproject to 2.15.1 Resolves: asterisk#1016                     
  Bundled pjproject has been upgraded to 2.15.1. For more
  information visit pjproject Github page: https://github.com/pjsip/pjproject/releases/tag/2.15.1

- #### res_pjsip: Add new AOR option "qualify_2xx_only"                                
  The pjsip.conf AOR section now has a "qualify_2xx_only"
  option that can be set so that only 2XX responses to OPTIONS requests
  used to qualify a contact will mark the contact as available.

- #### app_queue: allow dynamically adding a queue member in paused state.             
  use the p option of AddQueueMember() for paused member state.
  Optionally, use the r(reason) option to specify a custom reason for the pause.

- #### manager.c: Add Processed Call Count to CoreStatus output                        
  The current processed call count is now returned as CoreProcessedCalls from the
  CoreStatus AMI Action.

- #### func_curl.c: Add additional CURL options for SSL requests                       
  The following new configuration options are now available
  in the res_curl.conf file, and the CURL() function: 'ssl_verifyhost'
  (CURLOPT_SSL_VERIFYHOST), 'ssl_cainfo' (CURLOPT_CAINFO), 'ssl_capath'
  (CURLOPT_CAPATH), 'ssl_cert' (CURLOPT_SSLCERT), 'ssl_certtype'
  (CURLOPT_SSLCERTTYPE), 'ssl_key' (CURLOPT_SSLKEY), 'ssl_keytype',
  (CURLOPT_SSLKEYTYPE) and 'ssl_keypasswd' (CURLOPT_KEYPASSWD). See the
  libcurl documentation for more details.

- #### res_stir_shaken: Allow sending Identity headers for unknown TNs                 
  You can now set the "unknown_tn_attest_level" option
  in the attestation and/or profile objects in stir_shaken.conf to
  enable sending Identity headers for callerid TNs not explicitly
  configured.


### Upgrade Notes:

- #### alembic: Database updates required.                                             
  Two commits in this release...
  'Add SHA-256 and SHA-512-256 as authentication digest algorithms'
  'res_pjsip: Add new AOR option "qualify_2xx_only"'
  ...have modified alembic scripts for the following database tables: ps_aors,
  ps_contacts, ps_auths, ps_globals. If you don't use the scripts to update
  your database, reads from those tables will succeeed but inserts into the
  ps_contacts table by res_pjsip_registrar will fail.


### Commit Authors:

- Abdelkader Boudih: (3)
- Alexey Khabulyak: (1)
- Alexey Vasilyev: (1)
- Allan Nathanson: (2)
- Artem Umerov: (1)
- George Joseph: (17)
- Jaco Kroon: (1)
- James Terhune: (1)
- Joshua C. Colp: (1)
- Kent: (1)
- Maksim Nesterov: (1)
- Maximilian Fridrich: (1)
- Mike Pultz: (3)
- Naveen Albert: (6)
- Sean Bright: (6)
- Sperl Viktor: (2)
- Stanislav Abramenkov: (2)
- Steffen Arntz: (1)
- Tinet-Mucw: (1)
- Viktor Litvinov: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 437: [new-feature]: sig_analog: Add Last Number Redial
  - 851: [bug]: unable to read audiohook both side when packet lost on one side of the call 
  - 921: [bug]: Stir-Shaken doesn’t allow B or C attestation for unknown callerid which is allowed by ATIS-1000074.v003, §5.2.4
  - 927: [bug]: no audio when media source changed during the call
  - 948: [improvement]: Support SHA-256 algorithm on REGISTER and INVITE challenges
  - 993: [bug]: sig_analog: Feature Group D / E911 no longer work
  - 999: [bug]: Crash when setting a global variable with invalid UTF8 characters
  - 1007: [improvement]: Cannot dynamically add queue member in paused state from dialplan or command line
  - 1013: [improvement]: chan_pjsip: Send VIDUPDATE RTP frames for H.264 streams on endpoints without WebRTC
  - 1021: [improvement]: proper queue_log paused state when member added dynamically
  - 1023: [improvement]: Improve PJSIP_MEDIA_OFFER documentation
  - 1028: [bug]: "pjsip show endpoints" shows some identifies on endpoints that shouldn't be there
  - 1029: [bug]: chan_dahdi: Wrong channel state set when RINGING received
  - 1054: [bug]: chan_iax2: Frames unnecessarily backlogged with jitterbuffer if no voice frames have been received yet
  - 1058: [bug]: Asterisk fails to compile following commit 71a2e8c on Ubuntu 20.04
  - 1064: [improvement]: ast_tls_script: Add option to skip passphrase for CA private key
  - 1075: [bug]: res_prometheus does not set Content-Type header in HTTP response
  - 1095: [bug]: res_pjsip missing "Failed to authenticate" log entry for unknown endpoint
  - 1097: [bug]: res_pjsip/pjsip_options. ODBC: Unknown column 'qualify_2xx_only'

### Commits By Author:

- #### Abdelkader Boudih (3):
  - normalize contrib/ast-db-manage/queue_log.ini.sample
  - res_config_pgsql: normalize database connection option with cel and cdr by sup..
  - samples: Use "asterisk" instead of "postgres" for username

- #### Alexey Khabulyak (1):
  - format_gsm.c: Added mime type

- #### Alexey Vasilyev (1):
  - res_rtp_asterisk.c: Fix bridged_payload matching with sample rate for DTMF

- #### Allan Nathanson (2):
  - config.c: retain leading whitespace before comments
  - config.c: fix #tryinclude being converted to #include on rewrite

- #### Artem Umerov (1):
  - logger.h: Fix build when AST_DEVMODE is not defined.

- #### George Joseph (17):
  - res_stir_shaken: Allow sending Identity headers for unknown TNs
  - Allow C++ source files (as extension .cc) in the main directory
  - Add ability to pass arguments to unit tests from the CLI
  - Header fixes for compiling C++ source files
  - gcc14: Fix issues caught by gcc 14
  - Add C++ Standard detection to configure and fix a new C++20 compile issue
  - Add SHA-256 and SHA-512-256 as authentication digest algorithms
  - docs: Enable since/version handling for XML, CLI and ARI documentation
  - docs: Various XML fixes
  - res_pjsip_authenticator_digest: Fix issue with missing auth and DONT_OPTIMIZE
  - docs: Add version information to configObject and configOption XML elements
  - README.md, asterisk.c: Update Copyright Dates
  - docs: Add version information to manager event instance XML elements
  - docs: Add version information to application and function XML elements
  - res_pjsip: Fix startup/reload memory leak in config_auth.
  - res_pjsip_authenticator_digest: Make correct error messages appear again.
  - alembic: Database updates required.

- #### Jaco Kroon (1):
  - res_odbc: release threads from potential starvation.

- #### James Terhune (1):
  - main/stasis_channels.c: Fix crash when setting a global variable with invalid ..

- #### Joshua C. Colp (1):
  - LICENSE: Update company name, email, and address.

- #### Kent (1):
  - res_pjsip: Add new AOR option "qualify_2xx_only"

- #### Maksim Nesterov (1):
  - func_uuid: Add a new dialplan function to generate UUIDs

- #### Maximilian Fridrich (1):
  - chan_pjsip: Send VIDUPDATE RTP frame for all H.264 streams

- #### Mike Pultz (3):
  - func_curl.c: Add additional CURL options for SSL requests
  - manager.c: Add Processed Call Count to CoreStatus output
  - res_curl.conf.sample: clean up sample configuration and add new SSL options

- #### Naveen Albert (6):
  - sig_analog: Fix regression with FGD and E911 signaling.
  - chan_iax2: Add log message for rejected calls.
  - chan_dahdi: Fix wrong channel state when RINGING recieved.
  - sig_analog: Add Last Number Redial feature.
  - chan_iax2: Avoid unnecessarily backlogging non-voice frames.
  - ast_tls_cert: Add option to skip passphrase for CA private key.

- #### Sean Bright (6):
  - config.c: Fix off-nominal reference leak.
  - manager.c: Rename restrictedFile to is_restricted_file.
  - manager: Add `<since>` tags for all AMI actions.
  - dialplan_functions_doc.xml: Document PJSIP_MEDIA_OFFER's `media` argument.
  - strings.c: Improve numeric detection in `ast_strings_match()`.
  - res_prometheus.c: Set Content-Type header on /metrics response.

- #### Sperl Viktor (2):
  - app_queue: allow dynamically adding a queue member in paused state.
  - app_queue: indicate the paused state of a dynamically added member in queue_log.

- #### Stanislav Abramenkov (2):
  - Upgrade bundled pjproject to 2.15.1 Resolves: asterisk#1016
  - res_pjproject: Fix typo (OpenmSSL->OpenSSL)

- #### Steffen Arntz (1):
  - logger.c fix: malformed JSON template

- #### Tinet-mucw (1):
  - audiohook.c: resolving the issue with audiohook both reading when packet loss ..

- #### Viktor Litvinov (1):
  - res_rtp_asterisk.c: Set Mark on rtp when timestamp skew is too big


### Commit List:

-  alembic: Database updates required.
-  res_pjsip_authenticator_digest: Make correct error messages appear again.
-  res_pjsip: Fix startup/reload memory leak in config_auth.
-  docs: Add version information to application and function XML elements
-  docs: Add version information to manager event instance XML elements
-  LICENSE: Update company name, email, and address.
-  res_prometheus.c: Set Content-Type header on /metrics response.
-  README.md, asterisk.c: Update Copyright Dates
-  docs: Add version information to configObject and configOption XML elements
-  res_pjsip_authenticator_digest: Fix issue with missing auth and DONT_OPTIMIZE
-  ast_tls_cert: Add option to skip passphrase for CA private key.
-  chan_iax2: Avoid unnecessarily backlogging non-voice frames.
-  config.c: fix #tryinclude being converted to #include on rewrite
-  sig_analog: Add Last Number Redial feature.
-  docs: Various XML fixes
-  strings.c: Improve numeric detection in `ast_strings_match()`.
-  docs: Enable since/version handling for XML, CLI and ARI documentation
-  logger.h: Fix build when AST_DEVMODE is not defined.
-  dialplan_functions_doc.xml: Document PJSIP_MEDIA_OFFER's `media` argument.
-  samples: Use "asterisk" instead of "postgres" for username
-  manager: Add `<since>` tags for all AMI actions.
-  logger.c fix: malformed JSON template
-  manager.c: Rename restrictedFile to is_restricted_file.
-  res_pjproject: Fix typo (OpenmSSL->OpenSSL)
-  Add SHA-256 and SHA-512-256 as authentication digest algorithms
-  config.c: retain leading whitespace before comments
-  config.c: Fix off-nominal reference leak.
-  normalize contrib/ast-db-manage/queue_log.ini.sample
-  Add C++ Standard detection to configure and fix a new C++20 compile issue
-  chan_dahdi: Fix wrong channel state when RINGING recieved.
-  Upgrade bundled pjproject to 2.15.1 Resolves: asterisk#1016
-  gcc14: Fix issues caught by gcc 14
-  Header fixes for compiling C++ source files
-  Add ability to pass arguments to unit tests from the CLI
-  res_pjsip: Add new AOR option "qualify_2xx_only"
-  res_odbc: release threads from potential starvation.
-  Allow C++ source files (as extension .cc) in the main directory
-  format_gsm.c: Added mime type
-  func_uuid: Add a new dialplan function to generate UUIDs
-  app_queue: allow dynamically adding a queue member in paused state.
-  chan_iax2: Add log message for rejected calls.
-  chan_pjsip: Send VIDUPDATE RTP frame for all H.264 streams
-  res_curl.conf.sample: clean up sample configuration and add new SSL options
-  res_rtp_asterisk.c: Set Mark on rtp when timestamp skew is too big
-  res_rtp_asterisk.c: Fix bridged_payload matching with sample rate for DTMF
-  manager.c: Add Processed Call Count to CoreStatus output
-  func_curl.c: Add additional CURL options for SSL requests
-  sig_analog: Fix regression with FGD and E911 signaling.
-  res_stir_shaken: Allow sending Identity headers for unknown TNs

### Commit Details:

#### alembic: Database updates required.
  Author: George Joseph
  Date:   2025-01-28

  This commit doesn't actually change anything.  It just adds the following
  upgrade notes that were omitted from the original commits.

  Resolves: #1097

  UpgradeNote: Two commits in this release...
  'Add SHA-256 and SHA-512-256 as authentication digest algorithms'
  'res_pjsip: Add new AOR option "qualify_2xx_only"'
  ...have modified alembic scripts for the following database tables: ps_aors,
  ps_contacts, ps_auths, ps_globals. If you don't use the scripts to update
  your database, reads from those tables will succeeed but inserts into the
  ps_contacts table by res_pjsip_registrar will fail.

#### res_pjsip_authenticator_digest: Make correct error messages appear again.
  Author: George Joseph
  Date:   2025-01-28

  When an incoming request can't be matched to an endpoint, the "artificial"
  auth object is used to create a challenge to return in a 401 response and we
  emit a "No matching endpoint found" log message. If the client then responds
  with an Authorization header but the request still can't be matched to an
  endpoint, the verification will fail and, as before, we'll create a challenge
  to return in a 401 response and we emit a "No matching endpoint found" log
  message.  HOWEVER, because there WAS an Authorization header and it failed
  verification, we should have also been emitting a "Failed to authenticate"
  log message but weren't because there was a check that short-circuited that
  it if the artificial auth was used.  Since many admins use the "Failed to
  authenticate" message with log parsers like fail2ban, those attempts were not
  being recognized as suspicious.

  Changes:

  * digest_check_auth() now always emits the "Failed to authenticate" log
    message if verification of an Authorization header failed even if the
    artificial auth was used.

  * The verification logic was refactored to be clearer about the handling
    of the return codes from verify().

  * Comments were added clarify what return codes digest_check_auth() should
    return to the distributor and the implications of changing them.

  Resolves: #1095

#### res_pjsip: Fix startup/reload memory leak in config_auth.
  Author: George Joseph
  Date:   2025-01-23

  An issue in config_auth.c:ast_sip_auth_digest_algorithms_vector_init() was
  causing double allocations for the two supported_algorithms vectors to the
  tune of 915 bytes.  The leak only happens on startup and when a reload is done
  and doesn't get bigger with the number of auth objects defined.

  * Pre-initialized the two vectors in config_auth:auth_alloc().
  * Removed the allocations in ast_sip_auth_digest_algorithms_vector_init().
  * Added a note to the doc for ast_sip_auth_digest_algorithms_vector_init()
    noting that the vector passed in should be initialized and empty.
  * Simplified the create_artificial_auth() function in pjsip_distributor.
  * Set the vector initialization count to 0 in config_global:global_apply().

#### docs: Add version information to application and function XML elements
  Author: George Joseph
  Date:   2025-01-23

  * Do a git blame on the embedded XML application or function element.

  * From the commit hash, grab the summary line.

  * Do a git log --grep <summary> to find the cherry-pick commits in all
    branches that match.

  * Do a git patch-id to ensure the commits are all related and didn't get
    a false match on the summary.

  * Do a git tag --contains <commit> to find the tags that contain each
    commit.

  * Weed out all tags not ..0.

  * Sort and discard any .0.0 and following tags where the commit
    appeared in an earlier branch.

  * The result is a single tag for each branch where the application or function
    was defined.

  The applications and functions defined in the following files were done by
  hand because the XML was extracted from the C source file relatively recently.
  * channels/pjsip/dialplan_functions_doc.xml
  * main/logger_doc.xml
  * main/manager_doc.xml
  * res/res_geolocation/geoloc_doc.xml
  * res/res_stir_shaken/stir_shaken_doc.xml


#### docs: Add version information to manager event instance XML elements
  Author: George Joseph
  Date:   2025-01-20

  * Do a git blame on the embedded XML managerEvent elements.

  * From the commit hash, grab the summary line.

  * Do a git log --grep <summary> to find the cherry-pick commits in all
    branches that match.

  * Do a git patch-id to ensure the commits are all related and didn't get
    a false match on the summary.

  * Do a git tag --contains <commit> to find the tags that contain each
    commit.

  * Weed out all tags not ..0.

  * Sort and discard any .0.0 and following tags where the commit
    appeared in an earlier branch.

  * The result is a single tag for each branch where the application or function
    was defined.

  The events defined in res/res_pjsip/pjsip_manager.xml were done by hand
  because the XML was extracted from the C source file relatively recently.

  Two bugs were fixed along the way...

  * The get_documentation awk script was exiting after it processed the first
    DOCUMENTATION block it found in a file.  We have at least 1 source file
    with multiple DOCUMENTATION blocks so only the first one in them was being
    processed.  The awk script was changed to continue searching rather
    than exiting after the first block.

  * Fixing the awk script revealed an issue in logger.c where the third
    DOCUMENTATION block contained a XML fragment that consisted only of
    a managerEventInstance element that wasn't wrapped in a managerEvent
    element.  Since logger_doc.xml already existed, the remaining fragments
    in logger.c were moved to it and properly organized.


#### LICENSE: Update company name, email, and address.
  Author: Joshua C. Colp
  Date:   2025-01-21


#### res_prometheus.c: Set Content-Type header on /metrics response.
  Author: Sean Bright
  Date:   2025-01-21

  This should resolve the Prometheus error:

  > Error scraping target: non-compliant scrape target
    sending blank Content-Type and no
    fallback_scrape_protocol specified for target.

  Resolves: #1075

#### README.md, asterisk.c: Update Copyright Dates
  Author: George Joseph
  Date:   2025-01-20


#### docs: Add version information to configObject and configOption XML elements
  Author: George Joseph
  Date:   2025-01-16

  Most of the configObjects and configOptions that are implemented with
  ACO or Sorcery now have `<since>/<version>` elements added.  There are
  probably some that the script I used didn't catch.  The version tags were
  determined by the following...
   * Do a git blame on the API call that created the object or option.
   * From the commit hash, grab the summary line.
   * Do a `git log --grep <summary>` to find the cherry-pick commits in all
     branches that match.
   * Do a `git patch-id` to ensure the commits are all related and didn't get
     a false match on the summary.
   * Do a `git tag --contains <commit>` to find the tags that contain each
     commit.
   * Weed out all tags not <major>.<minor>.0.
   * Sort and discard any <major>.0.0 and following tags where the commit
     appeared in an earlier branch.
   * The result is a single tag for each branch where the API was last touched.

  configObjects and configOptions elements implemented with the base
  ast_config APIs were just not possible to find due to the non-deterministic
  way they are accessed.

  Also note that if the API call was on modified after it was added, the
  version will be the one it was last modified in.

  Final note:  The configObject and configOption elements were introduced in
  12.0.0 so options created before then may not have any XML documentation.


#### res_pjsip_authenticator_digest: Fix issue with missing auth and DONT_OPTIMIZE
  Author: George Joseph
  Date:   2025-01-17

  The return code fom digest_check_auth wasn't explicitly being initialized.
  The return code also wasn't explicitly set to CHALLENGE when challenges
  were sent.  When optimization was turned off (DONT_OPTIMIZE), the compiler
  was setting it to "0"(CHALLENGE) which worked fine.  However, with
  optimization turned on, it was setting it to "1" (SUCCESS) so if there was
  no incoming Authorization header, the function was returning SUCCESS to the
  distributor allowing the request to incorrectly succeed.

  The return code is now initialized correctly and is now explicitly set
  to CHALLENGE when we send challenges.


#### ast_tls_cert: Add option to skip passphrase for CA private key.
  Author: Naveen Albert
  Date:   2025-01-14

  Currently, the ast_tls_cert file is hardcoded to use the -des3 option
  for 3DES encryption, and the script needs to be manually modified
  to not require a passphrase. Add an option (-e) that disables
  encryption of the CA private key so no passphrase is required.

  Resolves: #1064

#### chan_iax2: Avoid unnecessarily backlogging non-voice frames.
  Author: Naveen Albert
  Date:   2025-01-09

  Currently, when receiving an unauthenticated call, we keep track
  of the negotiated format in the chosenformat, which allows us
  to later create the channel using the right format. However,
  this was not done for authenticated calls. This meant that in
  certain circumstances, if we had not yet received a voice frame
  from the peer, only certain other types of frames (e.g. text),
  there were no variables containing the appropriate frame.
  This led to problems in the jitterbuffer callback where we
  unnecessarily bailed out of retrieving a frame from the jitterbuffer.
  This was logic intentionally added in commit 73103bdcd5b342ce5dfa32039333ffadad551151
  in response to an earlier regression, and while this prevents
  crashes, it also backlogs legitimate frames unnecessarily.

  The abort logic was initially added because at this point in the
  code, we did not have the negotiated format available to us.
  However, it should always be available to us as a last resort
  in chosenformat, so we now pull it from there if needed. This
  allows us to process frames the jitterbuffer even if voicefmt
  and peerfmt aren't set and still avoid the crash. The failsafe
  logic is retained, but now it shouldn't be triggered anymore.

  Resolves: #1054

#### config.c: fix #tryinclude being converted to #include on rewrite
  Author: Allan Nathanson
  Date:   2024-09-16

  Correct an issue in ast_config_text_file_save2() when updating configuration
  files with "#tryinclude" statements. The API currently replaces "#tryinclude"
  with "#include". The API also creates empty template files if the referenced
  files do not exist. This change resolves these problems.

  Resolves: https://github.com/asterisk/asterisk/issues/920

#### sig_analog: Add Last Number Redial feature.
  Author: Naveen Albert
  Date:   2023-11-10

  This adds the Last Number Redial feature to
  simple switch.

  UserNote: Users can now redial the last number
  called if the lastnumredial setting is set to yes.

  Resolves: #437

#### docs: Various XML fixes
  Author: George Joseph
  Date:   2025-01-15

  * channels/pjsip/dialplan_functions_doc.xml: Added xmlns:xi to docs element.

  * main/bucket.c: Removed XML completely since the "bucket" and "file" objects
    are internal only with no config file.

  * main/named_acl.c: Fixed the configFile element name. It was "named_acl.conf"
    and should have been "acl.conf"

  * res/res_geolocation/geoloc_doc.xml: Added xmlns:xi to docs element.

  * res/res_http_media_cache.c: Fixed the configFile element name. It was
    "http_media_cache.conf" and should have been "res_http_media_cache.conf".


#### strings.c: Improve numeric detection in `ast_strings_match()`.
  Author: Sean Bright
  Date:   2025-01-15

  Essentially, we were treating 1234x1234 and 1234x5678 as 'equal'
  because we were able to convert the prefix of each of these strings to
  the same number.

  Resolves: #1028

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


#### logger.h: Fix build when AST_DEVMODE is not defined.
  Author: Artem Umerov
  Date:   2025-01-13

  Resolves: #1058

#### dialplan_functions_doc.xml: Document PJSIP_MEDIA_OFFER's `media` argument.
  Author: Sean Bright
  Date:   2025-01-14

  Resolves: #1023

#### samples: Use "asterisk" instead of "postgres" for username
  Author: Abdelkader Boudih
  Date:   2025-01-07


#### manager: Add `<since>` tags for all AMI actions.
  Author: Sean Bright
  Date:   2025-01-02


#### logger.c fix: malformed JSON template
  Author: Steffen Arntz
  Date:   2025-01-08

  this typo was mentioned before, but never got fixed.
  https://community.asterisk.org/t/logger-cannot-log-long-json-lines-properly/87618/6


#### manager.c: Rename restrictedFile to is_restricted_file.
  Author: Sean Bright
  Date:   2025-01-09

  Also correct the spelling of 'privileges.'


#### res_config_pgsql: normalize database connection option with cel and cdr by sup..
  Author: Abdelkader Boudih
  Date:   2025-01-08


#### res_pjproject: Fix typo (OpenmSSL->OpenSSL)
  Author: Stanislav Abramenkov
  Date:   2025-01-10

  Fix typo (OpenmSSL->OpenSSL) mentioned by bkford in #972


#### Add SHA-256 and SHA-512-256 as authentication digest algorithms
  Author: George Joseph
  Date:   2024-10-17

  * Refactored pjproject code to support the new algorithms and
  added a patch file to third-party/pjproject/patches

  * Added new parameters to the pjsip auth object:
    * password_digest = <algorithm>:<digest>
    * supported_algorithms_uac = List of algorithms to support
      when acting as a UAC.
    * supported_algorithms_uas = List of algorithms to support
      when acting as a UAS.
    See the auth object in pjsip.conf.sample for detailed info.

  * Updated both res_pjsip_authenticator_digest.c (for UAS) and
  res_pjsip_outbound_authentocator_digest.c (UAC) to suport the
  new algorithms.

  The new algorithms are only available with the bundled version
  of pjproject, or an external version > 2.14.1.  OpenSSL version
  1.1.1 or greater is required to support SHA-512-256.

  Resolves: #948

  UserNote: The SHA-256 and SHA-512-256 algorithms are now available
  for authentication as both a UAS and a UAC.


#### config.c: retain leading whitespace before comments
  Author: Allan Nathanson
  Date:   2024-10-30

  Configurations loaded with the ast_config_load2() API and later written
  out with ast_config_text_file_save2() will have any leading whitespace
  stripped away.  The APIs should make reasonable efforts to maintain the
  content and formatting of the configuration files.

  This change retains any leading whitespace from comment lines that start
  with a ";".

  Resolves: https://github.com/asterisk/asterisk/issues/970

#### config.c: Fix off-nominal reference leak.
  Author: Sean Bright
  Date:   2025-01-07

  This was identified and fixed by @Allan-N in #918 but it is an
  important fix in its own right.

  The fix here is slightly different than Allan's in that we just move
  the initialization of the problematic AO2 container to where it is
  first used.

  Fixes #1046


#### normalize contrib/ast-db-manage/queue_log.ini.sample
  Author: Abdelkader Boudih
  Date:   2025-01-05


#### Add C++ Standard detection to configure and fix a new C++20 compile issue
  Author: George Joseph
  Date:   2025-01-03

  * The autoconf-archive package contains macros useful for detecting C++
    standard and testing other C++ capabilities but that package was never
    included in the install_prereq script so many existing build environments
    won't have it.  Even if it is installed, older versions won't newer C++
    standards and will actually cause an error if you try to test for that
    version. To make it available for those environments, the
    ax_cxx_compile_stdcxx.m4 macro has copied from the latest release of
    autoconf-archive into the autoconf directory.

  * A convenience wrapper(ast_cxx_check_std) around ax_cxx_compile_stdcxx was
    also added so checking the standard version and setting the
    asterisk-specific PBX_ variables becomes a one-liner:
    `AST_CXX_CHECK_STD([std], [force_latest_std])`.
    Calling that with a version of `17` for instance, will set PBX_CXX17
    to 0 or 1 depending on whether the current c++ compiler supports stdc++17.
    HAVE_CXX17 will also be 'defined" or not depending on the result.

  * C++ compilers hardly ever default to the latest standard they support.  g++
    version 14 for instance supports up to C++23 but only uses C++17 by default.
    If you want to use C++23, you have to add `-std=gnu++=23` to the g++
    command line.  If you set the second argument of AST_CXX_CHECK_STD to "yes",
    the macro will automatically keep the highest `-std=gnu++` value that
    worked and pass that to the Makefiles.

  * The autoconf-archive package was added to install_prereq for future use.

  * Updated configure.ac to use AST_CXX_CHECK_STD() to check for C++
    versions 11, 14, 17, 20 and 23.

  * Updated configure.ac to accept the `--enable-latest-cxx-std` option which
    will set the second option to AST_CXX_CHECK_STD() to "yes".  The default
    is "no".

  * ast_copy_string() in strings.h declares the 'sz' variable as volatile and
    does an `sz--` on it later.  C++20 no longer allows the `++` and `--`
    increment and decrement operators to be used on variables declared as
    volatile however so that was changed to `sz -= 1`.


#### chan_dahdi: Fix wrong channel state when RINGING recieved.
  Author: Naveen Albert
  Date:   2024-12-16

  Previously, when AST_CONTROL_RINGING was received by
  a DAHDI device, it would set its channel state to
  AST_STATE_RINGING. However, an analysis of the codebase
  and other channel drivers reveals RINGING corresponds to
  physical power ringing, whereas AST_STATE_RING should be
  used for audible ringback on the channel. This also ensures
  the correct device state is returned by the channel state
  to device state conversion.

  Since there seems to be confusion in various places regarding
  AST_STATE_RING vs. AST_STATE_RINGING, some documentation has
  been added or corrected to clarify the actual purposes of these
  two channel states, and the associated device state mapping.

  An edge case that prompted this fix, but isn't explicitly
  addressed here, is that of an incoming call to an FXO port.
  The channel state will be "Ring", which maps to a device state
  of "In Use", not "Ringing" as would be more intuitive. However,
  this is semantic, since technically, Asterisk is treating this
  the same as any other incoming call, and so "Ring" is the
  semantic state (put another way, Asterisk isn't ringing anything,
  like in the cases where channels are in the "Ringing" state).

  Since FXO ports don't currently support Call Waiting, a suitable
  workaround for the above would be to ignore the device state and
  instead check the channel state (e.g. IMPORT(DAHDI/1-1,CHANNEL(state)))
  since it will be Ring if the FXO port is idle (but a call is ringing
  on it) and Up if the FXO port is actually in use. (In both cases,
  the device state would misleadingly be "In Use".)

  Resolves: #1029

#### Upgrade bundled pjproject to 2.15.1 Resolves: asterisk#1016
  Author: Stanislav Abramenkov
  Date:   2024-12-03

  UserNote: Bundled pjproject has been upgraded to 2.15.1. For more
  information visit pjproject Github page: https://github.com/pjsip/pjproject/releases/tag/2.15.1


#### gcc14: Fix issues caught by gcc 14
  Author: George Joseph
  Date:   2025-01-03

  * reqresp_parser.c: Fix misuse of "static" with linked list definitions
  * test_message.c: Fix segfaults caused by passing NULL as an sprintf fmt


#### Header fixes for compiling C++ source files
  Author: George Joseph
  Date:   2024-12-31

  A few tweaks needed to be done to some existing header files to allow them to
  be compiled when included from C++ source files.

  logger.h had declarations for ast_register_verbose() and
  ast_unregister_verbose() which caused C++ issues but those functions were
  actually removed from logger.c many years ago so the declarations were just
  removed from logger.h.


#### Add ability to pass arguments to unit tests from the CLI
  Author: George Joseph
  Date:   2024-12-27

  Unit tests can now be passed custom arguments from the command
  line.  For example, the following command would run the "mytest" test
  in the "/main/mycat" category with the option "myoption=54"

  `CLI> test execute category /main/mycat name mytest options myoption=54`

  You can also pass options to an entire category...

  `CLI> test execute category /main/mycat options myoption=54`

  Basically, everything after the "options" keyword is passed verbatim to
  the test which must decide what to do with it.

  * A new API ast_test_get_cli_args() was created to give the tests access to
  the cli_args->argc and cli_args->argv elements.

  * Although not needed for the option processing, a new macro
  ast_test_validate_cleanup_custom() was added to test.h that allows you
  to specify a custom error message instead of just "Condition failed".

  * The test_skel.c was updated to demonstrate parsing options and the use
  of the ast_test_validate_cleanup_custom() macro.


#### res_pjsip: Add new AOR option "qualify_2xx_only"
  Author: Kent
  Date:   2024-12-03

  Added a new option "qualify_2xx_only" to the res_pjsip AOR qualify
  feature to mark a contact as available only if an OPTIONS request
  returns a 2XX response. If the option is not specified or is false,
  any response to the OPTIONS request marks the contact as available.

  UserNote: The pjsip.conf AOR section now has a "qualify_2xx_only"
  option that can be set so that only 2XX responses to OPTIONS requests
  used to qualify a contact will mark the contact as available.


#### res_odbc: release threads from potential starvation.
  Author: Jaco Kroon
  Date:   2024-12-10

  Whenever a slot is freed up due to a failed connection, wake up a waiter
  before failing.

  In the case of a dead connection there could be waiters, for example,
  let's say two threads tries to acquire objects at the same time, with
  one in the cached connections, one will acquire the dead connection, and
  the other will enter into the wait state.  The thread with the dead
  connection will clear up the dead connection, and then attempt a
  re-acquire (at this point there cannot be cached connections else the
  other thread would have received that and tried to clean up), as such,
  at this point we're guaranteed that either there are no waiting threads,
  or that the maxconnections - connection_cnt threads will attempt to
  re-acquire connections, and then either succeed, using those
  connections, or failing, and then signalling to release more waiters.

  Also fix the pointer log for ODBC handle %p dead which would always
  reflect NULL.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

#### app_queue: indicate the paused state of a dynamically added member in queue_log.
  Author: Sperl Viktor
  Date:   2024-12-05

  Fixes: #1021

#### Allow C++ source files (as extension .cc) in the main directory
  Author: George Joseph
  Date:   2024-12-09

  Although C++ files (as extension .cc) have been handled in the module
  directories for many years, the main directory was missing one line in its
  Makefile that prevented C++ files from being recognised there.


#### format_gsm.c: Added mime type
  Author: Alexey Khabulyak
  Date:   2024-12-03

  Sometimes it's impossible to get a file extension from URL
  (eg. http://example.com/gsm/your) so we have to rely on content-type header.
  Currenly, asterisk does not support content-type for gsm format(unlike wav).
  Added audio/gsm according to https://www.rfc-editor.org/rfc/rfc4856.html


#### func_uuid: Add a new dialplan function to generate UUIDs
  Author: Maksim Nesterov
  Date:   2024-12-01

  This function is useful for uniquely identifying calls, recordings, and other entities in distributed environments, as well as for generating an argument for the AudioSocket application.


#### app_queue: allow dynamically adding a queue member in paused state.
  Author: Sperl Viktor
  Date:   2024-11-27

  Fixes: #1007

  UserNote: use the p option of AddQueueMember() for paused member state.
  Optionally, use the r(reason) option to specify a custom reason for the pause.


#### chan_iax2: Add log message for rejected calls.
  Author: Naveen Albert
  Date:   2023-11-06

  Add a log message for a path that currently silently drops IAX2
  frames without indicating that anything is wrong.


#### chan_pjsip: Send VIDUPDATE RTP frame for all H.264 streams
  Author: Maximilian Fridrich
  Date:   2024-12-02

  Currently, when a chan_pjsip channel receives a VIDUPDATE indication,
  an RTP VIDUPDATE frame is only queued on a H.264 stream if WebRTC is
  enabled on that endpoint. This restriction does not really make sense.

  Now, a VIDUPDATE RTP frame is written even if WebRTC is not enabled (as
  is the case with VP8, VP9, and H.265 streams).

  Resolves: #1013

#### audiohook.c: resolving the issue with audiohook both reading when packet loss ..
  Author: Tinet-mucw
  Date:   2024-08-22

  When there is 0% packet loss on one side of the call and 15% packet loss on the other side, reading frame is often failed when reading direction_both audiohook. when read_factory available = 0, write_factory available = 320; i think write factory is usable read; because after reading one frame, there is still another frame that can be read together with the next read factory frame.

  Resolves: #851

#### res_curl.conf.sample: clean up sample configuration and add new SSL options
  Author: Mike Pultz
  Date:   2024-11-21

  This update properly documents all the current configuration options supported
  by the curl implementation, including the new ssl_* options.


#### res_rtp_asterisk.c: Set Mark on rtp when timestamp skew is too big
  Author: Viktor Litvinov
  Date:   2024-10-02

  Set Mark bit in rtp stream when timestamp skew is bigger than MAX_TIMESTAMP_SKEW.

  Fixes: #927

#### res_rtp_asterisk.c: Fix bridged_payload matching with sample rate for DTMF
  Author: Alexey Vasilyev
  Date:   2024-11-25

  Fixes #1004


#### manager.c: Add Processed Call Count to CoreStatus output
  Author: Mike Pultz
  Date:   2024-11-21

  This update adds the processed call count to the CoreStatus AMI Action responsie. This output is
  similar to the values returned by "core show channels" or "core show calls" in the CLI.

  UserNote: The current processed call count is now returned as CoreProcessedCalls from the
  CoreStatus AMI Action.


#### func_curl.c: Add additional CURL options for SSL requests
  Author: Mike Pultz
  Date:   2024-11-09

  This patch adds additional CURL TLS options / options to support mTLS authenticated requests:

  * ssl_verifyhost - perform a host verification on the peer certificate (CURLOPT_SSL_VERIFYHOST)
  * ssl_cainfo - define a CA certificate file (CURLOPT_CAINFO)
  * ssl_capath - define a CA certificate directory (CURLOPT_CAPATH)
  * ssl_cert - define a client certificate for the request (CURLOPT_SSLCERT)
  * ssl_certtype - specify the client certificate type (CURLOPT_SSLCERTTYPE)
  * ssl_key - define a client private key for the request (CURLOPT_SSLKEY)
  * ssl_keytype - specify the client private key type (CURLOPT_SSLKEYTYPE)
  * ssl_keypasswd - set a password for the private key, if required (CURLOPT_KEYPASSWD)

  UserNote: The following new configuration options are now available
  in the res_curl.conf file, and the CURL() function: 'ssl_verifyhost'
  (CURLOPT_SSL_VERIFYHOST), 'ssl_cainfo' (CURLOPT_CAINFO), 'ssl_capath'
  (CURLOPT_CAPATH), 'ssl_cert' (CURLOPT_SSLCERT), 'ssl_certtype'
  (CURLOPT_SSLCERTTYPE), 'ssl_key' (CURLOPT_SSLKEY), 'ssl_keytype',
  (CURLOPT_SSLKEYTYPE) and 'ssl_keypasswd' (CURLOPT_KEYPASSWD). See the
  libcurl documentation for more details.


#### sig_analog: Fix regression with FGD and E911 signaling.
  Author: Naveen Albert
  Date:   2024-11-14

  Commit 466eb4a52b69e6dead7ebba13a83f14ef8a559c1 introduced a regression
  which completely broke Feature Group D and E911 signaling, by removing
  the call to analog_my_getsigstr, which affected multiple switch cases.
  Restore the original behavior for all protocols except Feature Group C
  CAMA (MF), which is all that patch was attempting to target.

  Resolves: #993

#### main/stasis_channels.c: Fix crash when setting a global variable with invalid ..
  Author: James Terhune
  Date:   2024-11-18

  Add check for null value of chan before referencing it with ast_channel_name()

  Resolves: #999

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


