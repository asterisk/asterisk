
Change Log for Release asterisk-20.5.0
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.5.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.4.0...20.5.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.5.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

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
- app_macro: Fix locking around datastore access
- Revert "app_stack: Print proper exit location for PBXless channels."
- .github: Use generic releaser
- install_prereq: Fix dependency install on aarch64.
- res_pjsip.c: Set contact_user on incoming call local Contact header
- extconfig: Allow explicit DB result set ordering to be disabled.
- rest-api: Run make ari-stubs
- res_pjsip_header_funcs: Make prefix argument optional.
- pjproject_bundled: Increase PJSIP_MAX_MODULE to 38
- manager: Tolerate stasis messages with no channel snapshot.
- core/ari/pjsip: Add refer mechanism
- chan_dahdi: Allow autoreoriginating after hangup.
- audiohook: Unlock channel in mute if no audiohooks present.
- sig_analog: Allow three-way flash to time out to silence.
- res_prometheus: Do not generate broken metrics
- res_pjsip: Enable TLS v1.3 if present.
- func_cut: Add example to documentation.
- extensions.conf.sample: Remove reference to missing context.
- func_export: Use correct function argument as variable name.
- app_queue: Add support for applying caller priority change immediately.
- .github: Fix cherry-pick reminder issues
- chan_iax2.c: Avoid crash with IAX2 switch support.
- res_geolocation: Ensure required 'location_info' is present.
- Adds manager actions to allow move/remove/forward individual messages in a particular mailbox folder. The forward command can be used to copy a message within a mailbox or to another mailbox. Also adds a VoicemailBoxSummarry, required to retrieve message ID's.
- app_voicemail: add CLI commands for message manipulation
- res_rtp_asterisk: Move ast_rtp_rtcp_report_alloc using `rtp->themssrc_valid` into the scope of the rtp_instance lock.
- .github: Minor tweak to Asterisk Releaser
- .github: Suppress cherry-pick reminder for some situations
- sig_analog: Allow immediate fake ring to be suppressed.

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

- ### core/ari/pjsip: Add refer mechanism
  There is a new ARI endpoint `/endpoints/refer` for referring
  an endpoint to some URI or endpoint.

- ### chan_dahdi: Allow autoreoriginating after hangup.
  The autoreoriginate setting now allows for kewlstart FXS
  channels to automatically reoriginate and provide dial tone to the
  user again after all calls on the line have cleared. This saves users
  from having to manually hang up and pick up the receiver again before
  making another call.

- ### sig_analog: Allow three-way flash to time out to silence.
  The threewaysilenthold option now allows the three-way
  dial tone to time out to silence, rather than continuing forever.

- ### res_pjsip: Enable TLS v1.3 if present.
  res_pjsip now allows TLS v1.3 to be enabled if supported by
  the underlying PJSIP library. The bundled version of PJSIP supports
  TLS v1.3.

- ### app_queue: Add support for applying caller priority change immediately.
  The 'queue priority caller' CLI command and
  'QueueChangePriorityCaller' AMI action now have an 'immediate'
  argument which allows the caller priority change to be reflected
  immediately, causing the position of a caller to move within the
  queue depending on the priorities of the other callers.

- ### Adds manager actions to allow move/remove/forward individual messages in a particular mailbox folder. The forward command can be used to copy a message within a mailbox or to another mailbox. Also adds a VoicemailBoxSummarry, required to retrieve message ID's.
  The following manager actions have been added
  VoicemailBoxSummary - Generate message list for a given mailbox
  VoicemailRemove - Remove a message from a mailbox folder
  VoicemailMove - Move a message from one folder to another within a mailbox
  VoicemailForward - Copy a message from one folder in one mailbox
  to another folder in another or the same mailbox.

