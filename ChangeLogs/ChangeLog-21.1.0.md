
Change Log for Release asterisk-21.1.0
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-21.1.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/21.0.2...21.1.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21.1.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- logger: Fix linking regression.
- Revert "core & res_pjsip: Improve topology change handling."
- menuselect: Use more specific error message.
- res_pjsip_nat: Fix potential use of uninitialized transport details
- app_if: Fix faulty EndIf branching.
- manager.c: Fix regression due to using wrong free function.
- doc: Remove obsolete CHANGES-staging and UPGRADE-staging directories
- config_options.c: Fix truncation of option descriptions.
- manager.c: Improve clarity of "manager show connected".
- make_xml_documentation: Really collect LOCAL_MOD_SUBDIRS documentation.
- general: Fix broken links.
- MergeApproved.yml:  Remove unneeded concurrency
- app_dial: Add option "j" to preserve initial stream topology of caller
- pbx_config.c: Don't crash when unloading module.
- ast_coredumper: Increase reliability
- logger.c: Move LOG_GROUP documentation to dedicated XML file.
- res_odbc.c: Allow concurrent access to request odbc connections
- res_pjsip_header_funcs.c: Check URI parameter length before copying.
- config.c: Log #exec include failures.
- make_xml_documentation: Properly handle absolute LOCAL_MOD_SUBDIRS.
- app_voicemail.c: Completely resequence mailbox folders.
- sig_analog: Fix channel leak when mwimonitor is enabled.
- res_rtp_asterisk.c: Update for OpenSSL 3+.
- alembic: Update list of TLS methods available on ps_transports.
- func_channel: Expose previously unsettable options.
- app.c: Allow ampersands in playback lists to be escaped.
- uri.c: Simplify ast_uri_make_host_with_port()
- func_curl.c: Remove CURLOPT() plaintext documentation.
- res_http_websocket.c: Set hostname on client for certificate validation.
- live_ast: Add astcachedir to generated asterisk.conf.
- SECURITY.md: Update with correct documentation URL
- func_lock: Add missing see-also refs to documentation.
- app_followme.c: Grab reference on nativeformats before using it
- configs: Improve documentation for bandwidth in iax.conf.
- logger: Add channel-based filtering.
- chan_iax2.c: Don't send unsanitized data to the logger.
- codec_ilbc: Disable system ilbc if version >= 3.0.0
- resource_channels.c: Explicit codec request when creating UnicastRTP.
- doc: Update IP Quality of Service links.
- chan_pjsip: Add PJSIPHangup dialplan app and manager action
- chan_iax2.c: Ensure all IEs are displayed when dumping frame contents.
- chan_dahdi: Warn if nonexistent cadence is requested.
- stasis: Update the snapshot after setting the redirect
- ari: Provide the caller ID RDNIS for the channels
- main/utils: Implement ast_get_tid() for OpenBSD
- res_rtp_asterisk.c: Fix runtime issue with LibreSSL
- app_directory: Add ADSI support to Directory.
- core_local: Fix local channel parsing with slashes.
- Remove files that are no longer updated
- app_voicemail: Add AMI event for mailbox PIN changes.
- app_queue.c: Emit unpause reason with PauseQueueMember event.
- bridge_simple: Suppress unchanged topology change requests
- res_pjsip: Include cipher limit in config error message.
- res_speech: allow speech to translate input channel
- res_rtp_asterisk.c: Fix memory leak in ephemeral certificate creation.
- res_pjsip_dtmf_info.c: Add 'INFO' to Allow header.
- api.wiki.mustache: Fix indentation in generated markdown
- pjsip_configuration.c: Disable DTLS renegotiation if WebRTC is enabled.
- configs: Fix typo in pjsip.conf.sample.
- res_pjsip_exten_state,res_pjsip_mwi: Allow unload on shutdown
- res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 characters
- .github: PRSubmitActions: Fix adding reviewers to PR
- .github: New PR Submit workflows
- .github: New PR Submit workflows
- res_stasis: signal when new command is queued
- ari/stasis: Indicate progress before playback on a bridge
- func_curl.c: Ensure channel is locked when manipulating datastores.
- .github: Fix job prereqs in PROpenedUpdated
- .github: Block PR tests until approved
- .github: Use generic releaser
- logger.h: Add ability to change the prefix on SCOPE_TRACE output
- Add libjwt to third-party
- res_pjsip: update qualify_timeout documentation with DNS note
- chan_dahdi: Clarify scope of callgroup/pickupgroup.
- func_json: Fix crashes for some types
- res_speech_aeap: add aeap error handling
- app_voicemail: Disable ADSI if unavailable.
- codec_builtin: Use multiples of 20 for maximum_ms
- lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS
- asterisk.c: Use the euid's home directory to read/write cli history
- res_pjsip_transport_websocket: Prevent transport from being destroyed before message finishes.
- cel: add publish user event helper
- chan_console: Fix deadlock caused by unclean thread exit.
- file.c: Add ability to search custom dir for sounds
- chan_iax2: Improve authentication debugging.
- res_rtp_asterisk: fix wrong counter management in ioqueue objects
- res_pjsip_pubsub: Add body_type to test_handler for unit tests
- make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h
- func_periodic_hook: Add hangup step to avoid timeout
- res_stasis_recording.c: Save recording state when unmuted.
- res_speech_aeap: check for null format on response
- func_periodic_hook: Don't truncate channel name
- safe_asterisk: Change directory permissions to 755
- chan_rtp: Implement RTP glue for UnicastRTP channels
- app_queue: periodic announcement configurable start time.
- variables: Add additional variable dialplan functions.
- Restore CHANGES and UPGRADE.txt to allow cherry-picks to work

User Notes:
----------------------------------------

- ### app_dial: Add option "j" to preserve initial stream topology of caller
  The option "j" is now available for the Dial application which
  uses the initial stream topology of the caller to create the outgoing
  channels.

- ### logger: Add channel-based filtering.
  The console log can now be filtered by
  channels or groups of channels, using the
  logger filter CLI commands.

- ### chan_pjsip: Add PJSIPHangup dialplan app and manager action
  A new dialplan app PJSIPHangup and AMI action allows you
  to hang up an unanswered incoming PJSIP call with a specific SIP
  response code in the 400 -> 699 range.

