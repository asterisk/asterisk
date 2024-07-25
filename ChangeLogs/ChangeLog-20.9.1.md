
## Change Log for Release asterisk-20.9.1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.9.1.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.9.0...20.9.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.9.1.tar.gz)  
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

  - 819: [bug]: Typo in voicemail.conf.sample that stops it from loading when using "make samples"
  - 822: [bug]: segfault in main/rtp_engine.c:1489 after updating 20.8.1 -> 20.9.0

### Commits By Author:

- #### George Joseph (2):
  - voicemail.conf.sample: Fix ':' comment typo
  - rtp_engine.c: Prevent segfault in ast_rtp_codecs_payloads_unset()


### Commit List:

-  rtp_engine.c: Prevent segfault in ast_rtp_codecs_payloads_unset()
-  voicemail.conf.sample: Fix ':' comment typo

### Commit Details:

#### rtp_engine.c: Prevent segfault in ast_rtp_codecs_payloads_unset()
  Author: George Joseph
  Date:   2024-07-25

  There can be empty slots in payload_mapping_tx corresponding to
  dynamic payload types that haven't been seen before so we now
  check for NULL before attempting to use 'type' in the call to
  ast_format_cmp.

  Note: Currently only chan_sip calls ast_rtp_codecs_payloads_unset()

  Resolves: #822

#### voicemail.conf.sample: Fix ':' comment typo
  Author: George Joseph
  Date:   2024-07-24

  ...and removed an errant trailing space.

  Resolves: #819