- ### app_voicemail: add CLI commands for message manipulation
  The following CLI commands have been added to app_voicemail
  voicemail show mailbox <mailbox> <context>
  Show contents of mailbox <mailbox>@<context>
  voicemail remove <mailbox> <context> <from_folder> <messageid>
  Remove message <messageid> from <from_folder> in mailbox <mailbox>@<context>
  voicemail move <mailbox> <context> <from_folder> <messageid> <to_folder>
  Move message <messageid> in mailbox <mailbox>&<context> from <from_folder> to <to_folder>
  voicemail forward <from_mailbox> <from_context> <from_folder> <messageid> <to_mailbox> <to_context> <to_folder>
  Forward message <messageid> in mailbox <mailbox>@<context> <from_folder> to
  mailbox <mailbox>@<context> <to_folder>

- ### sig_analog: Allow immediate fake ring to be suppressed.
  The immediatering option can now be set to no to suppress
  the fake audible ringback provided when immediate=yes on FXS channels.


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

  - #37: [Bug]: contrib/scripts/install_prereq tries to install armhf packages on aarch64 Debian platforms
  - #71: [new-feature]: core/ari/pjsip: Add refer mechanism to refer endpoints to some resource
  - #118: [new-feature]: chan_dahdi: Allow fake ringing to be inhibited when immediate=yes
  - #170: [improvement]: app_voicemail - add CLI commands to manipulate messages
  - #179: [bug]: Queue strategy “Linear” with Asterisk 20 on Realtime
  - #181: [improvement]: app_voicemail - add manager actions to display and manipulate messages
  - #202: [improvement]: app_queue: Add support for immediately applying queue caller priority change
  - #205: [new-feature]: sig_analog: Allow flash to time out to silent hold
  - #224: [new-feature]: chan_dahdi: Allow automatic reorigination on hangup
  - #226: [improvement]: Apply contact_user to incoming calls
  - #230: [bug]: PJSIP_RESPONSE_HEADERS function documentation is misleading
  - #233: [bug]: Deadlock with MixMonitorMute AMI action
  - #240: [new-feature]: sig_analog: Add Called Subscriber Held capability
  - #253: app_gosub patch appear to have broken predial handlers that utilize macros that call gosubs
  - #255: [bug]: pjsip_endpt_register_module: Assertion "Too many modules registered"
  - #263: [bug]: download_externals doesn't always handle versions correctly
  - #265: [bug]: app_macro isn't locking around channel datastore access
  - #267: [bug]: ari: refer with display_name key in request body leads to crash
  - #274: [bug]: Syntax Error in SQL Code
  - #275: [bug]:Asterisk make now requires ASTCFLAGS='-std=gnu99 -Wdeclaration-after-statement'
  - #277: [bug]: pbx.c: Compiler error with gcc 12.2
  - #281: [bug]: app_dial: Infinite loop if called channel hangs up while receiving digits

Commits By Author:
----------------------------------------

- ### Asterisk Development Team (1):
  - Update for 20.5.0-rc1

- ### Bastian Triller (1):
  - res_pjsip_session: Send Session Interval too small response

- ### George Joseph (12):
  - .github: Suppress cherry-pick reminder for some situations
  - .github: Minor tweak to Asterisk Releaser
  - .github: Fix cherry-pick reminder issues
  - pjproject_bundled: Increase PJSIP_MAX_MODULE to 38
  - rest-api: Run make ari-stubs
  - .github: Use generic releaser
  - download_externals:  Fix a few version related issues
  - alembic: Fix quoting of the 100rel column
  - .github: Update workflow-application-token-action to v2
  - ari-stubs: Fix broken documentation anchors
  - ari-stubs: Fix more local anchor references
  - ari-stubs: Fix more local anchor references

- ### Holger Hans Peter Freyther (1):
  - res_prometheus: Do not generate broken metrics

- ### Jason D. McCormick (1):
  - install_prereq: Fix dependency install on aarch64.

- ### Joshua C. Colp (3):
  - app_queue: Add support for applying caller priority change immediately.
  - audiohook: Unlock channel in mute if no audiohooks present.
  - manager: Tolerate stasis messages with no channel snapshot.

