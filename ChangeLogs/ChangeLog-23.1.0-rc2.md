
## Change Log for Release asterisk-23.1.0-rc2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-23.1.0-rc2.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/23.1.0-rc1...23.1.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-23.1.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 1
- Commit Authors: 1
- Issues Resolved: 1
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- George Joseph: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 1578: [bug]: Deadlock with externalMedia custom channel id and cpp map channel backend

### Commits By Author:

- #### George Joseph (1):

### Commit List:

-  channelstorage:  Allow storage driver read locking to be skipped.

### Commit Details:

#### channelstorage:  Allow storage driver read locking to be skipped.
  Author: George Joseph
  Date:   2025-11-06

  After PR #1498 added read locking to channelstorage_cpp_map_name_id, if ARI
  channels/externalMedia was called with a custom channel id AND the
  cpp_map_name_id channel storage backend is in use, a deadlock can occur when
  hanging up the channel. It's actually triggered in
  channel.c:__ast_channel_alloc_ap() when it gets a write lock on the
  channelstorage driver then subsequently does a lookup for channel uniqueid
  which now does a read lock. This is an invalid operation and causes the lock
  state to get "bad". When the channels try to hang up, a write lock is
  attempted again which hangs and causes the deadlock.

  Now instead of the cpp_map_name_id channelstorage driver "get" APIs
  automatically performing a read lock, they take a "lock" parameter which
  allows a caller who already has a write lock to indicate that the "get" API
  must not attempt its own lock.  This prevents the state from getting mesed up.

  The ao2_legacy driver uses the ao2 container's recursive mutex so doesn't
  have this issue but since it also implements the common channelstorage API,
  it needed its "get" implementations updated to take the lock parameter. They
  just don't use it.

  Resolves: #1578

