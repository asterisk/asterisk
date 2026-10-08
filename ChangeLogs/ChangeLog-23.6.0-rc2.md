
## Change Log for Release asterisk-23.6.0-rc2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-23.6.0-rc2.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/23.6.0-rc1...23.6.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-23.6.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 3
- Commit Authors: 2
- Issues Resolved: 3
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Developer Notes:

- #### Audiohooks: Fix whisper framehook timer leak and masquerade handling
  ast_channel_fd_add() now returns -1 if it fails to allocate
  space for the descriptor.


### Commit Authors:

- Joshua C. Colp: (2)
- Mike Bradeen: (1)

## Issue and Commit Detail:

### Closed Issues:

  - #2134: [bug]: Bridging channels which have been spied on with ChanSpy fails
  - #2179: [bug]: No initial NOTIFY for dialog subscriptions when hint references CustomPresence without AstDB entry (regression in 22.11.0)
  - #2186: [bug]: dialog-info NOTIFY for ringing extension no longer contains <remote> identity (caller ID) since 22.11.0

### Commits By Author:

- **Joshua C. Colp** (2):
  - extstate/pjsip: Handle lack of or late loading of presence state.
  - extstate: Include causing device channel details in legacy callbacks.

- **Mike Bradeen** (1):
  - Audiohooks: Fix whisper framehook timer leak and masquerade handling

### Commit List:

-  Audiohooks: Fix whisper framehook timer leak and masquerade handling
-  extstate/pjsip: Handle lack of or late loading of presence state.
-  extstate: Include causing device channel details in legacy callbacks.

### Commit Details:

__Audiohooks: Fix whisper framehook timer leak and masquerade handling__
  Author: Mike Bradeen
  Date:   2026-09-25

  Resolves several issues with the framehook added to allow whispering without
  an underlying stream.

  The timer is now owned by the audiohook list rather than the framehook.  It is
  retired on both channels during a masquerade and recreated when the audiohook(s)
  are moved.

  If the timer cannot be started, whisper audio is still mixed into the outbound
  media as it was previously (requiring an underlying stream.)

  An audiohook that cannot be moved to the new channel is now left DONE so that
  its owner does not wait on it forever.

  Resolves: #2134

  DeveloperNote: ast_channel_fd_add() now returns -1 if it fails to allocate
  space for the descriptor.

__extstate/pjsip: Handle lack of or late loading of presence state.__
  Author: Joshua C. Colp
  Date:   2026-09-25

  The extension state implementation is stateful resulting in state
  not being updated if a presence state provider does not publish an
  update after loading. To handle providers which don't do this we
  query for updated state if we are asked for it and the existing
  state is invalid. This ensures that if the presence state provider
  is loaded later we will get the updated state when asked.

  The func_presencestate module is written to publish the persisted
  custom presence states at load time. This failed for entries with
  a not_set state as this resulted in querying the presence provider
  that was not yet registered. The provider is now registered before
  this ensuring the update can be published.

  This change also fixes an issue in PJSIP where a SUBSCRIBE for a
  hint that had an invalid presence state configuration would not
  send an initial NOTIFY. We now treat an invalid presence state as
  not set instead.

  Fixes: #2179

__extstate: Include causing device channel details in legacy callbacks.__
  Author: Joshua C. Colp
  Date:   2026-09-29

  When a legacy extension state callback registers it can optionally
  do so in a way that it will receive the channel(s) that have caused
  a device to ring. This is used in PJSIP for the dialog-info+xml
  support so that phones can display who is calling the monitored
  extension.

  An issue was introduced in the extension state rewrite where this
  did not occur for the actual device that caused the extension state
  update. It was only being done for additional devices that could be
  involved. This has been fixed so that it is now done for the causing
  device.

  Fixes: #2186


