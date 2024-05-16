
## Change Log for Release asterisk-20.8.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.8.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.7.0...20.8.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.8.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 44
- Commit Authors: 15
- Issues Resolved: 26
- Security Advisories Resolved: 0

### User Notes:

- #### res_pjsip_logger: Preserve logging state on reloads.                            
  Issuing "pjsip reload" will no longer disable
  logging if it was previously enabled from the CLI.

- #### loader.c: Allow dependent modules to be unloaded recursively.                   
  In certain circumstances, modules with dependency relations
  can have their dependents automatically recursively unloaded and loaded
  again using the "module refresh" CLI command or the ModuleLoad AMI command.

- #### tcptls/iostream:  Add support for setting SNI on client TLS connections         
  Secure websocket client connections now send SNI in
  the TLS client hello.

- #### res_pjsip_endpoint_identifier_ip: Add endpoint identifier transport address.    
  set identify_by=transport for the pjsip endpoint. Then
  use the existing 'match' option and the new 'transport' option of
  the identify.
  Fixes: #672

- #### res_pjsip_endpoint_identifier_ip: Endpoint identifier request URI               
  this new feature let users match endpoints based on the
  indound SIP requests' URI. To do so, add 'request_uri' to the
  endpoint's 'identify_by' option. The 'match_request_uri' option of
  the identify can be an exact match for the entire request uri, or a
  regular expression (between slashes). It's quite similar to the
  header identifer.
  Fixes: #599

- #### res_pjsip_refer.c: Allow GET_TRANSFERRER_DATA                                   
  the GET_TRANSFERRER_DATA dialplan variable can now be used also in pjsip.

- #### manager.c: Add new parameter 'PreDialGoSub' to Originate AMI action             
  When using the Originate AMI Action, we now can pass the PreDialGoSub parameter, instructing the asterisk to perform an subrouting at channel before call start. With this parameter an call initiated by AMI can request the channel to start the call automaticaly, adding a SIP header to using GoSUB, instructing to autoanswer the channel, and proceeding the outbuound extension executing. Exemple of an context to perform the previus indication:
  [addautoanswer]
  exten => _s,1,Set(PJSIP_HEADER(add,Call-Info)=answer-after=0)
  exten => _s,n,Set(PJSIP_HEADER(add,Alert-Info)=answer-after=0)
  exten => _s,n,Return()

- #### manager.c: Add CLI command to kick AMI sessions.                                
  The "manager kick session" CLI command now
  allows kicking a specified AMI session.

- #### chan_dahdi: Allow specifying waitfordialtone per call.                          
  "waitfordialtone" may now be specified for DAHDI
  trunk channels on a per-call basis using the CHANNEL function.

- #### Upgrade bundled pjproject to 2.14.1                                             
  Bundled pjproject has been upgraded to 2.14.1. For more
  information visit pjproject Github page: https://github.com/pjsip/pjproject/releases/tag/2.14.1


### Upgrade Notes:

- #### pbx_variables.c: Prevent SEGV due to stack overflow.                            
  The maximum amount of dialplan recursion
  using variable substitution (such as by using EVAL_EXTEN)
  is capped at 15.


### Commit Authors:

