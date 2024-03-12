
Change Log for Release asterisk-certified-18.9-cert8
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-18.9-cert8.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert7...certified-18.9-cert8)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-18.9-cert8.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

Summary:
----------------------------------------

- Rename dialplan_functions.xml to dialplan_functions_doc.xml                     
- openssl: Supress deprecation warnings from OpenSSL 3.0                          
- app_chanspy: Add 'D' option for dual-channel audio                              
- manager.c: Fix regression due to using wrong free function.                     
- doc: Remove obsolete CHANGES-staging directrory                                 
- MergeApproved.yml:  Remove unneeded concurrency                                 
- SECURITY.md: Update with correct documentation URL                              
- chan_pjsip: Add PJSIPHangup dialplan app and manager action                     
- Remove files that are no longer updated                                         
- res_speech: allow speech to translate input channel                             
- res_stasis: signal when new command is queued                                   
- logger.h: Add ability to change the prefix on SCOPE_TRACE output                
- Add libjwt to third-party                                                       
- res_pjsip: update qualify_timeout documentation with DNS note                   
- lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS                            
- cel: add publish user event helper                                              
- file.c: Add ability to search custom dir for sounds                             
- make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h               
- func_periodic_hook: Add hangup step to avoid timeout                            
- func_periodic_hook: Don't truncate channel name                                 
- safe_asterisk: Change directory permissions to 755                              
- variables: Add additional variable dialplan functions.                          
- ari-stubs: Fix more local anchor references                                     
- ari-stubs: Fix more local anchor references                                     
- ari-stubs: Fix broken documentation anchors                                     
- rest-api: Updates for new documentation site                                    
- app_voicemail: Fix for loop declarations                                        
- download_externals:  Fix a few version related issues                           
- Remove .lastclean and .version from source control                              
- manager: Tolerate stasis messages with no channel snapshot.                     
- audiohook: Unlock channel in mute if no audiohooks present.                     
- app_queue: Add support for applying caller priority change immediately.         
- app.h: Move declaration of ast_getdata_result before its first use              
- doc: Remove obsolete CHANGES-staging and UPGRADE-staging                        
- res_geolocation: Ensure required 'location_info' is present.                    
- Adds manager actions to allow move/remove/forward individual messages in a par..
- app_voicemail: add CLI commands for message manipulation                        
- Cleanup deleted files                                                           

User Notes:
----------------------------------------

- ### app_chanspy: Add 'D' option for dual-channel audio                              
  The ChanSpy application now accepts the 'D' option which
  will interleave the spied audio within the outgoing frames. The
  purpose of this is to allow the audio to be read as a Dual channel
  stream with separate incoming and outgoing audio. Setting both the
  'o' option and the 'D' option and results in the 'D' option being
  ignored.

- ### chan_pjsip: Add PJSIPHangup dialplan app and manager action                     
  A new dialplan app PJSIPHangup and AMI action allows you
  to hang up an unanswered incoming PJSIP call with a specific SIP
  response code in the 400 -> 699 range.

- ### res_speech: allow speech to translate input channel                             
  res_speech now supports translation of an input channel
  to a format supported by the speech provider, provided a translation
  path is available between the source format and provider capabilites.

- ### res_stasis: signal when new command is queued                                   
  Call setup times should be significantly improved
  when using ARI.

- ### lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS                            
  You no longer need to select DEBUG_THREADS to use
  DETECT_DEADLOCKS.  This removes a significant amount of overhead
  if you just want to detect possible deadlocks vs needing full
  lock tracing.

- ### file.c: Add ability to search custom dir for sounds                             
  A new option "sounds_search_custom_dir" has been added to
  asterisk.conf that allows asterisk to search
  AST_DATA_DIR/sounds/custom for sounds files before searching the
  standard AST_DATA_DIR/sounds/<lang> directory.

- ### make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h               
  The "Build Options" entry in the "core show settings"
  CLI command has been renamed to "ABI related Build Options" and
  a new entry named "All Build Options" has been added that shows
  both breaking and non-breaking options.

- ### variables: Add additional variable dialplan functions.                          
  Four new dialplan functions have been added.
  GLOBAL_DELETE and DELETE have been added which allows
  the deletion of global and channel variables.
  GLOBAL_EXISTS and VARIABLE_EXISTS have been added
  which checks whether a global or channel variable has
  been set.

