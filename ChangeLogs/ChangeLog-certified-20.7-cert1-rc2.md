
## Change Log for Release asterisk-certified-20.7-cert1-rc2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-20.7-cert1-rc2.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-20.7-cert1-rc1...certified-20.7-cert1-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-20.7-cert1-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 16
- Commit Authors: 6
- Issues Resolved: 7
- Security Advisories Resolved: 0

### User Notes:

- #### tcptls/iostream:  Add support for setting SNI on client TLS connections         
  Secure websocket client connections now send SNI in
  the TLS client hello.


### Upgrade Notes:

- #### pbx_variables.c: Prevent SEGV due to stack overflow.                            
  The maximum amount of dialplan recursion
  using variable substitution (such as by using EVAL_EXTEN)
  is capped at 15.


### Commit Authors:

- George Joseph: (8)
- Ivan Poddubny: (1)
- Martin Nystroem: (1)
- Naveen Albert: (1)
- Sean Bright: (4)
- Spiridonov Dmitry: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 480: [improvement]: pbx_variables.c: Prevent infinite recursion and stack overflow with variable expansion
  - 666: [improvement]: ARI debug should contain endpoint and method
  - 676: [bug]: res_stir_shaken implicit declaration of function errors/warnings
  - 689: [bug] Document the `Events` argument of the `Login` AMI action
  - 713: [bug]: SNI isn't being set on websocket client connections
  - 716: [bug]: Memory leak in res_stir_shaken tn_config, plus a few other issues
  - 719: [bug]: segfault on start if compiled with DETECT_DEADLOCKS

### Commits By Author:

- ### George Joseph (8):
  - Fix incorrect application and function documentation references
  - manager.c: Add missing parameters to Login documentation
  - rtp_engine and stun: call ast_register_atexit instead of ast_register_cleanup
  - make_buildopts_h: Always include DETECT_DEADLOCKS
  - tcptls/iostream:  Add support for setting SNI on client TLS connections
  - res_stir_shaken:  Fix compilation for CentOS7 (openssl 1.0.2)
  - logger.h:  Add SCOPE_CALL and SCOPE_CALL_WITH_RESULT
  - stir_shaken:  Fix memory leak, typo in config, tn canonicalization

- ### Ivan Poddubny (1):
  - asterisk.c: Fix sending incorrect messages to systemd notify

- ### Martin Nystroem (1):
  - res_ari.c: Add additional output to ARI requests when debug is enabled

- ### Naveen Albert (1):
  - pbx_variables.c: Prevent SEGV due to stack overflow.

- ### Sean Bright (4):
  - alembic: Correct NULLability of PJSIP id columns.
  - alembic: Quote new MySQL keyword 'qualify.'
  - res_pjsip: Fix alembic downgrade for boolean columns.
  - alembic: Fix compatibility with SQLAlchemy 2.0+.

- ### Spiridonov Dmitry (1):
  - sorcery.c: Fixed crash error when executing "module reload"


### Commit List:

-  stir_shaken:  Fix memory leak, typo in config, tn canonicalization
-  sorcery.c: Fixed crash error when executing "module reload"
-  logger.h:  Add SCOPE_CALL and SCOPE_CALL_WITH_RESULT
-  res_stir_shaken:  Fix compilation for CentOS7 (openssl 1.0.2)
-  res_ari.c: Add additional output to ARI requests when debug is enabled
-  alembic: Fix compatibility with SQLAlchemy 2.0+.
-  pbx_variables.c: Prevent SEGV due to stack overflow.
-  res_pjsip: Fix alembic downgrade for boolean columns.
-  alembic: Quote new MySQL keyword 'qualify.'
-  asterisk.c: Fix sending incorrect messages to systemd notify
-  tcptls/iostream:  Add support for setting SNI on client TLS connections
-  make_buildopts_h: Always include DETECT_DEADLOCKS
-  alembic: Correct NULLability of PJSIP id columns.
-  rtp_engine and stun: call ast_register_atexit instead of ast_register_cleanup
-  manager.c: Add missing parameters to Login documentation
-  Fix incorrect application and function documentation references

### Commit Details:

#### stir_shaken:  Fix memory leak, typo in config, tn canonicalization
  Author: George Joseph
  Date:   2024-04-25

  * Fixed possible memory leak in tn_config:tn_get_etn() where we
  weren't releasing etn if tn or eprofile were null.
  * We now canonicalize TNs before using them for lookups or adding
  them to Identity headers.
  * Fixed a typo in stir_shaken.conf.sample.

  Resolves: #716

#### sorcery.c: Fixed crash error when executing "module reload"
  Author: Spiridonov Dmitry
  Date:   2024-04-14

  Fixed crash error when cli "module reload". The error appears when
  compiling with res_prometheus and using the sorcery memory cache for
  registrations


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


#### res_pjsip: Fix alembic downgrade for boolean columns.
  Author: Sean Bright
  Date:   2024-03-18

  When downgrading, ensure that we don't touch columns that didn't
  actually change during upgrade.


#### alembic: Quote new MySQL keyword 'qualify.'
  Author: Sean Bright
  Date:   2024-03-15

  Fixes #651


#### asterisk.c: Fix sending incorrect messages to systemd notify
  Author: Ivan Poddubny
  Date:   2024-05-05

  Send "RELOADING=1" instead of "RELOAD=1" to follow the format
  expected by systemd (see sd_notify(3) man page).

  Do not send STOPPING=1 in remote console mode:
  attempting to execute "asterisk -rx" by the main process leads to
  a warning if NotifyAccess=main (the default) or to a forced termination
  if NotifyAccess=all.


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


#### make_buildopts_h: Always include DETECT_DEADLOCKS
  Author: George Joseph
  Date:   2024-04-27

  Since DETECT_DEADLOCKS is now split from DEBUG_THREADS, it must
  always be included in buildopts.h instead of only when
  ADD_CFLAGS_TO_BUILDOPTS_H is defined.  A SEGV will result otherwise.

  Resolves: #719

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

#### Fix incorrect application and function documentation references
  Author: George Joseph
  Date:   2024-04-01

  There were a few references in the embedded documentation XML
  where the case didn't match or where the referenced app or function
  simply didn't exist any more.  These were causing 404 responses
  in docs.asterisk.org.