- Fabrice Fontaine: (1)
- George Joseph: (8)
- Henrik Liljedahl: (1)
- Holger Hans Peter Freyther: (1)
- Ivan Poddubny: (2)
- Jonatascalebe: (1)
- Joshua Elson: (1)
- Martin Nystroem: (1)
- Martin Tomec: (1)
- Maximilian Fridrich: (1)
- Naveen Albert: (14)
- Sean Bright: (8)
- Sperl Viktor: (2)
- Spiridonov Dmitry: (1)
- Stanislav Abramenkov: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 246: [bug]: res_pjsip_logger: Reload disables logging
  - 472: [new-feature]: chan_dahdi: Allow waitfordialtone to be specified per call
  - 474: [new-feature]: loader.c: Allow dependent modules to be unloaded automatically
  - 480: [improvement]: pbx_variables.c: Prevent infinite recursion and stack overflow with variable expansion
  - 485: [new-feature]: manager.c: Allow kicking specific manager sessions
  - 525: [bug]: say.c: Money announcements off by one cent due to floating point rounding
  - 579: [improvement]: Allow GET_TRANSFERRER_DATA for pjsip
  - 599: [improvement]: Endpoint identifier request line
  - 611: [bug]: res_pjsip_session: Polling on non-existing file descriptors when stream is removed
  - 624: [bug]: Park() application does not continue execution if lot is full
  - 642: [bug]: Prometheus bridge metrics contains duplicate entries and help
  - 666: [improvement]: ARI debug should contain endpoint and method
  - 669: [bug]: chan_dahdi: Tens or hundreds of thousands of channel opens attempted during restart
  - 672: [improvement]: Endpoint identifier transport
  - 673: [new-feature]: chan_dahdi: Add AMI action to show spans
  - 676: [bug]: res_stir_shaken implicit declaration of function errors/warnings
  - 681: [new-feature]: callerid.c: Parse all received parameters
  - 683: [improvement]: func_callerid: Warn if invalid redirecting reason is set
  - 689: [bug] Document the `Events` argument of the `Login` AMI action
  - 696: [bug]: Unexpected control subclass '14'
  - 713: [bug]: SNI isn't being set on websocket client connections
  - 716: [bug]: Memory leak in res_stir_shaken tn_config, plus a few other issues
  - 719: [bug]: segfault on start if compiled with DETECT_DEADLOCKS
  - 721: [improvement]: logger: Add unique verbose prefixes for higher verbose levels
  - 729: [bug]: Build failure with uclibc-ng
  - ASTERISK-29912: res_pjsip: module reload disables logging

### Commits By Author:

- ### Fabrice Fontaine (1):
  - res/stasis/control.c: include signal.h

- ### George Joseph (8):
  - Fix incorrect application and function documentation references
  - res_stir_shaken:  Fix compilation for CentOS7 (openssl 1.0.2)
  - manager.c: Add missing parameters to Login documentation
  - rtp_engine and stun: call ast_register_atexit instead of ast_register_cleanup
  - logger.h:  Add SCOPE_CALL and SCOPE_CALL_WITH_RESULT
  - make_buildopts_h: Always include DETECT_DEADLOCKS
  - stir_shaken:  Fix memory leak, typo in config, tn canonicalization
  - tcptls/iostream:  Add support for setting SNI on client TLS connections

- ### Henrik Liljedahl (1):
  - res_pjsip_sdp_rtp.c: Initial RTP inactivity check must consider the rtp_timeou..

- ### Holger Hans Peter Freyther (1):
  - res_prometheus: Fix duplicate output of metric and help text

- ### Ivan Poddubny (2):
  - asterisk.c: Fix sending incorrect messages to systemd notify
  - configs: Fix a misleading IPv6 ACL example in Named ACLs

- ### Joshua Elson (1):
  - Implement Configurable TCP Keepalive Settings in PJSIP Transports

- ### Martin Nystroem (1):
  - res_ari.c: Add additional output to ARI requests when debug is enabled

- ### Martin Tomec (1):
  - res_pjsip_refer.c: Allow GET_TRANSFERRER_DATA

- ### Maximilian Fridrich (1):
  - res_pjsip_session: Reset pending_media_state->read_callbacks

- ### Naveen Albert (14):
  - res_parking: Fail gracefully if parking lot is full.
  - chan_dahdi: Allow specifying waitfordialtone per call.
  - manager.c: Add CLI command to kick AMI sessions.
  - pbx_variables.c: Prevent SEGV due to stack overflow.
  - menuselect: Minor cosmetic fixes.
  - chan_dahdi: Don't retry opening nonexistent channels on restart.
  - chan_dahdi: Add DAHDIShowStatus AMI action.
  - func_callerid: Emit warning if invalid redirecting reason set.
  - file.c, channel.c: Don't emit warnings if progress received.
  - callerid.c: Parse previously ignored Caller ID parameters.
  - loader.c: Allow dependent modules to be unloaded recursively.
  - say.c: Fix cents off-by-one due to floating point rounding.
  - logger: Add unique verbose prefixes for levels 5-10.
  - res_pjsip_logger: Preserve logging state on reloads.

