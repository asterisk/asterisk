
## Change Log for Release asterisk-certified-22.8-cert4

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-22.8-cert4.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-22.8-cert3...certified-22.8-cert4)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-22.8-cert4.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 13
- Commit Authors: 6
- Issues Resolved: 8
- Security Advisories Resolved: 0

### User Notes:

- #### res_pjsip_maintenance: Add PJSIP endpoint maintenance mode
  New module res_pjsip_maintenance adds runtime maintenance
  mode for PJSIP endpoints. Use "pjsip set maintenance <on|off>
  <endpoint|all>" to enable or disable, and "pjsip show maintenance"
  to list affected endpoints. AMI actions PJSIPSetMaintenance and
  PJSIPShowMaintenance provide programmatic access. No configuration
  file changes required.


### Upgrade Notes:


### Developer Notes:

- #### res_pjsip_maintenance: Add PJSIP endpoint maintenance mode
  ast_sip_session_supplement gains a new optional
  callback - int (*session_create)(struct ast_sip_endpoint *endpoint,
  const char *destination). It is called from the global supplement
  list (not per-session) at the start of ast_sip_session_create_outgoing()
  via ast_sip_session_check_supplement_create(). Returning non-zero
  blocks the outgoing session. Modules that need to gate outbound
  SIP session creation should register a supplement with this callback
  set rather than hooking into chan_pjsip directly.


### Commit Authors:

- Daniel Donoghue: (1)
- George Joseph: (4)
- Joshua C. Colp: (1)
- Mike Bradeen: (1)
- Naveen Albert: (5)
- Sean Bright: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 1781: [bug]: More discarded-qualifiers errors with gcc 15.2.1
  - 1783: [bug]: Several unused-but-set-variable warnings with gcc 16
  - 1786: [bug]: chan_dahdi: A few more discarded-qualifiers errors not caught previously
  - 1903: [bug]: g++ 16 no longer defines __STDC_VERSION__ causing channelstorage_cpp_map_name_id.cc to fail
  - 1947: [bug]: chan_dahdi fails to build with gcc-16 when openr2 is installed
  - 2054: [bug]: backtrace.c: Compilation failure due to use of removed type
  - 2059: [bug]: Occasional call drops during re-INVITE when codec changes
  - 2061: [bug]: backtrace.c: stdbool.h needed for portability

### Commits By Author:

- #### Daniel Donoghue (1):
  - res_pjsip_maintenance: Add PJSIP endpoint maintenance mode

- #### George Joseph (4):
  - SECURITY.md: Add warning about reporting multiple issues in one advisory.
  - chan_dahdi: Fix set but not used in mfcr2_show_links_of().
  - compat.h: Ensure check for `__STDC_VERSION__` is not attempted for c++.
  - SECURITY.md: Update with additional instructions.

- #### Joshua C. Colp (1):
  - pjsip: Eliminate some unique taskprocessors.

- #### Mike Bradeen (1):
  - res_pjsip_sdp_rtp: Fix intermittent call drop on reINVITE sdp change

- #### Naveen Albert (5):
  - backtrace.c: Include stdbool.h
  - backtrace.c: Avoid removed bfd_boolean type.
  - build: Fix another GCC discarded-qualifiers const error.
  - chan_dahdi: Fix discarded-qualifiers errors.
  - build: Fix unused-but-set-variable warnings with gcc 16.

- #### Sean Bright (1):
  - app_voicemail.c: Fix may-be-uninitialized error

### Commit List:

-  res_pjsip_sdp_rtp: Fix intermittent call drop on reINVITE sdp change
-  pjsip: Eliminate some unique taskprocessors.
-  backtrace.c: Include stdbool.h
-  backtrace.c: Avoid removed bfd_boolean type.
-  app_voicemail.c: Fix may-be-uninitialized error
-  build: Fix another GCC discarded-qualifiers const error.
-  SECURITY.md: Add warning about reporting multiple issues in one advisory.
-  chan_dahdi: Fix discarded-qualifiers errors.
-  build: Fix unused-but-set-variable warnings with gcc 16.
-  chan_dahdi: Fix set but not used in mfcr2_show_links_of().
-  compat.h: Ensure check for `__STDC_VERSION__` is not attempted for c++.
-  res_pjsip_maintenance: Add PJSIP endpoint maintenance mode
-  SECURITY.md: Update with additional instructions.

### Commit Details:

#### res_pjsip_sdp_rtp: Fix intermittent call drop on reINVITE sdp change
  Author: Mike Bradeen
  Date:   2026-08-05

  A reINVITE that changed the offered codec set (e.g. ulaw+alaw -> alaw
  only) could lead to call teardown when a bridge write still using the
  old format occurred during the re-negotiation.

  This change avoids the termination by making the following changes.

  First, Asterisk now holds the lock across both the tx payload update
  and the channel format update so they commit atomically.

  Second, when processing the sdp on the new incoming offer, merge the
  new offer with the old until the negotiation is complete.

  Also adds unit tests for the sdp merge

  Fixes: #2059

#### pjsip: Eliminate some unique taskprocessors.
  Author: Joshua C. Colp
  Date:   2026-07-15

  When placing outbound calls each call would get its own
  taskprocessor for things related to the call. In practice this
  is overkill as few things actually occur. Inbound calls on
  the other hand already use one of the fixed number of
  distributor taskprocessors. This also occurred for OPTIONS
  requests with each AOR having its own taskprocessor.

  This change moves both to using a distributor taskprocessor
  instead.