- ### app_queue: Add support for applying caller priority change immediately.         
  The 'queue priority caller' CLI command and
  'QueueChangePriorityCaller' AMI action now have an 'immediate'
  argument which allows the caller priority change to be reflected
  immediately, causing the position of a caller to move within the
  queue depending on the priorities of the other callers.

- ### Adds manager actions to allow move/remove/forward individual messages in a par..
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


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

  - #129: [bug]: res_speech_aeap: Crash due to NULL format on setup
  - #170: [improvement]: app_voicemail - add CLI commands to manipulate messages
  - #181: [improvement]: app_voicemail - add manager actions to display and manipulate messages
  - #200: [bug]: Regression: In app.h an enum is used before its declaration.
  - #202: [improvement]: app_queue: Add support for immediately applying queue caller priority change
  - #233: [bug]: Deadlock with MixMonitorMute AMI action
  - #263: [bug]: download_externals doesn't always handle versions correctly
  - #275: [bug]:Asterisk make now requires ASTCFLAGS='-std=gnu99 -Wdeclaration-after-statement'
  - #289: [new-feature]: Add support for deleting channel and global variables
  - #315: [improvement]: Search /var/lib/asterisk/sounds/custom for sound files before  /var/lib/asterisk/sounds/<lang>
  - #316: [bug]: Privilege Escalation in Astrisk's Group permissions.
  - #319: [bug]: func_periodic_hook truncates long channel names when setting EncodedChannel
  - #321: [bug]: Performance suffers unnecessarily when debugging deadlocks
  - #325: [bug]: hangup after beep to avoid waiting for timeout
  - #330: [improvement]: Add cel user event helper function
  - #349: [improvement]: Add libjwt to third-party
  - #352: [bug]: Update qualify_timeout documentation to include DNS note
  - #360: [improvement]: Update documentation for CHANGES/UPGRADE files
  - #362: [improvement]: Speed up ARI command processing
  - #513: [bug]: manager.c: Crash due to regression using wrong free function when built with MALLOC_DEBUG
  - #569: [improvement]: Add option to interleave input and output frames on spied channel

Commits By Author:
----------------------------------------

- ### George Joseph (21):
  - Cleanup deleted files
  - doc: Remove obsolete CHANGES-staging and UPGRADE-staging
  - app.h: Move declaration of ast_getdata_result before its first use
  - Remove .lastclean and .version from source control
  - download_externals:  Fix a few version related issues
  - rest-api: Updates for new documentation site
  - ari-stubs: Fix broken documentation anchors
  - ari-stubs: Fix more local anchor references
  - ari-stubs: Fix more local anchor references
  - safe_asterisk: Change directory permissions to 755
  - func_periodic_hook: Don't truncate channel name
  - make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h
  - file.c: Add ability to search custom dir for sounds
  - lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS
  - Add libjwt to third-party
  - logger.h: Add ability to change the prefix on SCOPE_TRACE output
  - chan_pjsip: Add PJSIPHangup dialplan app and manager action
  - SECURITY.md: Update with correct documentation URL
  - MergeApproved.yml:  Remove unneeded concurrency
  - doc: Remove obsolete CHANGES-staging directrory
  - Rename dialplan_functions.xml to dialplan_functions_doc.xml

- ### Joshua C. Colp (4):
  - app_queue: Add support for applying caller priority change immediately.
  - audiohook: Unlock channel in mute if no audiohooks present.
  - manager: Tolerate stasis messages with no channel snapshot.
  - variables: Add additional variable dialplan functions.

- ### Mark Murawski (1):
  - Remove files that are no longer updated

- ### Mike Bradeen (9):
  - app_voicemail: add CLI commands for message manipulation
  - Adds manager actions to allow move/remove/forward individual messages in a par..
  - app_voicemail: Fix for loop declarations
  - func_periodic_hook: Add hangup step to avoid timeout
  - cel: add publish user event helper
  - res_pjsip: update qualify_timeout documentation with DNS note
  - res_stasis: signal when new command is queued
  - res_speech: allow speech to translate input channel
  - app_chanspy: Add 'D' option for dual-channel audio

- ### Naveen Albert (1):
  - manager.c: Fix regression due to using wrong free function.

- ### Sean Bright (2):
  - res_geolocation: Ensure required 'location_info' is present.
  - openssl: Supress deprecation warnings from OpenSSL 3.0


Detail:
----------------------------------------