- ### Sean Bright (8):
  - res_monitor.c: Don't emit a warning about 'X' being unrecognized.
  - alembic: Quote new MySQL keyword 'qualify.'
  - res_pjsip: Fix alembic downgrade for boolean columns.
  - res_config_mysql.c: Support hostnames up to 255 bytes.
  - alembic: Fix compatibility with SQLAlchemy 2.0+.
  - cli.c: `core show channels concise` is not really deprecated.
  - alembic: Correct NULLability of PJSIP id columns.
  - app_queue.c: Properly handle invalid strategies from realtime.

- ### Sperl Viktor (2):
  - res_pjsip_endpoint_identifier_ip: Endpoint identifier request URI
  - res_pjsip_endpoint_identifier_ip: Add endpoint identifier transport address.

- ### Spiridonov Dmitry (1):
  - sorcery.c: Fixed crash error when executing "module reload"

- ### Stanislav Abramenkov (1):
  - Upgrade bundled pjproject to 2.14.1

- ### jonatascalebe (1):
  - manager.c: Add new parameter 'PreDialGoSub' to Originate AMI action


### Commit List:

-  configs: Fix a misleading IPv6 ACL example in Named ACLs
-  asterisk.c: Fix sending incorrect messages to systemd notify
-  res/stasis/control.c: include signal.h
-  res_pjsip_logger: Preserve logging state on reloads.
-  logger: Add unique verbose prefixes for levels 5-10.
-  say.c: Fix cents off-by-one due to floating point rounding.
-  loader.c: Allow dependent modules to be unloaded recursively.
-  tcptls/iostream:  Add support for setting SNI on client TLS connections
-  stir_shaken:  Fix memory leak, typo in config, tn canonicalization
-  make_buildopts_h: Always include DETECT_DEADLOCKS
-  sorcery.c: Fixed crash error when executing "module reload"
-  callerid.c: Parse previously ignored Caller ID parameters.
-  logger.h:  Add SCOPE_CALL and SCOPE_CALL_WITH_RESULT
-  app_queue.c: Properly handle invalid strategies from realtime.
-  file.c, channel.c: Don't emit warnings if progress received.
-  alembic: Correct NULLability of PJSIP id columns.
-  rtp_engine and stun: call ast_register_atexit instead of ast_register_cleanup
-  manager.c: Add missing parameters to Login documentation
-  func_callerid: Emit warning if invalid redirecting reason set.
-  chan_dahdi: Add DAHDIShowStatus AMI action.
-  res_pjsip_endpoint_identifier_ip: Add endpoint identifier transport address.
-  res_stir_shaken:  Fix compilation for CentOS7 (openssl 1.0.2)
-  Fix incorrect application and function documentation references
-  cli.c: `core show channels concise` is not really deprecated.
-  res_pjsip_endpoint_identifier_ip: Endpoint identifier request URI
-  chan_dahdi: Don't retry opening nonexistent channels on restart.
-  Implement Configurable TCP Keepalive Settings in PJSIP Transports
-  res_pjsip_refer.c: Allow GET_TRANSFERRER_DATA
-  res_ari.c: Add additional output to ARI requests when debug is enabled
-  alembic: Fix compatibility with SQLAlchemy 2.0+.
-  manager.c: Add new parameter 'PreDialGoSub' to Originate AMI action
-  menuselect: Minor cosmetic fixes.
-  pbx_variables.c: Prevent SEGV due to stack overflow.
-  res_prometheus: Fix duplicate output of metric and help text
-  manager.c: Add CLI command to kick AMI sessions.
-  chan_dahdi: Allow specifying waitfordialtone per call.
-  res_parking: Fail gracefully if parking lot is full.
-  res_config_mysql.c: Support hostnames up to 255 bytes.
-  res_pjsip: Fix alembic downgrade for boolean columns.
-  Upgrade bundled pjproject to 2.14.1
-  alembic: Quote new MySQL keyword 'qualify.'
-  res_pjsip_session: Reset pending_media_state->read_callbacks
-  res_monitor.c: Don't emit a warning about 'X' being unrecognized.

### Commit Details:

