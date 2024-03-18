
Change Log for Release asterisk-21.2.0
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-21.2.0.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/21.1.0...21.2.0)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21.2.0.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

Summary:
----------------------------------------

- res_pjsip_stir_shaken.c:  Add checks for missing parameters                     
- app_dial: Add dial time for progress/ringing.                                   
- app_voicemail: Properly reinitialize config after unit tests.                   
- app_queue.c : fix "queue add member" usage string                               
- app_voicemail: Allow preventing mark messages as urgent.                        
- res_pjsip: Use consistent type for boolean columns.                             
- attestation_config.c: Use ast_free instead of ast_std_free                      
- Makefile: Add stir_shaken/cache to directories created on install               
- Stir/Shaken Refactor                                                            
- translate.c: implement new direct comp table mode                               
- README.md: Removed outdated link                                                
- strings.h: Ensure ast_str_buffer(…) returns a 0 terminated string.              
- res_rtp_asterisk.c: Correct coefficient in MOS calculation.                     
- dsp.c: Fix and improve potentially inaccurate log message.                      
- pjsip show channelstats: Prevent possible segfault when faxing                  
- Reduce startup/shutdown verbose logging                                         
- configure: Rerun bootstrap on modern platform.                                  
- Upgrade bundled pjproject to 2.14.                                              
- res_pjsip_outbound_registration.c: Add User-Agent header override               
- app_speech_utils.c: Allow partial speech results.                               
- utils: Make behavior of ast_strsep* match strsep.                               
- app_chanspy: Add 'D' option for dual-channel audio                              
- app_if: Fix next priority calculation.                                          
- res_pjsip_t38.c: Permit IPv6 SDP connection addresses.                          
- BuildSystem: Bump autotools versions on OpenBSD.                                
- main/utils: Simplify the FreeBSD ast_get_tid() handling                         
- res_pjsip_session.c: Correctly format SDP connection addresses.                 
- rtp_engine.c: Correct sample rate typo for L16/44100.                           
- manager.c: Fix erroneous reloads in UpdateConfig.                               
- res_calendar_icalendar: Print iCalendar error on parsing failure.               
- app_confbridge: Don't emit warnings on valid configurations.                    
- app_voicemail_odbc: remove macrocontext from voicemail_messages table           
- chan_dahdi: Allow MWI to be manually toggled on channels.                       
- chan_rtp.c: MulticastRTP missing refcount without codec option                  
- chan_rtp.c: Change MulticastRTP nameing to avoid memory leak                    
- func_frame_trace: Add CLI command to dump frame queue.                          

User Notes:
----------------------------------------

- ### app_dial: Add dial time for progress/ringing.                                   
  The timeout argument to Dial now allows
  specifying the maximum amount of time to dial if
  early media is not received.

- ### app_voicemail: Allow preventing mark messages as urgent.                        
  The leaveurgent mailbox option can now be used to
  control whether callers may leave messages marked as 'Urgent'.

- ### Stir/Shaken Refactor                                                            
  Asterisk's stir-shaken feature has been refactored to
  correct interoperability, RFC compliance, and performance issues.
  See https://docs.asterisk.org/Deployment/STIR-SHAKEN for more
  information.

- ### Upgrade bundled pjproject to 2.14.                                              
  Bundled pjproject has been upgraded to 2.14. For more
  information on what all is included in this change, check out the
  pjproject Github page: https://github.com/pjsip/pjproject/releases

- ### res_pjsip_outbound_registration.c: Add User-Agent header override               
  PJSIP outbound registrations now support a per-registration
  User-Agent header

- ### app_speech_utils.c: Allow partial speech results.                               
  The SpeechBackground dialplan application now supports a 'p'
  option that will return partial results from speech engines that
  provide them when a timeout occurs.

- ### app_chanspy: Add 'D' option for dual-channel audio                              
  The ChanSpy application now accepts the 'D' option which
  will interleave the spied audio within the outgoing frames. The
  purpose of this is to allow the audio to be read as a Dual channel
  stream with separate incoming and outgoing audio. Setting both the
  'o' option and the 'D' option and results in the 'D' option being
  ignored.