- ### Rename dialplan_functions.xml to dialplan_functions_doc.xml                     
  Author: George Joseph  
  Date:   2024-02-26  

  When using COMPILE_DOUBLE, dialplan_functions.xml is mistaken
  for the source for an embedded XML document and gets compiled
  to dialplan_functions.o.  This causes dialplan_functions.c to
  be ignored making its functions unavailable and causing chan_pjsip
  to fail to load.

- ### openssl: Supress deprecation warnings from OpenSSL 3.0                          
  Author: Sean Bright  
  Date:   2022-03-25  

  There is work going on to update our OpenSSL usage to avoid the
  deprecated functions but in the meantime make it possible to compile
  in devmode.


- ### app_chanspy: Add 'D' option for dual-channel audio                              
  Author: Mike Bradeen  
  Date:   2024-01-31  

  Adds the 'D' option to app chanspy that causes the input and output
  frames of the spied channel to be interleaved in the spy output frame.
  This allows the input and output of the spied channel to be decoded
  separately by the receiver.

  If the 'o' option is also set, the 'D' option is ignored as the
  audio being spied is inherently one direction.

  Fixes: #569

  UserNote: The ChanSpy application now accepts the 'D' option which
  will interleave the spied audio within the outgoing frames. The
  purpose of this is to allow the audio to be read as a Dual channel
  stream with separate incoming and outgoing audio. Setting both the
  'o' option and the 'D' option and results in the 'D' option being
  ignored.


- ### manager.c: Fix regression due to using wrong free function.                     
  Author: Naveen Albert  
  Date:   2023-12-26  

  Commit 424be345639d75c6cb7d0bd2da5f0f407dbd0bd5 introduced
  a regression by calling ast_free on memory allocated by
  realpath. This causes Asterisk to abort when executing this
  function. Since the memory is allocated by glibc, it should
  be freed using ast_std_free.

  Resolves: #513

- ### doc: Remove obsolete CHANGES-staging directrory                                 
  Author: George Joseph  
  Date:   2023-12-15  

  This should have been removed after the last release but
  was missed.


- ### MergeApproved.yml:  Remove unneeded concurrency                                 
  Author: George Joseph  
  Date:   2023-12-06  

  The concurrency parameter on the MergeAndCherryPick job has
  been rmeoved.  It was a hold-over from earlier days.


- ### SECURITY.md: Update with correct documentation URL                              
  Author: George Joseph  
  Date:   2023-11-09  


- ### chan_pjsip: Add PJSIPHangup dialplan app and manager action                     
  Author: George Joseph  
  Date:   2023-10-31  

  See UserNote below.

  Exposed the existing Hangup AMI action in manager.c so we can use
  all of it's channel search and AMI protocol handling without
  duplicating that code in dialplan_functions.c.

  Added a lookup function to res_pjsip.c that takes in the
  string represenation of the pjsip_status_code enum and returns
  the actual status code.  I.E.  ast_sip_str2rc("DECLINE") returns
  603.  This allows the caller to specify PJSIPHangup(decline) in
  the dialplan, just like Hangup(call_rejected).

  Also extracted the XML documentation to its own file since it was
  almost as large as the code itself.

  UserNote: A new dialplan app PJSIPHangup and AMI action allows you
  to hang up an unanswered incoming PJSIP call with a specific SIP
  response code in the 400 -> 699 range.


- ### Remove files that are no longer updated                                         
  Author: Mark Murawski  
  Date:   2023-10-30  

  Fixes: #360

- ### res_speech: allow speech to translate input channel                             
  Author: Mike Bradeen  
  Date:   2023-09-07  

  * Allow res_speech to translate the input channel if the
    format is translatable to a format suppored by the
    speech provider.

  Resolves: #129

  UserNote: res_speech now supports translation of an input channel
  to a format supported by the speech provider, provided a translation
  path is available between the source format and provider capabilites.


- ### res_stasis: signal when new command is queued                                   
  Author: Mike Bradeen  
  Date:   2023-10-02  

  res_statsis's app loop sleeps for up to .2s waiting on input
  to a channel before re-checking the command queue. This can
  cause delays between channel setup and bridge.

  This change is to send a SIGURG on the sleeping thread when
  a new command is enqueued. This exits the sleeping thread out
  of the ast_waitfor() call triggering the new command being
  processed on the channel immediately.

  Resolves: #362

  UserNote: Call setup times should be significantly improved
  when using ARI.


