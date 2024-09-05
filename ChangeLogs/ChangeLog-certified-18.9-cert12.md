
## Change Log for Release asterisk-certified-18.9-cert12

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-18.9-cert12.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert11...certified-18.9-cert12)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-18.9-cert12.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 6
- Commit Authors: 5
- Issues Resolved: 3
- Security Advisories Resolved: 1
  - [GHSA-v428-g3cw-7hv9](https://github.com/asterisk/asterisk/security/advisories/GHSA-v428-g3cw-7hv9): A malformed Contact or Record-Route URI in an incoming SIP request can cause Asterisk to crash when res_resolver_unbound is used

### User Notes:

- #### res_pjsip_notify: add dialplan application                                      
  A new dialplan application PJSIPNotify is now available
  which can send SIP NOTIFY requests from the dialplan.
  The pjsip send notify CLI command has also been enhanced to allow
  sending NOTIFY messages to a specific channel. Syntax:
  pjsip send notify <option> channel <channel>

- #### channel: Add multi-tenant identifier.                                           
  tenantid has been added to channels. It can be read in
  dialplan via CHANNEL(tenantid), and it can be set using
  Set(CHANNEL(tenantid)=My tenant ID). In pjsip.conf, it is recommended to
  use the new tenantid option for pjsip endpoints (e.g., tenantid=My
  tenant ID) so that it will show up in Newchannel events. You can set it
  like any other channel variable using set_var in pjsip.conf as well, but
  note that this will NOT show up in Newchannel events. Tenant ID is also
  available in CDR and can be accessed with CDR(tenantid). The peer tenant
  ID can also be accessed with CDR(peertenantid). CEL includes tenant ID
  as well if it has been set.


### Upgrade Notes:

- #### channel: Add multi-tenant identifier.                                           
  A new versioned struct (ast_channel_initializers) has been
  added that gets passed to __ast_channel_alloc_ap. The new function
  ast_channel_alloc_with_initializers should be used when creating
  channels that require the use of this struct. Currently the only value
  in the struct is for tenantid, but now more fields can be added to the
  struct as necessary rather than the __ast_channel_alloc_ap function. A
  new option (tenantid) has been added to endpoints in pjsip.conf as well.
  CEL has had its version bumped to include tenant ID.


### Commit Authors:

- Ben Ford: (1)
- George Joseph: (2)
- Jean-Denis Girard: (1)
- Mike Bradeen: (1)
- Sean Bright: (1)

## Issue and Commit Detail:

### Closed Issues:

  - !GHSA-v428-g3cw-7hv9: A malformed Contact or Record-Route URI in an incoming SIP request can cause Asterisk to crash when res_resolver_unbound is used
  - 740: [new-feature]: Add multi-tenant identifier to chan_pjsip
  - 799: [improvement]: Add PJSIPNOTIFY dialplan application
  - 831: [bug]: app_voicemail ODBC

### Commits By Author:

- #### Ben Ford (1):
  - channel: Add multi-tenant identifier.

- #### George Joseph (2):
  - res_resolver_unbound: Test for NULL ub_result in unbound_resolver_callback
  - manager.c: Fix FRACK when doing CoreShowChannelMap in DEVMODE

- #### Jean-Denis Girard (1):
  - app_voicemail: Fix sql insert mismatch caused by cherry-pick

- #### Mike Bradeen (1):
  - res_pjsip_notify: add dialplan application

- #### Sean Bright (1):
  - alembic: Make 'revises' header comment match reality.


### Commit List:

-  app_voicemail: Fix sql insert mismatch caused by cherry-pick
-  alembic: Make 'revises' header comment match reality.
-  res_pjsip_notify: add dialplan application
-  manager.c: Fix FRACK when doing CoreShowChannelMap in DEVMODE
-  channel: Add multi-tenant identifier.
-  res_resolver_unbound: Test for NULL ub_result in unbound_resolver_callback

### Commit Details:

#### app_voicemail: Fix sql insert mismatch caused by cherry-pick
  Author: Jean-Denis Girard
  Date:   2024-08-07

  When commit e8c9cb80 was cherry-picked in from master, the
  fact that the 20 and 18 branches still had the old "macrocontext"
  column wasn't taken into account so the number of named parameters
  didn't match the number of '?' placeholders.  They do now.

  We also now use ast_asprintf to create the full mailbox query SQL
  statement instead of trying to calculate the proper length ourselves.

  Resolves: #831

#### alembic: Make 'revises' header comment match reality.
  Author: Sean Bright
  Date:   2024-08-17


#### res_pjsip_notify: add dialplan application
  Author: Mike Bradeen
  Date:   2024-07-09

  Add dialplan application PJSIPNOTIFY to send either pre-configured
  NOTIFY messages from pjsip_notify.conf or with headers defined in
  dialplan.

  Also adds the ability to send pre-configured NOTIFY commands to a
  channel via the CLI.

  Resolves: #799

  UserNote: A new dialplan application PJSIPNotify is now available
  which can send SIP NOTIFY requests from the dialplan.

  The pjsip send notify CLI command has also been enhanced to allow
  sending NOTIFY messages to a specific channel. Syntax:

  pjsip send notify <option> channel <channel>


#### manager.c: Fix FRACK when doing CoreShowChannelMap in DEVMODE
  Author: George Joseph
  Date:   2024-08-08

  If you run an AMI CoreShowChannelMap on a channel that isn't in a
  bridge and you're in DEVMODE, you can get a FRACK because the
  bridge id is empty.  We now simply return an empty list for that
  request.


#### channel: Add multi-tenant identifier.
  Author: Ben Ford
  Date:   2024-05-21

  This patch introduces a new identifier for channels: tenantid. It's
  a stringfield on the channel that can be used for general purposes. It
  will be inherited by other channels the same way that linkedid is.

  You can set tenantid in a few ways. The first is to set it in the
  dialplan with the Set and CHANNEL functions:

  exten => example,1,Set(CHANNEL(tenantid)=My tenant ID)

  It can also be accessed via CHANNEL:

  exten => example,2,NoOp(CHANNEL(tenantid))

  Another method is to use the new tenantid option for pjsip endpoints in
  pjsip.conf:

  [my_endpoint]
  type=endpoint
  tenantid=My tenant ID

  This is considered the best approach since you will be able to see the
  tenant ID as early as the Newchannel event.

  It can also be set using set_var in pjsip.conf on the endpoint like
  setting other channel variable:

  set_var=CHANNEL(tenantid)=My tenant ID

  Note that set_var will not show tenant ID on the Newchannel event,
  however.

  Tenant ID has also been added to CDR. It's read-only and can be accessed
  via CDR(tenantid). You can also get the tenant ID of the last channel
  communicated with via CDR(peertenantid).

  Tenant ID will also show up in CEL records if it has been set, and the
  version number has been bumped accordingly.

  Fixes: #740

  UserNote: tenantid has been added to channels. It can be read in
  dialplan via CHANNEL(tenantid), and it can be set using
  Set(CHANNEL(tenantid)=My tenant ID). In pjsip.conf, it is recommended to
  use the new tenantid option for pjsip endpoints (e.g., tenantid=My
  tenant ID) so that it will show up in Newchannel events. You can set it
  like any other channel variable using set_var in pjsip.conf as well, but
  note that this will NOT show up in Newchannel events. Tenant ID is also
  available in CDR and can be accessed with CDR(tenantid). The peer tenant
  ID can also be accessed with CDR(peertenantid). CEL includes tenant ID
  as well if it has been set.

  UpgradeNote: A new versioned struct (ast_channel_initializers) has been
  added that gets passed to __ast_channel_alloc_ap. The new function
  ast_channel_alloc_with_initializers should be used when creating
  channels that require the use of this struct. Currently the only value
  in the struct is for tenantid, but now more fields can be added to the
  struct as necessary rather than the __ast_channel_alloc_ap function. A
  new option (tenantid) has been added to endpoints in pjsip.conf as well.
  CEL has had its version bumped to include tenant ID.


#### res_resolver_unbound: Test for NULL ub_result in unbound_resolver_callback
  Author: George Joseph
  Date:   2024-08-12

  The ub_result pointer passed to unbound_resolver_callback by
  libunbound can be NULL if the query was for something malformed
  like `.1` or `[.1]`.  If it is, we now set a 'ns_r_formerr' result
  and return instead of crashing with a SEGV.  This causes pjproject
  to simply cancel the transaction with a "No answer record in the DNS
  response" error.  The existing "off nominal" unit test was also
  updated to check this condition.

  Although not necessary for this fix, we also made
  ast_dns_resolver_completed() tolerant of a NULL result.

  Resolves: GHSA-v428-g3cw-7hv9