- ### app_voicemail_odbc: remove macrocontext from voicemail_messages table           
  The fix requires removing the macrocontext column from the
  voicemail_messages table in the voicemail database via alembic upgrade.

- ### chan_dahdi: Allow MWI to be manually toggled on channels.                       
  The 'dahdi set mwi' now allows MWI on channels
  to be manually toggled if needed for troubleshooting.
  Resolves: #440


Upgrade Notes:
----------------------------------------

- ### Stir/Shaken Refactor                                                            
  The stir-shaken refactor is a breaking change but since
  it's not working now we don't think it matters. The
  stir_shaken.conf file has changed significantly which means that
  existing ones WILL need to be changed.  The stir_shaken.conf.sample
  file in configs/samples/ has quite a bit more information.  This is
  also an ABI breaking change since some of the existing objects
  needed to be changed or removed, and new ones added.  Additionally,
  if res_stir_shaken is enabled in menuselect, you'll need to either
  have the development package for libjwt v1.15.3 installed or use
  the --with-libjwt-bundled option with ./configure.

- ### app_voicemail_odbc: remove macrocontext from voicemail_messages table           
  The fix requires that the voicemail database be upgraded via
  alembic. Upgrading to the latest voicemail database via alembic will
  remove the macrocontext column from the voicemail_messages table.


Closed Issues:
----------------------------------------

  - #46: [bug]: Stir/Shaken: Wrong CID used when looking up certificates
  - #351: [improvement]: Refactor res_stir_shaken to use libjwt
  - #406: [improvement]: pjsip: Upgrade bundled version to pjproject 2.14
  - #440: [new-feature]: chan_dahdi: Allow manually toggling MWI on channels
  - #492: [improvement]: res_calendar_icalendar: Print icalendar error if available on parsing failure
  - #515: [improvement]: Implement option to override User-Agent-Header on a per-registration basis
  - #527: [bug]: app_voicemail_odbc no longer working after removal of macrocontext.
  - #529: [bug]: MulticastRTP without selected codec leeds to "FRACK!, Failed assertion bad magic number 0x0 for object" after ~30 calls
  - #533: [improvement]: channel.c, func_frame_trace.c: Improve debuggability of channel frame queue
  - #551: [bug]: manager: UpdateConfig triggers reload with "Reload: no"
  - #560: [bug]: EndIf() causes next priority to be skipped
  - #565: [bug]: Application Read() returns immediately
  - #569: [improvement]: Add option to interleave input and output frames on spied channel
  - #572: [improvement]: Copy partial speech results when Asterisk is ready to move on but the speech backend is not
  - #582: [improvement]: Reduce unneeded logging during startup and shutdown
  - #586: [bug]: The "restrict" keyword used in chan_iax2.c isn't supported in older gcc versions
  - #588: [new-feature]: app_dial: Allow Dial to be aborted if early media is not received
  - #592: [bug]: In certain circumstances, "pjsip show channelstats" can segfault when a fax session is active
  - #595: [improvement]: dsp.c: Fix and improve confusing warning message.
  - #597: [bug]: wrong MOS calculation
  - #601: [new-feature]: translate.c: implement new direct comp table mode (PR #585)
  - #619: [new-feature]: app_voicemail: Allow preventing callers from marking messages as urgent
  - #629: [bug]: app_voicemail: Multiple executions of unit tests cause segfault
  - #634: [bug]: make install doesn't create the stir_shaken cache directory
  - #636: [bug]: Possible SEGV in res_stir_shaken due to wrong free function
  - #645: [bug]: Occasional SEGV in res_pjsip_stir_shaken.c

Commits By Author:
----------------------------------------

- ### Ben Ford (1):
  - Upgrade bundled pjproject to 2.14.

- ### Brad Smith (2):
  - main/utils: Simplify the FreeBSD ast_get_tid() handling
  - BuildSystem: Bump autotools versions on OpenBSD.