- ### logger.h: Add ability to change the prefix on SCOPE_TRACE output                
  Author: George Joseph  
  Date:   2023-10-05  

  You can now define the _TRACE_PREFIX_ macro to change the
  default trace line prefix of "file:line function" to
  something else.  Full documentation in logger.h.


- ### Add libjwt to third-party                                                       
  Author: George Joseph  
  Date:   2023-09-21  

  The current STIR/SHAKEN implementation is not currently usable due
  to encryption issues. Rather than trying to futz with OpenSSL and
  the the current code, we can take advantage of the existing
  capabilities of libjwt but we first need to add it to the
  third-party infrastructure already in place for jansson and
  pjproject.

  A few tweaks were also made to the third-party infrastructure as
  a whole.  The jansson "dest" install directory was renamed "dist"
  to better match convention, and the third-party Makefile was updated
  to clean all product directories not just the ones currently in
  use.

  Resolves: #349

- ### res_pjsip: update qualify_timeout documentation with DNS note                   
  Author: Mike Bradeen  
  Date:   2023-09-26  

  The documentation on qualify_timeout does not explicitly state that the timeout
  includes any time required to perform any needed DNS queries on the endpoint.

  If the OPTIONS response is delayed due to the DNS query, it can still render an
  endpoint as Unreachable if the net time is enough for qualify_timeout to expire.

  Resolves: #352

- ### lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS                            
  Author: George Joseph  
  Date:   2023-09-13  

  Previously, DETECT_DEADLOCKS depended on DEBUG_THREADS.
  Unfortunately, DEBUG_THREADS adds a lot of lock tracking overhead
  to all of the lock lifecycle calls whereas DETECT_DEADLOCKS just
  causes the lock calls to loop over trylock in 200us intervals until
  the lock is obtained and spits out log messages if it takes more
  than 5 seconds.  From a code perspective, the only reason they were
  tied together was for logging.  So... The ifdefs in lock.c were
  refactored to allow DETECT_DEADLOCKS to be enabled without
  also enabling DEBUG_THREADS.

  Resolves: #321

  UserNote: You no longer need to select DEBUG_THREADS to use
  DETECT_DEADLOCKS.  This removes a significant amount of overhead
  if you just want to detect possible deadlocks vs needing full
  lock tracing.


- ### cel: add publish user event helper                                              
  Author: Mike Bradeen  
  Date:   2023-09-14  

  Add a wrapper function around ast_cel_publish_event that
  packs event and extras into a blob before publishing

  Resolves:#330

- ### file.c: Add ability to search custom dir for sounds                             
  Author: George Joseph  
  Date:   2023-09-11  

  To better co-exist with sounds files that may be managed by
  packages, custom sound files may now be placed in
  AST_DATA_DIR/sounds/custom instead of the standard
  AST_DATA_DIR/sounds/<lang> directory.  If the new
  "sounds_search_custom_dir" option in asterisk.conf is set
  to "true", asterisk will search the custom directory for sounds
  files before searching the standard directory.  For performance
  reasons, the "sounds_search_custom_dir" defaults to "false".

  Resolves: #315

  UserNote: A new option "sounds_search_custom_dir" has been added to
  asterisk.conf that allows asterisk to search
  AST_DATA_DIR/sounds/custom for sounds files before searching the
  standard AST_DATA_DIR/sounds/<lang> directory.


- ### make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h               
  Author: George Joseph  
  Date:   2023-09-13  

  The previous behavior of make_buildopts_h was to not add the
  non-ABI-breaking MENUSELECT_CFLAGS like DETECT_DEADLOCKS,
  REF_DEBUG, etc. to the buildopts.h file because "it caused
  ccache to invalidate files and extended compile times". They're
  only defined by passing them on the gcc command line with '-D'
  options.   In practice, including them in the include file rarely
  causes any impact because the only time ccache cares is if you
  actually change an option so the hit occurrs only once after
  you change it.

  OK so why would we want to include them?  Many IDEs follow the
  include files to resolve defines and if the options aren't in an
  include file, it can cause the IDE to mark blocks of "ifdeffed"
  code as unused when they're really not.

  So...

  * Added a new menuselect compile option ADD_CFLAGS_TO_BUILDOPTS_H
    which tells make_buildopts_h to include the non-ABI-breaking
    flags in buildopts.h as well as the ABI-breaking ones. The default
    is disabled to preserve current behavior.  As before though,
    only the ABI-breaking flags appear in AST_BUILDOPTS and only
    those are used to calculate AST_BUILDOPT_SUM.
    A new AST_BUILDOPT_ALL define was created to capture all of the
    flags.

  * make_version_c was streamlined to use buildopts.h and also to
    create asterisk_build_opts_all[] and ast_get_build_opts_all(void)

  * "core show settings" now shows both AST_BUILDOPTS and
    AST_BUILDOPTS_ALL.

  UserNote: The "Build Options" entry in the "core show settings"
  CLI command has been renamed to "ABI related Build Options" and
  a new entry named "All Build Options" has been added that shows
  both breaking and non-breaking options.


