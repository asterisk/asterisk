
## Change Log for Release asterisk-certified-18.9-cert9

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-18.9-cert9.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert8...certified-18.9-cert9)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-18.9-cert9.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 8
- Commit Authors: 4
- Issues Resolved: 4
- Security Advisories Resolved: 0

### User Notes:

- #### tcptls/iostream:  Add support for setting SNI on client TLS connections         
  Secure websocket client connections now send SNI in
  the TLS client hello.


### Upgrade Notes:


### Commit Authors:

- George Joseph: (5)
- Ivan Poddubny: (1)
- Mark Murawski: (1)
- Sean Bright: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 360: [improvement]: Update documentation for CHANGES/UPGRADE files
  - 689: [bug] Document the `Events` argument of the `Login` AMI action
  - 713: [bug]: SNI isn't being set on websocket client connections
  - 719: [bug]: segfault on start if compiled with DETECT_DEADLOCKS

### Commits By Author:

- ### George Joseph (5):
  - Fix incorrect application and function documentation references
  - manager.c: Add missing parameters to Login documentation
  - rtp_engine and stun: call ast_register_atexit instead of ast_register_cleanup
  - make_buildopts_h: Always include DETECT_DEADLOCKS
  - tcptls/iostream:  Add support for setting SNI on client TLS connections

- ### Ivan Poddubny (1):
  - asterisk.c: Fix sending incorrect messages to systemd notify

- ### Mark Murawski (1):
  - Remove files that are no longer updated

- ### Sean Bright (1):
  - res_http_websocket.c: Set hostname on client for certificate validation.


### Commit List:

-  asterisk.c: Fix sending incorrect messages to systemd notify
-  res_http_websocket.c: Set hostname on client for certificate validation.
-  tcptls/iostream:  Add support for setting SNI on client TLS connections
-  make_buildopts_h: Always include DETECT_DEADLOCKS
-  rtp_engine and stun: call ast_register_atexit instead of ast_register_cleanup
-  manager.c: Add missing parameters to Login documentation
-  Fix incorrect application and function documentation references
-  Remove files that are no longer updated

### Commit Details:

#### asterisk.c: Fix sending incorrect messages to systemd notify
  Author: Ivan Poddubny
  Date:   2024-05-05

  Send "RELOADING=1" instead of "RELOAD=1" to follow the format
  expected by systemd (see sd_notify(3) man page).

  Do not send STOPPING=1 in remote console mode:
  attempting to execute "asterisk -rx" by the main process leads to
  a warning if NotifyAccess=main (the default) or to a forced termination
  if NotifyAccess=all.


#### res_http_websocket.c: Set hostname on client for certificate validation.
  Author: Sean Bright
  Date:   2023-11-09

  Additionally add a `assert()` to in the TLS client setup code to
  ensure that hostname is set when it is supposed to be.

  Fixes #433


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


#### Remove files that are no longer updated
  Author: Mark Murawski
  Date:   2023-10-30

  Fixes: #360