#### configs: Fix a misleading IPv6 ACL example in Named ACLs
  Author: Ivan Poddubny
  Date:   2024-05-05

  "deny=::" is equivalent to "::/128".
  In order to mean "deny everything by default" it must be "::/0".


#### asterisk.c: Fix sending incorrect messages to systemd notify
  Author: Ivan Poddubny
  Date:   2024-05-05

  Send "RELOADING=1" instead of "RELOAD=1" to follow the format
  expected by systemd (see sd_notify(3) man page).

  Do not send STOPPING=1 in remote console mode:
  attempting to execute "asterisk -rx" by the main process leads to
  a warning if NotifyAccess=main (the default) or to a forced termination
  if NotifyAccess=all.


#### res/stasis/control.c: include signal.h
  Author: Fabrice Fontaine
  Date:   2024-05-01

  Include signal.h to avoid the following build failure with uclibc-ng
  raised since
  https://github.com/asterisk/asterisk/commit/2694792e13c7f3ab1911c4a69fba0df32c544177:

  stasis/control.c: In function 'exec_command_on_condition':
  stasis/control.c:313:3: warning: implicit declaration of function 'pthread_kill'; did you mean 'pthread_yield'? [-Wimplicit-function-declaration]
    313 |   pthread_kill(control->control_thread, SIGURG);
        |   ^~~~~~~~~~~~
        |   pthread_yield
  stasis/control.c:313:41: error: 'SIGURG' undeclared (first use in this function)
    313 |   pthread_kill(control->control_thread, SIGURG);
        |                                         ^~~~~~

  cherry-pick-to: 18
  cherry-pick-to: 20
  cherry-pick-to: 21

  Fixes: #729

#### res_pjsip_logger: Preserve logging state on reloads.
  Author: Naveen Albert
  Date:   2023-08-09

  Currently, reloading res_pjsip will cause logging
  to be disabled. This is because logging can also
  be controlled via the debug option in pjsip.conf
  and this defaults to "no".

  To improve this, logging is no longer disabled on
  reloads if logging had not been previously
  enabled using the debug option from the config.
  This ensures that logging enabled from the CLI
  will persist through a reload.

  ASTERISK-29912 #close

  Resolves: #246

  UserNote: Issuing "pjsip reload" will no longer disable
  logging if it was previously enabled from the CLI.


#### logger: Add unique verbose prefixes for levels 5-10.
  Author: Naveen Albert
  Date:   2024-04-27

  Add unique verbose prefixes for levels higher than 4, so
  that these can be visually differentiated from each other.

  Resolves: #721

#### say.c: Fix cents off-by-one due to floating point rounding.
  Author: Naveen Albert
  Date:   2024-01-10

  Some of the money announcements can be off by one cent,
  due to the use of floating point in the money calculations,
  which is bad for obvious reasons.

  This replaces floating point with simple string parsing
  to ensure the cents value is converted accurately.

  Resolves: #525

#### loader.c: Allow dependent modules to be unloaded recursively.
  Author: Naveen Albert
  Date:   2023-12-02

  Because of the (often recursive) nature of module dependencies in
  Asterisk, hot swapping a module on the fly is cumbersome if a module
  is depended on by other modules. Currently, dependencies must be
  popped manually by unloading dependents, unloading the module of
  interest, and then loading modules again in reverse order.

  To make this easier, the ability to do this recursively in certain
  circumstances has been added, as an optional extension to the
  "module refresh" command. If requested, Asterisk will check if a module
  that has a positive usecount could be unloaded safely if anything
  recursively dependent on it were unloaded. If so, it will go ahead
  and unload all these modules and load them back again. This makes
  hot swapping modules that provide dependencies much easier.

  Resolves: #474

  UserNote: In certain circumstances, modules with dependency relations
  can have their dependents automatically recursively unloaded and loaded
  again using the "module refresh" CLI command or the ModuleLoad AMI command.


#### res_pjsip_sdp_rtp.c: Initial RTP inactivity check must consider the rtp_timeou..
  Author: Henrik Liljedahl
  Date:   2024-04-11

  First rtp activity check was performed after 500ms regardless of the rtp_timeout setting. Having a call in ringing state for more than rtp_timeout and the first rtp package is received more than 500ms after sdp negotiation and before the rtp_timeout, erronously caused the call to be hungup. Changed to perform the first rtp inactivity check after the timeout setting preventing calls to be disconnected before the rtp_timeout has elapsed since sdp negotiation.

  Fixes #710


