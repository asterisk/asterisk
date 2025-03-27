
## Change Log for Release asterisk-22.3.0

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.3.0.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.2.0...22.3.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.3.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 28
- Commit Authors: 12
- Issues Resolved: 12
- Security Advisories Resolved: 0

### User Notes:

- #### ari/pjsip: Make it possible to control transfers through ARI                    
  Call transfers on the PJSIP channel can now be controlled by
  ARI. This can be enabled by using the PJSIP_TRANSFER_HANDLING(ari-only)
  dialplan function.


### Upgrade Notes:


### Commit Authors:

- Allan Nathanson: (1)
- Ben Ford: (1)
- Fabriziopicconi: (1)
- George Joseph: (10)
- Holger Hans Peter Freyther: (1)
- Jeremy Lainé: (1)
- Joshua Elson: (1)
- Luz Paz: (3)
- Maximilian Fridrich: (1)
- Mike Bradeen: (1)
- Naveen Albert: (1)
- Sean Bright: (6)

## Issue and Commit Detail:

### Closed Issues:

  - 211: [bug]: stasis: Off-nominal channel leave causes bridge to be destroyed
  - 1085: [bug]: utils: Compilation failure with DEVMODE due to old-style definitions
  - 1101: [bug]: when setting a  var with a double quotes and using Set(HASH)
  - 1109: [bug]: Off nominal memory leak in res/ari/resource_channels.c
  - 1112: [bug]: STIR/SHAKEN verification doesn't allow anonymous callerid to be passed to the dialplan.
  - 1119: [bug]: Realtime database not working after upgrade from 22.0.0 to 22.2.0
  - 1122: Need status on CVE-2024-57520 claim.
  - 1124: [bug]: Race condition between bridge and channel delete can over-write cause code set in hangup.
  - 1131: [bug]: CHANGES link broken in README.md
  - 1135: [bug]: Problems with video decoding due to RTP marker bit set
  - 1149: [bug]: res_pjsip: Mismatch in tcp_keepalive_enable causes not to enable
  - 1164: [bug]: WARNING Message in messages.log for res_curl.conf [globals]

### Commits By Author:

- #### Allan Nathanson (1):
  - config.c: #include of non-existent file should not crash

- #### Ben Ford (1):
  - documentation: Update Gosub, Goto, and add new documentationtype.

- #### George Joseph (10):
  - docs: Add version information to ARI resources and methods.
  - docs: Add version information to AGI command XML elements.
  - func_strings.c: Prevent SEGV in HASH single-argument mode.
  - resource_channels.c: Fix memory leak in ast_ari_channels_external_media.
  - res_stir_shaken: Allow missing or anonymous CID to continue to the dialplan.
  - res_config_pgsql: Fix regression that removed dbname config.
  - bridging: Fix multiple bridging issues causing SEGVs and FRACKs.
  - swagger_model.py: Fix invalid escape sequence in get_list_parameter_type().
  - manager.c: Check for restricted file in action_createconfig.
  - README.md: Updates and Fixes

- #### Holger Hans Peter Freyther (1):
  - ari/pjsip: Make it possible to control transfers through ARI

- #### Jeremy Lainé (1):
  - docs: Fix minor typo in MixMonitor AMI action

- #### Joshua Elson (1):
  - fix: Correct default flag for tcp_keepalive_enable option

- #### Luz Paz (3):
  - docs: Fix various typos in main/ Found via `codespell -q 3 -S "./CREDITS" -L a..
  - docs: Fix various typos in channels/ Found via `codespell -q 3 -S "./CREDITS,*..
  - docs: Fix typos in cdr/ Found via codespell

- #### Maximilian Fridrich (1):
  - Revert "res_rtp_asterisk.c: Set Mark on rtp when timestamp skew is too big"

- #### Mike Bradeen (1):
  - bridge_channel: don't set cause code on channel during bridge delete if alread..

- #### Naveen Albert (1):
  - utils: Disable old style definition warnings for libdb.

