
## Change Log for Release asterisk-22.12.0-rc4

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.12.0-rc4.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.12.0-rc3...22.12.0-rc4)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.12.0-rc4.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 3
- Commit Authors: 3
- Issues Resolved: 3
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- AbdurahmanZA: (1)
- Joshua C. Colp: (1)
- kaos-kult: (1)

## Issue and Commit Detail:

### Closed Issues:

  - #2242: [bug]: pbx_config: each dialplan reload leaks a stasis/p:manager:core subscription when users.conf exists
  - #2249: [bug]: Park slot hints stuck at Unavailable after startup (regression in 22.11.0)
  - #2257: [bug]: Segfault in extension_state_update_sources() when a hint loses its presence part (strcmp on NULL, regression in 22.11.0)

### Commits By Author:

- **AbdurahmanZA** (1):
  - extension_state: Fix crash when a hint loses its presence sources.

- **Joshua C. Colp** (1):
  - extstate: Handle lack of or late loading of device state.

- **kaos-kult** (1):
  - pbx_config: Don't leak a stasis subscription on each reload with users.conf

### Commit List:

-  extstate: Handle lack of or late loading of device state.
-  extension_state: Fix crash when a hint loses its presence sources.
-  pbx_config: Don't leak a stasis subscription on each reload with users.conf

### Commit Details:

__extstate: Handle lack of or late loading of device state.__
  Author: Joshua C. Colp
  Date:   2026-10-26

  The extension state implementation is stateful resulting in state
  not being updated if a device state provider does not publish an
  update after loading. To handle providers which don't do this we
  query for updated state if we are asked for it and the existing
  state is invalid. This ensures that if the device state provider
  is loaded later we will get the updated state when asked.

  The "core show hints" and "core show hint" CLI commands have also
  been changed so that they will update invalid sources for both
  device and presence before displaying the information.

  Fixes: #2249

__extension_state: Fix crash when a hint loses its presence sources.__
  Author: AbdurahmanZA
  Date:   2026-10-26

  If an existing hint with a presence part ("devices,presence") was
  updated to a hint without one, extension_state_update_sources()
  passed a NULL presence_state_sources to strcmp() and crashed.

  This is triggered by "dialplan add extension <exten>,hint,... replace"
  from the CLI or the AMI Command action, because pbx_config always
  places the presence part in the application data, and by a dialplan
  reload that removes the presence part of a hint.

  The presence sources are now compared NULL-safely.

  Fixes: #2257

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

__pbx_config: Don't leak a stasis subscription on each reload with users.conf__
  Author: kaos-kult
  Date:   2026-10-19

  pbx_load_users() runs on every load and reload of pbx_config. When
  users.conf exists it subscribed to the manager topic so it could log
  the users.conf deprecation warning once Asterisk was fully booted.
  FullyBooted is only published at startup, so the subscription made by
  each later reload was never removed: every dialplan reload left a
  stasis/p:manager:core taskprocessor behind, each of which goes on
  receiving every manager message.

  Only subscribe while Asterisk is still starting and no subscription
  exists yet. The warning is still logged once, at startup.

  Resolves: #2242


