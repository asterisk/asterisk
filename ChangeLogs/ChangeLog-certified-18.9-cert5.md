
Change Log for Release certified-18.9-cert5
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-certified-18.9-cert5.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert4...certified-18.9-cert5)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-certified-18.9-cert5.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- apply_patches: Use globbing instead of file/sort.
- apply_patches: Sort patch list before applying
- bundled_pjproject: Backport security fixes from pjproject 2.13.1
- .github: Updates for AsteriskReleaser
- res_musiconhold: avoid moh state access on unlocked chan
- utils: add lock timestamps for DEBUG_THREADS
- .github: Back out triggering PROpenedOrUpdated by label
- .github: Move publish docs to new file CreateDocs.yml
- .github: Remove result check from PROpenUpdateGateTests
- .github: Fix use of 'contains'
- .github: Add recheck label test to additional jobs
- .github: Fix recheck label typos
- .github: Fix recheck label manipulation
- .github: Allow PR submit checks to be re-run by label
- res_pjsip_session: Added new function calls to avoid ABI issues.
- test_statis_endpoints:  Fix channel_messages test again
- test_stasis_endpoints.c: Make channel_messages more stable
- build: Fix a few gcc 13 issues
- .github: Rework for merge approval
- AMI: Add CoreShowChannelMap action.
- .github: Fix issues with cherry-pick-reminder
- indications: logging changes
- .github Ignore error when adding reviewrs to PR
- .github: Update field descriptions for AsteriskReleaser
- .github: Change title of AsteriskReleaser job
- .github: Don't add cherry-pick reminder if it's already present
- .github: Fix quoting in PROpenedOrUpdated
- .github: Add cherry-pick reminder to new PRs
- core: Cleanup gerrit and JIRA references. (#40) (#61)
- .github: Tweak improvement issue type language.
- .github: Tweak new feature language, and move feature requests elsewhere.
- .github: Fix staleness check to only run on certain labels.
- .github: Add AsteriskReleaser
- cel: add local optimization begin event
- .github: Fix CherryPickTest to only run when it should
- .github: Fix reference to CHERRY_PICK_TESTING_IN_PROGRESS
- .github: Remove separate set labels step from new PR
- .github: Refactor CP progress and add new PR test progress
- .github: Add cherry-pick test progress labels
- .github: Update issue templates
- .github: Remove unnecessary parameter in CherryPickTest
- Initial GitHub PRs
- Initial GitHub Issue Templates
- test.c: Fix counting of tests and add 2 new tests
- res_mixmonitor: MixMonitorMute by MixMonitor ID
- format_sln: add .slin as supported file extension
- bridge_builtin_features: add beep via touch variable
- cli: increase channel column width
- app_senddtmf: Add option to answer target channel.
- app_directory: Add a 'skip call' option.
- app_read: Add an option to return terminator on empty digits.
- app_directory: add ability to specify configuration file

User Notes:
----------------------------------------

- ### AMI: Add CoreShowChannelMap action.
  New AMI action CoreShowChannelMap has been added.

- ### cel: add local optimization begin event
  The new AST_CEL_LOCAL_OPTIMIZE_BEGIN can be used
  by itself or in conert with the existing
  AST_CEL_LOCAL_OPTIMIZE to book-end local channel optimizaion.

- ### app_read: Add an option to return terminator on empty digits.
  A new option 'e' has been added to allow Read() to return the
  terminator as the dialed digits in the case where only the terminator
  is entered.

- ### format_sln: add .slin as supported file extension
  format_sln now recognizes '.slin' as a valid
  file extension in addition to the existing
  '.sln' and '.raw'.

- ### bridge_builtin_features: add beep via touch variable
  Add optional touch variable : TOUCH_MIXMONITOR_BEEP(interval)
  Setting TOUCH_MIXMONITOR_BEEP/TOUCH_MONITOR_BEEP to a valid
  interval in seconds will result in a periodic beep being
  played to the monitored channel upon MixMontior/Monitor
  feature start.
  If an interval less than 5 seconds is specified, the interval
  will default to 5 seconds.  If the value is set to an invalid
  interval, the default of 15 seconds will be used.

- ### app_directory: Add a 'skip call' option.
  A new option 's' has been added to the Directory() application that
  will skip calling the extension and instead set the extension as
  DIRECTORY_EXTEN channel variable.

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

- ### app_senddtmf: Add option to answer target channel.
  A new option has been added to SendDTMF() which will answer the
  specified channel if it is not already up. If no channel is specified,
  the current channel will be answered instead.

- ### test.c: Fix counting of tests and add 2 new tests
  The "tests" attribute of the "testsuite" element in the
  output XML now reflects only the tests actually requested
  to be executed instead of all the tests registered.
  The "failures" attribute was added to the "testsuite"
  element.
  Also added two new unit tests that just pass and fail
  to be used for testing CI itself.

- ### cli: increase channel column width
  This change increases the display width on 'core show channels'
  amd 'core show channels verbose'
  For 'core show channels', the Channel name field is increased to
  64 characters and the Location name field is increased to 32
  characters.
  For 'core show channels verbose', the Channel name field is
  increased to 80 characters, the Context is increased to 24
  characters and the Extension is increased to 24 characters.


Upgrade Notes:
----------------------------------------

- ### cel: add local optimization begin event
  The existing AST_CEL_LOCAL_OPTIMIZE can continue
  to be used as-is and the AST_CEL_LOCAL_OPTIMIZE_BEGIN event
  can be ignored if desired.


Closed Issues:
----------------------------------------

  - #39: [Bug]: Remove .gitreview from repository.
  - #52: [improvement]: Add local optimization begin cel event
  - #89: [improvement]:  indications: logging changes
  - #104: [improvement]: Add AMI action to get a list of connected channels
  - #110: [improvement]: utils - add lock timing information with DEBUG_THREADS
  - #133: [bug]: unlock channel after moh state access
  - #145: [bug]: ABI issue with pjproject and pjsip_inv_session
  - #155: [bug]: GCC 13 is catching a few new trivial issues
  - #158: [bug]: test_stasis_endpoints.c: Unit test channel_messages is unstable
  - #188: [improvement]:  pjsip: Upgrade bundled version to pjproject 2.13.1 #187 
  - #193: [bug]: third-party/apply-patches doesn't sort the patch file list before applying

Commits By Author:
----------------------------------------

- ### Ben Ford (2):
  - AMI: Add CoreShowChannelMap action.
  - res_pjsip_session: Added new function calls to avoid ABI issues.

- ### George Joseph (33):
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
  - .github: Move publish docs to new file CreateDocs.yml
  - .github: Back out triggering PROpenedOrUpdated by label
  - .github: Updates for AsteriskReleaser
  - bundled_pjproject: Backport security fixes from pjproject 2.13.1
  - apply_patches: Sort patch list before applying

- ### Gitea (1):
  - .github: Tweak new feature language, and move feature requests elsewhere.

- ### Joshua C. Colp (2):
  - .github: Fix staleness check to only run on certain labels.
  - .github: Tweak improvement issue type language.

- ### Mike Bradeen (12):
  - app_directory: add ability to specify configuration file
  - app_read: Add an option to return terminator on empty digits.
  - app_directory: Add a 'skip call' option.
  - app_senddtmf: Add option to answer target channel.
  - cli: increase channel column width
  - bridge_builtin_features: add beep via touch variable
  - format_sln: add .slin as supported file extension
  - res_mixmonitor: MixMonitorMute by MixMonitor ID
  - cel: add local optimization begin event
  - indications: logging changes
  - utils: add lock timestamps for DEBUG_THREADS
  - res_musiconhold: avoid moh state access on unlocked chan

- ### Sean Bright (2):
  - core: Cleanup gerrit and JIRA references. (#40) (#61)
  - apply_patches: Use globbing instead of file/sort.


Detail:
----------------------------------------

- ### apply_patches: Use globbing instead of file/sort.
  Author: Sean Bright  
  Date:   2023-07-06  

  This accomplishes the same thing as a `find ... | sort` but with the
  added benefit of clarity and avoiding a call to a subshell.

  Additionally drop the -s option from call to patch as it is not POSIX.

- ### apply_patches: Sort patch list before applying
  Author: George Joseph  
  Date:   2023-07-06  

  The apply_patches script wasn't sorting the list of patches in
  the "patches" directory before applying them. This left the list
  in an indeterminate order. In most cases, the list is actually
  sorted but rarely, they can be out of order and cause dependent
  patches to fail to apply.

  We now sort the list but the "sort" program wasn't in the
  configure scripts so we needed to add that and regenerate
  the scripts as well.

  Resolves: #193

- ### bundled_pjproject: Backport security fixes from pjproject 2.13.1
  Author: George Joseph  
  Date:   2023-07-05  

  Merge-pull-request-from-GHSA-9pfh-r8x4-w26w.patch
  Merge-pull-request-from-GHSA-cxwq-5g9x-x7fr.patch
  Locking-fix-so-that-SSL_shutdown-and-SSL_write-are-n.patch
  Don-t-call-SSL_shutdown-when-receiving-SSL_ERROR_SYS.patch

  Resolves: #188

- ### .github: Updates for AsteriskReleaser
  Author: George Joseph  
  Date:   2023-06-30  


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

- ### test_statis_endpoints:  Fix channel_messages test again
  Author: George Joseph  
  Date:   2023-06-12  


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


- ### AMI: Add CoreShowChannelMap action.
  Author: Ben Ford  
  Date:   2023-05-18  

  Adds a new AMI action (CoreShowChannelMap) that takes in a channel name
  and provides a list of all channels that are connected to that channel,
  following local channel connections as well.

  Resolves: #104

  UserNote: New AMI action CoreShowChannelMap has been added.

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


- ### .github: Change title of AsteriskReleaser job
  Author: George Joseph  
  Date:   2023-05-23  


- ### .github: Don't add cherry-pick reminder if it's already present
  Author: George Joseph  
  Date:   2023-05-22  


- ### .github: Fix quoting in PROpenedOrUpdated
  Author: George Joseph  
  Date:   2023-05-16  


- ### .github: Add cherry-pick reminder to new PRs
  Author: George Joseph  
  Date:   2023-05-15  


- ### core: Cleanup gerrit and JIRA references. (#40) (#61)
  Author: Sean Bright  
  Date:   2023-05-10  

  * Remove .gitreview and switch to pulling the main asterisk branch
    version from configure.ac instead.
  
  * Replace references to JIRA with GitHub.
  
  * Other minor cleanup found along the way.
  
  Resolves: #39
- ### .github: Tweak improvement issue type language.
  Author: Joshua C. Colp  
  Date:   2023-05-09  


- ### .github: Tweak new feature language, and move feature requests elsewhere.
  Author: Gitea  
  Date:   2023-05-09  


- ### .github: Fix staleness check to only run on certain labels.
  Author: Joshua C. Colp  
  Date:   2023-05-09  


- ### .github: Add AsteriskReleaser
  Author: George Joseph  
  Date:   2023-05-05  


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


- ### .github: Add cherry-pick test progress labels
  Author: George Joseph  
  Date:   2023-05-02  


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


- ### app_senddtmf: Add option to answer target channel.
  Author: Mike Bradeen  
  Date:   2023-02-06  

  Adds a new option to SendDTMF() which will answer the specified
  channel if it is not already up. If no channel is specified, the
  current channel will be answered instead.

  ASTERISK-30422


- ### app_directory: Add a 'skip call' option.
  Author: Mike Bradeen  
  Date:   2023-01-27  

  Adds 's' option to skip calling the extension and instead set the
  extension as DIRECTORY_EXTEN channel variable.

  ASTERISK-30405


- ### app_read: Add an option to return terminator on empty digits.
  Author: Mike Bradeen  
  Date:   2023-01-30  

  Adds 'e' option to allow Read() to return the terminator as the
  dialed digits in the case where only the terminator is entered.

  ie; if "#" is entered, return "#" if the 'e' option is set and ""
  if it is not.

  ASTERISK-30411


- ### app_directory: add ability to specify configuration file
  Author: Mike Bradeen  
  Date:   2023-01-25  

  Adds option to app_directory to specify a filename from which to
  read configuration instead of voicemail.conf ie;

  same => n,Directory(,,c(directory.conf))

  This configuration should contain a list of extensions using the
  voicemail.conf format, ie;

  2020=2020,Dog Dog,,,,attach=no|saycid=no|envelope=no|delete=no

  ASTERISK-30404