- #### Sean Bright (6):
  - docs: Indent <since> tags.
  - channel.c: Remove dead AST_GENERATOR_FD code.
  - res_rtp_asterisk.c: Use correct timeout value for T.140 RED timer.
  - docs: AMI documentation fixes.
  - res_rtp_asterisk.c: Don't truncate spec-compliant `ice-ufrag` or `ice-pwd`.
  - res_config_curl.c: Remove unnecessary warnings.

- #### fabriziopicconi (1):
  - rtp.conf.sample: Correct stunaddr example.


### Commit List:

-  documentation: Update Gosub, Goto, and add new documentationtype.
-  res_config_curl.c: Remove unnecessary warnings.
-  README.md: Updates and Fixes
-  res_rtp_asterisk.c: Don't truncate spec-compliant `ice-ufrag` or `ice-pwd`.
-  fix: Correct default flag for tcp_keepalive_enable option
-  docs: AMI documentation fixes.
-  config.c: #include of non-existent file should not crash
-  manager.c: Check for restricted file in action_createconfig.
-  swagger_model.py: Fix invalid escape sequence in get_list_parameter_type().
-  Revert "res_rtp_asterisk.c: Set Mark on rtp when timestamp skew is too big"
-  res_rtp_asterisk.c: Use correct timeout value for T.140 RED timer.
-  docs: Fix typos in cdr/ Found via codespell
-  bridging: Fix multiple bridging issues causing SEGVs and FRACKs.
-  res_config_pgsql: Fix regression that removed dbname config.
-  res_stir_shaken: Allow missing or anonymous CID to continue to the dialplan.
-  resource_channels.c: Fix memory leak in ast_ari_channels_external_media.
-  ari/pjsip: Make it possible to control transfers through ARI
-  channel.c: Remove dead AST_GENERATOR_FD code.
-  func_strings.c: Prevent SEGV in HASH single-argument mode.
-  docs: Add version information to AGI command XML elements.
-  docs: Fix minor typo in MixMonitor AMI action
-  utils: Disable old style definition warnings for libdb.
-  rtp.conf.sample: Correct stunaddr example.
-  docs: Add version information to ARI resources and methods.
-  docs: Indent <since> tags.

### Commit Details:

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


#### res_config_curl.c: Remove unnecessary warnings.
  Author: Sean Bright
  Date:   2025-03-17

  Resolves: #1164

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

#### res_rtp_asterisk.c: Don't truncate spec-compliant `ice-ufrag` or `ice-pwd`.
  Author: Sean Bright
  Date:   2025-03-07

  RFC 8839[1] indicates that the `ice-ufrag` and `ice-pwd` attributes
  can be up to 256 bytes long. While we don't generate values of that
  size, we should be able to accomodate them without truncating.

  1. https://www.rfc-editor.org/rfc/rfc8839#name-ice-ufrag-and-ice-pwd-attri


#### fix: Correct default flag for tcp_keepalive_enable option
  Author: Joshua Elson
  Date:   2025-03-06

  Resolves an issue where the tcp_keepalive_enable option was not properly enabled in the sample configuration due to an incorrect default flag setting.

  Fixes: #1149

#### docs: AMI documentation fixes.
  Author: Sean Bright
  Date:   2025-02-18

  Most of this patch is adding missing PJSIP-related event
  documentation, but the one functional change was adding a sorcery
  to-string handler for endpoint's `redirect_method` which was not
  showing up in the AMI event details or `pjsip show endpoint
  <endpoint>` output.

  The rest of the changes are summarized below:

  * app_agent_pool.c: Typo fix Epoche -> Epoch.
  * stasis_bridges.c: Add missing AttendedTransfer properties.
  * stasis_channels.c: Add missing AgentLogoff properties.
  * pjsip_manager.xml:
    - Add missing AorList properties.
    - Add missing AorDetail properties.
    - Add missing ContactList properties.
    - Add missing ContactStatusDetail properties.
    - Add missing EventDetail properties.
    - Add missing AuthList properties.
    - Add missing AuthDetail properties.
    - Add missing TransportDetail properties.
    - Add missing EndpointList properties.
    - Add missing IdentifyDetail properties.
  * res_pjsip_registrar.c: Add missing InboundRegistrationDetail documentation.
  * res_pjsip_pubsub.c:
    - Add missing ResourceListDetail documentation.
    - Add missing InboundSubscriptionDetail documentation.
    - Add missing OutboundSubscriptionDetail documentation.
  * res_pjsip_outbound_registration.c: Add missing OutboundRegistrationDetail documentation.


