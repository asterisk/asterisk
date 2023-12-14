
Change Log for Release asterisk-certified-18.9-cert6
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-certified-18.9-cert6.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert5...certified-18.9-cert6)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-certified-18.9-cert6.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- res_pjsip_header_funcs: Duplicate new header value, don't copy.
- res_rtp_asterisk.c: Check DTLS packets against ICE candidate list
- manager.c: Prevent path traversal with GetConfig.
- res_pjsip: disable raw bad packet logging

User Notes:
----------------------------------------

- ### app_read: Add an option to return terminator on empty digits.
  A new option 'e' has been added to allow Read() to return the
  terminator as the dialed digits in the case where only the terminator
  is entered.

- ### format_sln: add .slin as supported file extension
  format_sln now recognizes '.slin' as a valid
  file extension in addition to the existing
  '.sln' and '.raw'.

- ### app_directory: Add a 'skip call' option.
  A new option 's' has been added to the Directory() application that
  will skip calling the extension and instead set the extension as
  DIRECTORY_EXTEN channel variable.

- ### app_senddtmf: Add option to answer target channel.
  A new option has been added to SendDTMF() which will answer the
  specified channel if it is not already up. If no channel is specified,
  the current channel will be answered instead.

- ### cli: increase channel column width
  This change increases the display width on 'core show channels'
  amd 'core show channels verbose'
  For 'core show channels', the Channel name field is increased to
  64 characters and the Location name field is increased to 32
  characters.
  For 'core show channels verbose', the Channel name field is
  increased to 80 characters, the Context is increased to 24
  characters and the Extension is increased to 24 characters.

- ### bridge_builtin_features: add beep via touch variable
  Add optional touch variable : TOUCH_MIXMONITOR_BEEP(interval)
  Setting TOUCH_MIXMONITOR_BEEP/TOUCH_MONITOR_BEEP to a valid
  interval in seconds will result in a periodic beep being
  played to the monitored channel upon MixMontior/Monitor
  feature start.
  If an interval less than 5 seconds is specified, the interval
  will default to 5 seconds.  If the value is set to an invalid
  interval, the default of 15 seconds will be used.

- ### test.c: Fix counting of tests and add 2 new tests
  The "tests" attribute of the "testsuite" element in the
  output XML now reflects only the tests actually requested
  to be executed instead of all the tests registered.
  The "failures" attribute was added to the "testsuite"
  element.
  Also added two new unit tests that just pass and fail
  to be used for testing CI itself.

- ### res_mixmonitor: MixMonitorMute by MixMonitor ID
  It is now possible to specify the MixMonitorID when calling
  the manager action: MixMonitorMute.  This will allow an
  individual MixMonitor instance to be muted via ID.
  The MixMonitorID can be stored as a channel variable using
  the 'i' MixMonitor option and is returned upon creation if
  this option is used.
  As part of this change, if no MixMonitorID is specified in
  the manager action MixMonitorMute, Asterisk will set the mute
  flag on all MixMonitor audiohooks on the channel.  Previous
  behavior would set the flag on the first MixMonitor audiohook
  found.


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

None

Commits By Author:
----------------------------------------

- ### Ben Ford (1):
  - manager.c: Prevent path traversal with GetConfig.

- ### George Joseph (1):
  - res_rtp_asterisk.c: Check DTLS packets against ICE candidate list

- ### Gitea (1):
  - res_pjsip_header_funcs: Duplicate new header value, don't copy.

- ### Mike Bradeen (1):
  - res_pjsip: disable raw bad packet logging


Detail:
----------------------------------------

- ### res_pjsip_header_funcs: Duplicate new header value, don't copy.
  Author: Gitea  
  Date:   2023-07-10  

  When updating an existing header the 'update' code incorrectly
  just copied the new value into the existing buffer. If the
  new value exceeded the available buffer size memory outside
  of the buffer would be written into, potentially causing
  a crash.

  This change makes it so that the 'update' now duplicates
  the new header value instead of copying it into the existing
  buffer.

- ### res_rtp_asterisk.c: Check DTLS packets against ICE candidate list
  Author: George Joseph  
  Date:   2023-11-09  

  When ICE is in use, we can prevent a possible DOS attack by allowing
  DTLS protocol messages (client hello, etc) only from sources that
  are in the active remote candidates list.

  Resolves: GHSA-hxj9-xwr8-w8pq

- ### manager.c: Prevent path traversal with GetConfig.
  Author: Ben Ford  
  Date:   2023-11-13  

  When using AMI GetConfig, it was possible to access files outside of the
  Asterisk configuration directory by using filenames with ".." and "./"
  even while live_dangerously was not enabled. This change resolves the
  full path and ensures we are still in the configuration directory before
  attempting to access the file.

- ### res_pjsip: disable raw bad packet logging
  Author: Mike Bradeen  
  Date:   2023-12-14  

      Add patch to split the log level for invalid packets received on the signaling port.
      The warning regarding the packet will move to level 2 so that it can still be displayed,
      while the raw packet will be at level 4.