- ### app_voicemail: Add AMI event for mailbox PIN changes.
  The VoicemailPasswordChange event is
  now emitted whenever a mailbox password is updated,
  containing the mailbox information and the new
  password.
  Resolves: #398

- ### res_speech: allow speech to translate input channel
  res_speech now supports translation of an input channel
  to a format supported by the speech provider, provided a translation
  path is available between the source format and provider capabilites.

- ### res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 characters
  With this update, the PJSIP realm lengths have been extended
  to support up to 255 characters.

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

- ### chan_rtp: Implement RTP glue for UnicastRTP channels
  The dial string option 'g' was added to the UnicastRTP channel
  which enables RTP glue and therefore native RTP bridges with those
  channels.

- ### app_queue: periodic announcement configurable start time.
  Introduce a new queue configuration option called
  'periodic-announce-startdelay' which will vary the normal (historic)
  behavior of starting the periodic announcement cycle at
  periodic-announce-frequency seconds after entering the queue to start
  the periodic announcement cycle at period-announce-startdelay seconds
  after joining the queue.  The default behavior if this config option is
  not set remains unchanged.
  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### variables: Add additional variable dialplan functions.
  Four new dialplan functions have been added.
  GLOBAL_DELETE and DELETE have been added which allows
  the deletion of global and channel variables.
  GLOBAL_EXISTS and VARIABLE_EXISTS have been added
  which checks whether a global or channel variable has
  been set.


Upgrade Notes:
----------------------------------------

- ### app.c: Allow ampersands in playback lists to be escaped.
  Ampersands in URLs passed to the `Playback()`,
  `Background()`, `SpeechBackground()`, `Read()`, `Authenticate()`, or
  `Queue()` applications as filename arguments can now be escaped by
  single quoting the filename. Additionally, this is also possible when
  using the `CONFBRIDGE` dialplan function, or configuring various
  features in `confbridge.conf` and `queues.conf`.

- ### pjsip_configuration.c: Disable DTLS renegotiation if WebRTC is enabled.
  The dtls_rekey will be disabled if webrtc support is
  requested on an endpoint. A warning will also be emitted.

- ### res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 characters
  As part of this update, the maximum allowable length
  for PJSIP endpoints and relevant resources has been increased from
  40 to 255 characters. To take advantage of this enhancement, it is
  recommended to run the necessary procedures (e.g., Alembic) to
  update your schemas.


Closed Issues:
----------------------------------------

  - #84: [bug]: codec_ilbc:  Fails to build with ilbc version 3.0.4
  - #129: [bug]: res_speech_aeap: Crash due to NULL format on setup
  - #242: [new-feature]: logger: Allow filtering logs in CLI by channel
  - #248: [bug]: core_local: Local channels cannot have slashes in the destination
  - #260: [bug]: maxptime must be changed to multiples of 20
  - #286: [improvement]: chan_iax2: Improve authentication debugging
  - #289: [new-feature]: Add support for deleting channel and global variables
  - #294: [improvement]: chan_dahdi: Improve call pickup documentation
  - #298: [improvement]: chan_rtp: Implement RTP glue
  - #301: [bug]: Number of ICE TURN threads continually growing
  - #303: [bug]: SpeechBackground never exits
  - #308: [bug]: chan_console: Deadlock when hanging up console channels
  - #315: [improvement]: Search /var/lib/asterisk/sounds/custom for sound files before  /var/lib/asterisk/sounds/<lang>
  - #316: [bug]: Privilege Escalation in Astrisk's Group permissions.
  - #319: [bug]: func_periodic_hook truncates long channel names when setting EncodedChannel
  - #321: [bug]: Performance suffers unnecessarily when debugging deadlocks
  - #325: [bug]: hangup after beep to avoid waiting for timeout
  - #330: [improvement]: Add cel user event helper function
  - #335: [bug]: res_pjsip_pubsub: The bad_event unit test causes a SEGV in build_resource_tree
  - #337: [bug]: asterisk.c: The CLI history file is written to the wrong directory in some cases
  - #341: [bug]: app_if.c : nested EndIf incorrectly exits parent If
  - #345: [improvement]: Increase pj_sip Realm Size to 255 Characters for Improved Functionality
  - #349: [improvement]: Add libjwt to third-party
  - #352: [bug]: Update qualify_timeout documentation to include DNS note
  - #354: [improvement]: app_voicemail: Disable ADSI if unavailable on a line
  - #356: [new-feature]: app_directory: Add ADSI support.
  - #360: [improvement]: Update documentation for CHANGES/UPGRADE files
  - #362: [improvement]: Speed up ARI command processing
  - #379: [bug]: Orphaned taskprocessors cause shutdown delays
  - #384: [bug]: Unnecessary re-INVITE after answer
  - #388: [bug]: Crash in app_followme.c due to not acquiring a reference to nativeformats
  - #396: [improvement]: res_pjsip: Specify max ciphers allowed if too many provided
  - #398: [new-feature]: app_voicemail: Add AMI event for password change
  - #409: [improvement]: chan_dahdi: Emit warning if specifying nonexistent cadence
  - #423: [improvement]: func_lock: Add missing see-also refs
  - #425: [improvement]: configs: Improve documentation for bandwidth in iax.conf.sample
  - #428: [bug]: cli: Output is truncated from "config show help"
  - #430: [bug]: Fix broken links
  - #442: [bug]: func_channel: Some channel options are not settable
  - #445: [bug]: ast_coredumper isn't figuring out file locations properly in all cases
  - #458: [bug]: Memory leak in chan_dahdi when mwimonitor=yes on FXO
  - #462: [new-feature]: app_dial: Add new option to preserve initial stream topology of caller
  - #465: [improvement]: Change res_odbc connection pool request logic to not lock around blocking operations
  - #482: [improvement]: manager.c: Improve clarity of "manager show connected" output
  - #509: [bug]: res_pjsip: Crash when looking up transport state in use
  - #513: [bug]: manager.c: Crash due to regression using wrong free function when built with MALLOC_DEBUG
  - #520: [improvement]: menuselect: Use more specific error message.
  - #530: [bug]: bridge_channel.c: Stream topology change amplification with multiple layers of Local channels
  - #539: [bug]: Existence of logger.xml causes linking failure

Commits By Author:
----------------------------------------

- ### Asterisk Development Team (2):
  - Update for 21.1.0-rc1
  - Update for 21.1.0-rc2