- ### func_periodic_hook: Add hangup step to avoid timeout                            
  Author: Mike Bradeen  
  Date:   2023-09-12  

  func_periodic_hook does not hangup after playback, relying on hangup
  which keeps the channel alive longer than necessary.

  Resolves: #325

- ### func_periodic_hook: Don't truncate channel name                                 
  Author: George Joseph  
  Date:   2023-09-11  

  func_periodic_hook was truncating long channel names which
  causes issues when you need to run other dialplan functions/apps
  on the channel.

  Resolves: #319

- ### safe_asterisk: Change directory permissions to 755                              
  Author: George Joseph  
  Date:   2023-09-11  

  If the safe_asterisk script detects that the /var/lib/asterisk
  directory doesn't exist, it now creates it with 755 permissions
  instead of 770.  safe_asterisk needing to create that directory
  should be extremely rare though because it's normally created
  by 'make install' which already sets the permissions to 755.

  Resolves: #316

- ### variables: Add additional variable dialplan functions.                          
  Author: Joshua C. Colp  
  Date:   2023-08-31  

  Using the Set dialplan application does not actually
  delete channel or global variables. Instead the
  variables are set to an empty value.

  This change adds two dialplan functions,
  GLOBAL_DELETE and DELETE which can be used to
  delete global and channel variables instead
  of just setting them to empty.

  There is also no ability within the dialplan to
  determine if a global or channel variable has
  actually been set or not.

  This change also adds two dialplan functions,
  GLOBAL_EXISTS and VARIABLE_EXISTS which can be
  used to determine if a global or channel variable
  has been set or not.

  Resolves: #289

  UserNote: Four new dialplan functions have been added.
  GLOBAL_DELETE and DELETE have been added which allows
  the deletion of global and channel variables.
  GLOBAL_EXISTS and VARIABLE_EXISTS have been added
  which checks whether a global or channel variable has
  been set.


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


- ### rest-api: Updates for new documentation site                                    
  Author: George Joseph  
  Date:   2023-06-26  

  The new documentation site uses traditional markdown instead
  of the Confluence flavored version.  This required changes in
  the mustache templates and the python that generates the files.


- ### app_voicemail: Fix for loop declarations                                        
  Author: Mike Bradeen  
  Date:   2023-08-29  

  Resolve for loop initial declarations added in cli changes.

  Resolves: #275

- ### download_externals:  Fix a few version related issues                           
  Author: George Joseph  
  Date:   2023-08-18  

  * Fixed issue with the script not parsing the new tag format for
    certified releases.  The format changed from certified/18.9-cert5
    to certified-18.9-cert5.

  * Fixed issue where the asterisk version wasn't being considered
    when looking for cached versions.

  Resolves: #263

- ### Remove .lastclean and .version from source control                              
  Author: George Joseph  
  Date:   2023-08-18  

  Historically these were checked in for certified releases but
  since the move to github and the unified release process,
  they are no longer needed and cause issues.


- ### manager: Tolerate stasis messages with no channel snapshot.                     
  Author: Joshua C. Colp  
  Date:   2023-08-09  

  In some cases I have yet to determine some stasis messages may
  be created without a channel snapshot. This change adds some
  tolerance to this scenario, preventing a crash from occurring.


- ### audiohook: Unlock channel in mute if no audiohooks present.                     
  Author: Joshua C. Colp  
  Date:   2023-08-09  

  In the case where mute was called on a channel that had no
  audiohooks the code was not unlocking the channel, resulting
  in a deadlock.

  Resolves: #233

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


- ### res_geolocation: Ensure required 'location_info' is present.                    
  Author: Sean Bright  
  Date:   2023-07-07  

  Fixes #189


- ### Adds manager actions to allow move/remove/forward individual messages in a par..
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


- ### Cleanup deleted files                                                           
  Author: George Joseph  
  Date:   2024-02-20  


