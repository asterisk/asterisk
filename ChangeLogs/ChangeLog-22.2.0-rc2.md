
## Change Log for Release asterisk-22.2.0-rc2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.2.0-rc2.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.2.0-rc1...22.2.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.2.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 3
- Commit Authors: 1
- Issues Resolved: 2
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:

- #### alembic: Database updates required.                                             
  Two commits in this release...
  'Add SHA-256 and SHA-512-256 as authentication digest algorithms'
  'res_pjsip: Add new AOR option "qualify_2xx_only"'
  ...have modified alembic scripts for the following database tables: ps_aors,
  ps_contacts, ps_auths, ps_globals. If you don't use the scripts to update
  your database, reads from those tables will succeeed but inserts into the
  ps_contacts table by res_pjsip_registrar will fail.


### Commit Authors:

- George Joseph: (3)

## Issue and Commit Detail:

### Closed Issues:

  - 1095: [bug]: res_pjsip missing "Failed to authenticate" log entry for unknown endpoint
  - 1097: [bug]: res_pjsip/pjsip_options. ODBC: Unknown column 'qualify_2xx_only'

### Commits By Author:

- #### George Joseph (3):
  - res_pjsip: Fix startup/reload memory leak in config_auth.
  - alembic: Database updates required.
  - res_pjsip_authenticator_digest: Make correct error messages appear again.


### Commit List:

-  res_pjsip_authenticator_digest: Make correct error messages appear again.
-  alembic: Database updates required.
-  res_pjsip: Fix startup/reload memory leak in config_auth.

### Commit Details:

#### res_pjsip_authenticator_digest: Make correct error messages appear again.
  Author: George Joseph
  Date:   2025-01-28

  When an incoming request can't be matched to an endpoint, the "artificial"
  auth object is used to create a challenge to return in a 401 response and we
  emit a "No matching endpoint found" log message. If the client then responds
  with an Authorization header but the request still can't be matched to an
  endpoint, the verification will fail and, as before, we'll create a challenge
  to return in a 401 response and we emit a "No matching endpoint found" log
  message.  HOWEVER, because there WAS an Authorization header and it failed
  verification, we should have also been emitting a "Failed to authenticate"
  log message but weren't because there was a check that short-circuited that
  it if the artificial auth was used.  Since many admins use the "Failed to
  authenticate" message with log parsers like fail2ban, those attempts were not
  being recognized as suspicious.

  Changes:

  * digest_check_auth() now always emits the "Failed to authenticate" log
    message if verification of an Authorization header failed even if the
    artificial auth was used.

  * The verification logic was refactored to be clearer about the handling
    of the return codes from verify().

  * Comments were added clarify what return codes digest_check_auth() should
    return to the distributor and the implications of changing them.

  Resolves: #1095

#### alembic: Database updates required.
  Author: George Joseph
  Date:   2025-01-28

  This commit doesn't actually change anything.  It just adds the following
  upgrade notes that were omitted from the original commits.

  Resolves: #1097

  UpgradeNote: Two commits in this release...
  'Add SHA-256 and SHA-512-256 as authentication digest algorithms'
  'res_pjsip: Add new AOR option "qualify_2xx_only"'
  ...have modified alembic scripts for the following database tables: ps_aors,
  ps_contacts, ps_auths, ps_globals. If you don't use the scripts to update
  your database, reads from those tables will succeeed but inserts into the
  ps_contacts table by res_pjsip_registrar will fail.

#### res_pjsip: Fix startup/reload memory leak in config_auth.
  Author: George Joseph
  Date:   2025-01-23

  An issue in config_auth.c:ast_sip_auth_digest_algorithms_vector_init() was
  causing double allocations for the two supported_algorithms vectors to the
  tune of 915 bytes.  The leak only happens on startup and when a reload is done
  and doesn't get bigger with the number of auth objects defined.

  * Pre-initialized the two vectors in config_auth:auth_alloc().
  * Removed the allocations in ast_sip_auth_digest_algorithms_vector_init().
  * Added a note to the doc for ast_sip_auth_digest_algorithms_vector_init()
    noting that the vector passed in should be initialized and empty.
  * Simplified the create_artificial_auth() function in pjsip_distributor.
  * Set the vector initialization count to 0 in config_global:global_apply().