#### config.c: #include of non-existent file should not crash
  Author: Allan Nathanson
  Date:   2025-03-03

  Corrects a segmentation fault when a configuration file has a #include
  statement that referenced a file that does not exist.

  Resolves: https://github.com/asterisk/asterisk/issues/1139

#### manager.c: Check for restricted file in action_createconfig.
  Author: George Joseph
  Date:   2025-03-03

  The `CreateConfig` manager action now ensures that a config file can
  only be created in the AST_CONFIG_DIR unless `live_dangerously` is set.

  Resolves: #1122

#### swagger_model.py: Fix invalid escape sequence in get_list_parameter_type().
  Author: George Joseph
  Date:   2025-03-04

  Recent python versions complain when backslashes in strings create invalid
  escape sequences.  This causes issues for strings used as regex patterns like
  `'^List\[(.*)\]$'` where you want the regex parser to treat `[` and `]`
  as literals.  Double-backslashing is one way to fix it but simply converting
  the string to a raw string `re.match(r'^List\[(.*)\]$', text)` is easier
  and less error prone.


#### Revert "res_rtp_asterisk.c: Set Mark on rtp when timestamp skew is too big"
  Author: Maximilian Fridrich
  Date:   2025-02-28

  This reverts commit f30ad96b3f467739c38ff415e80bffc4afff1da7.

  The original change was not RFC compliant and caused issues because it
  set the RTP marker bit in cases when it shouldn't be set. See the
  linked issue #1135 for a detailed explanation.

  Fixes: #1135.

#### res_rtp_asterisk.c: Use correct timeout value for T.140 RED timer.
  Author: Sean Bright
  Date:   2025-02-24

  Found while reviewing #1128


#### docs: Fix typos in cdr/ Found via codespell
  Author: Luz Paz
  Date:   2025-02-12


#### docs: Fix various typos in channels/ Found via `codespell -q 3 -S "./CREDITS,*..
  Author: Luz Paz
  Date:   2025-02-04


#### docs: Fix various typos in main/ Found via `codespell -q 3 -S "./CREDITS" -L a..
  Author: Luz Paz
  Date:   2025-02-04