- ### Bastian Triller (1):
  - func_json: Fix crashes for some types

- ### Brad Smith (2):
  - res_rtp_asterisk.c: Fix runtime issue with LibreSSL
  - main/utils: Implement ast_get_tid() for OpenBSD

- ### Eduardo (1):
  - codec_builtin: Use multiples of 20 for maximum_ms

- ### George Joseph (26):
  - Restore CHANGES and UPGRADE.txt to allow cherry-picks to work
  - safe_asterisk: Change directory permissions to 755
  - func_periodic_hook: Don't truncate channel name
  - make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h
  - res_pjsip_pubsub: Add body_type to test_handler for unit tests
  - file.c: Add ability to search custom dir for sounds
  - asterisk.c: Use the euid's home directory to read/write cli history
  - lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS
  - Add libjwt to third-party
  - logger.h: Add ability to change the prefix on SCOPE_TRACE output
  - .github: Use generic releaser
  - .github: Block PR tests until approved
  - .github: Fix job prereqs in PROpenedUpdated
  - .github: New PR Submit workflows
  - .github: New PR Submit workflows
  - .github: PRSubmitActions: Fix adding reviewers to PR
  - res_pjsip_exten_state,res_pjsip_mwi: Allow unload on shutdown
  - api.wiki.mustache: Fix indentation in generated markdown
  - bridge_simple: Suppress unchanged topology change requests
  - chan_pjsip: Add PJSIPHangup dialplan app and manager action
  - codec_ilbc: Disable system ilbc if version >= 3.0.0
  - SECURITY.md: Update with correct documentation URL
  - ast_coredumper: Increase reliability
  - MergeApproved.yml:  Remove unneeded concurrency
  - doc: Remove obsolete CHANGES-staging and UPGRADE-staging directories
  - Revert "core & res_pjsip: Improve topology change handling."

- ### Holger Hans Peter Freyther (3):
  - ari/stasis: Indicate progress before playback on a bridge
  - ari: Provide the caller ID RDNIS for the channels
  - stasis: Update the snapshot after setting the redirect

- ### Jaco Kroon (1):
  - app_queue: periodic announcement configurable start time.

- ### Joshua C. Colp (1):
  - variables: Add additional variable dialplan functions.

- ### Mark Murawski (1):
  - Remove files that are no longer updated

- ### Matthew Fredrickson (2):
  - app_followme.c: Grab reference on nativeformats before using it
  - res_odbc.c: Allow concurrent access to request odbc connections

- ### Maximilian Fridrich (3):
  - chan_rtp: Implement RTP glue for UnicastRTP channels
  - app_dial: Add option "j" to preserve initial stream topology of caller
  - res_pjsip_nat: Fix potential use of uninitialized transport details

- ### Mike Bradeen (7):
  - res_speech_aeap: check for null format on response
  - func_periodic_hook: Add hangup step to avoid timeout
  - cel: add publish user event helper
  - res_speech_aeap: add aeap error handling
  - res_pjsip: update qualify_timeout documentation with DNS note
  - res_stasis: signal when new command is queued
  - res_speech: allow speech to translate input channel

- ### Naveen Albert (21):
  - chan_iax2: Improve authentication debugging.
  - chan_console: Fix deadlock caused by unclean thread exit.
  - app_voicemail: Disable ADSI if unavailable.
  - chan_dahdi: Clarify scope of callgroup/pickupgroup.
  - res_pjsip: Include cipher limit in config error message.
  - app_voicemail: Add AMI event for mailbox PIN changes.
  - core_local: Fix local channel parsing with slashes.
  - app_directory: Add ADSI support to Directory.
  - chan_dahdi: Warn if nonexistent cadence is requested.
  - logger: Add channel-based filtering.
  - configs: Improve documentation for bandwidth in iax.conf.
  - func_lock: Add missing see-also refs to documentation.
  - func_channel: Expose previously unsettable options.
  - sig_analog: Fix channel leak when mwimonitor is enabled.
  - general: Fix broken links.
  - manager.c: Improve clarity of "manager show connected".
  - config_options.c: Fix truncation of option descriptions.
  - manager.c: Fix regression due to using wrong free function.
  - app_if: Fix faulty EndIf branching.
  - menuselect: Use more specific error message.
  - logger: Fix linking regression.

- ### Samuel Olaechea (1):
  - configs: Fix typo in pjsip.conf.sample.

- ### Sean Bright (24):
  - res_stasis_recording.c: Save recording state when unmuted.
  - func_curl.c: Ensure channel is locked when manipulating datastores.
  - pjsip_configuration.c: Disable DTLS renegotiation if WebRTC is enabled.
  - res_pjsip_dtmf_info.c: Add 'INFO' to Allow header.
  - res_rtp_asterisk.c: Fix memory leak in ephemeral certificate creation.
  - app_queue.c: Emit unpause reason with PauseQueueMember event.
  - chan_iax2.c: Ensure all IEs are displayed when dumping frame contents.
  - doc: Update IP Quality of Service links.
  - resource_channels.c: Explicit codec request when creating UnicastRTP.
  - chan_iax2.c: Don't send unsanitized data to the logger.
  - live_ast: Add astcachedir to generated asterisk.conf.
  - res_http_websocket.c: Set hostname on client for certificate validation.
  - func_curl.c: Remove CURLOPT() plaintext documentation.
  - uri.c: Simplify ast_uri_make_host_with_port()
  - app.c: Allow ampersands in playback lists to be escaped.
  - alembic: Update list of TLS methods available on ps_transports.
  - res_rtp_asterisk.c: Update for OpenSSL 3+.
  - app_voicemail.c: Completely resequence mailbox folders.
  - make_xml_documentation: Properly handle absolute LOCAL_MOD_SUBDIRS.
  - config.c: Log #exec include failures.
  - res_pjsip_header_funcs.c: Check URI parameter length before copying.
  - logger.c: Move LOG_GROUP documentation to dedicated XML file.
  - pbx_config.c: Don't crash when unloading module.
  - make_xml_documentation: Really collect LOCAL_MOD_SUBDIRS documentation.

- ### Tinet-mucw (1):
  - res_pjsip_transport_websocket: Prevent transport from being destroyed before message finishes.

- ### Vitezslav Novy (1):
  - res_rtp_asterisk: fix wrong counter management in ioqueue objects

- ### sungtae kim (1):
  - res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 characters