- ### Matthew Fredrickson (2):
  - Revert "app_stack: Print proper exit location for PBXless channels."
  - app_macro: Fix locking around datastore access

- ### Maximilian Fridrich (2):
  - core/ari/pjsip: Add refer mechanism
  - main/refer.c: Fix double free in refer_data_destructor + potential leak

- ### Mike Bradeen (3):
  - app_voicemail: add CLI commands for message manipulation
  - Adds manager actions to allow move/remove/forward individual messages in a particular mailbox folder. The forward command can be used to copy a message within a mailbox or to another mailbox. Also adds a VoicemailBoxSummarry, required to retrieve message ID's.
  - app_voicemail: Fix for loop declarations

- ### MikeNaso (1):
  - res_pjsip.c: Set contact_user on incoming call local Contact header

- ### Naveen Albert (7):
  - sig_analog: Allow immediate fake ring to be suppressed.
  - sig_analog: Allow three-way flash to time out to silence.
  - chan_dahdi: Allow autoreoriginating after hangup.
  - res_pjsip_header_funcs: Make prefix argument optional.
  - sig_analog: Add Called Subscriber Held capability.
  - pbx.c: Fix gcc 12 compiler warning.
  - app_dial: Fix infinite loop when sending digits.

- ### Sean Bright (6):
  - res_geolocation: Ensure required 'location_info' is present.
  - chan_iax2.c: Avoid crash with IAX2 switch support.
  - func_export: Use correct function argument as variable name.
  - extensions.conf.sample: Remove reference to missing context.
  - res_pjsip: Enable TLS v1.3 if present.
  - extconfig: Allow explicit DB result set ordering to be disabled.

- ### phoneben (1):
  - func_cut: Add example to documentation.

- ### zhengsh (2):
  - res_rtp_asterisk: Move ast_rtp_rtcp_report_alloc using `rtp->themssrc_valid` into the scope of the rtp_instance lock.
  - app_audiosocket: Fixed timeout with -1 to avoid busy loop.


Detail:
----------------------------------------

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


- ### app_macro: Fix locking around datastore access
  Author: Matthew Fredrickson  
  Date:   2023-08-21  

  app_macro sometimes would crash due to datastore list corruption on the
  channel because of lack of locking around find and create process for
  the macro datastore. This patch locks the channel lock prior to protect
  against this problem.

  Resolves: #265

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

- ### .github: Use generic releaser
  Author: George Joseph  
  Date:   2023-08-15  


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


- ### core/ari/pjsip: Add refer mechanism
  Author: Maximilian Fridrich  
  Date:   2023-05-10  

  This change adds support for refers that are not session based. It
  includes a refer implementation for the PJSIP technology which results
  in out-of-dialog REFERs being sent to a PJSIP endpoint. These can be
  triggered using the new ARI endpoint `/endpoints/refer`.

  Resolves: #71

  UserNote: There is a new ARI endpoint `/endpoints/refer` for referring
  an endpoint to some URI or endpoint.


- ### chan_dahdi: Allow autoreoriginating after hangup.
  Author: Naveen Albert  
  Date:   2023-08-04  

  Currently, if an FXS channel is still off hook when
  all calls on the line have hung up, the user is provided
  reorder tone until going back on hook again.

  In addition to not reflecting what most commercial switches
  actually do, it's very common for switches to automatically
  reoriginate for the user so that dial tone is provided without
  the user having to depress and release the hookswitch manually.
  This can increase convenience for users.

  This behavior is now supported for kewlstart FXS channels.
  It's supported only for kewlstart (FXOKS) mainly because the
  behavior doesn't make any sense for ground start channels,
  and loop start signalling doesn't provide the necessary DAHDI
  event that makes this easy to implement. Likely almost everyone
  is using FXOKS over FXOLS anyways since FXOLS is pretty useless
  these days.

  ASTERISK-30357 #close

  Resolves: #224

  UserNote: The autoreoriginate setting now allows for kewlstart FXS
  channels to automatically reoriginate and provide dial tone to the
  user again after all calls on the line have cleared. This saves users
  from having to manually hang up and pick up the receiver again before
  making another call.


