
## Change Log for Release asterisk-certified-18.9-cert15

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-18.9-cert15.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert14...certified-18.9-cert15)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-18.9-cert15.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 25
- Commit Authors: 8
- Issues Resolved: 10
- Security Advisories Resolved: 0

### User Notes:

- #### res_pjsip: Add new endpoint option "suppress_moh_on_sendonly"                   
  The new "suppress_moh_on_sendonly" endpoint option
  can be used to prevent playing MOH back to a caller if the remote
  end sends "sendonly" or "inactive" (hold) to Asterisk in an SDP.

- #### app_mixmonitor: Add 'D' option for dual-channel audio.                          
  The MixMonitor application now has a new 'D' option which
  interleaves the recorded audio in the output frames. This allows for
  stereo recording output with one channel being the transmitted audio and
  the other being the received audio. The 't' and 't' options are
  compatible with this.


### Upgrade Notes:


### Commit Authors:

- Ben Ford: (2)
- George Joseph: (12)
- Joshua C. Colp: (1)
- Marcel Wagner: (1)
- Mike Bradeen: (1)
- Naveen Albert: (1)
- Sean Bright: (6)
- Shyju Kanaprath: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 430: [bug]: Fix broken links
  - 527: [bug]: app_voicemail_odbc no longer working after removal of macrocontext.
  - 937: [bug]: Wrong format for sample config file 'geolocation.conf.sample'
  - 938: [bug]: memory leak - CBAnn leaks a small amount format_cap related memory for every confbridge
  - 945: [improvement]: Add stereo recording support for app_mixmonitor
  - 979: [improvement]: Add ability to suppress MOH when a remote endpoint sends "sendonly" or "inactive"
  - 982: [bug]: The addition of tenantid to the ast_sip_endpoint structure broke ABI compatibility
  - 995: [bug]: suppress_moh_on_sendonly should use AST_BOOL_VALUES instead of YESNO_VALUES in alembic script
  - 1131: [bug]: CHANGES link broken in README.md
  - ASTERISK-29976: Should Readme include information about install_prereq script?

### Commits By Author:

- #### Ben Ford (2):
  - app_mixmonitor: Add 'D' option for dual-channel audio.
  - documentation: Update Gosub, Goto, and add new documentationtype.

- #### George Joseph (12):
  - Fix application references to Background
  - manager.c: Add unit test for Originate app and appdata permissions
  - geolocation.sample.conf: Fix comment marker at end of file
  - core_unreal.c: Fix memory leak in ast_unreal_new_channels()
  - res_pjsip: Move tenantid to end of ast_sip_endpoint
  - res_pjsip: Add new endpoint option "suppress_moh_on_sendonly"
  - res_pjsip: Change suppress_moh_on_sendonly to OPT_BOOL_T
  - gcc14: Fix issues caught by gcc 14
  - README.md, asterisk.c: Update Copyright Dates
  - README.md: Updates and Fixes
  - build_tools: Backport from 18
  - res_pjsip: Backport pjsip uri utilities.

- #### Joshua C. Colp (1):
  - LICENSE: Update company name, email, and address.

- #### Marcel Wagner (1):
  - documentation: Add information on running install_prereq script in readme

- #### Mike Bradeen (1):
  - app_voicemail: add NoOp alembic script to maintain sync

- #### Naveen Albert (1):
  - general: Fix broken links.

- #### Sean Bright (6):
  - res_pjsip_pubsub: Persist subscription 'generator_data' in sorcery
  - alembic: Drop redundant voicemail_messages index.
  - manager.c: Rename restrictedFile to is_restricted_file.
  - xml.c: Update deprecated libxml2 API usage.
  - chan_dahdi.c: Resolve a format-truncation build warning.
  - chan_sip.c: Fix __sip_reliable_xmit build error

- #### Shyju Kanaprath (1):
  - README.md: Removed outdated link


### Commit List:

-  res_pjsip: Backport pjsip uri utilities.
-  build_tools: Backport from 18
-  chan_sip.c: Fix __sip_reliable_xmit build error
-  chan_dahdi.c: Resolve a format-truncation build warning.
-  xml.c: Update deprecated libxml2 API usage.
-  documentation: Update Gosub, Goto, and add new documentationtype.
-  README.md: Updates and Fixes
-  README.md: Removed outdated link
-  general: Fix broken links.
-  documentation: Add information on running install_prereq script in readme
-  LICENSE: Update company name, email, and address.
-  README.md, asterisk.c: Update Copyright Dates
-  manager.c: Rename restrictedFile to is_restricted_file.
-  gcc14: Fix issues caught by gcc 14
-  res_pjsip: Change suppress_moh_on_sendonly to OPT_BOOL_T
-  res_pjsip: Add new endpoint option "suppress_moh_on_sendonly"
-  res_pjsip: Move tenantid to end of ast_sip_endpoint
-  app_mixmonitor: Add 'D' option for dual-channel audio.
-  core_unreal.c: Fix memory leak in ast_unreal_new_channels()
-  geolocation.sample.conf: Fix comment marker at end of file
-  manager.c: Add unit test for Originate app and appdata permissions
-  alembic: Drop redundant voicemail_messages index.
-  app_voicemail: add NoOp alembic script to maintain sync
-  res_pjsip_pubsub: Persist subscription 'generator_data' in sorcery
-  Fix application references to Background

