
## Change Log for Release asterisk-certified-18.9-cert10

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-18.9-cert10.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert9...certified-18.9-cert10)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-18.9-cert10.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 4
- Commit Authors: 2
- Issues Resolved: 0
- Security Advisories Resolved: 0

### User Notes:

- #### app_voicemail_odbc: Allow audio to be kept on disk                              
  This commit adds a new voicemail.conf option
  'odbc_audio_on_disk' which when set causes the ODBC variant of
  app_voicemail_odbc to leave the message and greeting audio files
  on disk and only store the message metadata in the database.
  Much more information can be found in the voicemail.conf.sample
  file.


### Upgrade Notes:


### Commit Authors:

- George Joseph: (2)
- Sean Bright: (2)

## Issue and Commit Detail:

### Closed Issues:

None

### Commits By Author:

- ### George Joseph (2):
  - logger.h:  Add SCOPE_CALL and SCOPE_CALL_WITH_RESULT
  - app_voicemail_odbc: Allow audio to be kept on disk

- ### Sean Bright (2):
  - app_voicemail.c: Completely resequence mailbox folders.
  - logger.h: Include SCOPE_CALL_WITH_INT_RESULT() in non-dev-mode builds.


### Commit List:

-  logger.h: Include SCOPE_CALL_WITH_INT_RESULT() in non-dev-mode builds.
-  app_voicemail_odbc: Allow audio to be kept on disk
-  logger.h:  Add SCOPE_CALL and SCOPE_CALL_WITH_RESULT
-  app_voicemail.c: Completely resequence mailbox folders.

### Commit Details:

#### logger.h: Include SCOPE_CALL_WITH_INT_RESULT() in non-dev-mode builds.
  Author: Sean Bright
  Date:   2024-06-29

  Fixes #785


#### app_voicemail_odbc: Allow audio to be kept on disk
  Author: George Joseph
  Date:   2024-04-09

  This commit adds a new voicemail.conf option 'odbc_audio_on_disk'
  which when set causes the ODBC variant of app_voicemail to leave
  the message and greeting audio files on disk and only store the
  message metadata in the database.  This option came from a concern
  that the database could grow to large and cause remote access
  and/or replication to become slow.  In a clustering situation
  with this option, all asterisk instances would share the same
  database for the metadata and either use a shared filesystem
  or other filesystem replication service much more suitable
  for synchronizing files.

  The changes to app_voicemail to implement this feature were actually
  quite small but due to the complexity of the module, the actual
  source code changes were greater.  They fall into the following
  categories:

  * Tracing.  The module is so complex that it was impossible to
  figure out the path taken for various scenarios without the addition
  of many SCOPE_ENTER, SCOPE_EXIT and ast_trace statements, even in
  code that's not related to the functional change.  Making this worse
  was the fact that many "if" statements in this module didn't use
  braces.  Since the tracing macros add multiple statements, many "if"
  statements had to be converted to use braces.

  * Excessive use of PATH_MAX.  Previous maintainers of this module
  used PATH_MAX to allocate character arrays for filesystem paths
  and SQL statements as though they cost nothing.  In fact, PATH_MAX
  is defined as 4096 bytes!  Some functions had (and still have)
  multiples of these.  One function has 7.  Given that the vast
  majority of installations use the default spool directory path
  `/var/spool/asterisk/voicemail`, the actual path length is usually
  less than 80 bytes.  That's over 4000 bytes wasted.  It was the
  same for SQL statement buffers.  A 4K buffer for statement that
  only needed 60 bytes.  All of these PATH_MAX allocations in the
  ODBC related code were changed to dynamically allocated buffers.
  The rest will have to be addressed separately.

  * Bug fixes.  During the development of this feature, several
  pre-existing ODBC related bugs were discovered and fixed.  They
  had to do with leaving orphaned files on disk, not preserving
  original message ids when moving messages between folders,
  not honoring the "formats" config parameter in certain circumstances,
  etc.

  UserNote: This commit adds a new voicemail.conf option
  'odbc_audio_on_disk' which when set causes the ODBC variant of
  app_voicemail_odbc to leave the message and greeting audio files
  on disk and only store the message metadata in the database.
  Much more information can be found in the voicemail.conf.sample
  file.


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


#### app_voicemail.c: Completely resequence mailbox folders.
  Author: Sean Bright
  Date:   2023-11-27

  Resequencing is a process that occurs when we open a voicemail folder
  and discover that there are gaps between messages (e.g. `msg0000.txt`
  is missing but `msg0001.txt` exists). Resequencing involves shifting
  the existing messages down so we end up with a sequential list of
  messages.

  Currently, this process stops after reaching a threshold based on the
  message limit (`maxmsg`) configured on the current folder. However, if
  `maxmsg` is lowered when a voicemail folder contains more than
  `maxmsg + 10` messages, resequencing will not run completely leaving
  the mailbox in an inconsistent state.

  We now resequence up to the maximum number of messages permitted by
  `app_voicemail` (currently hard-coded at 9999 messages).

  Fixes #86