- ### audiohook: Unlock channel in mute if no audiohooks present.
  Author: Joshua C. Colp  
  Date:   2023-08-09  

  In the case where mute was called on a channel that had no
  audiohooks the code was not unlocking the channel, resulting
  in a deadlock.

  Resolves: #233

- ### sig_analog: Allow three-way flash to time out to silence.
  Author: Naveen Albert  
  Date:   2023-07-10  

  sig_analog allows users to flash and use the three-way dial
  tone as a primitive hold function, simply by never timing
  it out.

  Some systems allow this dial tone to time out to silence,
  so the user is not annoyed by a persistent dial tone.
  This option allows the dial tone to time out normally to
  silence.

  ASTERISK-30004 #close
  Resolves: #205

  UserNote: The threewaysilenthold option now allows the three-way
  dial tone to time out to silence, rather than continuing forever.


- ### res_prometheus: Do not generate broken metrics
  Author: Holger Hans Peter Freyther  
  Date:   2023-04-07  

  In 8d6fdf9c3adede201f0ef026dab201b3a37b26b6 invisible bridges were
  skipped but that lead to producing metrics with no name and no help.

  Keep track of the number of metrics configured and then only emit these.
  Add a basic testcase that verifies that there is no '(NULL)' in the
  output.

  ASTERISK-30474


- ### res_pjsip: Enable TLS v1.3 if present.
  Author: Sean Bright  
  Date:   2023-08-02  

  Fixes #221

  UserNote: res_pjsip now allows TLS v1.3 to be enabled if supported by
  the underlying PJSIP library. The bundled version of PJSIP supports
  TLS v1.3.


- ### func_cut: Add example to documentation.
  Author: phoneben  
  Date:   2023-07-19  

  This adds an example to the XML documentation clarifying usage
  of the CUT function to address a common misusage.


- ### extensions.conf.sample: Remove reference to missing context.
  Author: Sean Bright  
  Date:   2023-07-16  

  c3ff4648 removed the [iaxtel700] context but neglected to remove
  references to it.

  This commit addresses that and also removes iaxtel and freeworlddialup
  references from other config files.


- ### func_export: Use correct function argument as variable name.
  Author: Sean Bright  
  Date:   2023-07-12  

  Fixes #208


- ### app_queue: Add support for applying caller priority change immediately.
  Author: Joshua C. Colp  
  Date:   2023-07-07  

  The app_queue module provides both an AMI action and a CLI command
  to change the priority of a caller in a queue. Up to now this change
  of priority has only been reflected to new callers into the queue.

  This change adds an "immediate" option to both the AMI action and
  CLI command which immediately applies the priority change respective
  to the other callers already in the queue. This can allow, for example,
  a caller to be placed at the head of the queue immediately if their
  priority is sufficient.

  Resolves: #202

  UserNote: The 'queue priority caller' CLI command and
  'QueueChangePriorityCaller' AMI action now have an 'immediate'
  argument which allows the caller priority change to be reflected
  immediately, causing the position of a caller to move within the
  queue depending on the priorities of the other callers.


- ### .github: Fix cherry-pick reminder issues
  Author: George Joseph  
  Date:   2023-07-17  


- ### chan_iax2.c: Avoid crash with IAX2 switch support.
  Author: Sean Bright  
  Date:   2023-07-07  

  A change made in 82cebaa0 did not properly handle the case when a
  channel was not provided, triggering a crash. ast_check_hangup(...)
  does not protect against NULL pointers.

  Fixes #180


- ### res_geolocation: Ensure required 'location_info' is present.
  Author: Sean Bright  
  Date:   2023-07-07  

  Fixes #189


