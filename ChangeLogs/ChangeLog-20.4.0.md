
Change Log for Release 20.4.0
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.4.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.3.1...20.4.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.4.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- app.h: Move declaration of ast_getdata_result before its first use
- doc: Remove obsolete CHANGES-staging and UPGRADE-staging
- .github: Updates for AsteriskReleaser
- app_voicemail: fix imap compilation errors
- res_musiconhold: avoid moh state access on unlocked chan
- utils: add lock timestamps for DEBUG_THREADS
- .github: Back out triggering PROpenedOrUpdated by label
- .github: Move publish docs to new file CreateDocs.yml
- rest-api: Updates for new documentation site
- .github: Remove result check from PROpenUpdateGateTests
- .github: Fix use of 'contains'
- .github: Add recheck label test to additional jobs
- .github: Fix recheck label typos
- .github: Fix recheck label manipulation
- .github: Allow PR submit checks to be re-run by label
- app_voicemail_imap: Fix message count when IMAP server is unavailable
- res_pjsip_rfc3326: Prefer Q.850 cause code over SIP.
- res_pjsip_session: Added new function calls to avoid ABI issues.
- app_queue: Add force_longest_waiting_caller option.
- pjsip_transport_events.c: Use %zu printf specifier for size_t.
- res_crypto.c: Gracefully handle potential key filename truncation.
- configure: Remove obsolete and deprecated constructs.
- res_fax_spandsp.c: Clean up a spaces/tabs issue
- ast-db-manage: Synchronize revisions between comments and code.
- test_statis_endpoints:  Fix channel_messages test again
- res_crypto.c: Avoid using the non-portable ALLPERMS macro.
- tcptls: when disabling a server port, we should set the accept_fd to -1.
- AMI: Add parking position parameter to Park action
- test_stasis_endpoints.c: Make channel_messages more stable
- build: Fix a few gcc 13 issues
- .github: Rework for merge approval
- ast-db-manage: Fix alembic branching error caused by #122.
- app_followme: fix issue with enable_callee_prompt=no (#88)
- sounds: Update download URL to use HTTPS.
- configure: Makefile downloader enable follow redirects.
- res_musiconhold: Add option to loop last file.
- chan_dahdi: Fix Caller ID presentation for FXO ports.
- AMI: Add CoreShowChannelMap action.
- sig_analog: Add fuller Caller ID support.
- res_stasis.c: Add new type 'sdp_label' for bridge creation.
- app_queue: Preserve reason for realtime queues
- .github: Fix issues with cherry-pick-reminder
- indications: logging changes
- .github Ignore error when adding reviewrs to PR
- .github: Update field descriptions for AsteriskReleaser
- callerid: Allow specifying timezone for date/time.
- logrotate: Fix duplicate log entries.
- chan_pjsip: Allow topology/session refreshes in early media state
- chan_dahdi: Fix broken hidecallerid setting.
- .github: Change title of AsteriskReleaser job
- asterisk.c: Fix option warning for remote console.
- .github: Don't add cherry-pick reminder if it's already present
- .github: Fix quoting in PROpenedOrUpdated
- .github: Add cherry-pick reminder to new PRs
- configure: fix test code to match gethostbyname_r prototype.
- res_pjsip_pubsub.c: Use pjsip version for pending NOTIFY check. (#77)
- res_sorcery_memory_cache.c: Fix memory leak
- xml.c: Process XML Inclusions recursively.
- .github: Tweak improvement issue type language.
- .github: Tweak new feature language, and move feature requests elsewhere.
- .github: Fix staleness check to only run on certain labels.

User Notes:
----------------------------------------

- ### AMI: Add parking position parameter to Park action
  New ParkingSpace parameter has been added to AMI action Park.

- ### res_musiconhold: Add option to loop last file.
  The loop_last option in musiconhold.conf now
  allows the last file in the directory to be looped once reached.

- ### AMI: Add CoreShowChannelMap action.
  New AMI action CoreShowChannelMap has been added.

- ### sig_analog: Add fuller Caller ID support.
  Additional Caller ID properties are now supported on
  incoming calls to FXS stations, namely the
  redirecting reason and call qualifier.

- ### res_stasis.c: Add new type 'sdp_label' for bridge creation.
  When creating a bridge using the ARI the 'type' argument now
  accepts a new value 'sdp_label' which will configure the bridge to add
  labels for each stream in the SDP with the corresponding channel id.

- ### app_queue: Preserve reason for realtime queues
  Make paused reason in realtime queues persist an
  Asterisk restart. This was fixed for non-realtime
  queues in ASTERISK_25732.


Upgrade Notes:
----------------------------------------

- ### app_queue: Preserve reason for realtime queues
  Add a new column to the queue_member table:
  reason_paused VARCHAR(80) so the reason can be preserved.


Closed Issues:
----------------------------------------

  - #45: [bug]: Non-bundled PJSIP check for evsub pending NOTIFY check is insufficient/ineffective
  - #55: [bug]: res_sorcery_memory_cache: Memory leak when calling sorcery_memory_cache_open
  - #64: [bug]: app_voicemail_imap wrong behavior when losing IMAP connection
  - #65: [bug]: heap overflow by default at startup
  - #66: [improvement]: Fix preserve reason of pause when Asterisk is restared for realtime queues
  - #73: [new-feature]: pjsip: Allow topology/session refreshes in early media state
  - #87: [bug]: app_followme: Setting enable_callee_prompt=no breaks timeout
  - #89: [improvement]:  indications: logging changes
  - #91: [improvement]: Add parameter on ARI bridge create to allow it to send SDP labels
  - #94: [new-feature]: sig_analog: Add full Caller ID support for incoming calls
  - #96: [bug]: make install-logrotate causes logrotate to fail on service restart
  - #98: [new-feature]: callerid: Allow timezone to be specified at runtime
  - #100: [bug]: sig_analog: hidecallerid setting is broken
  - #102: [bug]: Strange warning - 'T' option is not compatible with remote console mode and has no effect.
  - #104: [improvement]: Add AMI action to get a list of connected channels
  - #108: [new-feature]: fair handling of calls in multi-queue scenarios
  - #110: [improvement]: utils - add lock timing information with DEBUG_THREADS
  - #116: [bug]: SIP Reason: "Call completed elsewhere" no longer propagating
  - #120: [bug]: chan_dahdi: Fix broken presentation for FXO caller ID
  - #122: [new-feature]: res_musiconhold: Add looplast option
  - #133: [bug]: unlock channel after moh state access
  - #136: [bug]: Makefile downloader does not follow redirects.
  - #145: [bug]: ABI issue with pjproject and pjsip_inv_session
  - #155: [bug]: GCC 13 is catching a few new trivial issues
  - #158: [bug]: test_stasis_endpoints.c: Unit test channel_messages is unstable
  - #174: [bug]: app_voicemail imap compile errors
  - #200: [bug]: Regression: In app.h an enum is used before its declaration.

Commits By Author:
----------------------------------------

- ### Asterisk Development Team (2):
  - Update for 20.4.0-rc1
  - Update for 20.4.0-rc2

- ### Ben Ford (2):
  - AMI: Add CoreShowChannelMap action.
  - res_pjsip_session: Added new function calls to avoid ABI issues.

- ### George Joseph (23):
  - .github: Add cherry-pick reminder to new PRs
  - .github: Fix quoting in PROpenedOrUpdated
  - .github: Don't add cherry-pick reminder if it's already present
  - .github: Change title of AsteriskReleaser job
  - .github: Update field descriptions for AsteriskReleaser
  - .github Ignore error when adding reviewrs to PR
  - .github: Fix issues with cherry-pick-reminder
  - .github: Rework for merge approval
  - build: Fix a few gcc 13 issues
  - test_stasis_endpoints.c: Make channel_messages more stable
  - test_statis_endpoints:  Fix channel_messages test again
  - .github: Allow PR submit checks to be re-run by label
  - .github: Fix recheck label manipulation
  - .github: Fix recheck label typos
  - .github: Add recheck label test to additional jobs
  - .github: Fix use of 'contains'
  - .github: Remove result check from PROpenUpdateGateTests
  - rest-api: Updates for new documentation site
  - .github: Move publish docs to new file CreateDocs.yml
  - .github: Back out triggering PROpenedOrUpdated by label
  - .github: Updates for AsteriskReleaser
  - doc: Remove obsolete CHANGES-staging and UPGRADE-staging
  - app.h: Move declaration of ast_getdata_result before its first use

- ### Gitea (1):
  - .github: Tweak new feature language, and move feature requests elsewhere.

- ### Jaco Kroon (2):
  - configure: fix test code to match gethostbyname_r prototype.
  - tcptls: when disabling a server port, we should set the accept_fd to -1.

- ### Jiajian Zhou (1):
  - AMI: Add parking position parameter to Park action

- ### Joe Searle (1):
  - res_stasis.c: Add new type 'sdp_label' for bridge creation.

- ### Joshua C. Colp (2):
  - .github: Fix staleness check to only run on certain labels.
  - .github: Tweak improvement issue type language.

- ### Maximilian Fridrich (1):
  - chan_pjsip: Allow topology/session refreshes in early media state

- ### Miguel Angel Nubla (1):
  - configure: Makefile downloader enable follow redirects.

- ### Mike Bradeen (4):
  - indications: logging changes
  - utils: add lock timestamps for DEBUG_THREADS
  - res_musiconhold: avoid moh state access on unlocked chan
  - app_voicemail: fix imap compilation errors

- ### Nathan Bruning (1):
  - app_queue: Add force_longest_waiting_caller option.

- ### Naveen Albert (7):
  - asterisk.c: Fix option warning for remote console.
  - chan_dahdi: Fix broken hidecallerid setting.
  - logrotate: Fix duplicate log entries.
  - callerid: Allow specifying timezone for date/time.
  - sig_analog: Add fuller Caller ID support.
  - chan_dahdi: Fix Caller ID presentation for FXO ports.
  - res_musiconhold: Add option to loop last file.

- ### Niklas Larsson (1):
  - app_queue: Preserve reason for realtime queues

- ### Olaf Titz (1):
  - app_voicemail_imap: Fix message count when IMAP server is unavailable

- ### Sean Bright (10):
  - xml.c: Process XML Inclusions recursively.
  - res_pjsip_pubsub.c: Use pjsip version for pending NOTIFY check. (#77)
  - sounds: Update download URL to use HTTPS.
  - ast-db-manage: Fix alembic branching error caused by #122.
  - res_crypto.c: Avoid using the non-portable ALLPERMS macro.
  - ast-db-manage: Synchronize revisions between comments and code.
  - configure: Remove obsolete and deprecated constructs.
  - res_crypto.c: Gracefully handle potential key filename truncation.
  - pjsip_transport_events.c: Use %zu printf specifier for size_t.
  - res_pjsip_rfc3326: Prefer Q.850 cause code over SIP.

- ### alex2grad (1):
  - app_followme: fix issue with enable_callee_prompt=no (#88)

- ### zhengsh (1):
  - res_sorcery_memory_cache.c: Fix memory leak

- ### zhou_jiajian (1):
  - res_fax_spandsp.c: Clean up a spaces/tabs issue


Detail:
----------------------------------------

- ### app.h: Move declaration of ast_getdata_result before its first use
  Author: George Joseph  
  Date:   2023-07-10  

  The ast_app_getdata() and ast_app_getdata_terminator() declarations
  in app.h were changed recently to return enum ast_getdata_result
  (which is how they were defined in app.c).  The existing
  declaration of ast_getdata_result in app.h was about 1000 lines
  after those functions however so under certain circumstances,
  a "use before declaration" error was thrown by the compiler.
  The declaration of the enum was therefore moved to before those
  functions.

  Resolves: #200

- ### doc: Remove obsolete CHANGES-staging and UPGRADE-staging
  Author: George Joseph  
  Date:   2023-07-10  


- ### .github: Updates for AsteriskReleaser
  Author: George Joseph  
  Date:   2023-06-30  


- ### app_voicemail: fix imap compilation errors
  Author: Mike Bradeen  
  Date:   2023-06-26  

  Fixes two compilation errors in app_voicemail_imap, one due to an unsed
  variable and one due to a new variable added in the incorrect location
  in _163.

  Resolves: #174

- ### res_musiconhold: avoid moh state access on unlocked chan
  Author: Mike Bradeen  
  Date:   2023-05-31  

  Move channel unlock to after moh state access to avoid
  potential unlocked access to state.

  Resolves: #133

- ### utils: add lock timestamps for DEBUG_THREADS
  Author: Mike Bradeen  
  Date:   2023-05-23  

  Adds last locked and unlocked timestamps as well as a
  counter for the number of times the lock has been
  attempted (vs locked/unlocked) to debug output printed
  using the DEBUG_THREADS option.

  Resolves: #110

- ### .github: Back out triggering PROpenedOrUpdated by label
  Author: George Joseph  
  Date:   2023-06-29  


- ### .github: Move publish docs to new file CreateDocs.yml
  Author: George Joseph  
  Date:   2023-06-27  


- ### rest-api: Updates for new documentation site
  Author: George Joseph  
  Date:   2023-06-26  

  The new documentation site uses traditional markdown instead
  of the Confluence flavored version.  This required changes in
  the mustache templates and the python that generates the files.


- ### .github: Remove result check from PROpenUpdateGateTests
  Author: George Joseph  
  Date:   2023-06-27  


- ### .github: Fix use of 'contains'
  Author: George Joseph  
  Date:   2023-06-26  


- ### .github: Add recheck label test to additional jobs
  Author: George Joseph  
  Date:   2023-06-26  


- ### .github: Fix recheck label typos
  Author: George Joseph  
  Date:   2023-06-26  


- ### .github: Fix recheck label manipulation
  Author: George Joseph  
  Date:   2023-06-26  


- ### .github: Allow PR submit checks to be re-run by label
  Author: George Joseph  
  Date:   2023-06-26  


- ### app_voicemail_imap: Fix message count when IMAP server is unavailable
  Author: Olaf Titz  
  Date:   2023-06-15  

  Some callers of __messagecount did not correctly handle error return,
  instead returning a -1 message count.
  This caused a notification with "Messages-Waiting: yes" and
  "Voice-Message: -1/0 (0/0)" if the IMAP server was unavailable.

  Fixes: #64

- ### res_pjsip_rfc3326: Prefer Q.850 cause code over SIP.
  Author: Sean Bright  
  Date:   2023-06-12  

  Resolves: #116

- ### res_pjsip_session: Added new function calls to avoid ABI issues.
  Author: Ben Ford  
  Date:   2023-06-05  

  Added two new functions (ast_sip_session_get_dialog and
  ast_sip_session_get_pjsip_inv_state) that retrieve the dialog and the
  pjsip_inv_state respectively from the pjsip_inv_session on the
  ast_sip_session struct. This is due to pjproject adding a new field to
  the pjsip_inv_session struct that caused crashes when trying to access
  fields that were no longer where they were expected to be if a module
  was compiled against a different version of pjproject.

  Resolves: #145

- ### app_queue: Add force_longest_waiting_caller option.
  Author: Nathan Bruning  
  Date:   2023-01-24  

  This adds an option 'force_longest_waiting_caller' which changes the
  global behavior of the queue engine to prevent queue callers from
  'jumping ahead' when an agent is in multiple queues.

  Resolves: #108

  Also closes old asterisk issues:
  - ASTERISK-17732
  - ASTERISK-17570


- ### pjsip_transport_events.c: Use %zu printf specifier for size_t.
  Author: Sean Bright  
  Date:   2023-06-05  

  Partially resolves #143.


- ### res_crypto.c: Gracefully handle potential key filename truncation.
  Author: Sean Bright  
  Date:   2023-06-05  

  Partially resolves #143.


- ### configure: Remove obsolete and deprecated constructs.
  Author: Sean Bright  
  Date:   2023-06-01  

  These were uncovered when trying to run `bootstrap.sh` with Autoconf
  2.71:

  * AC_CONFIG_HEADER() is deprecated in favor of AC_CONFIG_HEADERS().
  * AC_HEADER_TIME is obsolete.
  * $as_echo is deprecated in favor of AS_ECHO() which requires an update
    to ax_pthread.m4.

  Note that the generated artifacts in this commit are from Autoconf 2.69.

  Resolves #139


- ### res_fax_spandsp.c: Clean up a spaces/tabs issue
  Author: zhou_jiajian  
  Date:   2023-05-26  


- ### ast-db-manage: Synchronize revisions between comments and code.
  Author: Sean Bright  
  Date:   2023-06-06  

  In a handful of migrations, the comment header that indicates the
  current and previous revisions has drifted from the identifiers
  revision and down_revision variables. This updates the comment headers
  to match the code.


- ### test_statis_endpoints:  Fix channel_messages test again
  Author: George Joseph  
  Date:   2023-06-12  


- ### res_crypto.c: Avoid using the non-portable ALLPERMS macro.
  Author: Sean Bright  
  Date:   2023-06-05  

  ALLPERMS is not POSIX and it's trivial enough to not jump through
  autoconf hoops to check for it.

  Fixes #149.


- ### tcptls: when disabling a server port, we should set the accept_fd to -1.
  Author: Jaco Kroon  
  Date:   2023-06-02  

  If we don't set this to -1 if the structure can be potentially re-used
  later then it's possible that we'll issue a close() on an unrelated file
  descriptor, breaking asterisk in other interesting ways.

  I believe this to be an unlikely scenario, but it costs nothing to be
  safe.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### AMI: Add parking position parameter to Park action
  Author: Jiajian Zhou  
  Date:   2023-05-19  

  Add a parking space extension parameter (ParkingSpace) to the Park action.
  Park action will attempt to park the call to that extension.
  If the extension is already in use, then execution will continue at the next priority.

  UserNote: New ParkingSpace parameter has been added to AMI action Park.

- ### test_stasis_endpoints.c: Make channel_messages more stable
  Author: George Joseph  
  Date:   2023-06-09  

  The channel_messages test was assuming that stasis would return
  messages in a specific order.  This is an incorrect assumption as
  message ordering was never guaranteed.  This was causing the test
  to fail occasionally.  We now test all the messages for the
  required message types instead of testing one by one.

  Resolves: #158

- ### build: Fix a few gcc 13 issues
  Author: George Joseph  
  Date:   2023-06-09  

  * gcc 13 is now catching when a function is declared as returning
    an enum but defined as returning an int or vice versa.  Fixed
    a few in app.h, loader.c, stasis_message.c.

  * gcc 13 is also now (incorrectly) complaining of dangling pointers
    when assigning a pointer to a local char array to a char *. Had
    to change that to an ast_alloca.

  Resolves: #155

- ### .github: Rework for merge approval
  Author: George Joseph  
  Date:   2023-06-06  


- ### ast-db-manage: Fix alembic branching error caused by #122.
  Author: Sean Bright  
  Date:   2023-06-05  

  Fixes #147.


- ### app_followme: fix issue with enable_callee_prompt=no (#88)
  Author: alex2grad  
  Date:   2023-06-05  

  * app_followme: fix issue with enable_callee_prompt=no

  If the FollowMe option 'enable_callee_prompt' is set to 'no' then Asterisk
  incorrectly sets a winner channel to the channel from which any control frame was read.

  This fix sets the winner channel only to the answered channel.

  Resolves: #87

  ASTERISK-30326


- ### sounds: Update download URL to use HTTPS.
  Author: Sean Bright  
  Date:   2023-06-01  

  Related to #136


- ### configure: Makefile downloader enable follow redirects.
  Author: Miguel Angel Nubla  
  Date:   2023-06-01  

  If curl is used for building, any download such as a sounds package
  will fail to follow HTTP redirects and will download wrong data.

  Resolves: #136

- ### res_musiconhold: Add option to loop last file.
  Author: Naveen Albert  
  Date:   2023-05-25  

  Adds the loop_last option to res_musiconhold,
  which allows the last audio file in the directory
  to be looped perpetually once reached, rather than
  circling back to the beginning again.

  Resolves: #122
  ASTERISK-30462

  UserNote: The loop_last option in musiconhold.conf now
  allows the last file in the directory to be looped once reached.


- ### chan_dahdi: Fix Caller ID presentation for FXO ports.
  Author: Naveen Albert  
  Date:   2023-05-25  

  Currently, the presentation for incoming channels is
  always available, because it is never actually set,
  meaning the channel presentation can be nonsensical.
  If the presentation from the incoming Caller ID spill
  is private or unavailable, we now update the channel
  presentation to reflect this.

  Resolves: #120
  ASTERISK-30333
  ASTERISK-21741


- ### AMI: Add CoreShowChannelMap action.
  Author: Ben Ford  
  Date:   2023-05-18  

  Adds a new AMI action (CoreShowChannelMap) that takes in a channel name
  and provides a list of all channels that are connected to that channel,
  following local channel connections as well.

  Resolves: #104

  UserNote: New AMI action CoreShowChannelMap has been added.

- ### sig_analog: Add fuller Caller ID support.
  Author: Naveen Albert  
  Date:   2023-05-18  

  A previous change, ASTERISK_29991, made it possible
  to send additional Caller ID parameters that were
  not previously supported.

  This change adds support for analog DAHDI channels
  to now be able to receive these parameters for
  on-hook Caller ID, in order to enhance the usability
  of CPE that support these parameters.

  Resolves: #94
  ASTERISK-30331

  UserNote: Additional Caller ID properties are now supported on
  incoming calls to FXS stations, namely the
  redirecting reason and call qualifier.


- ### res_stasis.c: Add new type 'sdp_label' for bridge creation.
  Author: Joe Searle  
  Date:   2023-05-25  

  Add new type 'sdp_label' when creating a bridge using the ARI. This will
  add labels to the SDP for each stream, the label is set to the
  corresponding channel id.

  Resolves: #91

  UserNote: When creating a bridge using the ARI the 'type' argument now
  accepts a new value 'sdp_label' which will configure the bridge to add
  labels for each stream in the SDP with the corresponding channel id.


- ### app_queue: Preserve reason for realtime queues
  Author: Niklas Larsson  
  Date:   2023-05-05  

  When Asterisk is restarted it does not preserve paused reason for
  members of realtime queues. This was fixed for non-realtime queues in
  ASTERISK_25732

  Resolves: #66

  UpgradeNote: Add a new column to the queue_member table:
  reason_paused VARCHAR(80) so the reason can be preserved.

  UserNote: Make paused reason in realtime queues persist an
  Asterisk restart. This was fixed for non-realtime
  queues in ASTERISK_25732.


- ### .github: Fix issues with cherry-pick-reminder
  Author: George Joseph  
  Date:   2023-06-05  


- ### indications: logging changes
  Author: Mike Bradeen  
  Date:   2023-05-16  

  Increase verbosity to indicate failure due to missing country
  and to specify default on CLI dump

  Resolves: #89

- ### .github Ignore error when adding reviewrs to PR
  Author: George Joseph  
  Date:   2023-06-05  


- ### .github: Update field descriptions for AsteriskReleaser
  Author: George Joseph  
  Date:   2023-05-26  


- ### callerid: Allow specifying timezone for date/time.
  Author: Naveen Albert  
  Date:   2023-05-18  

  The Caller ID generation routine currently is hardcoded
  to always use the system time zone. This makes it possible
  to optionally specify any TZ-format time zone.

  Resolves: #98
  ASTERISK-30330


- ### logrotate: Fix duplicate log entries.
  Author: Naveen Albert  
  Date:   2023-05-18  

  The Asterisk logrotate script contains explicit
  references to files with the .log extension,
  which are also included when *log is expanded.
  This causes issues with newer versions of logrotate.
  This fixes this by ensuring that a log file cannot
  be referenced multiple times after expansion occurs.

  Resolves: #96
  ASTERISK-30442
  Reported by: EN Barnett
  Tested by: EN Barnett


- ### chan_pjsip: Allow topology/session refreshes in early media state
  Author: Maximilian Fridrich  
  Date:   2023-05-10  

  With this change, session modifications in the early media state are
  possible if the SDP was sent reliably and confirmed by a PRACK. For
  details, see RFC 6337, escpecially section 3.2.

  Resolves: #73

- ### chan_dahdi: Fix broken hidecallerid setting.
  Author: Naveen Albert  
  Date:   2023-05-18  

  The hidecallerid setting in chan_dahdi.conf currently
  is broken for a couple reasons.

  First, the actual code in sig_analog to "allow" or "block"
  Caller ID depending on this setting improperly used
  ast_set_callerid instead of updating the presentation.
  This issue was mostly fixed in ASTERISK_29991, and that
  fix is carried forward to this code as well.

  Secondly, the hidecallerid setting is set on the DAHDI
  pvt but not carried forward to the analog pvt properly.
  This is because the chan_dahdi config loading code improperly
  set permhidecallerid to permhidecallerid from the config file,
  even though hidecallerid is what is actually set from the config
  file. (This is done correctly for call waiting, a few lines above.)
  This is fixed to read the proper value.

  Thirdly, in sig_analog, hidecallerid is set to permhidecallerid
  only on hangup. This can lead to potential security vulnerabilities
  as an allowed Caller ID from an initial call can "leak" into subsequent
  calls if no hangup occurs between them. This is fixed by setting
  hidecallerid to permcallerid when calls begin, rather than when they end.
  This also means we don't need to also set hidecallerid in chan_dahdi.c
  when copying from the config, as we would have to otherwise.

  Fourthly, sig_analog currently only allows dialing *67 or *82 if
  that would actually toggle the presentation. A comment is added
  clarifying that this behavior is okay.

  Finally, a couple log messages are updated to be more accurate.

  Resolves: #100
  ASTERISK-30349 #close


- ### .github: Change title of AsteriskReleaser job
  Author: George Joseph  
  Date:   2023-05-23  


- ### asterisk.c: Fix option warning for remote console.
  Author: Naveen Albert  
  Date:   2023-05-18  

  Commit 09e989f972e2583df4e9bf585c246c37322d8d2f
  categorized the T option as not being compatible
  with remote consoles, but they do affect verbose
  messages with remote console. This fixes this.

  Resolves: #102

- ### .github: Don't add cherry-pick reminder if it's already present
  Author: George Joseph  
  Date:   2023-05-22  


- ### .github: Fix quoting in PROpenedOrUpdated
  Author: George Joseph  
  Date:   2023-05-16  


- ### .github: Add cherry-pick reminder to new PRs
  Author: George Joseph  
  Date:   2023-05-15  


- ### configure: fix test code to match gethostbyname_r prototype.
  Author: Jaco Kroon  
  Date:   2023-05-10  

  This enables the test to work with CC=clang.

  Without this the test for 6 args would fail with:

  utils.c:99:12: error: static declaration of 'gethostbyname_r' follows non-static declaration
  static int gethostbyname_r (const char *name, struct hostent *ret, char *buf,
             ^
  /usr/include/netdb.h:177:12: note: previous declaration is here
  extern int gethostbyname_r (const char *__restrict __name,
             ^

  Fixing the expected return type to int sorts this out.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### res_pjsip_pubsub.c: Use pjsip version for pending NOTIFY check. (#77)
  Author: Sean Bright  
  Date:   2023-05-11  

  The functionality we are interested in is present only in pjsip 2.13
  and newer.

  Resolves: #45

- ### res_sorcery_memory_cache.c: Fix memory leak
  Author: zhengsh  
  Date:   2023-05-03  

  Replace the original call to ast_strdup with a call to ast_strdupa to fix the leak issue.

  Resolves: #55
  ASTERISK-30429


- ### xml.c: Process XML Inclusions recursively.
  Author: Sean Bright  
  Date:   2023-05-09  

  If processing an XInclude results in new <xi:include> elements, we
  need to run XInclude processing again. This continues until no
  replacement occurs or an error is encountered.

  There is a separate issue with dynamic strings (ast_str) that will be
  addressed separately.

  Resolves: #65

- ### .github: Tweak improvement issue type language.
  Author: Joshua C. Colp  
  Date:   2023-05-09  


- ### .github: Tweak new feature language, and move feature requests elsewhere.
  Author: Gitea  
  Date:   2023-05-09  


- ### .github: Fix staleness check to only run on certain labels.
  Author: Joshua C. Colp  
  Date:   2023-05-09  