Detail:
----------------------------------------

- ### logger: Fix linking regression.
  Author: Naveen Albert  
  Date:   2024-01-16  

  Commit 008731b0a4b96c4e6c340fff738cc12364985b64
  caused a regression by resulting in logger.xml
  being compiled and linked into the asterisk
  binary in lieu of logger.c on certain platforms
  if Asterisk was compiled in dev mode.

  To fix this, we ensure the file has a unique
  name without the extension. Most existing .xml
  files have been named differently from any
  .c files in the same directory or did not
  pose this issue.

  channels/pjsip/dialplan_functions.xml does not
  pose this issue but is also being renamed
  to adhere to this policy.

  Resolves: #539

- ### Revert "core & res_pjsip: Improve topology change handling."
  Author: George Joseph  
  Date:   2024-01-12  

  This reverts commit 315eb551dbd18ecd424a2f32179d4c1f6f6edd26.

  Over the past year, we've had several reports of "topology storms"
  occurring where 2 external facing channels connected by one or more
  local channels and bridges will get themselves in a state where
  they continually send each other topology change requests.  This
  usually manifests itself in no-audio calls and a flood of
  "Exceptionally long queue length" messages.  It appears that this
  commit is the cause so we're reverting it for now until we can
  determine a more appropriate solution.

  Resolves: #530

- ### menuselect: Use more specific error message.
  Author: Naveen Albert  
  Date:   2024-01-04  

  Instead of using the same error message for
  missing dependencies and conflicts, be specific
  about what actually went wrong.

  Resolves: #520

- ### res_pjsip_nat: Fix potential use of uninitialized transport details
  Author: Maximilian Fridrich  
  Date:   2024-01-08  

  The ast_sip_request_transport_details must be zero initialized,
  otherwise this could lead to a SEGV.

  Resolves: #509

- ### app_if: Fix faulty EndIf branching.
  Author: Naveen Albert  
  Date:   2023-12-23  

  This fixes faulty branching logic for the
  EndIf application. Instead of computing
  the next priority, which should be done
  for false conditionals or ExitIf, we should
  simply advance to the next priority.

  Resolves: #341

- ### manager.c: Fix regression due to using wrong free function.
  Author: Naveen Albert  
  Date:   2023-12-26  

  Commit 424be345639d75c6cb7d0bd2da5f0f407dbd0bd5 introduced
  a regression by calling ast_free on memory allocated by
  realpath. This causes Asterisk to abort when executing this
  function. Since the memory is allocated by glibc, it should
  be freed using ast_std_free.

  Resolves: #513

- ### doc: Remove obsolete CHANGES-staging and UPGRADE-staging directories
  Author: George Joseph  
  Date:   2023-12-15  

  These should have been deleted after the release of 21.0.0
  but were missed.


- ### config_options.c: Fix truncation of option descriptions.
  Author: Naveen Albert  
  Date:   2023-11-09  

  This increases the format width of option descriptions
  to avoid needless truncation for longer descriptions.

  Resolves: #428

- ### manager.c: Improve clarity of "manager show connected".
  Author: Naveen Albert  
  Date:   2023-12-05  

  Improve the "manager show connected" CLI command
  to clarify that the last two columns are permissions
  related, not counts, and use sufficient widths
  to consistently display these values.

  ASTERISK-30143 #close
  Resolves: #482