#### tcptls/iostream:  Add support for setting SNI on client TLS connections
  Author: George Joseph
  Date:   2024-04-23

  If the hostname field of the ast_tcptls_session_args structure is
  set (which it is for websocket client connections), that hostname
  will now automatically be used in an SNI TLS extension in the client
  hello.

  Resolves: #713

  UserNote: Secure websocket client connections now send SNI in
  the TLS client hello.


#### stir_shaken:  Fix memory leak, typo in config, tn canonicalization
  Author: George Joseph
  Date:   2024-04-25

  * Fixed possible memory leak in tn_config:tn_get_etn() where we
  weren't releasing etn if tn or eprofile were null.
  * We now canonicalize TNs before using them for lookups or adding
  them to Identity headers.
  * Fixed a typo in stir_shaken.conf.sample.

  Resolves: #716

#### make_buildopts_h: Always include DETECT_DEADLOCKS
  Author: George Joseph
  Date:   2024-04-27

  Since DETECT_DEADLOCKS is now split from DEBUG_THREADS, it must
  always be included in buildopts.h instead of only when
  ADD_CFLAGS_TO_BUILDOPTS_H is defined.  A SEGV will result otherwise.

  Resolves: #719

#### sorcery.c: Fixed crash error when executing "module reload"
  Author: Spiridonov Dmitry
  Date:   2024-04-14

  Fixed crash error when cli "module reload". The error appears when
  compiling with res_prometheus and using the sorcery memory cache for
  registrations


#### callerid.c: Parse previously ignored Caller ID parameters.
  Author: Naveen Albert
  Date:   2024-04-01

  Commit f2f397c1a8cc48913434ebb297f0ff50d96993db previously
  made it possible to send Caller ID parameters to FXS stations
  which, prior to that, could not be sent.

  This change is complementary in that we now handle receiving
  all these parameters on FXO lines and provide these up to
  the dialplan, via chan_dahdi. In particular:

  * If a redirecting reason is provided, the channel's redirecting
    reason is set. No redirecting number is set, since there is
    no parameter for this in the Caller ID protocol, but the reason
    can be checked to determine if and why a call was forwarded.
  * If the Call Qualifier parameter is received, the Call Qualifier
    variable is set.
  * Some comments have been added to explain why some of the code
    is the way it is, to assist other people looking at it.

  With this change, Asterisk's Caller ID implementation is now
  reasonably complete for both FXS and FXO operation.

  Resolves: #681

#### logger.h:  Add SCOPE_CALL and SCOPE_CALL_WITH_RESULT
  Author: George Joseph
  Date:   2024-04-09

  If you're tracing a large function that may call another function
  multiple times in different circumstances, it can be difficult to
  see from the trace output exactly which location that function
  was called from.  There's no good way to automatically determine
  the calling location.  SCOPE_CALL and SCOPE_CALL_WITH_RESULT
  simply print out a trace line before and after the call.

  The difference between SCOPE_CALL and SCOPE_CALL_WITH_RESULT is
  that SCOPE_CALL ignores the function's return value (if any) where
  SCOPE_CALL_WITH_RESULT allows you to specify the type of the
  function's return value so it can be assigned to a variable.
  SCOPE_CALL_WITH_INT_RESULT is just a wrapper for SCOPE_CALL_WITH_RESULT
  and the "int" return type.


#### app_queue.c: Properly handle invalid strategies from realtime.
  Author: Sean Bright
  Date:   2024-04-13

  The existing code sets the queue strategy to `ringall` but it is then
  immediately overwritten with an invalid one.

  Fixes #707


#### file.c, channel.c: Don't emit warnings if progress received.
  Author: Naveen Albert
  Date:   2024-04-09

  Silently ignore AST_CONTROL_PROGRESS where appropriate,
  as most control frames already are.

  Resolves: #696

#### alembic: Correct NULLability of PJSIP id columns.
  Author: Sean Bright
  Date:   2024-04-06

  Fixes #695