- ### Flole998 (1):
  - res_pjsip_outbound_registration.c: Add User-Agent header override

- ### George Joseph (6):
  - Reduce startup/shutdown verbose logging
  - pjsip show channelstats: Prevent possible segfault when faxing
  - Stir/Shaken Refactor
  - Makefile: Add stir_shaken/cache to directories created on install
  - attestation_config.c: Use ast_free instead of ast_std_free
  - res_pjsip_stir_shaken.c:  Add checks for missing parameters

- ### Joshua C. Colp (1):
  - utils: Make behavior of ast_strsep* match strsep.

- ### Mike Bradeen (2):
  - app_voicemail_odbc: remove macrocontext from voicemail_messages table
  - app_chanspy: Add 'D' option for dual-channel audio

- ### Naveen Albert (10):
  - func_frame_trace: Add CLI command to dump frame queue.
  - chan_dahdi: Allow MWI to be manually toggled on channels.
  - res_calendar_icalendar: Print iCalendar error on parsing failure.
  - manager.c: Fix erroneous reloads in UpdateConfig.
  - app_if: Fix next priority calculation.
  - configure: Rerun bootstrap on modern platform.
  - dsp.c: Fix and improve potentially inaccurate log message.
  - app_voicemail: Allow preventing mark messages as urgent.
  - app_voicemail: Properly reinitialize config after unit tests.
  - app_dial: Add dial time for progress/ringing.

- ### PeterHolik (2):
  - chan_rtp.c: Change MulticastRTP nameing to avoid memory leak
  - chan_rtp.c: MulticastRTP missing refcount without codec option

- ### Sean Bright (6):
  - app_confbridge: Don't emit warnings on valid configurations.
  - rtp_engine.c: Correct sample rate typo for L16/44100.
  - res_pjsip_session.c: Correctly format SDP connection addresses.
  - res_pjsip_t38.c: Permit IPv6 SDP connection addresses.
  - strings.h: Ensure ast_str_buffer(…) returns a 0 terminated string.
  - res_pjsip: Use consistent type for boolean columns.

- ### Sebastian Jennen (1):
  - translate.c: implement new direct comp table mode

- ### Shaaah (1):
  - app_queue.c : fix "queue add member" usage string

- ### Shyju Kanaprath (1):
  - README.md: Removed outdated link

- ### cmaj (1):
  - app_speech_utils.c: Allow partial speech results.

- ### romryz (1):
  - res_rtp_asterisk.c: Correct coefficient in MOS calculation.


Detail:
----------------------------------------

- ### res_pjsip_stir_shaken.c:  Add checks for missing parameters                     
  Author: George Joseph  
  Date:   2024-03-11  

  * Added checks for missing session, session->channel and rdata
    in stir_shaken_incoming_request.

  * Added checks for missing session, session->channel and tdata
    in stir_shaken_outgoing_request.

  Resolves: #645

- ### app_dial: Add dial time for progress/ringing.                                   
  Author: Naveen Albert  
  Date:   2024-02-08  

  Add a timeout option to control the amount of time
  to wait if no early media is received before giving
  up. This allows aborting early if the destination
  is not being responsive.

  Resolves: #588

  UserNote: The timeout argument to Dial now allows
  specifying the maximum amount of time to dial if
  early media is not received.


- ### app_voicemail: Properly reinitialize config after unit tests.                   
  Author: Naveen Albert  
  Date:   2024-02-29  

  Most app_voicemail unit tests were not properly cleaning up
  after themselves after running. This led to test mailboxes
  lingering around in the system. It also meant that if any
  unit tests in app_voicemail that create mailboxes were executed
  and the module was not unloaded/loaded again prior to running
  the test_voicemail_vm_info unit test, Asterisk would segfault
  due to an attempt to copy a NULL string.

  The load_config test did actually have logic to reinitialize
  the config after the test. However, this did not work in practice
  since load_config() would not reload the config since voicemail.conf
  had not changed during the test; thus, additional logic has been
  added to ensure that voicemail.conf is truly reloaded, after any
  unit tests which modify the users list.

  This prevents the SEGV due to invalid mailboxes lingering around,
  and also ensures that the system state is restored to what it was
  prior to the tests running.

  Resolves: #629

