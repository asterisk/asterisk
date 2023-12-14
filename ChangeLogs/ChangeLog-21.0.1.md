
Change Log for Release asterisk-21.0.1
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-21.0.1.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/21.0.0...21.0.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21.0.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- res_pjsip_header_funcs: Duplicate new header value, don't copy.
- res_pjsip: disable raw bad packet logging
- res_rtp_asterisk.c: Check DTLS packets against ICE candidate list
- manager.c: Prevent path traversal with GetConfig.

User Notes:
----------------------------------------

- ### http.c: Minor simplification to HTTP status output.
  For bound addresses, the HTTP status page now combines the bound
  address and bound port in a single line. Additionally, the SSL bind
  address has been renamed to TLS.


Upgrade Notes:
----------------------------------------

- ### chan_sip: Remove deprecated module.
  This module was deprecated in Asterisk 17
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.

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

- ### chan_skinny: Remove deprecated module.
  This module was deprecated in Asterisk 19
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.

- ### chan_mgcp: Remove deprecated module.
  This module was deprecated in Asterisk 19
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.

- ### translate.c: Prefer better codecs upon translate ties.
  When setting up translation between two codecs the quality was not taken into account,
  resulting in suboptimal translation. The quality is now taken into account,
  which can reduce the number of translation steps required, and improve the resulting quality.

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

- ### chan_alsa: Remove deprecated module.
  This module was deprecated in Asterisk 19
  and is now being removed in accordance with
  the Asterisk Module Deprecation policy.

- ### pbx_builtins: Remove deprecated and defunct functionality.
  The previously deprecated ImportVar and SetAMAFlags
  applications have now been removed.


Closed Issues:
----------------------------------------

None

Commits By Author:
----------------------------------------

- ### Ben Ford (1):
  - manager.c: Prevent path traversal with GetConfig.

- ### George Joseph (1):
  - res_rtp_asterisk.c: Check DTLS packets against ICE candidate list

- ### Gitea (1):
  - res_pjsip_header_funcs: Duplicate new header value, don't copy.

- ### Mike Bradeen (1):
  - res_pjsip: disable raw bad packet logging


Detail:
----------------------------------------

- ### res_pjsip_header_funcs: Duplicate new header value, don't copy.
  Author: Gitea  
  Date:   2023-07-10  

  When updating an existing header the 'update' code incorrectly
  just copied the new value into the existing buffer. If the
  new value exceeded the available buffer size memory outside
  of the buffer would be written into, potentially causing
  a crash.

  This change makes it so that the 'update' now duplicates
  the new header value instead of copying it into the existing
  buffer.

- ### res_pjsip: disable raw bad packet logging
  Author: Mike Bradeen  
  Date:   2023-07-25  

  Add patch to split the log level for invalid packets received on the
  signaling port.  The warning regarding the packet will move to level 2
  so that it can still be displayed, while the raw packet will be at level
  4.

- ### res_rtp_asterisk.c: Check DTLS packets against ICE candidate list
  Author: George Joseph  
  Date:   2023-11-09  

  When ICE is in use, we can prevent a possible DOS attack by allowing
  DTLS protocol messages (client hello, etc) only from sources that
  are in the active remote candidates list.

  Resolves: GHSA-hxj9-xwr8-w8pq

- ### manager.c: Prevent path traversal with GetConfig.
  Author: Ben Ford  
  Date:   2023-11-13  

  When using AMI GetConfig, it was possible to access files outside of the
  Asterisk configuration directory by using filenames with ".." and "./"
  even while live_dangerously was not enabled. This change resolves the
  full path and ensures we are still in the configuration directory before
  attempting to access the file.