#### rtp_engine and stun: call ast_register_atexit instead of ast_register_cleanup
  Author: George Joseph
  Date:   2024-04-02

  rtp_engine.c and stun.c were calling ast_register_cleanup which
  is skipped if any loadable module can't be cleanly unloaded
  when asterisk shuts down.  Since this will always be the case,
  their cleanup functions never get run.  In a practical sense
  this makes no difference since asterisk is shutting down but if
  you're in development mode and trying to use the leak sanitizer,
  the leaks from both of those modules clutter up the output.


#### manager.c: Add missing parameters to Login documentation
  Author: George Joseph
  Date:   2024-04-03

  * Added the AuthType and Key parameters for MD5 authentication.

  * Added the Events parameter.

  Resolves: #689

#### func_callerid: Emit warning if invalid redirecting reason set.
  Author: Naveen Albert
  Date:   2024-04-01

  Emit a warning if REDIRECTING(reason) is set to an invalid
  reason, consistent with what happens when
  REDIRECTING(orig-reason) is set to an invalid reason.

  Resolves: #683

#### chan_dahdi: Add DAHDIShowStatus AMI action.
  Author: Naveen Albert
  Date:   2024-03-29

  * Add an AMI action to correspond to the "dahdi show status"
    command, allowing span information to be retrieved via AMI.
  * Show span number and sig type in "dahdi show channels".

  Resolves: #673

#### res_pjsip_endpoint_identifier_ip: Add endpoint identifier transport address.
  Author: Sperl Viktor
  Date:   2024-03-28

  Add a new identify_by option to res_pjsip_endpoint_identifier_ip
  called 'transport' this matches endpoints based on the bound
  ip address (local) instead of the 'ip' option, which matches on
  the source ip address (remote).

  UserNote: set identify_by=transport for the pjsip endpoint. Then
  use the existing 'match' option and the new 'transport' option of
  the identify.

  Fixes: #672

#### res_stir_shaken:  Fix compilation for CentOS7 (openssl 1.0.2)
  Author: George Joseph
  Date:   2024-04-01

  * OpenSSL 1.0.2 doesn't support X509_get0_pubkey so we now use
    X509_get_pubkey.  The difference is that X509_get_pubkey requires
    the caller to free the EVP_PKEY themselves so we now let
    RAII_VAR do that.
  * OpenSSL 1.0.2 doesn't support upreffing an X509_STORE so we now
    wrap it in an ao2 object.
  * OpenSSL 1.0.2 doesn't support X509_STORE_get0_objects to get all
    the certs from an X509_STORE and there's no easy way to polyfill
    it so the CLI commands that list profiles will show a "not
    supported" message instead of listing the certs in a store.

  Resolves: #676

#### Fix incorrect application and function documentation references
  Author: George Joseph
  Date:   2024-04-01

  There were a few references in the embedded documentation XML
  where the case didn't match or where the referenced app or function
  simply didn't exist any more.  These were causing 404 responses
  in docs.asterisk.org.


#### cli.c: `core show channels concise` is not really deprecated.
  Author: Sean Bright
  Date:   2024-04-01

  Fixes #675


#### res_pjsip_endpoint_identifier_ip: Endpoint identifier request URI
  Author: Sperl Viktor
  Date:   2024-03-28

  Add ability to match against PJSIP request URI.

  UserNote: this new feature let users match endpoints based on the
  indound SIP requests' URI. To do so, add 'request_uri' to the
  endpoint's 'identify_by' option. The 'match_request_uri' option of
  the identify can be an exact match for the entire request uri, or a
  regular expression (between slashes). It's quite similar to the
  header identifer.

  Fixes: #599

#### chan_dahdi: Don't retry opening nonexistent channels on restart.
  Author: Naveen Albert
  Date:   2024-03-26

  Commit 729cb1d390b136ccc696430aa5c68d60ea4028be added logic to retry
  opening DAHDI channels on "dahdi restart" if they failed initially,
  up to 1,000 times in a loop, to address cases where the channel was
  still in use. However, this retry loop does not use the actual error,
  which means chan_dahdi will also retry opening nonexistent channels
  1,000 times per channel, causing a flood of unnecessary warning logs
  for an operation that will never succeed, with tens or hundreds of
  thousands of open attempts being made.

  The original patch would have been more targeted if it only retried
  on the specific relevant error (likely EBUSY, although it's hard to
  say since the original issue is no longer available).

  To avoid the problem above while avoiding the possibility of breakage,
  this skips the retry logic if the error is ENXIO (No such device or
  address), since this will never succeed.

  Resolves: #669

