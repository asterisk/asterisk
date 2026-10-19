
## Change Log for Release asterisk-certified-20.7-cert12

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-20.7-cert12.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-20.7-cert11...certified-20.7-cert12)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-20.7-cert12.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

### Summary:

- Commits: 14
- Commit Authors: 4
- Issues Resolved: 8
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Developer Notes:


### Commit Authors:

- George Joseph: (5)
- Mike Bradeen: (1)
- Naveen Albert: (5)
- Sean Bright: (3)

## Issue and Commit Detail:

### Closed Issues:

  - #1781: [bug]: More discarded-qualifiers errors with gcc 15.2.1
  - #1783: [bug]: Several unused-but-set-variable warnings with gcc 16
  - #1786: [bug]: chan_dahdi: A few more discarded-qualifiers errors not caught previously
  - #1903: [bug]: g++ 16 no longer defines __STDC_VERSION__ causing channelstorage_cpp_map_name_id.cc to fail
  - #1947: [bug]: chan_dahdi fails to build with gcc-16 when openr2 is installed
  - #2054: [bug]: backtrace.c: Compilation failure due to use of removed type
  - #2061: [bug]: backtrace.c: stdbool.h needed for portability
  - #2230: [bug]: The timestamp on the first packet after in-band digit generation is incorrect

### Commits By Author:

- **George Joseph** (5):
  - SECURITY.md: Add warning about reporting multiple issues in one advisory.
  - res_stir_shaken.c: Fix GCC set-but-not-used error in unload_module.
  - chan_dahdi: Fix set but not used in mfcr2_show_links_of().
  - compat.h: Ensure check for `__STDC_VERSION__` is not attempted for c++.
  - SECURITY.md: Update with additional instructions.

- **Mike Bradeen** (1):
  - res_rtp_asterisk: Fix rtp timestamp calculation during DTMF generation

- **Naveen Albert** (5):
  - backtrace.c: Include stdbool.h
  - backtrace.c: Avoid removed bfd_boolean type.
  - build: Fix another GCC discarded-qualifiers const error.
  - chan_dahdi: Fix discarded-qualifiers errors.
  - build: Fix unused-but-set-variable warnings with gcc 16.

- **Sean Bright** (3):
  - app_voicemail.c: Fix may-be-uninitialized error
  - chan_sip.c: Fix __sip_reliable_xmit build error
  - build: Fix GCC discarded-qualifiers const errors.

### Commit List:

-  res_rtp_asterisk: Fix rtp timestamp calculation during DTMF generation
-  backtrace.c: Include stdbool.h
-  backtrace.c: Avoid removed bfd_boolean type.
-  app_voicemail.c: Fix may-be-uninitialized error
-  build: Fix another GCC discarded-qualifiers const error.
-  SECURITY.md: Add warning about reporting multiple issues in one advisory.
-  res_stir_shaken.c: Fix GCC set-but-not-used error in unload_module.
-  chan_sip.c: Fix __sip_reliable_xmit build error
-  chan_dahdi: Fix discarded-qualifiers errors.
-  build: Fix unused-but-set-variable warnings with gcc 16.
-  build: Fix GCC discarded-qualifiers const errors.
-  chan_dahdi: Fix set but not used in mfcr2_show_links_of().
-  compat.h: Ensure check for `__STDC_VERSION__` is not attempted for c++.
-  SECURITY.md: Update with additional instructions.

### Commit Details:

__res_rtp_asterisk: Fix rtp timestamp calculation during DTMF generation__
  Author: Mike Bradeen
  Date:   2026-10-13

  Avoid advancing the last tx timestamp when audio is suppressed during DTMF
  generation to avoid mis-calculating the timestamp on the first packet after
  the RTP Event end.

  Resolves: #2230

__backtrace.c: Include stdbool.h__
  Author: Naveen Albert
  Date:   2026-08-04

  stdbool.h is needed now that we use the bool type directly
  instead of bfd_boolean. See also 1d95b744c06974f0a00c143c6e0fc979af930908.

  Resolves: #2061

__backtrace.c: Avoid removed bfd_boolean type.__
  Author: Naveen Albert
  Date:   2026-08-03

  bfd_boolean has been deprecated for some time and has now been
  removed in gdb, so use bool directly instead.

  See: https://github.com/gnutools/binutils-gdb/commit/1d95b744c06974f0a00c143c6e0fc979af930908

  Resolves: #2054

__app_voicemail.c: Fix may-be-uninitialized error__
  Author: Sean Bright
  Date:   2026-08-02

  A pointer to `new` is passed to `inboxcount(...)` which increments the
  pointed-to value.

__build: Fix another GCC discarded-qualifiers const error.__
  Author: Naveen Albert
  Date:   2026-02-18

  Follow on commit to 27a39cba7e6832cb30cb64edaf879f447b669628
  to fix compilation with BETTER_BACKTRACES with gcc 15.2.1.

  Resolves: #1781

__SECURITY.md: Add warning about reporting multiple issues in one advisory.__
  Author: George Joseph
  Date:   2026-08-04


__res_stir_shaken.c: Fix GCC set-but-not-used error in unload_module.__
  Author: George Joseph
  Date:   2026-07-31


__chan_sip.c: Fix __sip_reliable_xmit build error__
  Author: Sean Bright
  Date:   2024-10-17

  Fixes #954

__chan_dahdi: Fix discarded-qualifiers errors.__
  Author: Naveen Albert
  Date:   2026-02-18

  Fix discarded-qualifiers errors to compile successfully with gcc 15.2.1.

  Associated changes have also been made to libss7; however, for compatibility
  we cast const char* values to char*. In the future, these casts could be
  removed.

  Resolves: #1786

__build: Fix unused-but-set-variable warnings with gcc 16.__
  Author: Naveen Albert
  Date:   2026-02-18

  Fix or remove a few variables that were being set but not actually
  used anywhere, causing warnings with gcc 16.

  Resolves: #1783

__build: Fix GCC discarded-qualifiers const errors.__
  Author: Sean Bright
  Date:   2026-07-28

  GCC 15.2.1 pays attention to the discarding of the const
  qualifier when strchr, strrchr, memchr, or memrchr are now
  used. This change fixes numerous errors with this throughout
  chan_sip. The fixes can be broken down into the following:

  1. The value passed to strchr or strrchr can be cast as it is
     expected and allowed to be modified.
  2. The pointer passed to strchr or strrchr is not meant to be
     modified and so the contents must be duplicated.
  3. It was declared const and never should have been.

__chan_dahdi: Fix set but not used in mfcr2_show_links_of().__
  Author: George Joseph
  Date:   2026-05-21

  When openr2 is installed mfcr2_show_links_of() is no longer ifdeffed out
  which makes gcc-16 complain with 'variable ‘x’ set but not used'.

  Resolves: #1947

__compat.h: Ensure check for `__STDC_VERSION__` is not attempted for c++.__
  Author: George Joseph
  Date:   2026-04-27

  `__STDC_VERSION__` is specific to C but up until gcc 16, the g++ compiler
  also defined it.  With g++ 16.0 it's no longer defined (which is the correct
  behavior) so compiling channelstorage_cpp_map_name_id.cc fails.  The
  check for `__STDC_VERSION__` in compat.h is now skipped if we're compiling
  a C++ source file.

  Resolves: #1903

__SECURITY.md: Update with additional instructions.__
  Author: George Joseph
  Date:   2026-03-19

  Also added line breaks for people reading this file directly
  from the code base.


