
## Change Log for Release asterisk-21.10.0-rc2

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-21.10.0-rc2.html)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/21.10.0-rc1...21.10.0-rc2)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21.10.0-rc2.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 2
- Commit Authors: 2
- Issues Resolved: 3
- Security Advisories Resolved: 0

### User Notes:


### Upgrade Notes:


### Commit Authors:

- Michal Hajek: (1)
- Sean Bright: (1)

## Issue and Commit Detail:

### Closed Issues:

  - 1276: MixMonitor produces broken recordings in bridged calls with asymmetric codecs (e.g., alaw vs G.722)
  - 1279: [bug]: regression: 20.12.0 downgrades quality of wav16 recordings
  - 1282: [bug]: Alternate Channel Storage Backends menuselect not enabling it

### Commits By Author:

- #### Michal Hajek (1):
  - audiohook.c: Improve frame pairing logic to avoid MixMonitor breakage with mix..

- #### Sean Bright (1):
  - channelstorage_makeopts.xml: Remove errant XML character.


### Commit List:

-  channelstorage_makeopts.xml: Remove errant XML character.

### Commit Details:

#### audiohook.c: Improve frame pairing logic to avoid MixMonitor breakage with mix..
  Author: Michal Hajek
  Date:   2025-05-21

  This patch adjusts the read/write synchronization logic in audiohook_read_frame_both()
  to better handle calls where participants use different codecs or sample sizes
  (e.g., alaw vs G.722). The previous hard threshold of 2 * samples caused MixMonitor
  recordings to break or stutter when frames were not aligned between both directions.

  The new logic uses a more tolerant limit (1.5 * samples), which prevents audio tearing
  without causing excessive buffer overruns. This fix specifically addresses issues
  with MixMonitor when recording directly on a channel in a bridge using mixed codecs.

  Reported-by: Michal Hajek <michal.hajek@daktela.com>

  Resolves: #1276
  Resolves: #1279

#### channelstorage_makeopts.xml: Remove errant XML character.
  Author: Sean Bright
  Date:   2025-06-30

  Resolves: #1282

