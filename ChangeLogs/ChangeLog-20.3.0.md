
Change Log for Release 20.3.0
========================================

Summary:
----------------------------------------

- Set up new ChangeLogs directory
- .github: Add AsteriskReleaser
- chan_pjsip: also return all codecs on empty re-INVITE for late offers
- cel: add local optimization begin event
- core: Cleanup gerrit and JIRA references. (#57)
- .github: Fix CherryPickTest to only run when it should
- .github: Fix reference to CHERRY_PICK_TESTING_IN_PROGRESS
- .github: Remove separate set labels step from new PR
- .github: Refactor CP progress and add new PR test progress
- res_pjsip: mediasec: Add Security-Client headers after 401
- .github: Add cherry-pick test progress labels
- LICENSE: Update link to trademark policy.
- chan_dahdi: Add dialmode option for FXS lines.
- .github: Update issue templates
- .github: Remove unnecessary parameter in CherryPickTest
- Initial GitHub PRs
- Initial GitHub Issue Templates
- pbx_dundi: Fix PJSIP endpoint configuration check.
- Revert "app_queue: periodic announcement configurable start time."
- res_pjsip_stir_shaken: Fix JSON field ordering and disallowed TN characters.
- pbx_dundi: Add PJSIP support.
- install_prereq: Add Linux Mint support.
- chan_pjsip: fix music on hold continues after INVITE with replaces
- voicemail.conf: Fix incorrect comment about #include.
- app_queue: Fix minor xmldoc duplication and vagueness.
- test.c: Fix counting of tests and add 2 new tests
- res_calendar: output busy state as part of show calendar.
- loader.c: Minor module key check simplification.
- ael: Regenerate lexers and parsers.
- bridge_builtin_features: add beep via touch variable
- res_mixmonitor: MixMonitorMute by MixMonitor ID
- format_sln: add .slin as supported file extension
- res_agi: RECORD FILE plays 2 beeps.
- func_json: Fix JSON parsing issues.
- app_senddtmf: Add SendFlash AMI action.
- app_dial: Fix DTMF not relayed to caller on unanswered calls.
- configure: fix detection of re-entrant resolver functions
- cli: increase channel column width
- app_queue: periodic announcement configurable start time.
- make_version: Strip svn stuff and suppress ref HEAD errors
- res_http_media_cache: Introduce options and customize
- main/iostream.c: fix build with libressl
- contrib: rc.archlinux.asterisk uses invalid redirect.

User Notes:
----------------------------------------

- ### cel: add local optimization begin event
  The new AST_CEL_LOCAL_OPTIMIZE_BEGIN can be used
  by itself or in conert with the existing
  AST_CEL_LOCAL_OPTIMIZE to book-end local channel optimizaion.

- ### chan_dahdi: Add dialmode option for FXS lines.
  A "dialmode" option has been added which allows
  specifying, on a per-channel basis, what methods of
  subscriber dialing (pulse and/or tone) are permitted.
  Additionally, this can be changed on a channel
  at any point during a call using the CHANNEL
  function.

- ### pbx_dundi: Add PJSIP support.
  DUNDi now supports chan_pjsip. Outgoing calls using
  PJSIP require the pjsip_outgoing_endpoint option
  to be set in dundi.conf.

- ### cli: increase channel column width
  This change increases the display width on 'core show channels'
  amd 'core show channels verbose'
  For 'core show channels', the Channel name field is increased to
  64 characters and the Location name field is increased to 32
  characters.
  For 'core show channels verbose', the Channel name field is
  increased to 80 characters, the Context is increased to 24
  characters and the Extension is increased to 24 characters.

- ### app_senddtmf: Add SendFlash AMI action.
  The SendFlash AMI action now allows sending
  a hook flash event on a channel.

- ### res_http_media_cache: Introduce options and customize
  The res_http_media_cache module now attempts to load
  configuration from the res_http_media_cache.conf file.
  The following options were added:
    * timeout_secs
    * user_agent
    * follow_location
    * max_redirects
    * protocols
    * redirect_protocols
    * dns_cache_timeout_secs

- ### test.c: Fix counting of tests and add 2 new tests
  The "tests" attribute of the "testsuite" element in the
  output XML now reflects only the tests actually requested
  to be executed instead of all the tests registered.
  The "failures" attribute was added to the "testsuite"
  element.
  Also added two new unit tests that just pass and fail
  to be used for testing CI itself.

- ### res_mixmonitor: MixMonitorMute by MixMonitor ID
  It is now possible to specify the MixMonitorID when calling
  the manager action: MixMonitorMute.  This will allow an
  individual MixMonitor instance to be muted via ID.
  The MixMonitorID can be stored as a channel variable using
  the 'i' MixMonitor option and is returned upon creation if
  this option is used.
  As part of this change, if no MixMonitorID is specified in
  the manager action MixMonitorMute, Asterisk will set the mute
  flag on all MixMonitor audiohooks on the channel.  Previous
  behavior would set the flag on the first MixMonitor audiohook
  found.

- ### bridge_builtin_features: add beep via touch variable
  Add optional touch variable : TOUCH_MIXMONITOR_BEEP(interval)
  Setting TOUCH_MIXMONITOR_BEEP/TOUCH_MONITOR_BEEP to a valid
  interval in seconds will result in a periodic beep being
  played to the monitored channel upon MixMontior/Monitor
  feature start.
  If an interval less than 5 seconds is specified, the interval
  will default to 5 seconds.  If the value is set to an invalid
  interval, the default of 15 seconds will be used.

- ### format_sln: add .slin as supported file extension
  format_sln now recognizes '.slin' as a valid
  file extension in addition to the existing
  '.sln' and '.raw'.


Upgrade Notes:
----------------------------------------

- ### cel: add local optimization begin event
  The existing AST_CEL_LOCAL_OPTIMIZE can continue
  to be used as-is and the AST_CEL_LOCAL_OPTIMIZE_BEGIN event
  can be ignored if desired.


Closed Issues:
----------------------------------------

  - #35: [New Feature]: chan_dahdi: Allow disabling pulse or tone dialing
  - #39: [Bug]: Remove .gitreview from repository.
  - #43: [Bug]: Link to trademark policy is no longer correct
  - #48: [bug]: res_pjsip: Mediasec requires different headers on 401 response
  - #52: [improvement]: Add local optimization begin cel event

Commits By Author:
----------------------------------------

- ### Asterisk Development Team (1):
  - Update for 20.3.0-rc1

- ### Fabrice Fontaine (2):
  - main/iostream.c: fix build with libressl
  - configure: fix detection of re-entrant resolver functions

- ### George Joseph (13):
  - make_version: Strip svn stuff and suppress ref HEAD errors
  - test.c: Fix counting of tests and add 2 new tests
  - Initial GitHub Issue Templates
  - Initial GitHub PRs
  - .github: Remove unnecessary parameter in CherryPickTest
  - .github: Update issue templates
  - .github: Add cherry-pick test progress labels
  - .github: Refactor CP progress and add new PR test progress
  - .github: Remove separate set labels step from new PR
  - .github: Fix reference to CHERRY_PICK_TESTING_IN_PROGRESS
  - .github: Fix CherryPickTest to only run when it should
  - .github: Add AsteriskReleaser
  - Set up new ChangeLogs directory

- ### Henning Westerholt (2):
  - chan_pjsip: fix music on hold continues after INVITE with replaces
  - chan_pjsip: also return all codecs on empty re-INVITE for late offers

- ### Holger Hans Peter Freyther (1):
  - res_http_media_cache: Introduce options and customize

- ### Jaco Kroon (2):
  - app_queue: periodic announcement configurable start time.
  - res_calendar: output busy state as part of show calendar.

- ### Joshua C. Colp (2):
  - pbx_dundi: Fix PJSIP endpoint configuration check.
  - LICENSE: Update link to trademark policy.

- ### Joshua Colp (1):
  - Revert "app_queue: periodic announcement configurable start time."

- ### Maximilian Fridrich (1):
  - res_pjsip: mediasec: Add Security-Client headers after 401

- ### Mike Bradeen (5):
  - cli: increase channel column width
  - format_sln: add .slin as supported file extension
  - res_mixmonitor: MixMonitorMute by MixMonitor ID
  - bridge_builtin_features: add beep via touch variable
  - cel: add local optimization begin event

- ### Naveen Albert (8):
  - app_dial: Fix DTMF not relayed to caller on unanswered calls.
  - app_senddtmf: Add SendFlash AMI action.
  - func_json: Fix JSON parsing issues.
  - app_queue: Fix minor xmldoc duplication and vagueness.
  - voicemail.conf: Fix incorrect comment about #include.
  - pbx_dundi: Add PJSIP support.
  - res_pjsip_stir_shaken: Fix JSON field ordering and disallowed TN characters.
  - chan_dahdi: Add dialmode option for FXS lines.

- ### Sean Bright (5):
  - contrib: rc.archlinux.asterisk uses invalid redirect.
  - res_agi: RECORD FILE plays 2 beeps.
  - ael: Regenerate lexers and parsers.
  - loader.c: Minor module key check simplification.
  - core: Cleanup gerrit and JIRA references. (#57)

- ### The_Blode (1):
  - install_prereq: Add Linux Mint support.


Detail:
----------------------------------------

- ### Set up new ChangeLogs directory
  Author: George Joseph  
  Date:   2023-05-09  


- ### .github: Add AsteriskReleaser
  Author: George Joseph  
  Date:   2023-05-05  


- ### chan_pjsip: also return all codecs on empty re-INVITE for late offers
  Author: Henning Westerholt  
  Date:   2023-05-03  

  We should also return all codecs on an re-INVITE without SDP for a
  call that used late offer (e.g. no SDP in the initial INVITE, SDP
  in the ACK). Bugfix for feature introduced in ASTERISK-30193
  (https://issues.asterisk.org/jira/browse/ASTERISK-30193)

  Migration from previous gerrit change that was not merged.


- ### cel: add local optimization begin event
  Author: Mike Bradeen  
  Date:   2023-05-02  

  The current AST_CEL_LOCAL_OPTIMIZE event is and has been
  triggered on a local optimization end to serve as a flag
  indicating the event occurred.  This change adds a second
  AST_CEL_LOCAL_OPTIMIZE_BEGIN event for further detail.

  Resolves: #52

  UpgradeNote: The existing AST_CEL_LOCAL_OPTIMIZE can continue
  to be used as-is and the AST_CEL_LOCAL_OPTIMIZE_BEGIN event
  can be ignored if desired.

  UserNote: The new AST_CEL_LOCAL_OPTIMIZE_BEGIN can be used
  by itself or in conert with the existing
  AST_CEL_LOCAL_OPTIMIZE to book-end local channel optimizaion.


- ### core: Cleanup gerrit and JIRA references. (#57)
  Author: Sean Bright  
  Date:   2023-05-03  

  * Remove .gitreview and switch to pulling the main asterisk branch
    version from configure.ac instead.

  * Replace references to JIRA with GitHub.

  * Other minor cleanup found along the way.

  Resolves: #39

- ### .github: Fix CherryPickTest to only run when it should
  Author: George Joseph  
  Date:   2023-05-03  

  Fixed CherryPickTest so it triggers only on the
  "cherry-pick-test" label instead of all labels.


- ### .github: Fix reference to CHERRY_PICK_TESTING_IN_PROGRESS
  Author: George Joseph  
  Date:   2023-05-02  


- ### .github: Remove separate set labels step from new PR
  Author: George Joseph  
  Date:   2023-05-02  


- ### .github: Refactor CP progress and add new PR test progress
  Author: George Joseph  
  Date:   2023-05-02  


- ### res_pjsip: mediasec: Add Security-Client headers after 401
  Author: Maximilian Fridrich  
  Date:   2023-05-02  

  When using mediasec, requests sent after a 401 must still contain the
  Security-Client header according to
  draft-dawes-sipcore-mediasec-parameter.

  Resolves: #48

- ### .github: Add cherry-pick test progress labels
  Author: George Joseph  
  Date:   2023-05-02  


- ### LICENSE: Update link to trademark policy.
  Author: Joshua C. Colp  
  Date:   2023-05-01  

  Resolves: #43

- ### chan_dahdi: Add dialmode option for FXS lines.
  Author: Naveen Albert  
  Date:   2023-04-28  

  Currently, both pulse and tone dialing are always enabled
  on all FXS lines, with no way of disabling one or the other.

  In some circumstances, it is desirable or necessary to
  disable one of these, and this behavior can be problematic.

  A new "dialmode" option is added which allows setting the
  methods to support on a per channel basis for FXS (FXO
  signalled lines). The four options are "both", "pulse",
  "dtmf"/"tone", and "none".

  Additionally, integration with the CHANNEL function is
  added so that this setting can be updated for a channel
  during a call.

  Resolves: #35
  ASTERISK-29992

  UserNote: A "dialmode" option has been added which allows
  specifying, on a per-channel basis, what methods of
  subscriber dialing (pulse and/or tone) are permitted.

  Additionally, this can be changed on a channel
  at any point during a call using the CHANNEL
  function.


- ### .github: Update issue templates
  Author: George Joseph  
  Date:   2023-05-01  


- ### .github: Remove unnecessary parameter in CherryPickTest
  Author: George Joseph  
  Date:   2023-05-01  


- ### Initial GitHub PRs
  Author: George Joseph  
  Date:   2023-04-28  


- ### Initial GitHub Issue Templates
  Author: George Joseph  
  Date:   2023-04-28  


- ### pbx_dundi: Fix PJSIP endpoint configuration check.
  Author: Joshua C. Colp  
  Date:   2023-04-13  

  ASTERISK-28233


- ### Revert "app_queue: periodic announcement configurable start time."
  Author: Joshua Colp  
  Date:   2023-04-11  

  This reverts commit 3fd0b65bae4b1b14434737ffcf0da4aa9ff717f6.

  Reason for revert: Causes segmentation fault.


- ### res_pjsip_stir_shaken: Fix JSON field ordering and disallowed TN characters.
  Author: Naveen Albert  
  Date:   2023-02-17  

  The current STIR/SHAKEN signing process is inconsistent with the
  RFCs in a couple ways that can cause interoperability issues.

  RFC8225 specifies that the keys must be ordered lexicographically, but
  currently the fields are simply ordered according to the order
  in which they were added to the JSON object, which is not
  compliant with the RFC and can cause issues with some carriers.

  To fix this, we now leverage libjansson's ability to dump a JSON
  object sorted by key value, yielding the correct field ordering.

  Additionally, telephone numbers must have any leading + prefix removed
  and must not contain characters outside of 0-9, *, and # in order
  to comply with the RFCs. Numbers are now properly formatted as such.

  ASTERISK-30407 #close


- ### pbx_dundi: Add PJSIP support.
  Author: Naveen Albert  
  Date:   2022-12-09  

  Adds PJSIP as a supported technology to DUNDi.

  To facilitate this, we now allow an endpoint to be specified
  for outgoing PJSIP calls. We also allow users to force a specific
  channel technology for outgoing SIP-protocol calls.

  ASTERISK-28109 #close
  ASTERISK-28233 #close


- ### install_prereq: Add Linux Mint support.
  Author: The_Blode  
  Date:   2023-03-17  

  ASTERISK-30359 #close


- ### chan_pjsip: fix music on hold continues after INVITE with replaces
  Author: Henning Westerholt  
  Date:   2023-03-21  

  In a three party scenario with INVITE with replaces, we need to
  unhold the call, otherwise one party continues to get music on
  hold, and the call is not properly bridged between them.

  ASTERISK-30428


- ### voicemail.conf: Fix incorrect comment about #include.
  Author: Naveen Albert  
  Date:   2023-03-28  

  A comment at the top of voicemail.conf says that #include
  cannot be used in voicemail.conf because this breaks
  the ability for app_voicemail to auto-update passwords.
  This is factually incorrect, since Asterisk has no problem
  updating files that are #include'd in the main configuration
  file, and this does work in voicemail.conf as well.

  ASTERISK-30479 #close


- ### app_queue: Fix minor xmldoc duplication and vagueness.
  Author: Naveen Albert  
  Date:   2023-04-03  

  The F option in the xmldocs for the Queue application
  was erroneously duplicated, causing it to display
  twice on the wiki. The two sections are now merged into one.

  Additionally, the description for the d option was quite
  vague. Some more details are added to provide context
  as to what this actually does.

  ASTERISK-30486 #close


- ### test.c: Fix counting of tests and add 2 new tests
  Author: George Joseph  
  Date:   2023-03-28  

  The unit test XML output was counting all registered tests as "run"
  even when only a subset were actually requested to be run and
  the "failures" attribute was missing.

  * The "tests" attribute of the "testsuite" element in the
    output XML now reflects only the tests actually requested
    to be executed instead of all the tests registered.

  * The "failures" attribute was added to the "testsuite"
    element.

  Also added 2 new unit tests that just pass and fail to be
  used for CI testing.


- ### res_calendar: output busy state as part of show calendar.
  Author: Jaco Kroon  
  Date:   2023-03-23  

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### loader.c: Minor module key check simplification.
  Author: Sean Bright  
  Date:   2023-03-23  


- ### ael: Regenerate lexers and parsers.
  Author: Sean Bright  
  Date:   2023-03-21  

  Various changes to ensure that the lexers and parsers can be correctly
  generated when REBUILD_PARSERS is enabled.

  Some notes:

  * Because of the version of flex we are using to generate the lexers
    (2.5.35) some post-processing in the Makefile is still required.

  * The generated lexers do not contain the problematic C99 check that
    was being replaced by the call to sed in the respective Makefiles so
    it was removed.

  * Since these files are generated, they will include trailing
    whitespace in some places. This does not need to be corrected.


- ### bridge_builtin_features: add beep via touch variable
  Author: Mike Bradeen  
  Date:   2023-03-01  

  Add periodic beep option to one-touch recording by setting
  the touch variable TOUCH_MONITOR_BEEP or
  TOUCH_MIXMONITOR_BEEP to the desired interval in seconds.

  If the interval is less than 5 seconds, a minimum of 5
  seconds will be imposed.  If the interval is set to an
  invalid value, it will default to 15 seconds.

  A new test event PERIODIC_HOOK_ENABLED was added to the
  func_periodic_hook hook_on function to indicate when
  a hook is started.  This is so we can test that the touch
  variable starts the hook as expected.

  ASTERISK-30446


- ### res_mixmonitor: MixMonitorMute by MixMonitor ID
  Author: Mike Bradeen  
  Date:   2023-03-13  

  While it is possible to create multiple mixmonitor instances
  on a channel, it was not previously possible to mute individual
  instances.

  This change includes the ability to specify the MixMonitorID
  when calling the manager action: MixMonitorMute.  This will
  allow an individual MixMonitor instance to be muted via id.
  This id can be stored as a channel variable using the 'i'
  MixMonitor option.

  As part of this change, if no MixMonitorID is specified in
  the manager action MixMonitorMute, Asterisk will set the mute
  flag on all MixMonitor spy-type audiohooks on the channel.
  This is done via the new audiohook function:
  ast_audiohook_set_mute_all.

  ASTERISK-30464


- ### format_sln: add .slin as supported file extension
  Author: Mike Bradeen  
  Date:   2023-03-14  

  Adds '.slin' to existing supported file extensions:
  .sln and .raw

  ASTERISK-30465


- ### res_agi: RECORD FILE plays 2 beeps.
  Author: Sean Bright  
  Date:   2023-03-08  

  Sending the "RECORD FILE" command without the optional
  `offset_samples` argument can result in two beeps playing on the
  channel.

  This bug has been present since Asterisk 0.3.0 (2003-02-06).

  ASTERISK-30457 #close


- ### func_json: Fix JSON parsing issues.
  Author: Naveen Albert  
  Date:   2023-02-26  

  Fix issue with returning empty instead of dumping
  the JSON string when recursing.

  Also adds a unit test to capture this fix.

  ASTERISK-30441 #close


- ### app_senddtmf: Add SendFlash AMI action.
  Author: Naveen Albert  
  Date:   2023-02-26  

  Adds an AMI action to send a flash event
  on a channel.

  ASTERISK-30440 #close


- ### app_dial: Fix DTMF not relayed to caller on unanswered calls.
  Author: Naveen Albert  
  Date:   2023-03-04  

  DTMF frames are not handled in app_dial when sent towards the
  caller. This means that if DTMF is sent to the calling party
  and the call has not yet been answered, the DTMF is not audible.
  This is now fixed by relaying DTMF frames if only a single
  destination is being dialed.

  ASTERISK-29516 #close


- ### configure: fix detection of re-entrant resolver functions
  Author: Fabrice Fontaine  
  Date:   2023-03-08  

  uClibc does not provide res_nsearch:
  asterisk-16.0.0/main/dns.c:506: undefined reference to `res_nsearch'

  Patch coded by Yann E. MORIN:
  http://lists.busybox.net/pipermail/buildroot/2018-October/232630.html

  ASTERISK-21795 #close

  Signed-off-by: Bernd Kuhls <bernd.kuhls@t-online.de>
  [Retrieved from:
  https: //git.buildroot.net/buildroot/tree/package/asterisk/0005-configure-fix-detection-of-re-entrant-resolver-funct.patch]
  Signed-off-by: Fabrice Fontaine <fontaine.fabrice@gmail.com>

- ### cli: increase channel column width
  Author: Mike Bradeen  
  Date:   2023-03-06  

  For 'core show channels', the Channel name field is increased
  to 64 characters and the Location name field is increased to
  32 characters.

  For 'core show channels verbose', the Channel name field is
  increased to 80 characters, the Context is increased to 24
  characters and the Extension is increased to 24 characters.

  ASTERISK-30455


- ### app_queue: periodic announcement configurable start time.
  Author: Jaco Kroon  
  Date:   2023-02-21  

  This newly introduced periodic-announce-startdelay makes it possible to
  configure the initial start delay of the first periodic announcement
  after which periodic-announce-frequency takes over.

  ASTERISK-30437 #close
  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### make_version: Strip svn stuff and suppress ref HEAD errors
  Author: George Joseph  
  Date:   2023-03-13  

  * All of the code that used subversion has been removed.

  * When Asterisk is checked out from a tag or commit instead
    of one of the regular branches, git would emit messages like
    "fatal: ref HEAD is not a symbolic ref" which weren't fatal
    at all.  Those are now suppressed.


- ### res_http_media_cache: Introduce options and customize
  Author: Holger Hans Peter Freyther  
  Date:   2022-10-16  

  Make the existing CURL parameters configurable and allow
  to specify the usable protocols, proxy and DNS timeout.

  ASTERISK-30340


- ### main/iostream.c: fix build with libressl
  Author: Fabrice Fontaine  
  Date:   2023-02-25  

  Fix the following build failure with libressl by using SSL_is_server
  which is available since version 2.7.0 and
  https://github.com/libressl-portable/openbsd/commit/d7ec516916c5eaac29b02d7a8ac6570f63b458f7:

  iostream.c: In function 'ast_iostream_close':
  iostream.c:559:41: error: invalid use of incomplete typedef 'SSL' {aka 'struct ssl_st'}
    559 |                         if (!stream->ssl->server) {
        |                                         ^~

  ASTERISK-30107 #close

  Fixes: - http://autobuild.buildroot.org/results/ce4d62d00bb77ba5b303cacf6be7e350581a62f9

- ### contrib: rc.archlinux.asterisk uses invalid redirect.
  Author: Sean Bright  
  Date:   2023-03-02  

  `rc.archlinux.asterisk`, which explicitly requests bash in its
  shebang, uses the following command syntax:

    ${DAEMON} -rx "core stop now" > /dev/null 2&>1

  The intent of which is to execute:

    ${DAEMON} -rx "core stop now"

  While sending both stdout and stderr to `/dev/null`. Unfortunately,
  because the `&` is in the wrong place, bash is interpreting the `2` as
  just an additional argument to the `$DAEMON` command and not as a file
  descriptor and proceeds to use the bashism `&>` to send stderr and
  stdout to a file named `1`.

  So we clean it up and just use bash's shortcut syntax.

  Issue raised and a fix suggested (but not used) by peutch on GitHub¹.

  ASTERISK-30449 #close

  1. https://github.com/asterisk/asterisk/pull/31