- ### app_queue.c : fix "queue add member" usage string                               
  Author: Shaaah  
  Date:   2024-01-23  

  Fixing bracket placement in the "queue add member" cli usage string.


- ### app_voicemail: Allow preventing mark messages as urgent.                        
  Author: Naveen Albert  
  Date:   2024-02-24  

  This adds an option to allow preventing callers from leaving
  messages marked as 'urgent'.

  Resolves: #619

  UserNote: The leaveurgent mailbox option can now be used to
  control whether callers may leave messages marked as 'Urgent'.


- ### res_pjsip: Use consistent type for boolean columns.                             
  Author: Sean Bright  
  Date:   2024-02-27  

  This migrates the relevant schema objects from the `('yes', 'no')`
  definition to the `('0', '1', 'off', 'on', 'false', 'true', 'yes', 'no')`
  one.

  Fixes #617


- ### attestation_config.c: Use ast_free instead of ast_std_free                      
  Author: George Joseph  
  Date:   2024-03-05  

  In as_check_common_config, we were calling ast_std_free on
  raw_key but raw_key was allocated with ast_malloc so it
  should be freed with ast_free.

  Resolves: #636

- ### Makefile: Add stir_shaken/cache to directories created on install               
  Author: George Joseph  
  Date:   2024-03-04  

  The default location for the stir_shaken cache is
  /var/lib/asterisk/keys/stir_shaken/cache but we were only creating
  /var/lib/asterisk/keys/stir_shaken on istall.  We now create
  the cache sub-directory.

  Resolves: #634

- ### Stir/Shaken Refactor                                                            
  Author: George Joseph  
  Date:   2023-10-26  

  Why do we need a refactor?

  The original stir/shaken implementation was started over 3 years ago
  when little was understood about practical implementation.  The
  result was an implementation that wouldn't actually interoperate
  with any other stir-shaken implementations.

  There were also a number of stir-shaken features and RFC
  requirements that were never implemented such as TNAuthList
  certificate validation, sending Reason headers in SIP responses
  when verification failed but we wished to continue the call, and
  the ability to send Media Key(mky) grants in the Identity header
  when the call involved DTLS.

  Finally, there were some performance concerns around outgoing
  calls and selection of the correct certificate and private key.
  The configuration was keyed by an arbitrary name which meant that
  for every outgoing call, we had to scan the entire list of
  configured TNs to find the correct cert to use.  With only a few
  TNs configured, this wasn't an issue but if you have a thousand,
  it could be.

  What's changed?

  * Configuration objects have been refactored to be clearer about
    their uses and to fix issues.
      * The "general" object was renamed to "verification" since it
        contains parameters specific to the incoming verification
        process.  It also never handled ca_path and crl_path
        correctly.
      * A new "attestation" object was added that controls the
        outgoing attestation process.  It sets default certificates,
        keys, etc.
      * The "certificate" object was renamed to "tn" and had it's key
        change to telephone number since outgoing call attestation
        needs to look up certificates by telephone number.
      * The "profile" object had more parameters added to it that can
        override default parameters specified in the "attestation"
        and "verification" objects.
      * The "store" object was removed altogther as it was never
        implemented.

  * We now use libjwt to create outgoing Identity headers and to
    parse and validate signatures on incoming Identiy headers.  Our
    previous custom implementation was much of the source of the
    interoperability issues.

  * General code cleanup and refactor.
      * Moved things to better places.
      * Separated some of the complex functions to smaller ones.
      * Using context objects rather than passing tons of parameters
        in function calls.
      * Removed some complexity and unneeded encapsuation from the
        config objects.

  Resolves: #351
  Resolves: #46

  UserNote: Asterisk's stir-shaken feature has been refactored to
  correct interoperability, RFC compliance, and performance issues.
  See https://docs.asterisk.org/Deployment/STIR-SHAKEN for more
  information.

  UpgradeNote: The stir-shaken refactor is a breaking change but since
  it's not working now we don't think it matters. The
  stir_shaken.conf file has changed significantly which means that
  existing ones WILL need to be changed.  The stir_shaken.conf.sample
  file in configs/samples/ has quite a bit more information.  This is
  also an ABI breaking change since some of the existing objects
  needed to be changed or removed, and new ones added.  Additionally,
  if res_stir_shaken is enabled in menuselect, you'll need to either
  have the development package for libjwt v1.15.3 installed or use
  the --with-libjwt-bundled option with ./configure.


