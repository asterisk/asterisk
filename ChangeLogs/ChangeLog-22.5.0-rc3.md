
## Change Log for Release asterisk-22.5.0-rc3

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.5.0-rc3.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.5.0-rc2...22.5.0-rc3)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.5.0-rc3.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 2
- Commit Authors: 1
- Issues Resolved: 2
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Commit Authors:

- George Joseph: (2)

## Issue and Commit Detail:

### Closed Issues:

  - 1287: [bug]: channelstorage.c: Compilation failure with DEBUG_FD_LEAKS
  - 1288: [bug]: Crash when destroying channel with C++ alternative storage backend enabled

### Commits By Author:

- #### George Joseph (2):
  - channelstorage_cpp_map_name_id: Fix callback returning non-matching channels.
  - channelstorage: Rename callbacks that conflict with DEBUG_FD_LEAKS.


### Commit List:

-  channelstorage: Rename callbacks that conflict with DEBUG_FD_LEAKS.
-  channelstorage_cpp_map_name_id: Fix callback returning non-matching channels.

### Commit Details:

#### channelstorage: Rename callbacks that conflict with DEBUG_FD_LEAKS.
  Author: George Joseph
  Date:   2025-07-08

  DEBUG_FD_LEAKS replaces calls to "open" and "close" with functions that keep
  track of file descriptors, even when those calls are actually callbacks
  defined in structures like ast_channelstorage_instance->open and don't touch
  file descriptors.  This causes compilation failures.  Those callbacks
  have been renamed to "open_instance" and "close_instance" respectively.

  Resolves: #1287

#### channelstorage_cpp_map_name_id: Fix callback returning non-matching channels.
  Author: George Joseph
  Date:   2025-07-09

  When the callback() API was invoked but no channel passed the test, callback
  would return the last channel tested instead of NULL.  It now correctly
  returns NULL when no channel matches.

  Resolves: #1288