#### Implement Configurable TCP Keepalive Settings in PJSIP Transports
  Author: Joshua Elson
  Date:   2024-03-18

  This commit introduces configurable TCP keepalive settings for both TCP and TLS transports. The changes allow for finer control over TCP connection keepalives, enhancing stability and reliability in environments prone to connection timeouts or where intermediate devices may prematurely close idle connections. This has proven necessary and has already been tested in production in several specialized environments where access to the underlying transport is unreliable in ways invisible to the operating system directly, so these keepalive and timeout mechanisms are necessary.

  Fixes #657


#### res_pjsip_refer.c: Allow GET_TRANSFERRER_DATA
  Author: Martin Tomec
  Date:   2024-02-06

  There was functionality in chan_sip to get REFER headers, with GET_TRANSFERRER_DATA variable. This commit implements the same functionality in pjsip, to ease transfer from chan_sip to pjsip.

  Fixes: #579

  UserNote: the GET_TRANSFERRER_DATA dialplan variable can now be used also in pjsip.

#### res_ari.c: Add additional output to ARI requests when debug is enabled
  Author: Martin Nystroem
  Date:   2024-03-22

  When ARI debug is enabled the logs will now output http method and the uri.

  Fixes: #666

#### alembic: Fix compatibility with SQLAlchemy 2.0+.
  Author: Sean Bright
  Date:   2024-03-20

  SQLAlchemy 2.0 changed the way that commits/rollbacks are handled
  causing the final `UPDATE` to our `alembic_version_<whatever>` tables
  to be rolled back instead of committed.

  We now use one connection to determine which
  `alembic_version_<whatever>` table to use and another to run the
  actual migrations. This prevents the erroneous rollback.

  This change is compatible with both SQLAlchemy 1.4 and 2.0.


#### manager.c: Add new parameter 'PreDialGoSub' to Originate AMI action
  Author: jonatascalebe
  Date:   2024-03-14

  manager.c: Add new parameter 'PreDialGoSub' to Originate AMI action

  The action originate does not has the ability to run an subroutine at initial channel, like the Aplication Originate. This update give this ability for de action originate too.

  For example, we can run a routine via Gosub on the channel to request an automatic answer, so the caller does not need to accept the call when using the originate command via manager, making the operation more efficient.

  UserNote: When using the Originate AMI Action, we now can pass the PreDialGoSub parameter, instructing the asterisk to perform an subrouting at channel before call start. With this parameter an call initiated by AMI can request the channel to start the call automaticaly, adding a SIP header to using GoSUB, instructing to autoanswer the channel, and proceeding the outbuound extension executing. Exemple of an context to perform the previus indication:
  [addautoanswer]
  exten => _s,1,Set(PJSIP_HEADER(add,Call-Info)=answer-after=0)
  exten => _s,n,Set(PJSIP_HEADER(add,Alert-Info)=answer-after=0)
  exten => _s,n,Return()


