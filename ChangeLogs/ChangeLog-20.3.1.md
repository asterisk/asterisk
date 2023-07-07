
Change Log for Release 20.3.1
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-20.3.1.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/20.3.0...20.3.1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-20.3.1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- apply_patches: Use globbing instead of file/sort.
- apply_patches: Sort patch list before applying
- pjsip: Upgrade bundled version to pjproject 2.13.1

User Notes:
----------------------------------------

- ### res_http_media_cache: Introduce options and customize
  The res_http_media_cache module now attempts to load
  configuration from the res_http_media_cache.conf file.
  The following options were added:
    * timeout_secs
    * user_agent
    * follow_location
    * max_redirects
    * protocols
    * redirect_protocols
    * dns_cache_timeout_secs

- ### format_sln: add .slin as supported file extension
  format_sln now recognizes '.slin' as a valid
  file extension in addition to the existing
  '.sln' and '.raw'.

- ### bridge_builtin_features: add beep via touch variable
  Add optional touch variable : TOUCH_MIXMONITOR_BEEP(interval)
  Setting TOUCH_MIXMONITOR_BEEP/TOUCH_MONITOR_BEEP to a valid
  interval in seconds will result in a periodic beep being
  played to the monitored channel upon MixMontior/Monitor
  feature start.
  If an interval less than 5 seconds is specified, the interval
  will default to 5 seconds.  If the value is set to an invalid
  interval, the default of 15 seconds will be used.

- ### app_senddtmf: Add SendFlash AMI action.
  The SendFlash AMI action now allows sending
  a hook flash event on a channel.

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

- ### pbx_dundi: Add PJSIP support.
  DUNDi now supports chan_pjsip. Outgoing calls using
  PJSIP require the pjsip_outgoing_endpoint option
  to be set in dundi.conf.

- ### test.c: Fix counting of tests and add 2 new tests
  The "tests" attribute of the "testsuite" element in the
  output XML now reflects only the tests actually requested
  to be executed instead of all the tests registered.
  The "failures" attribute was added to the "testsuite"
  element.
  Also added two new unit tests that just pass and fail
  to be used for testing CI itself.

- ### cli: increase channel column width
  This change increases the display width on 'core show channels'
  amd 'core show channels verbose'
  For 'core show channels', the Channel name field is increased to
  64 characters and the Location name field is increased to 32
  characters.
  For 'core show channels verbose', the Channel name field is
  increased to 80 characters, the Context is increased to 24
  characters and the Extension is increased to 24 characters.


Upgrade Notes:
----------------------------------------


Closed Issues:
----------------------------------------

  - #193: [bug]: third-party/apply-patches doesn't sort the patch file list before applying

Commits By Author:
----------------------------------------

- ### George Joseph (1):
  - apply_patches: Sort patch list before applying

- ### Sean Bright (1):
  - apply_patches: Use globbing instead of file/sort.

- ### Stanislav Abramenkov (1):
  - pjsip: Upgrade bundled version to pjproject 2.13.1


Detail:
----------------------------------------

- ### apply_patches: Use globbing instead of file/sort.
  Author: Sean Bright  
  Date:   2023-07-06  

  This accomplishes the same thing as a `find ... | sort` but with the
  added benefit of clarity and avoiding a call to a subshell.

  Additionally drop the -s option from call to patch as it is not POSIX.

- ### apply_patches: Sort patch list before applying
  Author: George Joseph  
  Date:   2023-07-06  

  The apply_patches script wasn't sorting the list of patches in
  the "patches" directory before applying them. This left the list
  in an indeterminate order. In most cases, the list is actually
  sorted but rarely, they can be out of order and cause dependent
  patches to fail to apply.

  We now sort the list but the "sort" program wasn't in the
  configure scripts so we needed to add that and regenerate
  the scripts as well.

  Resolves: #193

- ### pjsip: Upgrade bundled version to pjproject 2.13.1
  Author: Stanislav Abramenkov  
  Date:   2023-07-05  