- ### make_xml_documentation: Really collect LOCAL_MOD_SUBDIRS documentation.
  Author: Sean Bright  
  Date:   2023-12-01  

  Although `make_xml_documentation`'s `print_dependencies` command was
  corrected by the previous fix (#461) for #142, the `create_xml` was
  not properly handling `LOCAL_MOD_SUBDIRS` XML documentation.


- ### general: Fix broken links.
  Author: Naveen Albert  
  Date:   2023-11-09  

  This fixes a number of broken links throughout the
  tree, mostly caused by wiki.asterisk.org being replaced
  with docs.asterisk.org, which should eliminate the
  need for sporadic fixes as in f28047db36a70e81fe373a3d19132c43adf3f74b.

  Resolves: #430

- ### MergeApproved.yml:  Remove unneeded concurrency
  Author: George Joseph  
  Date:   2023-12-06  

  The concurrency parameter on the MergeAndCherryPick job has
  been rmeoved.  It was a hold-over from earlier days.


- ### app_dial: Add option "j" to preserve initial stream topology of caller
  Author: Maximilian Fridrich  
  Date:   2023-11-30  

  Resolves: #462

  UserNote: The option "j" is now available for the Dial application which
  uses the initial stream topology of the caller to create the outgoing
  channels.


- ### pbx_config.c: Don't crash when unloading module.
  Author: Sean Bright  
  Date:   2023-12-02  

  `pbx_config` subscribes to manager events to capture the `FullyBooted`
  event but fails to unsubscribe if the module is loaded after that
  event fires. If the module is unloaded, a crash occurs the next time a
  manager event is raised.

  We now unsubscribe when the module is unloaded if we haven't already
  unsubscribed.

  Fixes #470


- ### ast_coredumper: Increase reliability
  Author: George Joseph  
  Date:   2023-11-11  

  Instead of searching for the asterisk binary and the modules in the
  filesystem, we now get their locations, along with libdir, from
  the coredump itself...

  For the binary, we can use `gdb -c <coredump> ... "info proc exe"`.
  gdb can print this even without having the executable and symbols.

  Once we have the binary, we can get the location of the modules with
  `gdb ... "print ast_config_AST_MODULE_DIR`

  If there was no result then either it's not an asterisk coredump
  or there were no symbols loaded.  Either way, it's not usable.

  For libdir, we now run "strings" on the note0 section of the
  coredump (which has the shared library -> memory address xref) and
  search for "libasteriskssl|libasteriskpj", then take the dirname.

  Since we're now getting everything from the coredump, it has to be
  correct as long as we're not crossing namespace boundaries like
  running asterisk in a docker container but trying to run
  ast_coredumper from the host using a shared file system (which you
  shouldn't be doing).

  There is still a case for using --asterisk-bin and/or --libdir: If
  you've updated asterisk since the coredump was taken, the binary,
  libraries and modules won't match the coredump which will render it
  useless.  If you can restore or rebuild the original files that
  match the coredump and place them in a temporary directory, you can
  use --asterisk-bin, --libdir, and a new --moddir option to point to
  them and they'll be correctly captured in a tarball created
  with --tarball-coredumps.  If you also use --tarball-config, you can
  use a new --etcdir option to point to what normally would be the
  /etc/asterisk directory.

  Also addressed many "shellcheck" findings.

  Resolves: #445

- ### logger.c: Move LOG_GROUP documentation to dedicated XML file.
  Author: Sean Bright  
  Date:   2023-12-01  

  The `get_documentation` awk script will only extract the first
  DOCUMENTATION block that it finds in a given file. This is by design
  (9bc2127) to prevent AMI event documentation from being pulled in to
  the core.xml documentation file.

  Because of this, the `LOG_GROUP` documentation added in 89709e2 was
  not being properly extracted and was missing fom the resulting XML
  documentation file. This commit moves the `LOG_GROUP` documentation to
  a separate `logger.xml` file.


- ### res_odbc.c: Allow concurrent access to request odbc connections
  Author: Matthew Fredrickson  
  Date:   2023-11-30  

  There are valid scenarios where res_odbc's connection pool might have some dead
  or stuck connections while others are healthy (imagine network
  elements/firewalls/routers silently timing out connections to a single DB and a
  single IP address, or a heterogeneous connection pool connected to potentially
  multiple IPs/instances of a replicated DB using a DNS front end for load
  balancing and one replica fails).

  In order to time out those unhealthy connections without blocking access to
  other parts of Asterisk that may attempt access to the connection pool, it would
  be beneficial to not lock/block access around the entire pool in
  _ast_odbc_request_obj2 while doing potentially blocking operations on connection
  pool objects such as the connection_dead() test, odbc_obj_connect(), or by
  dereferencing a struct odbc_obj for the last time and triggering a
  odbc_obj_disconnect().

  This would facilitate much quicker and concurrent timeout of dead connections
  via the connection_dead() test, which could block potentially for a long period
  of time depending on odbc.ini or other odbc connector specific timeout settings.

  This also would make rapid failover (in the clustered DB scenario) much quicker.

  This patch changes the locking in _ast_odbc_request_obj2() to not lock around
  odbc_obj_connect(), _disconnect(), and connection_dead(), while continuing to
  lock around truly shared, non-immutable state like the connection_cnt member and
  the connections list on struct odbc_class.

  Fixes: #465

- ### res_pjsip_header_funcs.c: Check URI parameter length before copying.
  Author: Sean Bright  
  Date:   2023-12-04  

  Fixes #477


- ### config.c: Log #exec include failures.
  Author: Sean Bright  
  Date:   2023-11-22  

  If the script referenced by `#exec` does not exist, writes anything to
  stderr, or exits abnormally or with a non-zero exit status, we log
  that to Asterisk's error logging channel.

  Additionally, write out a warning if the script produces no output.

  Fixes #259


- ### make_xml_documentation: Properly handle absolute LOCAL_MOD_SUBDIRS.
  Author: Sean Bright  
  Date:   2023-11-27  

  If LOCAL_MOD_SUBDIRS contains absolute paths, do not prefix them with
  the path to Asterisk's source tree.

  Fixes #142


- ### app_voicemail.c: Completely resequence mailbox folders.
  Author: Sean Bright  
  Date:   2023-11-27  

  Resequencing is a process that occurs when we open a voicemail folder
  and discover that there are gaps between messages (e.g. `msg0000.txt`
  is missing but `msg0001.txt` exists). Resequencing involves shifting
  the existing messages down so we end up with a sequential list of
  messages.

  Currently, this process stops after reaching a threshold based on the
  message limit (`maxmsg`) configured on the current folder. However, if
  `maxmsg` is lowered when a voicemail folder contains more than
  `maxmsg + 10` messages, resequencing will not run completely leaving
  the mailbox in an inconsistent state.

  We now resequence up to the maximum number of messages permitted by
  `app_voicemail` (currently hard-coded at 9999 messages).

  Fixes #86


- ### sig_analog: Fix channel leak when mwimonitor is enabled.
  Author: Naveen Albert  
  Date:   2023-11-24  

  When mwimonitor=yes is enabled for an FXO port,
  the do_monitor thread will launch mwi_thread if it thinks
  there could be MWI on an FXO channel, due to the noise
  threshold being satisfied. This, in turns, calls
  analog_ss_thread_start in sig_analog. However, unlike
  all other instances where __analog_ss_thread is called
  in sig_analog, this call path does not properly set
  pvt->ss_astchan to the Asterisk channel, which means
  that the Asterisk channel is NULL when __analog_ss_thread
  starts executing. As a result, the thread exits and the
  channel is never properly cleaned up by calling ast_hangup.

  This caused issues with do_monitor on incoming calls,
  as it would think the channel was still owned even while
  receiving events, leading to an infinite barrage of
  warning messages; additionally, the channel would persist
  improperly.

  To fix this, the assignment is added to the call path
  where it is missing (which is only used for mwi_thread).
  A warning message is also added since previously there
  was no indication that __analog_ss_thread was exiting
  abnormally. This resolves both the channel leak and the
  condition that led to the warning messages.

  Resolves: #458

- ### res_rtp_asterisk.c: Update for OpenSSL 3+.
  Author: Sean Bright  
  Date:   2023-11-20  

  In 5ac5c2b0 we defined `OPENSSL_SUPPRESS_DEPRECATED` to silence
  deprecation warnings. This commit switches over to using
  non-deprecated API.


- ### alembic: Update list of TLS methods available on ps_transports.
  Author: Sean Bright  
  Date:   2023-11-14  

  Related to #221 and #222.

  Also adds `*.ini` to the `.gitignore` file in ast-db-manage for
  convenience.


- ### func_channel: Expose previously unsettable options.
  Author: Naveen Albert  
  Date:   2023-11-11  

  Certain channel options are not set anywhere or
  exposed in any way to users, making them unusable.
  This exposes some of these options which make sense
  for users to manipulate at runtime.

  Resolves: #442

- ### app.c: Allow ampersands in playback lists to be escaped.
  Author: Sean Bright  
  Date:   2023-11-07  

  Any function or application that accepts a `&`-separated list of
  filenames can now include a literal `&` in a filename by wrapping the
  entire filename in single quotes, e.g.:

  ```
  exten = _X.,n,Playback('https://example.com/sound.cgi?a=b&c=d'&hello-world)
  ```

  Fixes #172

  UpgradeNote: Ampersands in URLs passed to the `Playback()`,
  `Background()`, `SpeechBackground()`, `Read()`, `Authenticate()`, or
  `Queue()` applications as filename arguments can now be escaped by
  single quoting the filename. Additionally, this is also possible when
  using the `CONFBRIDGE` dialplan function, or configuring various
  features in `confbridge.conf` and `queues.conf`.


- ### uri.c: Simplify ast_uri_make_host_with_port()
  Author: Sean Bright  
  Date:   2023-11-09  


- ### func_curl.c: Remove CURLOPT() plaintext documentation.
  Author: Sean Bright  
  Date:   2023-11-13  

  I assume this was missed when initially converting to XML
  documentation and we've been kicking the can down the road since.


- ### res_http_websocket.c: Set hostname on client for certificate validation.
  Author: Sean Bright  
  Date:   2023-11-09  

  Additionally add a `assert()` to in the TLS client setup code to
  ensure that hostname is set when it is supposed to be.

  Fixes #433


- ### live_ast: Add astcachedir to generated asterisk.conf.
  Author: Sean Bright  
  Date:   2023-11-09  

  `astcachedir` (added in b0842713) was not added to `live_ast` so
  continued to point to the system `/var/cache` directory instead of the
  one in the live environment.


- ### SECURITY.md: Update with correct documentation URL
  Author: George Joseph  
  Date:   2023-11-09  


- ### func_lock: Add missing see-also refs to documentation.
  Author: Naveen Albert  
  Date:   2023-11-09  

  Resolves: #423

- ### app_followme.c: Grab reference on nativeformats before using it
  Author: Matthew Fredrickson  
  Date:   2023-10-25  

  Fixes a crash due to a lack of proper reference on the nativeformats
  object before passing it into ast_request().  Also found potentially
  similar use case bugs in app_chanisavail.c, bridge.c, and bridge_basic.c

  Fixes: #388

- ### configs: Improve documentation for bandwidth in iax.conf.
  Author: Naveen Albert  
  Date:   2023-11-09  

  This improves the documentation for the bandwidth setting
  in iax.conf by making it clearer what the ramifications
  of this setting are. It also changes the sample default
  from low to high, since only high is compatible with good
  codecs that people will want to use in the vast majority
  of cases, and this is a common gotcha that trips up new users.

  Resolves: #425

- ### logger: Add channel-based filtering.
  Author: Naveen Albert  
  Date:   2023-08-09  

  This adds the ability to filter console
  logging by channel or groups of channels.
  This can be useful on busy systems where
  an administrator would like to analyze certain
  calls in detail. A dialplan function is also
  included for the purpose of assigning a channel
  to a group (e.g. by tenant, or some other metric).

  ASTERISK-30483 #close

  Resolves: #242

  UserNote: The console log can now be filtered by
  channels or groups of channels, using the
  logger filter CLI commands.


- ### chan_iax2.c: Don't send unsanitized data to the logger.
  Author: Sean Bright  
  Date:   2023-11-08  

  This resolves an issue where non-printable characters could be sent to
  the console/log files.


- ### codec_ilbc: Disable system ilbc if version >= 3.0.0
  Author: George Joseph  
  Date:   2023-11-07  

  Fedora 37 started shipping ilbc 3.0.4 which we don't yet support.
  configure.ac now checks the system for "libilbc < 3" instead of
  just "libilbc".  If true, the system version of ilbc will be used.
  If not, the version included at codecs/ilbc will be used.

  Resolves: #84

- ### resource_channels.c: Explicit codec request when creating UnicastRTP.
  Author: Sean Bright  
  Date:   2023-11-06  

  Fixes #394


- ### doc: Update IP Quality of Service links.
  Author: Sean Bright  
  Date:   2023-11-07  

  Fixes #328


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


- ### chan_iax2.c: Ensure all IEs are displayed when dumping frame contents.
  Author: Sean Bright  
  Date:   2023-11-06  

  When IAX2 debugging was enabled (`iax2 set debug on`), if the last IE
  in a frame was one that may not have any data - such as the CALLTOKEN
  IE in an NEW request - it was not getting displayed.


- ### chan_dahdi: Warn if nonexistent cadence is requested.
  Author: Naveen Albert  
  Date:   2023-11-02  

  If attempting to ring a channel using a nonexistent cadence,
  emit a warning, before falling back to the default cadence.

  Resolves: #409

- ### stasis: Update the snapshot after setting the redirect
  Author: Holger Hans Peter Freyther  
  Date:   2023-10-21  

  The previous commit added the caller_rdnis attribute. Make it
  avialble during a possible ChanngelHangupRequest.


- ### ari: Provide the caller ID RDNIS for the channels
  Author: Holger Hans Peter Freyther  
  Date:   2023-10-14  

  Provide the caller ID RDNIS when available. This will allow an
  application to follow the redirect.


- ### main/utils: Implement ast_get_tid() for OpenBSD
  Author: Brad Smith  
  Date:   2023-11-01  

  Implement the ast_get_tid() function for OpenBSD. OpenBSD supports
  getting the TID via getthrid().


- ### res_rtp_asterisk.c: Fix runtime issue with LibreSSL
  Author: Brad Smith  
  Date:   2023-11-02  

  The module will fail to load. Use proper function DTLS_method() with LibreSSL.


- ### app_directory: Add ADSI support to Directory.
  Author: Naveen Albert  
  Date:   2023-09-27  

  This adds optional ADSI support to the Directory
  application, which allows callers with ADSI CPE
  to navigate the Directory system significantly
  faster than is possible using the audio prompts.
  Callers can see the directory name (and optionally
  extension) on their screenphone and confirm or
  reject a match immediately rather than waiting
  for it to be spelled out, enhancing usability.

  Resolves: #356

- ### core_local: Fix local channel parsing with slashes.
  Author: Naveen Albert  
  Date:   2023-08-09  

  Currently, trying to call a Local channel with a slash
  in the extension will fail due to the parsing of characters
  after such a slash as being dial modifiers. Additionally,
  core_local is inconsistent and incomplete with
  its parsing of Local dial strings in that sometimes it
  uses the first slash and at other times it uses the last.

  For instance, something like DAHDI/5 or PJSIP/device
  is a perfectly usable extension in the dialplan, but Local
  channels in particular prevent these from being called.

  This creates inconsistent behavior for users, since using
  a slash in an extension is perfectly acceptable, and using
  a Goto to accomplish this works fine, but if specified
  through a Local channel, the parsing prevents this.

  This fixes this by explicitly parsing options from the
  last slash in the extension, rather than the first one,
  which doesn't cause an issue for extensions with slashes.

  ASTERISK-30013 #close

  Resolves: #248

- ### Remove files that are no longer updated
  Author: Mark Murawski  
  Date:   2023-10-30  

  Fixes: #360

- ### app_voicemail: Add AMI event for mailbox PIN changes.
  Author: Naveen Albert  
  Date:   2023-10-30  

  This adds an AMI event that is emitted whenever a
  mailbox password is successfully changed, allowing
  AMI consumers to process these.

  UserNote: The VoicemailPasswordChange event is
  now emitted whenever a mailbox password is updated,
  containing the mailbox information and the new
  password.

  Resolves: #398

- ### app_queue.c: Emit unpause reason with PauseQueueMember event.
  Author: Sean Bright  
  Date:   2023-10-30  

  Fixes #395


- ### bridge_simple: Suppress unchanged topology change requests
  Author: George Joseph  
  Date:   2023-10-30  

  In simple_bridge_join, we were sending topology change requests
  even when the new and old topologies were the same.  In some
  circumstances, this can cause unnecessary re-invites and even
  a re-invite flood.  We now suppress those.

  Resolves: #384

- ### res_pjsip: Include cipher limit in config error message.
  Author: Naveen Albert  
  Date:   2023-10-30  

  If too many ciphers are specified in the PJSIP config,
  include the maximum number of ciphers that may be
  specified in the user-facing error message.

  Resolves: #396

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


- ### res_rtp_asterisk.c: Fix memory leak in ephemeral certificate creation.
  Author: Sean Bright  
  Date:   2023-10-25  

  Fixes #386


- ### res_pjsip_dtmf_info.c: Add 'INFO' to Allow header.
  Author: Sean Bright  
  Date:   2023-10-17  

  Fixes #376


- ### api.wiki.mustache: Fix indentation in generated markdown
  Author: George Joseph  
  Date:   2023-10-25  

  The '*' list indicator for default values and allowable values for
  path, query and POST parameters need to be indented 4 spaces
  instead of 2.

  Should resolve issue 38 in the documentation repo.


- ### pjsip_configuration.c: Disable DTLS renegotiation if WebRTC is enabled.
  Author: Sean Bright  
  Date:   2023-10-23  

  Per RFC8827:

      Implementations MUST NOT implement DTLS renegotiation and MUST
      reject it with a "no_renegotiation" alert if offered.

  So we disable it when webrtc=yes is set.

  Fixes #378

  UpgradeNote: The dtls_rekey will be disabled if webrtc support is
  requested on an endpoint. A warning will also be emitted.


- ### configs: Fix typo in pjsip.conf.sample.
  Author: Samuel Olaechea  
  Date:   2023-10-12  


- ### res_pjsip_exten_state,res_pjsip_mwi: Allow unload on shutdown
  Author: George Joseph  
  Date:   2023-10-19  

  Commit f66f77f last year prevents the res_pjsip_exten_state and
  res_pjsip_mwi modules from unloading due to possible pjproject
  asserts if the modules are reloaded. A side effect of the
  implementation is that the taskprocessors these modules use aren't
  being released. When asterisk is doing a graceful shutdown, it
  waits AST_TASKPROCESSOR_SHUTDOWN_MAX_WAIT seconds for all
  taskprocessors to stop but since those 2 modules don't release
  theirs, the shutdown hangs for that amount of time.

  This change allows the modules to be unloaded and their resources to
  be released when ast_shutdown_final is true.

  Resolves: #379

- ### res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 characters
  Author: sungtae kim  
  Date:   2023-09-23  

  This commit introduces an extension to the endpoint and relevant
  resource sizes for PJSIP, transitioning from its current 40-character
  constraint to a more versatile 255-character capacity. This enhancement
  significantly overcomes limitations related to domain qualification and
  practical usage, ultimately delivering improved functionality. In
  addition, it includes adjustments to accommodate the expanded realm size
  within the ARI, specifically enhancing the maximum realm length.

  Resolves: #345

  UserNote: With this update, the PJSIP realm lengths have been extended
  to support up to 255 characters.

  UpgradeNote: As part of this update, the maximum allowable length
  for PJSIP endpoints and relevant resources has been increased from
  40 to 255 characters. To take advantage of this enhancement, it is
  recommended to run the necessary procedures (e.g., Alembic) to
  update your schemas.


- ### .github: PRSubmitActions: Fix adding reviewers to PR
  Author: George Joseph  
  Date:   2023-10-19  


- ### .github: New PR Submit workflows
  Author: George Joseph  
  Date:   2023-10-17  

  The workflows that get triggered when PRs are submitted or updated
  have been replaced with ones that are more secure and have
  a higher level of parallelism.


- ### .github: New PR Submit workflows
  Author: George Joseph  
  Date:   2023-10-17  

  The workflows that get triggered when PRs are submitted or updated
  have been replaced with ones that are more secure and have
  a higher level of parallelism.


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


- ### ari/stasis: Indicate progress before playback on a bridge
  Author: Holger Hans Peter Freyther  
  Date:   2023-10-02  

  Make it possible to start a playback and the calling party
  to receive audio on a bridge before the call is connected.

  Model the implementation after play_on_channel and deliver a
  AST_CONTROL_PROGRESS before starting the playback.

  For a PJSIP channel this will result in sending a SIP 183
  Session Progress.


- ### func_curl.c: Ensure channel is locked when manipulating datastores.
  Author: Sean Bright  
  Date:   2023-10-09  


- ### .github: Fix job prereqs in PROpenedUpdated
  Author: George Joseph  
  Date:   2023-10-09  


- ### .github: Block PR tests until approved
  Author: George Joseph  
  Date:   2023-10-05  


- ### .github: Use generic releaser
  Author: George Joseph  
  Date:   2023-08-15  


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

- ### chan_dahdi: Clarify scope of callgroup/pickupgroup.
  Author: Naveen Albert  
  Date:   2023-09-04  

  Internally, chan_dahdi only applies callgroup and
  pickupgroup to FXO signalled channels, but this is
  not documented anywhere. This is now documented in
  the sample config, and a warning is emitted if a
  user tries configuring these settings for channel
  types that do not support these settings, since they
  will not have any effect.

  Resolves: #294

- ### func_json: Fix crashes for some types
  Author: Bastian Triller  
  Date:   2023-09-21  

  This commit fixes crashes in JSON_DECODE() for types null, true, false
  and real numbers.

  In addition it ensures that a path is not deeper than 32 levels.

  Also allow root object to be an array.

  Add unit tests for above cases.


- ### res_speech_aeap: add aeap error handling
  Author: Mike Bradeen  
  Date:   2023-09-21  

  res_speech_aeap previously did not register an error handler
  with aeap, so it was not notified of a disconnect. This resulted
  in SpeechBackground never exiting upon a websocket disconnect.

  Resolves: #303

- ### app_voicemail: Disable ADSI if unavailable.
  Author: Naveen Albert  
  Date:   2023-09-27  

  If ADSI is available on a channel, app_voicemail will repeatedly
  try to use ADSI, even if there is no CPE that supports it. This
  leads to many unnecessary delays during the session. If ADSI is
  available but ADSI setup fails, we now disable it to prevent
  further attempts to use ADSI during the session.

  Resolves: #354

- ### codec_builtin: Use multiples of 20 for maximum_ms
  Author: Eduardo  
  Date:   2023-07-28  

  Some providers require a multiple of 20 for the maxptime or fail to complete calls,
  e.g. Vivo in Brazil. To increase compatibility, only multiples of 20 are now used.

  Resolves: #260

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


- ### asterisk.c: Use the euid's home directory to read/write cli history
  Author: George Joseph  
  Date:   2023-09-15  

  The CLI .asterisk_history file is read from/written to the directory
  specified by the HOME environment variable. If the root user starts
  asterisk with the -U/-G options, or with runuser/rungroup set in
  asterisk.conf, the asterisk process is started as root but then it
  calls setuid/setgid to set the new user/group. This does NOT reset
  the HOME environment variable to the new user's home directory
  though so it's still left as "/root". In this case, the new user
  will almost certainly NOT have access to read from or write to the
  history file.

  * Added function process_histfile() which calls
    getpwuid(geteuid()) and uses pw->dir as the home directory
    instead of the HOME environment variable.
  * ast_el_read_default_histfile() and ast_el_write_default_histfile()
    have been modified to use the new process_histfile()
    function.

  Resolves: #337

- ### res_pjsip_transport_websocket: Prevent transport from being destroyed before message finishes.
  Author: Tinet-mucw  
  Date:   2023-09-13  

  From the gdb information, ast_websocket_read reads a message successfully,
  then transport_read is called in the serializer. During execution of pjsip_transport_down,
  ws_session->stream->fd is closed; ast_websocket_read encounters an error and exits the while loop.
  After executing transport_shutdown, the transport's reference count becomes 0, causing a crash when sending SIP messages.
  This was due to pjsip_transport_dec_ref executing earlier than pjsip_rx_data_clone, leading to this issue.
  In websocket_cb executeing pjsip_transport_add_ref, this we now ensure the transport is not destroyed while in the loop.

  Resolves: asterisk#299

- ### cel: add publish user event helper
  Author: Mike Bradeen  
  Date:   2023-09-14  

  Add a wrapper function around ast_cel_publish_event that
  packs event and extras into a blob before publishing

  Resolves:#330

- ### chan_console: Fix deadlock caused by unclean thread exit.
  Author: Naveen Albert  
  Date:   2023-09-09  

  To terminate a console channel, stop_stream causes pthread_cancel
  to make stream_monitor exit. However, commit 5b8fea93d106332bc0faa4b7fa8a6ea71e546cac
  added locking to this function which results in deadlock due to
  the stream_monitor thread being killed while it's holding the pvt lock.

  To resolve this, a flag is now set and read to indicate abort, so
  the use of pthread_cancel and pthread_kill can be avoided altogether.

  Resolves: #308

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


- ### chan_iax2: Improve authentication debugging.
  Author: Naveen Albert  
  Date:   2023-08-30  

  Improves and adds some logging to make it easier
  for users to debug authentication issues.

  Resolves: #286

- ### res_rtp_asterisk: fix wrong counter management in ioqueue objects
  Author: Vitezslav Novy  
  Date:   2023-09-05  

  In function  rtp_ioqueue_thread_remove counter in ioqueue object is not decreased
  which prevents unused ICE TURN threads from being removed.

  Resolves: #301

- ### res_pjsip_pubsub: Add body_type to test_handler for unit tests
  Author: George Joseph  
  Date:   2023-09-15  

  The ast_sip_subscription_handler "test_handler" used for the unit
  tests didn't set "body_type" so the NULL value was causing
  a SEGV in build_subscription_tree().  It's now set to "".

  Resolves: #335

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

- ### res_stasis_recording.c: Save recording state when unmuted.
  Author: Sean Bright  
  Date:   2023-09-12  

  Fixes #322


- ### res_speech_aeap: check for null format on response
  Author: Mike Bradeen  
  Date:   2023-09-08  

  * Fixed issue in res_speech_aeap when unable to provide an
    input format to check against.


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

- ### chan_rtp: Implement RTP glue for UnicastRTP channels
  Author: Maximilian Fridrich  
  Date:   2023-09-05  

  Resolves: #298

  UserNote: The dial string option 'g' was added to the UnicastRTP channel
  which enables RTP glue and therefore native RTP bridges with those
  channels.


- ### app_queue: periodic announcement configurable start time.
  Author: Jaco Kroon  
  Date:   2023-02-21  

  This newly introduced periodic-announce-startdelay makes it possible to
  configure the initial start delay of the first periodic announcement
  after which periodic-announce-frequency takes over.

  UserNote: Introduce a new queue configuration option called
  'periodic-announce-startdelay' which will vary the normal (historic)
  behavior of starting the periodic announcement cycle at
  periodic-announce-frequency seconds after entering the queue to start
  the periodic announcement cycle at period-announce-startdelay seconds
  after joining the queue.  The default behavior if this config option is
  not set remains unchanged.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

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


- ### Restore CHANGES and UPGRADE.txt to allow cherry-picks to work
  Author: George Joseph  
  Date:   2024-01-12  