### Commit Details:

#### res_pjsip: Backport pjsip uri utilities.
  Author: George Joseph
  Date:   2025-03-25

  The following utilities have been backported:

  ast_sip_is_uri_sip_sips
  ast_sip_is_allowed_uri
  ast_sip_pjsip_uri_get_username
  ast_sip_pjsip_uri_get_hostname
  ast_sip_pjsip_uri_get_other_param

  They were originally included in the commit for supporting TEL uris.
  Support for TEL uris is NOT included here however.


#### build_tools: Backport from 18
  Author: George Joseph
  Date:   2025-03-25

  There are several build fixes that never made it into certified/18.9.
  Unfortunately the commits that contained the fixes also contained other
  stuff that won't cherry-pick into cert so the build files had to be
  just copied from 18.


#### chan_sip.c: Fix __sip_reliable_xmit build error
  Author: Sean Bright
  Date:   2024-10-17

  Fixes #954


#### chan_dahdi.c: Resolve a format-truncation build warning.
  Author: Sean Bright
  Date:   2022-08-19

  With gcc (Ubuntu 11.2.0-19ubuntu1) 11.2.0:

  > chan_dahdi.c:4129:18: error: ‘%s’ directive output may be truncated
  >   writing up to 255 bytes into a region of size between 242 and 252
  >   [-Werror=format-truncation=]

  This removes the error-prone sizeof(...) calculations in favor of just
  doubling the size of the base buffer.


#### xml.c: Update deprecated libxml2 API usage.
  Author: Sean Bright
  Date:   2024-05-23

  Two functions are deprecated as of libxml2 2.12:

    * xmlSubstituteEntitiesDefault
    * xmlParseMemory

  So we update those with supported API.

  Additionally, `res_calendar_caldav` has been updated to use libxml2's
  xmlreader API instead of the SAX2 API which has always felt a little
  hacky (see deleted comment block in `res_calendar_caldav.c`).

  The xmlreader API has been around since libxml2 2.5.0 which was
  released in 2003.

  Fixes #725


#### documentation: Update Gosub, Goto, and add new documentationtype.
  Author: Ben Ford
  Date:   2025-03-14

  Gosub and Goto were not displaying their syntax correctly on the docs
  site. This change adds a new way to specify an optional context, an
  optional extension, and a required priority that the xml stylesheet can
  parse without having to know which optional parameters come in which
  order. In Asterisk, it looks like this:

    parameter name="context" documentationtype="dialplan_context"
    parameter name="extension" documentationtype="dialplan_extension"
    parameter name="priority" documentationtype="dialplan_priority" required="true"

  The stylesheet will ignore the context and extension parameters, but for
  priority, it will automatically inject the following:

    [[context,]extension,]priority

  This is the correct oder for applications such as Gosub and Goto.


#### README.md: Updates and Fixes
  Author: George Joseph
  Date:   2025-03-05

  * Outdated information has been removed.
  * New links added.
  * Placeholder added for link to change logs.

  Going forward, the release process will create HTML versions of the README
  and change log and will update the link in the README to the current
  change log for the branch...

  * In the development branches, the link will always point to the current
    release on GitHub.
  * In the "releases/*" branches and the tarballs, the link will point to the
    ChangeLogs/ChangeLog-<version>.html file in the source directory.
  * On the downloads website, the link will point to the
    ChangeLog-<version>.html file in the same directory.

  Resolves: #1131

#### README.md: Removed outdated link
  Author: Shyju Kanaprath
  Date:   2024-02-23

  Removed outdated link http://www.quicknet.net from README.md

  cherry-pick-to: 18
  cherry-pick-to: 20
  cherry-pick-to: 21

#### general: Fix broken links.
  Author: Naveen Albert
  Date:   2023-11-09

  This fixes a number of broken links throughout the
  tree, mostly caused by wiki.asterisk.org being replaced
  with docs.asterisk.org, which should eliminate the
  need for sporadic fixes as in f28047db36a70e81fe373a3d19132c43adf3f74b.

  Resolves: #430

#### documentation: Add information on running install_prereq script in readme
  Author: Marcel Wagner
  Date:   2022-03-23

  Adding information in the readme about running the install_preqreq script to install components that the ./configure script might indicate as missing.

  ASTERISK-29976 #close


#### LICENSE: Update company name, email, and address.
  Author: Joshua C. Colp
  Date:   2025-01-21


#### README.md, asterisk.c: Update Copyright Dates
  Author: George Joseph
  Date:   2025-01-20