- ### translate.c: implement new direct comp table mode                               
  Author: Sebastian Jennen  
  Date:   2024-02-25  

  The new mode lists for each codec translation the actual real cost in cpu microseconds per second translated audio.
  This allows to compare the real cpu usage of translations and helps in evaluation of codec implementation changes regarding performance (regression testing).

  - add new table mode
  - hide the 999999 comp values, as these only indicate an issue with transcoding
  - hide the 0 values, as these also do not contain any information (only indicate a multistep transcoding)

  Resolves: #601

- ### README.md: Removed outdated link                                                
  Author: Shyju Kanaprath  
  Date:   2024-02-23  

  Removed outdated link http://www.quicknet.net from README.md

  cherry-pick-to: 18
  cherry-pick-to: 20
  cherry-pick-to: 21

- ### strings.h: Ensure ast_str_buffer(…) returns a 0 terminated string.              
  Author: Sean Bright  
  Date:   2024-02-17  

  If a dynamic string is created with an initial length of 0,
  `ast_str_buffer(…)` will return an invalid pointer.

  This was a secondary discovery when fixing #65.


- ### res_rtp_asterisk.c: Correct coefficient in MOS calculation.                     
  Author: romryz  
  Date:   2024-02-06  

  Media Experience Score relies on incorrect pseudo_mos variable
  calculation. According to forming an opinion section of the
  documentation, calculation relies on ITU-T G.107 standard:

      https://docs.asterisk.org/Deployment/Media-Experience-Score/#forming-an-opinion

  ITU-T G.107 Annex B suggests to calculate MOS with a coefficient
  "seven times ten to the power of negative six", 7 * 10^(-6). which
  would mean 6 digits after the decimal point. Current implementation
  has 7 digits after the decimal point, which downrates the calls.

  Fixes: #597

- ### dsp.c: Fix and improve potentially inaccurate log message.                      
  Author: Naveen Albert  
  Date:   2024-02-09  

  If ast_dsp_process is called with a codec besides slin, ulaw,
  or alaw, a warning is logged that in-band DTMF is not supported,
  but this message is not always appropriate or correct, because
  ast_dsp_process is much more generic than just DTMF detection.

  This logs a more generic message in those cases, and also improves
  codec-mismatch logging throughout dsp.c by ensuring incompatible
  codecs are printed out.

  Resolves: #595

- ### pjsip show channelstats: Prevent possible segfault when faxing                  
  Author: George Joseph  
  Date:   2024-02-09  

  Under rare circumstances, it's possible for the original audio
  session in the active_media_state default_session to be corrupted
  instead of removed when switching to the t38/image media session
  during fax negotiation.  This can cause a segfault when a "pjsip
  show channelstats" attempts to print that audio media session's
  rtp statistics.  In these cases, the active_media_state
  topology is correctly showing only a single t38/image stream
  so we now check that there's an audio stream in the topology
  before attempting to use the audio media session to get the rtp
  statistics.

  Resolves: #592