#### backtrace.c: Include stdbool.h
  Author: Naveen Albert
  Date:   2026-08-04

  stdbool.h is needed now that we use the bool type directly
  instead of bfd_boolean. See also 1d95b744c06974f0a00c143c6e0fc979af930908.

  Resolves: #2061

#### backtrace.c: Avoid removed bfd_boolean type.
  Author: Naveen Albert
  Date:   2026-08-03

  bfd_boolean has been deprecated for some time and has now been
  removed in gdb, so use bool directly instead.

  See: https://github.com/gnutools/binutils-gdb/commit/1d95b744c06974f0a00c143c6e0fc979af930908

  Resolves: #2054

#### app_voicemail.c: Fix may-be-uninitialized error
  Author: Sean Bright
  Date:   2026-08-02

  A pointer to `new` is passed to `inboxcount(...)` which increments the
  pointed-to value.

#### build: Fix another GCC discarded-qualifiers const error.
  Author: Naveen Albert
  Date:   2026-02-18

  Follow on commit to 27a39cba7e6832cb30cb64edaf879f447b669628
  to fix compilation with BETTER_BACKTRACES with gcc 15.2.1.

  Resolves: #1781

#### SECURITY.md: Add warning about reporting multiple issues in one advisory.
  Author: George Joseph
  Date:   2026-08-04


#### chan_dahdi: Fix discarded-qualifiers errors.
  Author: Naveen Albert
  Date:   2026-02-18

  Fix discarded-qualifiers errors to compile successfully with gcc 15.2.1.

  Associated changes have also been made to libss7; however, for compatibility
  we cast const char* values to char*. In the future, these casts could be
  removed.

  Resolves: #1786

#### build: Fix unused-but-set-variable warnings with gcc 16.
  Author: Naveen Albert
  Date:   2026-02-18

  Fix or remove a few variables that were being set but not actually
  used anywhere, causing warnings with gcc 16.

  Resolves: #1783

#### chan_dahdi: Fix set but not used in mfcr2_show_links_of().
  Author: George Joseph
  Date:   2026-05-21

  When openr2 is installed mfcr2_show_links_of() is no longer ifdeffed out
  which makes gcc-16 complain with 'variable ‘x’ set but not used'.

  Resolves: #1947

#### compat.h: Ensure check for `__STDC_VERSION__` is not attempted for c++.
  Author: George Joseph
  Date:   2026-04-27

  `__STDC_VERSION__` is specific to C but up until gcc 16, the g++ compiler
  also defined it.  With g++ 16.0 it's no longer defined (which is the correct
  behavior) so compiling channelstorage_cpp_map_name_id.cc fails.  The
  check for `__STDC_VERSION__` in compat.h is now skipped if we're compiling
  a C++ source file.

  Resolves: #1903

#### res_pjsip_maintenance: Add PJSIP endpoint maintenance mode
  Author: Daniel Donoghue
  Date:   2026-03-10

  Introduces res_pjsip_maintenance, a loadable module that allows
  operators to place individual PJSIP endpoints into maintenance mode
  at runtime without unregistering or disabling them.

  While an endpoint is in maintenance mode:
   * New inbound INVITE and SUBSCRIBE dialogs are rejected with
     503 Service Unavailable and a Retry-After: 300 header.
   * In-progress dialogs (re-INVITE, UPDATE, BYE, etc.) are
     unaffected and complete normally.
   * Outbound originations via Dial() or ARI originate are refused
     before any SIP session is created.

  State is held in-memory only and is cleared on module unload
  or Asterisk restart.

  This module was developed with AI assistance (Claude).  All code
  has been reviewed and tested by the author, who takes full
  responsibility for the submission.

  CLI interface:
    pjsip set maintenance <on|off> <endpoint|all>
    pjsip show maintenance [endpoint]

  AMI interface:
    Action: PJSIPSetMaintenance
    Endpoint: <name>|all
    State: on|off

    Action: PJSIPShowMaintenance
    Endpoint: <name>  (optional; omit to list all)

    Emits PJSIPMaintenanceStatus events per result, followed by
    PJSIPMaintenanceStatusComplete. State changes also emit an
    unsolicited PJSIPMaintenanceStatus event.

  To support outbound blocking, a new session_create callback is
  added to ast_sip_session_supplement. Supplements that set this
  callback are invoked at the start of ast_sip_session_create_outgoing()
  in res_pjsip_session, before any dialog or invite session resources
  are allocated. res_pjsip_maintenance registers itself as a session
  supplement and uses this callback to gate outbound session creation
  on a per-endpoint basis.

  MODULEINFO:
    <depend>pjproject</depend>
    <depend>res_pjsip</depend>
    <depend>res_pjsip_session</depend>

  UserNote: New module res_pjsip_maintenance adds runtime maintenance
  mode for PJSIP endpoints. Use "pjsip set maintenance <on|off>
  <endpoint|all>" to enable or disable, and "pjsip show maintenance"
  to list affected endpoints. AMI actions PJSIPSetMaintenance and
  PJSIPShowMaintenance provide programmatic access. No configuration
  file changes required.

  DeveloperNote: ast_sip_session_supplement gains a new optional
  callback - int (*session_create)(struct ast_sip_endpoint *endpoint,
  const char *destination). It is called from the global supplement
  list (not per-session) at the start of ast_sip_session_create_outgoing()
  via ast_sip_session_check_supplement_create(). Returning non-zero
  blocks the outgoing session. Modules that need to gate outbound
  SIP session creation should register a supplement with this callback
  set rather than hooking into chan_pjsip directly.

#### SECURITY.md: Update with additional instructions.
  Author: George Joseph
  Date:   2026-03-19

  Also added line breaks for people reading this file directly
  from the code base.

