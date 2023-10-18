
Change Log for Release asterisk-21.0.0
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-21.0.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/21.0.0-pre1...21.0.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21.0.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- Update master branch for Asterisk 21
- translate.c: Prefer better codecs upon translate ties.
- chan_skinny: Remove deprecated module.
- app_osplookup: Remove deprecated module.
- chan_mgcp: Remove deprecated module.
- chan_alsa: Remove deprecated module.
- pbx_builtins: Remove deprecated and defunct functionality.
- chan_sip: Remove deprecated module.
- app_cdr: Remove deprecated application and option.
- app_macro: Remove deprecated module.
- res_monitor: Remove deprecated module.
- http.c: Minor simplification to HTTP status output.
- app_osplookup: Remove obsolete sample config.
- say.c: Fix French time playback. (#42)
- core: Cleanup gerrit and JIRA references. (#58)
- utils.h: Deprecate `ast_gethostbyname()`. (#79)
- res_pjsip_pubsub: Add new pubsub module capabilities. (#82)
- app_sla: Migrate SLA applications out of app_meetme.
- rest-api: Ran make ari stubs to fix resource_endpoints inconsistency
- .github: Update AsteriskReleaser for security releases
- users.conf: Deprecate users.conf configuration.
- Update version for Asterisk 21
- Remove unneeded CHANGES and UPGRADE files
- res_pjsip_pubsub: Add body_type to test_handler for unit tests
- ari-stubs: Fix more local anchor references
- ari-stubs: Fix more local anchor references
- ari-stubs: Fix broken documentation anchors
- res_pjsip_session: Send Session Interval too small response
- .github: Update workflow-application-token-action to v2
- app_dial: Fix infinite loop when sending digits.
- app_voicemail: Fix for loop declarations
- alembic: Fix quoting of the 100rel column
- pbx.c: Fix gcc 12 compiler warning.
- app_audiosocket: Fixed timeout with -1 to avoid busy loop.
- download_externals:  Fix a few version related issues
- main/refer.c: Fix double free in refer_data_destructor + potential leak
- sig_analog: Add Called Subscriber Held capability.
- Revert "app_stack: Print proper exit location for PBXless channels."
- install_prereq: Fix dependency install on aarch64.
- res_pjsip.c: Set contact_user on incoming call local Contact header
- extconfig: Allow explicit DB result set ordering to be disabled.
- rest-api: Run make ari-stubs
- res_pjsip_header_funcs: Make prefix argument optional.
- pjproject_bundled: Increase PJSIP_MAX_MODULE to 38
- manager: Tolerate stasis messages with no channel snapshot.
- Remove unneeded CHANGES and UPGRADE files

User Notes:
----------------------------------------

- ### sig_analog: Add Called Subscriber Held capability.
  Called Subscriber Held is now supported for analog
  FXS channels, using the calledsubscriberheld option. This allows
  a station  user to go on hook when receiving an incoming call
  and resume from another phone on the same line by going on hook,
  without disconnecting the call.

- ### res_pjsip_header_funcs: Make prefix argument optional.
  The prefix argument to PJSIP_HEADERS is now
  optional. If not specified, all header names will be
  returned.

- ### http.c: Minor simplification to HTTP status output.
  For bound addresses, the HTTP status page now combines the bound
  address and bound port in a single line. Additionally, the SSL bind
  address has been renamed to TLS.


Upgrade Notes:
----------------------------------------

- ### utils.h: Deprecate `ast_gethostbyname()`. (#79)
  ast_gethostbyname() has been deprecated and will be removed
  in Asterisk 23. New code should use `ast_sockaddr_resolve()` and
  `ast_sockaddr_resolve_first_af()`.

- ### app_sla: Migrate SLA applications out of app_meetme.
  The SLAStation and SLATrunk applications have been moved
  from app_meetme to app_sla. If you are using these applications and have
  autoload=no, you will need to explicitly load this module in modules.conf.

- ### users.conf: Deprecate users.conf configuration.
  The users.conf config is now deprecated
  and will be removed in a future version of Asterisk.

- ### res_monitor: Remove deprecated module.
  This module was deprecated in Asterisk 16
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.
  This also removes the 'w' and 'W' options
  for app_queue.
  MixMonitor should be default and only option
  for all settings that previously used either
  Monitor or MixMonitor.

- ### app_osplookup: Remove deprecated module.
  This module was deprecated in Asterisk 19
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.

- ### app_cdr: Remove deprecated application and option.
  The previously deprecated NoCDR application has been removed.
  Additionally, the previously deprecated 'e' option to the ResetCDR
  application has been removed.

- ### app_macro: Remove deprecated module.
  This module was deprecated in Asterisk 16
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.
  For most modules that interacted with app_macro,
  this change is limited to no longer looking for
  the current context from the macrocontext when set.
  The following modules have additional impacts:
  app_dial - no longer supports M^ connected/redirecting macro
  app_minivm - samples written using macro will no longer work.
  The sample needs to be re-written
  app_queue - can no longer call a macro on the called party's
  channel.  Use gosub which is currently supported
  ccss - no callback macro, gosub only
  app_voicemail - no macro support
  channel  - remove macrocontext and priority, no connected
  line or redirection macro options
  options - stdexten is deprecated to gosub as the default
  and only options
  pbx - removed macrolock
  pbx_dundi - no longer look for macro
  snmp - removed macro context, exten, and priority

- ### translate.c: Prefer better codecs upon translate ties.
  When setting up translation between two codecs the quality was not taken into account,
  resulting in suboptimal translation. The quality is now taken into account,
  which can reduce the number of translation steps required, and improve the resulting quality.

- ### chan_sip: Remove deprecated module.
  This module was deprecated in Asterisk 17
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.

- ### chan_alsa: Remove deprecated module.
  This module was deprecated in Asterisk 19
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.

- ### pbx_builtins: Remove deprecated and defunct functionality.
  The previously deprecated ImportVar and SetAMAFlags
  applications have now been removed.

- ### chan_mgcp: Remove deprecated module.
  This module was deprecated in Asterisk 19
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.

- ### chan_skinny: Remove deprecated module.
  This module was deprecated in Asterisk 19
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.


Closed Issues:
----------------------------------------

  - #37: [Bug]: contrib/scripts/install_prereq tries to install armhf packages on aarch64 Debian platforms
  - #39: [Bug]: Remove .gitreview from repository.
  - #41: [Bug]: say.c Time announcement does not say o'clock for the French language
  - #50: [improvement]: app_sla: Migrate SLA applications from app_meetme
  - #78: [improvement]: Deprecate ast_gethostbyname()
  - #81: [improvement]: Enhance and add additional PJSIP pubsub callbacks
  - #179: [bug]: Queue strategy “Linear” with Asterisk 20 on Realtime
  - #183: [deprecation]: Deprecate users.conf
  - #226: [improvement]: Apply contact_user to incoming calls
  - #230: [bug]: PJSIP_RESPONSE_HEADERS function documentation is misleading
  - #240: [new-feature]: sig_analog: Add Called Subscriber Held capability
  - #253: app_gosub patch appear to have broken predial handlers that utilize macros that call gosubs
  - #255: [bug]: pjsip_endpt_register_module: Assertion "Too many modules registered"
  - #263: [bug]: download_externals doesn't always handle versions correctly
  - #267: [bug]: ari: refer with display_name key in request body leads to crash
  - #274: [bug]: Syntax Error in SQL Code
  - #275: [bug]:Asterisk make now requires ASTCFLAGS='-std=gnu99 -Wdeclaration-after-statement'
  - #277: [bug]: pbx.c: Compiler error with gcc 12.2
  - #281: [bug]: app_dial: Infinite loop if called channel hangs up while receiving digits
  - #335: [bug]: res_pjsip_pubsub: The bad_event unit test causes a SEGV in build_resource_tree

Commits By Author:
----------------------------------------

- ### Asterisk Development Team (1):
  - Update for 21.0.0-rc1

- ### Bastian Triller (1):
  - res_pjsip_session: Send Session Interval too small response

- ### George Joseph (9):
  - Remove unneeded CHANGES and UPGRADE files
  - pjproject_bundled: Increase PJSIP_MAX_MODULE to 38
  - rest-api: Run make ari-stubs
  - download_externals:  Fix a few version related issues
  - alembic: Fix quoting of the 100rel column
  - .github: Update workflow-application-token-action to v2
  - ari-stubs: Fix broken documentation anchors
  - ari-stubs: Fix more local anchor references
  - ari-stubs: Fix more local anchor references

- ### Jason D. McCormick (1):
  - install_prereq: Fix dependency install on aarch64.

- ### Joshua C. Colp (1):
  - manager: Tolerate stasis messages with no channel snapshot.

- ### Matthew Fredrickson (1):
  - Revert "app_stack: Print proper exit location for PBXless channels."

- ### Maximilian Fridrich (1):
  - main/refer.c: Fix double free in refer_data_destructor + potential leak

- ### Mike Bradeen (1):
  - app_voicemail: Fix for loop declarations

- ### MikeNaso (1):
  - res_pjsip.c: Set contact_user on incoming call local Contact header

- ### Naveen Albert (4):
  - res_pjsip_header_funcs: Make prefix argument optional.
  - sig_analog: Add Called Subscriber Held capability.
  - pbx.c: Fix gcc 12 compiler warning.
  - app_dial: Fix infinite loop when sending digits.

- ### Sean Bright (1):
  - extconfig: Allow explicit DB result set ordering to be disabled.

- ### zhengsh (1):
  - app_audiosocket: Fixed timeout with -1 to avoid busy loop.


Detail:
----------------------------------------

- ### Update master branch for Asterisk 21
  Author: George Joseph  
  Date:   2022-07-20  


- ### translate.c: Prefer better codecs upon translate ties.
  Author: Naveen Albert  
  Date:   2021-05-27  

  If multiple codecs are available for the same
  resource and the translation costs between
  multiple codecs are the same, ties are
  currently broken arbitrarily, which means a
  lower quality codec would be used. This forces
  Asterisk to explicitly use the higher quality
  codec, ceteris paribus.

  ASTERISK-29455


- ### chan_skinny: Remove deprecated module.
  Author: Mike Bradeen  
  Date:   2022-11-16  

  ASTERISK-30300


- ### app_osplookup: Remove deprecated module.
  Author: Mike Bradeen  
  Date:   2022-11-18  

  ASTERISK-30302


- ### chan_mgcp: Remove deprecated module.
  Author: Mike Bradeen  
  Date:   2022-11-15  

  Also removes res_pktcops to avoid merge conflicts
  with ASTERISK~30301.

  ASTERISK-30299


- ### chan_alsa: Remove deprecated module.
  Author: Mike Bradeen  
  Date:   2022-11-14  

  ASTERISK-30298


- ### pbx_builtins: Remove deprecated and defunct functionality.
  Author: Naveen Albert  
  Date:   2022-11-29  

  This removes the ImportVar and SetAMAFlags applications
  which have been deprecated since Asterisk 12, but were
  never removed previously.

  Additionally, it removes remnants of defunct options
  that themselves were removed years ago.

  ASTERISK-30335 #close


- ### chan_sip: Remove deprecated module.
  Author: Mike Bradeen  
  Date:   2022-11-28  

  ASTERISK-30297


- ### app_cdr: Remove deprecated application and option.
  Author: Naveen Albert  
  Date:   2022-12-22  

  This removes the deprecated NoCDR application, which
  was deprecated in Asterisk 12, having long been fully
  superseded by the CDR_PROP function.

  The deprecated e option to ResetCDR is also removed
  for the same reason.

  ASTERISK-30371 #close


- ### app_macro: Remove deprecated module.
  Author: Mike Bradeen  
  Date:   2022-12-12  

  For most modules that interacted with app_macro, this change is limited
  to no longer looking for the current context from the macrocontext when
  set.  Additionally, the following modules are impacted:

  app_dial - no longer supports M^ connected/redirecting macro
  app_minivm - samples written using macro will no longer work.
  The sample needs a re-write

  app_queue - can no longer a macro on the called party's channel.
  Use gosub which is currently supported

  ccss - no callback macro, gosub only

  app_voicemail - no macro support

  channel  - remove macrocontext and priority, no connected line or
  redirection macro options
  options - stdexten is deprecated to gosub as the default and only
  pbx - removed macrolock
  pbx_dundi - no longer look for macro

  snmp - removed macro context, exten, and priority

  ASTERISK-30304


- ### res_monitor: Remove deprecated module.
  Author: Mike Bradeen  
  Date:   2022-11-18  

  ASTERISK-30303


- ### http.c: Minor simplification to HTTP status output.
  Author: Boris P. Korzun  
  Date:   2023-01-05  

  Change the HTTP status page (located at /httpstatus by default) by:

  * Combining the address and port into a single line.
  * Changing "SSL" to "TLS"

  ASTERISK-30433 #close


- ### app_osplookup: Remove obsolete sample config.
  Author: Naveen Albert  
  Date:   2023-02-24  

  ASTERISK_30302 previously removed app_osplookup,
  but its sample config was not removed.
  This removes it since nothing else uses it.

  ASTERISK-30438 #close


- ### say.c: Fix French time playback. (#42)
  Author: InterLinked1  
  Date:   2023-05-02  

  ast_waitstream was not called after ast_streamfile,
  resulting in "o'clock" being skipped in French.
  
  Additionally, the minute announcements should be
  feminine.
  
  Reported-by: Danny Lloyd
  
  Resolves: #41
  ASTERISK-30488
- ### core: Cleanup gerrit and JIRA references. (#58)
  Author: Sean Bright  
  Date:   2023-05-03  

  * Remove .gitreview and switch to pulling the main asterisk branch
    version from configure.ac instead.
  
  * Replace references to JIRA with GitHub.
  
  * Other minor cleanup found along the way.
  
  Resolves: #39
- ### utils.h: Deprecate `ast_gethostbyname()`. (#79)
  Author: Sean Bright  
  Date:   2023-05-11  

  Deprecate `ast_gethostbyname()` in favor of `ast_sockaddr_resolve()` and
  `ast_sockaddr_resolve_first_af()`. `ast_gethostbyname()` has not been
  used by any in-tree code since 2021.
  
  This function will be removed entirely in Asterisk 23.
  
  Resolves: #78
  
  UpgradeNote: ast_gethostbyname() has been deprecated and will be removed
  in Asterisk 23. New code should use `ast_sockaddr_resolve()` and
  `ast_sockaddr_resolve_first_af()`.
- ### res_pjsip_pubsub: Add new pubsub module capabilities. (#82)
  Author: InterLinked1  
  Date:   2023-05-18  

  The existing res_pjsip_pubsub APIs are somewhat limited in
  what they can do. This adds a few API extensions that make
  it possible for PJSIP pubsub modules to implement richer
  features than is currently possible.
  
  * Allow pubsub modules to get a handle to pjsip_rx_data on subscription
  * Allow pubsub modules to run a callback when a subscription is renewed
  * Allow pubsub modules to run a callback for outgoing NOTIFYs, with
    a handle to the tdata, so that modules can append their own headers
    to the NOTIFYs
  
  This change does not add any features directly, but makes possible
  several new features that will be added in future changes.
  
  Resolves: #81
  ASTERISK-30485 #close
  
  Master-Only: True
- ### app_sla: Migrate SLA applications out of app_meetme.
  Author: Naveen Albert  
  Date:   2023-05-02  

  This removes the dependency of the SLAStation and SLATrunk
  applications on app_meetme, in anticipation of the imminent
  removal of the deprecated app_meetme module.

  The user interface for the SLA applications is exactly the
  same, and in theory, users should not notice a difference.
  However, the SLA applications now use ConfBridge under the
  hood, rather than MeetMe, and they are now contained within
  their own module.

  Resolves: #50
  ASTERISK-30309

  UpgradeNote: The SLAStation and SLATrunk applications have been moved
  from app_meetme to app_sla. If you are using these applications and have
  autoload=no, you will need to explicitly load this module in modules.conf.

- ### rest-api: Ran make ari stubs to fix resource_endpoints inconsistency
  Author: George Joseph  
  Date:   2023-06-27  


- ### .github: Update AsteriskReleaser for security releases
  Author: George Joseph  
  Date:   2023-07-07  


- ### users.conf: Deprecate users.conf configuration.
  Author: Naveen Albert  
  Date:   2023-06-30  

  This deprecates the users.conf config file, which
  is no longer as widely supported but still integrated
  with a number of different modules.

  Because there is no real mechanism for marking a
  configuration file as "deprecated", and users.conf
  is not just used in a single place, this now emits
  a warning to the user when the PBX loads to notify
  about the deprecation.

  This configuration mechanism has been widely criticized
  and discouraged since its inception, and is no longer
  relevant to the configuration that most users are doing
  today. Removing it will allow for some simplification
  and cleanup in the codebase.

  Resolves: #183

  UpgradeNote: The users.conf config is now deprecated
  and will be removed in a future version of Asterisk.

- ### Update version for Asterisk 21
  Author: George Joseph  
  Date:   2023-08-09  


- ### Remove unneeded CHANGES and UPGRADE files
  Author: George Joseph  
  Date:   2023-08-09  


- ### res_pjsip_pubsub: Add body_type to test_handler for unit tests
  Author: George Joseph  
  Date:   2023-09-15  

  The ast_sip_subscription_handler "test_handler" used for the unit
  tests didn't set "body_type" so the NULL value was causing
  a SEGV in build_subscription_tree().  It's now set to "".

  Resolves: #335

- ### ari-stubs: Fix more local anchor references
  Author: George Joseph  
  Date:   2023-09-05  

  Also allow CreateDocs job to be run manually with default branches.


- ### ari-stubs: Fix more local anchor references
  Author: George Joseph  
  Date:   2023-09-05  

  Also allow CreateDocs job to be run manually with default branches.


- ### ari-stubs: Fix broken documentation anchors
  Author: George Joseph  
  Date:   2023-09-05  

  All of the links that reference page anchors with capital letters in
  the ids (#Something) have been changed to lower case to match the
  anchors that are generated by mkdocs.


- ### res_pjsip_session: Send Session Interval too small response
  Author: Bastian Triller  
  Date:   2023-08-28  

  Handle session interval lower than endpoint's configured minimum timer
  when sending first answer. Timer setting is checked during this step and
  needs to handled appropriately.
  Before this change, no response was sent at all. After this change a
  response with 422 Session Interval too small is sent to UAC.


- ### .github: Update workflow-application-token-action to v2
  Author: George Joseph  
  Date:   2023-08-31  


- ### app_dial: Fix infinite loop when sending digits.
  Author: Naveen Albert  
  Date:   2023-08-28  

  If the called party hangs up while digits are being
  sent, -1 is returned to indicate so, but app_dial
  was not checking the return value, resulting in
  the hangup being lost and looping forever until
  the caller manually hangs up the channel. We now
  abort if digit sending fails.

  ASTERISK-29428 #close

  Resolves: #281

- ### app_voicemail: Fix for loop declarations
  Author: Mike Bradeen  
  Date:   2023-08-29  

  Resolve for loop initial declarations added in cli changes.

  Resolves: #275

- ### alembic: Fix quoting of the 100rel column
  Author: George Joseph  
  Date:   2023-08-28  

  Add quoting around the ps_endpoints 100rel column in the ALTER
  statements.  Although alembic doesn't complain when generating
  sql statements, postgresql does (rightly so).

  Resolves: #274

- ### pbx.c: Fix gcc 12 compiler warning.
  Author: Naveen Albert  
  Date:   2023-08-27  

  Resolves: #277

- ### app_audiosocket: Fixed timeout with -1 to avoid busy loop.
  Author: zhengsh  
  Date:   2023-08-24  

  Resolves: asterisk#234

- ### download_externals:  Fix a few version related issues
  Author: George Joseph  
  Date:   2023-08-18  

  * Fixed issue with the script not parsing the new tag format for
    certified releases.  The format changed from certified/18.9-cert5
    to certified-18.9-cert5.

  * Fixed issue where the asterisk version wasn't being considered
    when looking for cached versions.

  Resolves: #263

- ### main/refer.c: Fix double free in refer_data_destructor + potential leak
  Author: Maximilian Fridrich  
  Date:   2023-08-21  

  Resolves: #267

- ### sig_analog: Add Called Subscriber Held capability.
  Author: Naveen Albert  
  Date:   2023-08-09  

  This adds support for Called Subscriber Held for FXS
  lines, which allows users to go on hook when receiving
  a call and resume the call later from another phone on
  the same line, without disconnecting the call. This is
  a convenience mechanism that most real PSTN telephone
  switches support.

  ASTERISK-30372 #close

  Resolves: #240

  UserNote: Called Subscriber Held is now supported for analog
  FXS channels, using the calledsubscriberheld option. This allows
  a station  user to go on hook when receiving an incoming call
  and resume from another phone on the same line by going on hook,
  without disconnecting the call.


- ### Revert "app_stack: Print proper exit location for PBXless channels."
  Author: Matthew Fredrickson  
  Date:   2023-08-10  

  This reverts commit 617dad4cba1513dddce87b8e95a61415fb587cf1.

  apps/app_stack.c: Revert buggy gosub patch

  This seems to break the case when a predial macro calls a gosub.
  When the gosub calls return, the Return function outputs:

  app_stack.c:423 return_exec: Return without Gosub: stack is empty

  This returns -1 to the calling macro, which returns to app_dial
  and causes the call to hangup instead of proceeding with the macro
  that invoked the gosub.

  Resolves: #253

- ### install_prereq: Fix dependency install on aarch64.
  Author: Jason D. McCormick  
  Date:   2023-04-28  

  Fixes dependency solutions in install_prereq for Debian aarch64
  platforms. install_prereq was attempting to forcibly install 32-bit
  armhf packages due to the aptitude search for dependencies.

  Resolves: #37

- ### res_pjsip.c: Set contact_user on incoming call local Contact header
  Author: MikeNaso  
  Date:   2023-08-08  

  If the contact_user is configured on the endpoint it will now be set on the local Contact header URI for incoming calls. The contact_user has already been set on the local Contact header URI for outgoing calls.

  Resolves: #226

- ### extconfig: Allow explicit DB result set ordering to be disabled.
  Author: Sean Bright  
  Date:   2023-07-12  

  Added a new boolean configuration flag -
  `order_multi_row_results_by_initial_column` - to both res_pgsql.conf
  and res_config_odbc.conf that allows the administrator to disable the
  explicit `ORDER BY` that was previously being added to all generated
  SQL statements that returned multiple rows.

  Fixes: #179

- ### rest-api: Run make ari-stubs
  Author: George Joseph  
  Date:   2023-08-09  

  An earlier cherry-pick that involved rest-api somehow didn't include
  a comment change in res/ari/resource_endpoints.h.  This commit
  corrects that.  No changes other than the comment.


- ### res_pjsip_header_funcs: Make prefix argument optional.
  Author: Naveen Albert  
  Date:   2023-08-09  

  The documentation for PJSIP_HEADERS claims that
  prefix is optional, but in the code it is actually not.
  However, there is no inherent reason for this, as users
  may want to retrieve all header names, not just those
  beginning with a certain prefix.

  This makes the prefix optional for this function,
  simply fetching all header names if not specified.
  As a result, the documentation is now correct.

  Resolves: #230

  UserNote: The prefix argument to PJSIP_HEADERS is now
  optional. If not specified, all header names will be
  returned.


- ### pjproject_bundled: Increase PJSIP_MAX_MODULE to 38
  Author: George Joseph  
  Date:   2023-08-11  

  The default is 32 with 8 being used by pjproject itself.  Recent
  commits have put us over the limit resulting in assertions in
  pjproject.  Since this value is used in invites, dialogs,
  transports and subscriptions as well as the global pjproject
  endpoint, we don't want to increase it too much.

  Resolves: #255

- ### manager: Tolerate stasis messages with no channel snapshot.
  Author: Joshua C. Colp  
  Date:   2023-08-09  

  In some cases I have yet to determine some stasis messages may
  be created without a channel snapshot. This change adds some
  tolerance to this scenario, preventing a crash from occurring.


- ### Remove unneeded CHANGES and UPGRADE files
  Author: George Joseph  
  Date:   2023-08-09  