#### manager.c: Rename restrictedFile to is_restricted_file.
  Author: Sean Bright
  Date:   2025-01-09

  Also correct the spelling of 'privileges.'


#### gcc14: Fix issues caught by gcc 14
  Author: George Joseph
  Date:   2025-01-03

  * reqresp_parser.c: Fix misuse of "static" with linked list definitions
  * test_message.c: Fix segfaults caused by passing NULL as an sprintf fmt


#### res_pjsip: Change suppress_moh_on_sendonly to OPT_BOOL_T
  Author: George Joseph
  Date:   2024-11-15

  The suppress_moh_on_sendonly endpoint option should have been
  defined as OPT_BOOL_T in pjsip_configuration.c and AST_BOOL_VALUES
  in the alembic script instead of OPT_YESNO_T and YESNO_VALUES.

  Also updated contrib/ast-db-manage/README.md to indicate that
  AST_BOOL_VALUES should always be used and provided an example.

  Resolves: #995

#### res_pjsip: Add new endpoint option "suppress_moh_on_sendonly"
  Author: George Joseph
  Date:   2024-11-05

  Normally, when one party in a call sends Asterisk an SDP with
  a "sendonly" or "inactive" attribute it means "hold" and causes
  Asterisk to start playing MOH back to the other party. This can be
  problematic if it happens at certain times, such as in a 183
  Progress message, because the MOH will replace any early media you
  may be playing to the calling party. If you set this option
  to "yes" on an endpoint and the endpoint receives an SDP
  with "sendonly" or "inactive", Asterisk will NOT play MOH back to
  the other party.

  Resolves: #979

  UserNote: The new "suppress_moh_on_sendonly" endpoint option
  can be used to prevent playing MOH back to a caller if the remote
  end sends "sendonly" or "inactive" (hold) to Asterisk in an SDP.


#### res_pjsip: Move tenantid to end of ast_sip_endpoint
  Author: George Joseph
  Date:   2024-11-06

  The tenantid field was originally added to the ast_sip_endpoint
  structure at the end of the AST_DECLARE_STRING_FIELDS block.  This
  caused everything after it in the structure to move down in memory
  and break ABI compatibility.  It's now at the end of the structure
  as an AST_STRING_FIELD_EXTENDED.  Given the number of string fields
  in the structure now, the initial string field allocation was
  also increased from 64 to 128 bytes.

  Resolves: #982

#### app_mixmonitor: Add 'D' option for dual-channel audio.
  Author: Ben Ford
  Date:   2024-10-28

  Adds the 'D' option to app_mixmonitor that interleaves the input and
  output frames of the channel being recorded in the monitor output frame.
  This allows for two streams in the recording: the transmitted audio and
  the received audio. The 't' and 'r' options are compatible with this.

  Fixes: #945

  UserNote: The MixMonitor application now has a new 'D' option which
  interleaves the recorded audio in the output frames. This allows for
  stereo recording output with one channel being the transmitted audio and
  the other being the received audio. The 't' and 't' options are
  compatible with this.


#### core_unreal.c: Fix memory leak in ast_unreal_new_channels()
  Author: George Joseph
  Date:   2024-10-15

  When the channel tech is multistream capable, the reference to
  chan_topology was passed to the new channel.  When the channel tech
  isn't multistream capable, the reference to chan_topology was never
  released.  "Local" channels are multistream capable so it didn't
  affect them but the confbridge "CBAnn" and the bridge_media
  "Recorder" channels are not so they caused a leak every time one
  of them was created.

  Also added tracing to ast_stream_topology_alloc() and
  stream_topology_destroy() to assist with debugging.

  Resolves: #938

#### geolocation.sample.conf: Fix comment marker at end of file
  Author: George Joseph
  Date:   2024-10-08

  Resolves: #937

#### manager.c: Add unit test for Originate app and appdata permissions
  Author: George Joseph
  Date:   2024-10-03

  This unit test checks that dialplan apps and app data specified
  as parameters for the Originate action are allowed with the
  permissions the user has.


#### alembic: Drop redundant voicemail_messages index.
  Author: Sean Bright
  Date:   2024-09-26

  The `voicemail_messages_dir` index is a left prefix of the table's
  primary key and therefore unnecessary.


#### app_voicemail: add NoOp alembic script to maintain sync
  Author: Mike Bradeen
  Date:   2024-01-17

  Adding a NoOp alembic script for the voicemail database to maintain
  version sync with other branches.

  Fixes: #527

#### res_pjsip_pubsub: Persist subscription 'generator_data' in sorcery
  Author: Sean Bright
  Date:   2024-09-23

  Fixes #895


#### Fix application references to Background
  Author: George Joseph
  Date:   2024-09-20

  The app is actually named "BackGround" but several references
  in XML documentation were spelled "Background" with the lower
  case "g".  This was causing documentation links to return
  "not found" messages.