- ### Reduce startup/shutdown verbose logging                                         
  Author: George Joseph  
  Date:   2024-01-31  

  When started with a verbose level of 3, asterisk can emit over 1500
  verbose message that serve no real purpose other than to fill up
  logs. When asterisk shuts down, it emits another 1100 that are of
  even less use. Since the testsuite runs asterisk with a verbose
  level of 3, and asterisk starts and stops for every one of the 700+
  tests, the number of log messages is staggering.  Besides taking up
  resources, it also makes it hard to debug failing tests.

  This commit changes the log level for those verbose messages to 5
  instead of 3 which reduces the number of log messages to only a
  handful. Of course, NOTICE, WARNING and ERROR message are
  unaffected.

  There's also one other minor change...
  ast_context_remove_extension_callerid2() logs a DEBUG message
  instead of an ERROR if the extension you're deleting doesn't exist.
  The pjsip_config_wizard calls that function to clean up the config
  and has been triggering that annoying error message for years.

  Resolves: #582

- ### configure: Rerun bootstrap on modern platform.                                  
  Author: Naveen Albert  
  Date:   2024-02-12  

  The last time configure was run, it was run on a system that
  did not enable -std=gnu11 by default, which meant that the
  restrict qualifier would not be recognized on certain platforms.
  This regenerates the configure files from running bootstrap.sh,
  so that these should be recognized on all supported platforms.

  Resolves: #586

- ### Upgrade bundled pjproject to 2.14.                                              
  Author: Ben Ford  
  Date:   2024-02-05  

  Fixes: #406

  UserNote: Bundled pjproject has been upgraded to 2.14. For more
  information on what all is included in this change, check out the
  pjproject Github page: https://github.com/pjsip/pjproject/releases


- ### res_pjsip_outbound_registration.c: Add User-Agent header override               
  Author: Flole998  
  Date:   2023-12-13  

  This introduces a setting for outbound registrations to override the
  global User-Agent header setting.

  Resolves: #515

  UserNote: PJSIP outbound registrations now support a per-registration
  User-Agent header


- ### app_speech_utils.c: Allow partial speech results.                               
  Author: cmaj  
  Date:   2024-02-02  

  Adds 'p' option to SpeechBackground() application.
  With this option, when the app timeout is reached,
  whatever the backend speech engine collected will
  be returned as if it were the final, full result.
  (This works for engines that make partial results.)

  Resolves: #572

  UserNote: The SpeechBackground dialplan application now supports a 'p'
  option that will return partial results from speech engines that
  provide them when a timeout occurs.


- ### utils: Make behavior of ast_strsep* match strsep.                               
  Author: Joshua C. Colp  
  Date:   2024-01-31  

  Given the scenario of passing an empty string to the
  ast_strsep functions the functions would return NULL
  instead of an empty string. This is counter to how
  strsep itself works.

  This change alters the behavior of the functions to
  match that of strsep.

  Fixes: #565

- ### app_chanspy: Add 'D' option for dual-channel audio                              
  Author: Mike Bradeen  
  Date:   2024-01-31  

  Adds the 'D' option to app chanspy that causes the input and output
  frames of the spied channel to be interleaved in the spy output frame.
  This allows the input and output of the spied channel to be decoded
  separately by the receiver.

  If the 'o' option is also set, the 'D' option is ignored as the
  audio being spied is inherently one direction.

  Fixes: #569

  UserNote: The ChanSpy application now accepts the 'D' option which
  will interleave the spied audio within the outgoing frames. The
  purpose of this is to allow the audio to be read as a Dual channel
  stream with separate incoming and outgoing audio. Setting both the
  'o' option and the 'D' option and results in the 'D' option being
  ignored.


- ### app_if: Fix next priority calculation.                                          
  Author: Naveen Albert  
  Date:   2024-01-28  

  Commit fa3922a4d28860d415614347d9f06c233d2beb07 fixed
  a branching issue but "overshoots" when calculating
  the next priority. This fixes that; accompanying
  test suite tests have also been extended.

  Resolves: #560

- ### res_pjsip_t38.c: Permit IPv6 SDP connection addresses.                          
  Author: Sean Bright  
  Date:   2024-01-29  

  The existing code prevented IPv6 addresses from being properly parsed.

  Fixes #558


- ### BuildSystem: Bump autotools versions on OpenBSD.                                
  Author: Brad Smith  
  Date:   2024-01-27  

  Bump up to the more commonly used and modern versions of
  autoconf and automake.