#### bridging: Fix multiple bridging issues causing SEGVs and FRACKs.
  Author: George Joseph
  Date:   2025-01-22

  Issues:

  * The bridging core allowed multiple bridges to be created with the same
    unique bridgeId at the same time.  Only the last bridge created with the
    duplicate name was actually saved to the core bridges container.

  * The bridging core was creating a stasis topic for the bridge and saving it
    in the bridge->topic field but not increasing its reference count.  In the
    case where two bridges were created with the same uniqueid (which is also
    the topic name), the second bridge would get the _existing_ topic the first
    bridge created.  When the first bridge was destroyed, it would take the
    topic with it so when the second bridge attempted to publish a message to
    it it either FRACKed or SEGVd.

  * The bridge destructor, which also destroys the bridge topic, is run from the
    bridge manager thread not the caller's thread.  This makes it possible for
    an ARI developer to create a new one with the same uniqueid believing the
    old one was destroyed when, in fact, the old one's destructor hadn't
    completed. This could cause the new bridge to get the old one's topic just
    before the topic was destroyed.  When the new bridge attempted to publish
    a message on that topic, asterisk could either FRACK or SEGV.

  * The ARI bridges resource also allowed multiple bridges to be created with
    the same uniqueid but it kept the duplicate bridges in its app_bridges
    container.  This created a situation where if you added two bridges with
    the same "bridge1" uniqueid, all operations on "bridge1" were performed on
    the first bridge created and the second was basically orphaned.  If you
    attempted to delete what you thought was the second bridge, you actually
    deleted the first one created.

  Changes:

  * A new API `ast_bridge_topic_exists(uniqueid)` was created to determine if
    a topic already exists for a bridge.

  * `bridge_base_init()` in bridge.c and `ast_ari_bridges_create()` in
    resource_bridges.c now call `ast_bridge_topic_exists(uniqueid)` to check
    if a bridge with the requested uniqueid already exists and will fail if it
    does.

  * `bridge_register()` in bridges.c now checks the core bridges container to
    make sure a bridge doesn't already exist with the requested uniqueid.
    Although most callers of `bridge_register()` will have already called
    `bridge_base_init()`, which will now fail on duplicate bridges, there
    is no guarantee of this so we must check again.

  * The core bridges container allocation was changed to reject duplicate
    uniqueids instead of silently replacing an existing one. This is a "belt
    and suspenders" check.

  * A global mutex was added to bridge.c to prevent concurrent calls to
    `bridge_base_init()` and `bridge_register()`.

  * Even though you can no longer create multiple bridges with the same uniqueid
    at the same time, it's still possible that the bridge topic might be
    destroyed while a second bridge with the same uniqueid was trying to use
    it. To address this, the bridging core now increments the reference count
    on bridge->topic when a bridge is created and decrements it when the
    bridge is destroyed.

  * `bridge_create_common()` in res_stasis.c now checks the stasis app_bridges
    container to make sure a bridge with the requested uniqueid doesn't already
    exist.  This may seem like overkill but there are so many entrypoints to
    bridge creation that we need to be safe and catch issues as soon in the
    process as possible.

  * The stasis app_bridges container allocation was changed to reject duplicate
    uniqueids instead of adding them. This is a "belt and suspenders" check.

  * The `bridge show all` CLI command now shows the bridge name as well as the
    bridge id.

  * Response code 409 "Conflict" was added as a possible response from the ARI
    bridge create resources to signal that a bridge with the requested uniqueid
    already exists.

  * Additional debugging was added to multiple bridging and stasis files.

  Resolves: #211

#### bridge_channel: don't set cause code on channel during bridge delete if alread..
  Author: Mike Bradeen
  Date:   2025-02-18

  Due to a potential race condition via ARI when hanging up a channel hangup with cause
  while also deleting a bridge containing that channel, the bridge delete can over-write
  the hangup cause code resulting in Normal Call Clearing instead of the set value.

  With this change, bridge deletion will only set the hangup code if it hasn't been
  previously set.

  Resolves: #1124

#### res_config_pgsql: Fix regression that removed dbname config.
  Author: George Joseph
  Date:   2025-02-11

  A recent commit accidentally removed the code that sets dbname.
  This commit adds it back in.

  Resolves: #1119

#### res_stir_shaken: Allow missing or anonymous CID to continue to the dialplan.
  Author: George Joseph
  Date:   2025-02-05

  The verification check for missing or anonymous callerid was happening before
  the endpoint's profile was retrieved which meant that the failure_action
  parameter wasn't available.  Therefore, if verification was enabled and there
  was no callerid or it was "anonymous", the call was immediately terminated
  instead of giving the dialplan the ability to decide what to do with the call.

  * The callerid check now happens after the verification context is created and
    the endpoint's stir_shaken_profile is available.

  * The check now processes the callerid failure just as it does for other
    verification failures and respects the failure_action parameter.  If set
    to "continue" or "continue_return_reason", `STIR_SHAKEN(0,verify_result)`
    in the dialplan will return "invalid_or_no_callerid".

  * If the endpoint's failure_action is "reject_request", the call will be
    rejected with `433 "Anonymity Disallowed"`.

  * If the endpoint's failure_action is "continue_return_reason", the call will
    continue but a `Reason: STIR; cause=433; text="Anonymity Disallowed"`
    header will be added to the next provisional or final response.

  Resolves: #1112

