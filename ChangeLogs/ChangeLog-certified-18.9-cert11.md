
## Change Log for Release asterisk-certified-18.9-cert11

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-18.9-cert11.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert10...certified-18.9-cert11)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-18.9-cert11.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 5
- Commit Authors: 2
- Issues Resolved: 4
- Security Advisories Resolved: 1
  - [GHSA-c4cg-9275-6w44](https://github.com/asterisk/asterisk/security/advisories/GHSA-c4cg-9275-6w44): Write=originate, is sufficient permissions for code execution / System() dialplan

### User Notes:

- #### res_pjsip_config_wizard.c: Refactor load process                                
  The res_pjsip_config_wizard.so module can now be reloaded.


### Upgrade Notes:


### Commit Authors:

- George Joseph: (4)
- Mike Bradeen: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-c4cg-9275-6w44: Write=originate, is sufficient permissions for code execution / System() dialplan
  - 780: [bug]: Infinite loop of "Indicated Video Update", max CPU usage
  - 801: [bug]: res_stasis: Occasional 200ms delay adding channel to a bridge
  - 816: [bug]: res_pjsip_config_wizard doesn't load properly if res_pjsip is loaded first
  - 819: [bug]: Typo in voicemail.conf.sample that stops it from loading when using "make samples"

### Commits By Author:

- #### George Joseph (4):
  - manager.c: Add entries to Originate blacklist
  - bridge_softmix: Fix queueing VIDUPDATE control frames
  - voicemail.conf.sample: Fix ':' comment typo
  - res_pjsip_config_wizard.c: Refactor load process

- #### Mike Bradeen (1):
  - res_stasis: fix intermittent delays on adding channel to bridge


### Commit List:

-  res_stasis: fix intermittent delays on adding channel to bridge
-  res_pjsip_config_wizard.c: Refactor load process
-  voicemail.conf.sample: Fix ':' comment typo
-  bridge_softmix: Fix queueing VIDUPDATE control frames
-  manager.c: Add entries to Originate blacklist

### Commit Details:

#### res_stasis: fix intermittent delays on adding channel to bridge
  Author: Mike Bradeen
  Date:   2024-07-10

  Previously, on command execution, the control thread was awoken by
  sending a SIGURG. It was found that this still resulted in some
  instances where the thread was not immediately awoken.

  This change instead sends a null frame to awaken the control thread,
  which awakens the thread more consistently.

  Resolves: #801

#### res_pjsip_config_wizard.c: Refactor load process
  Author: George Joseph
  Date:   2024-07-23

  The way we have been initializing the config wizard prevented it
  from registering its objects if res_pjsip happened to load
  before it.

  * We now use the object_type_registered sorcery observer to kick
  things off instead of the wizard_mapped observer.

  * The load_module function now checks if res_pjsip has been loaded
  already and if it was it fires the proper observers so the objects
  load correctly.

  Resolves: #816

  UserNote: The res_pjsip_config_wizard.so module can now be reloaded.

#### voicemail.conf.sample: Fix ':' comment typo
  Author: George Joseph
  Date:   2024-07-24

  ...and removed an errant trailing space.

  Resolves: #819

#### bridge_softmix: Fix queueing VIDUPDATE control frames
  Author: George Joseph
  Date:   2024-07-17

  softmix_bridge_write_control() now calls ast_bridge_queue_everyone_else()
  with the bridge_channel so the VIDUPDATE control frame isn't echoed back.

  softmix_bridge_write_control() was setting bridge_channel to NULL
  when calling ast_bridge_queue_everyone_else() for VIDUPDATE control
  frames.  This was causing the frame to be echoed back to the
  channel it came from.  In certain cases, like when two channels or
  bridges are being recorded, this can cause a ping-pong effect that
  floods the system with VIDUPDATE control frames.

  Resolves: #780

#### manager.c: Add entries to Originate blacklist
  Author: George Joseph
  Date:   2024-07-22

  Added Reload and DBdeltree to the list of dialplan application that
  can't be executed via the Originate manager action without also
  having write SYSTEM permissions.

  Added CURL, DB*, FILE, ODBC and REALTIME* to the list of dialplan
  functions that can't be executed via the Originate manager action
  without also having write SYSTEM permissions.

  If the Queue application is attempted to be run by the Originate
  manager action and an AGI parameter is specified in the app data,
  it'll be rejected unless the manager user has either the AGI or
  SYSTEM permissions.

  Resolves: #GHSA-c4cg-9275-6w44