- ### Adds manager actions to allow move/remove/forward individual messages in a particular mailbox folder. The forward command can be used to copy a message within a mailbox or to another mailbox. Also adds a VoicemailBoxSummarry, required to retrieve message ID's.
  Author: Mike Bradeen  
  Date:   2023-06-29  

  Resolves: #181

  UserNote: The following manager actions have been added

  VoicemailBoxSummary - Generate message list for a given mailbox

  VoicemailRemove - Remove a message from a mailbox folder

  VoicemailMove - Move a message from one folder to another within a mailbox

  VoicemailForward - Copy a message from one folder in one mailbox
  to another folder in another or the same mailbox.


- ### app_voicemail: add CLI commands for message manipulation
  Author: Mike Bradeen  
  Date:   2023-06-20  

  Adds CLI commands to allow move/remove/forward individual messages
  from a particular mailbox folder. The forward command can be used
  to copy a message within a mailbox or to another mailbox. Also adds
  a show mailbox, required to retrieve message ID's.

  Resolves: #170

  UserNote: The following CLI commands have been added to app_voicemail

  voicemail show mailbox <mailbox> <context>
  Show contents of mailbox <mailbox>@<context>

  voicemail remove <mailbox> <context> <from_folder> <messageid>
  Remove message <messageid> from <from_folder> in mailbox <mailbox>@<context>

  voicemail move <mailbox> <context> <from_folder> <messageid> <to_folder>
  Move message <messageid> in mailbox <mailbox>&<context> from <from_folder> to <to_folder>

  voicemail forward <from_mailbox> <from_context> <from_folder> <messageid> <to_mailbox> <to_context> <to_folder>
  Forward message <messageid> in mailbox <mailbox>@<context> <from_folder> to
  mailbox <mailbox>@<context> <to_folder>


- ### res_rtp_asterisk: Move ast_rtp_rtcp_report_alloc using `rtp->themssrc_valid` into the scope of the rtp_instance lock.
  Author: zhengsh  
  Date:   2023-06-30  

  From the gdb information, it was found that when calling __ast_free, the size of the
  allocated space pointed to by the pointer matches the size created when rtp->themssrc_valid
  is equal to 0. However, in reality, when reading the value of rtp->themssrc_valid in gdb,
  it is found to be 1.

  Within ast_rtcp_write(), the call to ast_rtp_rtcp_report_alloc() uses rtp->themssrc_valid,
  which is outside the protection of the rtp_instance lock. However,
  ast_rtcp_generate_report(), which is called by ast_rtcp_generate_compound_prefix(), uses
  rtp->themssrc_valid within the protection of the rtp_instance lock.

  This can lead to the possibility that the value of rtp->themssrc_valid used in the call to
  ast_rtp_rtcp_report_alloc() may be different from the value of rtp->themssrc_valid used
  within ast_rtcp_generate_report().

  Resolves: asterisk#63

- ### .github: Minor tweak to Asterisk Releaser
  Author: George Joseph  
  Date:   2023-07-12  


- ### .github: Suppress cherry-pick reminder for some situations
  Author: George Joseph  
  Date:   2023-07-11  

  In PROpenedOrUpdated, the cherry-pick reminder will now be
  suppressed if there are already valid 'cherry-pick-to' comments
  in the PR or the PR contained a 'cherry-pick-to: none' comment.


- ### sig_analog: Allow immediate fake ring to be suppressed.
  Author: Naveen Albert  
  Date:   2023-06-08  

  When immediate=yes on an FXS channel, sig_analog will
  start fake audible ringback that continues until the
  channel is answered. Even if it answers immediately,
  the ringback is still audible for a brief moment.
  This can be disruptive and unwanted behavior.

  This adds an option to disable this behavior, though
  the default behavior remains unchanged.

  ASTERISK-30003 #close
  Resolves: #118

  UserNote: The immediatering option can now be set to no to suppress
  the fake audible ringback provided when immediate=yes on FXS channels.