#### resource_channels.c: Fix memory leak in ast_ari_channels_external_media.
  Author: George Joseph
  Date:   2025-02-04

  Between ast_ari_channels_external_media(), external_media_rtp_udp(),
  and external_media_audiosocket_tcp(), the `variables` structure being passed
  around wasn't being cleaned up properly when there was a failure.

  * In ast_ari_channels_external_media(), the `variables` structure is now
    defined with RAII_VAR to ensure it always gets cleaned up.

  * The ast_variables_destroy() call was removed from external_media_rtp_udp().

  * The ast_variables_destroy() call was removed from
    external_media_audiosocket_tcp(), its `endpoint` allocation was changed to
    to use ast_asprintf() as external_media_rtp_udp() does, and it now
    returns an error on failure.

  * ast_ari_channels_external_media() now checks the new return code from
    external_media_audiosocket_tcp() and sets the appropriate error response.

  Resolves: #1109

#### ari/pjsip: Make it possible to control transfers through ARI
  Author: Holger Hans Peter Freyther
  Date:   2024-06-15

  Introduce a ChannelTransfer event and the ability to notify progress to
  ARI. Implement emitting this event from the PJSIP channel instead of
  handling the transfer in Asterisk when configured.

  Introduce a dialplan function to the PJSIP channel to switch between the
  "core" and "ari-only" behavior.

  UserNote: Call transfers on the PJSIP channel can now be controlled by
  ARI. This can be enabled by using the PJSIP_TRANSFER_HANDLING(ari-only)
  dialplan function.


#### channel.c: Remove dead AST_GENERATOR_FD code.
  Author: Sean Bright
  Date:   2025-02-06

  Nothing ever sets the `AST_GENERATOR_FD`, so this block of code will
  never execute. It also is the only place where the `generate` callback
  is called with the channel lock held which made it difficult to reason
  about the thread safety of `ast_generator`s.

  In passing, also note that `AST_AGENT_FD` isn't used either.


#### func_strings.c: Prevent SEGV in HASH single-argument mode.
  Author: George Joseph
  Date:   2025-01-30

  When in single-argument mode (very rarely used), a malformation of a column
  name (also very rare) could cause a NULL to be returned when retrieving the
  channel variable for that column.  Passing that to strncat causes a SEGV.  We
  now check for the NULL and print a warning message.

  Resolves: #1101

#### docs: Add version information to AGI command XML elements.
  Author: George Joseph
  Date:   2025-01-24

  This process was a bit different than the others because everything
  is in the same file, there's an array that contains the command
  names and their handler functions, and the last command was created
  over 15 years ago.

  * Dump a `git blame` of res/res_agi.c from BEFORE the handle_* prototypes
    were changed.
  * Create a command <> handler function xref by parsing the the agi_command
    array.
  * For each entry, grep the function definition line "static int handle_*"
    from the git blame output and capture the commit.  This will be the
    commit the command was created in.
  * Do a `git tag --contains <commit> | sort -V | head -1` to get the
    tag the function was created in.
  * Add a single since/version element to the command XML.  Multiple versions
    aren't supported here because the branching and tagging scheme changed
    several times in the 2000's.


#### docs: Fix minor typo in MixMonitor AMI action
  Author: Jeremy Lainé
  Date:   2025-01-28

  The `Options` argument was erroneously documented as lowercase
  `options`.


#### utils: Disable old style definition warnings for libdb.
  Author: Naveen Albert
  Date:   2025-01-23

  Newer versions of gcc now warn about old style definitions, such
  as those in libdb, which causes compilation failure with DEVMODE
  enabled. Ignore these warnings for libdb.

  Resolves: #1085

#### rtp.conf.sample: Correct stunaddr example.
  Author: fabriziopicconi
  Date:   2024-09-25


#### docs: Add version information to ARI resources and methods.
  Author: George Joseph
  Date:   2025-01-27

  * Dump a git blame of each file in rest-api/api-docs.

  * Get the commit for each "resourcePath" and "httpMethod" entry.

  * Find the tags for each commit (same as other processes).

  * Insert a "since" array after each "resourcePath" and "httpMethod" entry.


#### docs: Indent <since> tags.
  Author: Sean Bright
  Date:   2025-01-23

  Also updates the 'since' of applications/functions that existed before
  XML documentation was introduced (1.6.2.0).


