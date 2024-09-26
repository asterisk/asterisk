
## Change Log for Release asterisk-22.0.0-rc2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-22.0.0-rc2.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/22.0.0-rc1...22.0.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-22.0.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 3
- Commit Authors: 1
- Issues Resolved: 3
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Commit Authors:

- George Joseph: (3)

## Issue and Commit Detail:

### Closed Issues:

  - 884: [bug]: A ':' at the top of in stir_shaken.conf make Asterisk producing a core file when starting
  - 889: [bug]: res_stir_shaken/verification.c has a stale include for jansson.h that can cause compilation to fail
  - 904: [bug]: stir_shaken: attest_level isn't being propagated correctly from attestation to profile to tn

### Commits By Author:

- #### George Joseph (3):
  - res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid
  - res_stir_shaken: Remove stale include for jansson.h in verification.c
  - stir_shaken: Fix propagation of attest_level and a few other values


### Commit List:

-  stir_shaken: Fix propagation of attest_level and a few other values
-  res_stir_shaken: Remove stale include for jansson.h in verification.c
-  res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid

### Commit Details:

#### stir_shaken: Fix propagation of attest_level and a few other values
  Author: George Joseph
  Date:   2024-09-24

  attest_level, send_mky and check_tn_cert_public_url weren't
  propagating correctly from the attestation object to the profile
  and tn.

  * In the case of attest_level, the enum needed to be changed
  so the "0" value (the default) was "NOT_SET" instead of "A".  This
  now allows the merging of the attestation object, profile and tn
  to detect when a value isn't set and use the higher level value.

  * For send_mky and check_tn_cert_public_url, the tn default was
  forced to "NO" which always overrode the profile and attestation
  objects.  Their defaults are now "NOT_SET" so the propagation
  happens correctly.

  * Just to remove some redundant code in tn_config.c, a bunch of calls to
  generate_sorcery_enum_from_str() and generate_sorcery_enum_to_str() were
  replaced with a single call to generate_acfg_common_sorcery_handlers().

  Resolves: #904

#### res_stir_shaken: Remove stale include for jansson.h in verification.c
  Author: George Joseph
  Date:   2024-09-17

  verification.c had an include for jansson.h left over from previous
  versions of the module.  Since res_stir_shaken no longer has a
  dependency on jansson, the bundled version wasn't added to GCC's
  include path so if you didn't also have a jansson development package
  installed, the compile would fail.  Removing the stale include
  was the only thing needed.

  Resolves: #889

#### res_stir_shaken.c: Fix crash when stir_shaken.conf is invalid
  Author: George Joseph
  Date:   2024-09-13

  * If the call to ast_config_load() returns CONFIG_STATUS_FILEINVALID,
  check_for_old_config() now returns LOAD_DECLINE instead of continuing
  on with a bad pointer.

  * If CONFIG_STATUS_FILEMISSING is returned, check_for_old_config()
  assumes the config is being loaded from realtime and now returns
  LOAD_SUCCESS.  If it's actually not being loaded from realtime,
  sorcery will catch that later on.

  * Also refactored the error handling in load_module() a bit.

  Resolves: #884