#### menuselect: Minor cosmetic fixes.
  Author: Naveen Albert
  Date:   2024-03-21

  Improve some of the formatting from
  dd3f17c699e320d6d30c94298d8db49573ba28da
  (#521).


#### pbx_variables.c: Prevent SEGV due to stack overflow.
  Author: Naveen Albert
  Date:   2023-12-04

  It is possible for dialplan to result in an infinite
  recursion of variable substitution, which eventually
  leads to stack overflow. If we detect this, abort
  substitution and log an error for the user to fix
  the broken dialplan.

  Resolves: #480

  UpgradeNote: The maximum amount of dialplan recursion
  using variable substitution (such as by using EVAL_EXTEN)
  is capped at 15.


#### res_prometheus: Fix duplicate output of metric and help text
  Author: Holger Hans Peter Freyther
  Date:   2024-02-24

  The prometheus exposition format requires each line to be unique[1].
  This is handled by struct prometheus_metric having a list of children
  that is managed when registering a metric. In case the scrape callback
  is used, it is the responsibility of the implementation to handle this
  correctly.

  Originally the bridge callback didn't handle NULL snapshots, the crash
  fix lead to NULL metrics, and fixing that lead to duplicates.

  The original code assumed that snapshots are not NULL and then relied on
  "if (i > 0)" to establish the parent/children relationship between
  metrics of the same class. This is not workerable as the first bridge
  might be invisible/lacks a snapshot.

  Fix this by keeping a separate array of the first metric by class.
  Instead of relying on the index of the bridge, check whether the array
  has an entry. Use that array for the output.

  Add a test case that verifies that the help text is not duplicated.

  Resolves: #642

  [1] https://prometheus.io/docs/instrumenting/exposition_formats/#grouping-and-sorting


#### manager.c: Add CLI command to kick AMI sessions.
  Author: Naveen Albert
  Date:   2023-12-06

  This adds a CLI command that can be used to manually
  kick specific AMI sessions.

  Resolves: #485

  UserNote: The "manager kick session" CLI command now
  allows kicking a specified AMI session.


#### chan_dahdi: Allow specifying waitfordialtone per call.
  Author: Naveen Albert
  Date:   2023-12-02

  The existing "waitfordialtone" setting in chan_dahdi.conf
  applies permanently to a specific channel, regardless of
  how it is being used. This rather restrictively prevents
  a system from simultaneously being able to pick free lines
  for outgoing calls while also allowing barge-in to a trunk
  by some other arrangement.

  This allows specifying "waitfordialtone" using the CHANNEL
  function for only the next call that will be placed, allowing
  significantly more flexibility in the use of trunk interfaces.

  Resolves: #472

  UserNote: "waitfordialtone" may now be specified for DAHDI
  trunk channels on a per-call basis using the CHANNEL function.


#### res_parking: Fail gracefully if parking lot is full.
  Author: Naveen Albert
  Date:   2024-03-03

  Currently, if a parking lot is full, bridge setup returns -1,
  causing dialplan execution to terminate without TryExec.
  However, such failures should be handled more gracefully,
  the same way they are on other paths, as indicated by the
  module's author, here:

  http://lists.digium.com/pipermail/asterisk-dev/2018-December/077144.html

  Now, callers will hear the parking failure announcement, and dialplan
  will continue, which is consistent with existing failure modes.

  Resolves: #624

#### res_config_mysql.c: Support hostnames up to 255 bytes.
  Author: Sean Bright
  Date:   2024-03-18

  Fixes #654


#### res_pjsip: Fix alembic downgrade for boolean columns.
  Author: Sean Bright
  Date:   2024-03-18

  When downgrading, ensure that we don't touch columns that didn't
  actually change during upgrade.


#### Upgrade bundled pjproject to 2.14.1
  Author: Stanislav Abramenkov
  Date:   2024-03-12

  Fixes: asterisk#648

  UserNote: Bundled pjproject has been upgraded to 2.14.1. For more
  information visit pjproject Github page: https://github.com/pjsip/pjproject/releases/tag/2.14.1


#### alembic: Quote new MySQL keyword 'qualify.'
  Author: Sean Bright
  Date:   2024-03-15

  Fixes #651


#### res_pjsip_session: Reset pending_media_state->read_callbacks
  Author: Maximilian Fridrich
  Date:   2024-02-15

  In handle_negotiated_sdp the pending_media_state->read_callbacks must be
  reset before they are added in the SDP handlers in
  handle_negotiated_sdp_session_media. Otherwise, old callbacks for
  removed streams and file descriptors could be added to the channel and
  Asterisk would poll on non-existing file descriptors.

  Resolves: #611

#### res_monitor.c: Don't emit a warning about 'X' being unrecognized.
  Author: Sean Bright
  Date:   2024-03-07

  Code was added in 030f7d41 to warn if an unrecognized option was
  passed to an application, but code in Monitor was taking advantage of
  the fact that the application would silently accept an invalid option.

  We now recognize the invalid option but we don't do anything if it's
  set.

  Fixes #639