- ### main/utils: Simplify the FreeBSD ast_get_tid() handling                         
  Author: Brad Smith  
  Date:   2024-01-27  

  FreeBSD has had kernel threads for 20+ years.


- ### res_pjsip_session.c: Correctly format SDP connection addresses.                 
  Author: Sean Bright  
  Date:   2024-01-27  

  Resolves a regression identified by @justinludwig involving the
  rendering of IPv6 addresses in outgoing SDP.

  Also updates `media_address` on PJSIP endpoints so that if we are able
  to parse the configured value as an IP we store it in a format that we
  can directly use later. Based on my reading of the code it appeared
  that one could configure `media_address` as:

  ```
  [foo]
  type = endpoint
  ...
  media_address = [2001:db8::]
  ```

  And that value would be blindly copied into the outgoing SDP without
  regard to its format.

  Fixes #541


- ### rtp_engine.c: Correct sample rate typo for L16/44100.                           
  Author: Sean Bright  
  Date:   2024-01-28  

  Fixes #555


- ### manager.c: Fix erroneous reloads in UpdateConfig.                               
  Author: Naveen Albert  
  Date:   2024-01-25  

  Currently, a reload will always occur if the
  Reload header is provided for the UpdateConfig
  action. However, we should not be doing a reload
  if the header value has a falsy value, per the
  documentation, so this makes the reload behavior
  consistent with the existing documentation.

  Resolves: #551

- ### res_calendar_icalendar: Print iCalendar error on parsing failure.               
  Author: Naveen Albert  
  Date:   2023-12-14  

  If libical fails to parse a calendar, print the error message it provdes.

  Resolves: #492

- ### app_confbridge: Don't emit warnings on valid configurations.                    
  Author: Sean Bright  
  Date:   2024-01-21  

  The numeric bridge profile options `internal_sample_rate` and
  `maximum_sample_rate` are documented to accept the special values
  `auto` and `none`, respectively. While these values currently work,
  they also emit warnings when used which could be confusing for users.

  In passing, also ensure that we only accept the documented range of
  sample rate values between 8000 and 192000.

  Fixes #546


- ### app_voicemail_odbc: remove macrocontext from voicemail_messages table           
  Author: Mike Bradeen  
  Date:   2024-01-10  

  When app_macro was deprecated, the macrocontext column was removed from
  the INSERT statement but the binds were not renumbered. This broke the
  insert.

  This change removes the macrocontext column via alembic and re-numbers
  the existing columns in the INSERT.

  Fixes: #527

  UserNote: The fix requires removing the macrocontext column from the
  voicemail_messages table in the voicemail database via alembic upgrade.

  UpgradeNote: The fix requires that the voicemail database be upgraded via
  alembic. Upgrading to the latest voicemail database via alembic will
  remove the macrocontext column from the voicemail_messages table.


- ### chan_dahdi: Allow MWI to be manually toggled on channels.                       
  Author: Naveen Albert  
  Date:   2023-11-10  

  This adds a CLI command to manually toggle the MWI status
  of a channel, useful for troubleshooting or resetting
  MWI devices, similar to the capabilities offered with
  SIP messaging to manually control MWI status.

  UserNote: The 'dahdi set mwi' now allows MWI on channels
  to be manually toggled if needed for troubleshooting.

  Resolves: #440

- ### chan_rtp.c: MulticastRTP missing refcount without codec option                  
  Author: PeterHolik  
  Date:   2024-01-15  

  Fixes: #529

- ### chan_rtp.c: Change MulticastRTP nameing to avoid memory leak                    
  Author: PeterHolik  
  Date:   2024-01-16  

  Fixes: asterisk#536

- ### func_frame_trace: Add CLI command to dump frame queue.                          
  Author: Naveen Albert  
  Date:   2024-01-12  

  This adds a simple CLI command that can be used for
  analyzing all frames currently queued to a channel.

  A couple log messages are also adjusted to be more
  useful in tracing bridging problems.

  Resolves: #533

