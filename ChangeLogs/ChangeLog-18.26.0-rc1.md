
## Change Log for Release asterisk-18.26.0-rc1

### Links:

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/asterisk/releases/ChangeLog-18.26.0-rc1.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/18.25.0...18.26.0-rc1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-18.26.0-rc1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/asterisk)  

### Summary:

- Commits: 28
- Commit Authors: 7
- Issues Resolved: 13
- Security Advisories Resolved: 0

### User Notes:

- #### manager.c: Restrict ModuleLoad to the configured modules directory.             
  The ModuleLoad AMI action now restricts modules to the
  configured modules directory.

- #### manager: Enhance event filtering for performance                                
  You can now perform more granular filtering on events
  in manager.conf using expressions like
  `eventfilter(name(Newchannel),header(Channel),method(starts_with)) = PJSIP/`
  This is much more efficient than
  `eventfilter = Event: Newchannel.*Channel: PJSIP/`
  Full syntax guide is in configs/samples/manager.conf.sample.

- #### db.c: Remove limit on family/key length                                         
  The `ast_db_*()` APIs have had the 253 byte limit on
  "/family/key" removed and will now accept families and keys with a
  total length of up to SQLITE_MAX_LENGTH (currently 1e9!).  This
  affects the `DB*` dialplan applications, dialplan functions,
  manager actions and `databse` CLI commands.  Since the
  media_cache also uses the `ast_db_*()` APIs, you can now store
  resources with URIs longer than 253 bytes.


### Upgrade Notes:


### Commit Authors:

- Allan Nathanson: (1)
- Ben Ford: (1)
- George Joseph: (10)
- Jiangxc: (1)
- Naveen Albert: (6)
- Peter Jannesen: (2)
- Sean Bright: (7)

## Issue and Commit Detail:

### Closed Issues:

  - 487: [bug]: Segfault possibly in ast_rtp_stop
  - 821: [bug]: app_dial:  The progress timeout doesn't cause Dial to exit
  - 881: [bug]: Long URLs are being rejected by the media cache because of an astdb key length limit
  - 882: [bug]: Value CHANNEL(userfield) is lost by BRIDGE_ENTER
  - 897: [improvement]: Restrict ModuleLoad AMI action to the modules directory
  - 900: [bug]: astfd.c: NULL pointer passed to fclose with nonnull attribute causes compilation failure
  - 902: [bug]: app_voicemail: Pager emails are ill-formatted when custom subject is used
  - 916: [bug]: Compilation errors on FreeBSD
  - 924: [bug]: dnsmgr.c: dnsmgr_refresh() should not flag change if IP address order changes
  - 928: [bug]: chan_dahdi: MWI while off-hook when hung up on after recall ring
  - 937: [bug]: Wrong format for sample config file 'geolocation.conf.sample'
  - 938: [bug]: memory leak - CBAnn leaks a small amount format_cap related memory for every confbridge

### Commits By Author:

- #### Allan Nathanson (1):
  - dnsmgr.c: dnsmgr_refresh() incorrectly flags change with DNS round-robin

- #### Ben Ford (1):
  - manager.c: Restrict ModuleLoad to the configured modules directory.

- #### George Joseph (10):
  - db.c: Remove limit on family/key length
  - manager.c: Split XML documentation to manager_doc.xml
  - manager: Enhance event filtering for performance
  - manager.conf.sample: Fix mathcing typo
  - Fix application references to Background
  - res_rtp_asterisk: Fix dtls timer issues causing FRACKs and SEGVs
  - manager.c: Add unit test for Originate app and appdata permissions
  - geolocation.sample.conf: Fix comment marker at end of file
  - core_unreal.c: Fix memory leak in ast_unreal_new_channels()
  - pjproject_bundled:  Tweaks to support out-of-tree development

- #### Naveen Albert (6):
  - app_voicemail: Fix ill-formatted pager emails with custom subject.
  - astfd.c: Avoid calling fclose with NULL argument.
  - main, res, tests: Fix compilation errors on FreeBSD.
  - chan_dahdi: Never send MWI while off-hook.
  - app_dial: Fix progress timeout.
  - app_dial: Fix progress timeout calculation with no answer timeout.

- #### Peter Jannesen (2):
  - cel_custom: Allow absolute filenames.
  - channel: Preserve CHANNEL(userfield) on masquerade.

- #### Sean Bright (7):
  - res_pjsip_pubsub: Persist subscription 'generator_data' in sorcery
  - cdr_custom: Allow absolute filenames.
  - res_agi.c: Ensure SIGCHLD handler functions are properly balanced.
  - alembic: Drop redundant voicemail_messages index.
  - func_base64.c: Ensure we set aside enough room for base64 encoded data.
  - Revert "res_rtp_asterisk: Count a roll-over of the sequence number even on los..
  - chan_sip.c: Fix __sip_reliable_xmit build error

- #### jiangxc (1):
  - res_agi.c: Prevent possible double free during `SPEECH RECOGNIZE`


### Commit List:

-  app_dial: Fix progress timeout calculation with no answer timeout.
-  pjproject_bundled:  Tweaks to support out-of-tree development
-  chan_sip.c: Fix __sip_reliable_xmit build error
-  core_unreal.c: Fix memory leak in ast_unreal_new_channels()
-  dnsmgr.c: dnsmgr_refresh() incorrectly flags change with DNS round-robin
-  geolocation.sample.conf: Fix comment marker at end of file
-  func_base64.c: Ensure we set aside enough room for base64 encoded data.
-  app_dial: Fix progress timeout.
-  chan_dahdi: Never send MWI while off-hook.
-  manager.c: Add unit test for Originate app and appdata permissions
-  alembic: Drop redundant voicemail_messages index.
-  res_agi.c: Ensure SIGCHLD handler functions are properly balanced.
-  main, res, tests: Fix compilation errors on FreeBSD.
-  res_rtp_asterisk: Fix dtls timer issues causing FRACKs and SEGVs
-  manager.c: Restrict ModuleLoad to the configured modules directory.
-  res_agi.c: Prevent possible double free during `SPEECH RECOGNIZE`
-  cdr_custom: Allow absolute filenames.
-  astfd.c: Avoid calling fclose with NULL argument.
-  channel: Preserve CHANNEL(userfield) on masquerade.
-  cel_custom: Allow absolute filenames.
-  app_voicemail: Fix ill-formatted pager emails with custom subject.
-  res_pjsip_pubsub: Persist subscription 'generator_data' in sorcery
-  Fix application references to Background
-  manager.conf.sample: Fix mathcing typo
-  manager: Enhance event filtering for performance
-  manager.c: Split XML documentation to manager_doc.xml
-  db.c: Remove limit on family/key length

### Commit Details:

#### app_dial: Fix progress timeout calculation with no answer timeout.
  Author: Naveen Albert
  Date:   2024-10-16

  If to_answer is -1, simply comparing to see if the progress timeout
  is smaller than the answer timeout to prefer it will fail. Add
  an additional check that chooses the progress timeout if there is
  no answer timeout (or as before, if the progress timeout is smaller).

  Resolves: #821

#### pjproject_bundled:  Tweaks to support out-of-tree development
  Author: George Joseph
  Date:   2024-10-17

  * pjproject is now configured with --disable-libsrtp so it will
    build correctly when doing "out-of-tree" development.  Asterisk
    doesn't use pjproject for handling media so pjproject doesn't
    need libsrtp itself.

  * The pjsua app (which we used to use for the testsuite) no longer
    builds in pjproject's master branch so we just skip it.  The
    testsuite no longer needs it anyway.

  See third-party/pjproject/README-hacking.md for more info on building
  pjproject "out-of-tree".


#### chan_sip.c: Fix __sip_reliable_xmit build error
  Author: Sean Bright
  Date:   2024-10-17

  Fixes #954


#### Revert "res_rtp_asterisk: Count a roll-over of the sequence number even on los..
  Author: Sean Bright
  Date:   2024-10-07

  This reverts commit cb5e3445be6c55517c8d05aca601b648341f8ae9.

  The original change from 16 to 15 bit sequence numbers was predicated
  on the following from the now-defunct libSRTP FAQ on sourceforge.net:

  > *Q6. The use of implicit synchronization via ROC seems
  > dangerous. Can senders and receivers lose ROC synchronization?*
  >
  > **A.** It is possible to lose ROC synchronization between sender and
  > receiver(s), though it is not likely in practice, and practical
  > steps can be taken to avoid it. A burst loss of 2^16 packets or more
  > will always break synchronization. For example, a conversational
  > voice codec that sends 50 packets per second will have its ROC
  > increment about every 22 minutes. A network with a burst of packet
  > loss that long has problems other than ROC synchronization.
  >
  > There is a higher sensitivity to loss at the very outset of an SRTP
  > stream. If the sender's initial sequence number is close to the
  > maximum value of 2^16-1, and all packets are lost from the initial
  > packet until the sequence number cycles back to zero, the sender
  > will increment its ROC, but the receiver will not. The receiver
  > cannot determine that the initial packets were lost and that
  > sequence-number rollover has occurred. In this case, the receiver's
  > ROC would be zero whereas the sender's ROC would be one, while their
  > sequence numbers would be so close that the ROC-guessing algorithm
  > could not detect this fact.
  >
  > There is a simple solution to this problem: the SRTP sender should
  > randomly select an initial sequence number that is always less than
  > 2^15. This ensures correct SRTP operation so long as fewer than 2^15
  > initial packets are lost in succession, which is within the maximum
  > tolerance of SRTP packet-index determination (see Appendix A and
  > page 14, first paragraph of RFC 3711). An SRTP receiver should
  > carefully implement the index-guessing algorithm. A naive
  > implementation can unintentionally guess the value of
  > 0xffffffffffffLL whenever the SEQ in the packet is greater than 2^15
  > and the locally stored SEQ and ROC are zero. (This can happen when
  > the implementation fails to treat those zero values as a special
  > case.)
  >
  > When ROC synchronization is lost, the receiver will not be able to
  > properly process the packets. If anti-replay protection is turned
  > on, then the desynchronization will appear as a burst of replay
  > check failures. Otherwise, if authentication is being checked, then
  > it will appear as a burst of authentication failures. Otherwise, if
  > encryption is being used, the desynchronization may not be detected
  > by the SRTP layer, and the packets may be improperly decrypted.

  However, modern libSRTP (as of 1.0.1[1]) now mentions the following in
  their README.md[2]:

  > The sequence number in the rtp packet is used as the low 16 bits of
  > the sender's local packet index. Note that RTP will start its
  > sequence number in a random place, and the SRTP layer just jumps
  > forward to that number at its first invocation. An earlier version
  > of this library used initial sequence numbers that are less than
  > 32,768; this trick is no longer required as the
  > rdbx_estimate_index(...) function has been made smarter.

  So truncating our initial sequence number to 15 bit is no longer
  necessary.

  1. https://github.com/cisco/libsrtp/blob/0eb007f0dc611f27cbfe0bf9855ed85182496cec/CHANGES#L271-L289
  2. https://github.com/cisco/libsrtp/blob/2de20dd9e9c8afbaf02fcf5d4048ce1ec9ddc0ae/README.md#implementation-notes


#### core_unreal.c: Fix memory leak in ast_unreal_new_channels()
  Author: George Joseph
  Date:   2024-10-15

  When the channel tech is multistream capable, the reference to
  chan_topology was passed to the new channel.  When the channel tech
  isn't multistream capable, the reference to chan_topology was never
  released.  "Local" channels are multistream capable so it didn't
  affect them but the confbridge "CBAnn" and the bridge_media
  "Recorder" channels are not so they caused a leak every time one
  of them was created.

  Also added tracing to ast_stream_topology_alloc() and
  stream_topology_destroy() to assist with debugging.

  Resolves: #938

#### dnsmgr.c: dnsmgr_refresh() incorrectly flags change with DNS round-robin
  Author: Allan Nathanson
  Date:   2024-09-29

  The dnsmgr_refresh() function checks to see if the IP address associated
  with a name/service has changed. The gotcha is that the ast_get_ip_or_srv()
  function only returns the first IP address returned by the DNS query. If
  there are multiple IPs associated with the name and the returned order is
  not consistent (e.g. with DNS round-robin) then the other IP addresses are
  not included in the comparison and the entry is flagged as changed even
  though the IP is still valid.

  Updated the code to check all IP addresses and flag a change only if the
  original IP is no longer valid.

  Resolves: #924

#### geolocation.sample.conf: Fix comment marker at end of file
  Author: George Joseph
  Date:   2024-10-08

  Resolves: #937

#### func_base64.c: Ensure we set aside enough room for base64 encoded data.
  Author: Sean Bright
  Date:   2024-10-08

  Reported by SingularTricycle on IRC.

  Fixes #940


#### app_dial: Fix progress timeout.
  Author: Naveen Albert
  Date:   2024-10-03

  Under some circumstances, the progress timeout feature added in commit
  320c98eec87c473bfa814f76188a37603ea65ddd does not work as expected,
  such as if there is no media flowing. Adjust the waitfor call to
  explicitly use the progress timeout if it would be reached sooner than
  the answer timeout to ensure we handle the timers properly.

  Resolves: #821

#### chan_dahdi: Never send MWI while off-hook.
  Author: Naveen Albert
  Date:   2024-10-01

  In some circumstances, it is possible for the do_monitor thread to
  erroneously think that a line is on-hook and send an MWI FSK spill
  to it when the line is really off-hook and no MWI should be sent.
  Commit 0a8b3d34673277b70be6b0e8ac50191b1f3c72c6 previously fixed this
  issue in a more readily encountered scenario, but it has still been
  possible for MWI to be sent when it shouldn't be. To robustly fix
  this issue, query DAHDI for the hook status to ensure we don't send
  MWI on a line that is actually still off hook.

  Resolves: #928

#### manager.c: Add unit test for Originate app and appdata permissions
  Author: George Joseph
  Date:   2024-10-03

  This unit test checks that dialplan apps and app data specified
  as parameters for the Originate action are allowed with the
  permissions the user has.


#### alembic: Drop redundant voicemail_messages index.
  Author: Sean Bright
  Date:   2024-09-26

  The `voicemail_messages_dir` index is a left prefix of the table's
  primary key and therefore unnecessary.


#### res_agi.c: Ensure SIGCHLD handler functions are properly balanced.
  Author: Sean Bright
  Date:   2024-09-30

  Calls to `ast_replace_sigchld()` and `ast_unreplace_sigchld()` must be
  balanced to ensure that we can capture the exit status of child
  processes when we need to. This extends to functions that call
  `ast_replace_sigchld()` and `ast_unreplace_sigchld()` such as
  `ast_safe_fork()` and `ast_safe_fork_cleanup()`.

  The primary change here is ensuring that we do not call
  `ast_safe_fork_cleanup()` in `res_agi.c` if we have not previously
  called `ast_safe_fork()`.

  Additionally we reinforce some of the documentation and add an
  assertion to, ideally, catch this sooner were this to happen again.

  Fixes #922


#### main, res, tests: Fix compilation errors on FreeBSD.
  Author: Naveen Albert
  Date:   2024-09-29

  asterisk.c, manager.c: Increase buffer sizes to avoid truncation warnings.
  config.c: Include header file for WIFEXITED/WEXITSTATUS macros.
  res_timing_kqueue: Use more portable format specifier.
  test_crypto: Use non-linux limits.h header file.

  Resolves: #916

#### res_rtp_asterisk: Fix dtls timer issues causing FRACKs and SEGVs
  Author: George Joseph
  Date:   2024-09-16

  In dtls_srtp_handle_timeout(), when DTLSv1_get_timeout() returned
  success but with a timeout of 0, we were stopping the timer and
  decrementing the refcount on instance but not resetting the
  timeout_timer to -1.  When dtls_srtp_stop_timeout_timer()
  was later called, it was atempting to stop a stale timer and could
  decrement the refcount on instance again which would then cause
  the instance destructor to run early.  This would result in either
  a FRACK or a SEGV when ast_rtp_stop(0 was called.

  According to the OpenSSL docs, we shouldn't have been stopping the
  timer when DTLSv1_get_timeout() returned success and the new timeout
  was 0 anyway.  We should have been calling DTLSv1_handle_timeout()
  again immediately so we now reschedule the timer callback for
  1ms (almost immediately).

  Additionally, instead of scheduling the timer callback at a fixed
  interval returned by the initial call to DTLSv1_get_timeout()
  (usually 999 ms), we now reschedule the next callback based on
  the last call to DTLSv1_get_timeout().

  Resolves: #487

#### manager.c: Restrict ModuleLoad to the configured modules directory.
  Author: Ben Ford
  Date:   2024-09-25

  When using the ModuleLoad AMI action, it was possible to traverse
  upwards through the directories to files outside of the configured
  modules directory. We decided it would be best to restrict access to
  modules exclusively in the configured directory. You will now get an
  error when the specified module is outside of this limitation.

  Fixes: #897

  UserNote: The ModuleLoad AMI action now restricts modules to the
  configured modules directory.


#### res_agi.c: Prevent possible double free during `SPEECH RECOGNIZE`
  Author: jiangxc
  Date:   2024-07-17

  When using the speech recognition module, crashes can occur
  sporadically due to a "double free or corruption (out)" error. Now, in
  the section where the audio stream is being captured in a loop, each
  time after releasing fr, it is set to NULL to prevent repeated
  deallocation.

  Fixes #772


#### cdr_custom: Allow absolute filenames.
  Author: Sean Bright
  Date:   2024-09-26

  A follow up to #893 that brings the same functionality to
  cdr_custom. Also update the sample configuration files to note support
  for absolute paths.


#### astfd.c: Avoid calling fclose with NULL argument.
  Author: Naveen Albert
  Date:   2024-09-24

  Don't pass through a NULL argument to fclose, which is undefined
  behavior, and instead return -1 and set errno appropriately. This
  also avoids a compiler warning with glibc 2.38 and newer, as glibc
  commit 71d9e0fe766a3c22a730995b9d024960970670af
  added the nonnull attribute to this argument.

  Resolves: #900

#### channel: Preserve CHANNEL(userfield) on masquerade.
  Author: Peter Jannesen
  Date:   2024-09-20

  In certain circumstances a channel may undergo an operation
  referred to as a masquerade. If this occurs the CHANNEL(userfield)
  value was not preserved causing it to get lost. This change makes
  it so that this field is now preserved.

  Fixes: #882

#### cel_custom: Allow absolute filenames.
  Author: Peter Jannesen
  Date:   2024-09-20

  If a filename starts with a '/' in cel_custom [mappings] assume it is
  a absolute file path and not relative filename/path to
  AST_LOG_DIR/cel_custom/


#### app_voicemail: Fix ill-formatted pager emails with custom subject.
  Author: Naveen Albert
  Date:   2024-09-24

  Add missing end-of-headers newline to pager emails with custom
  subjects, since this was missing from this code path.

  Resolves: #902

#### res_pjsip_pubsub: Persist subscription 'generator_data' in sorcery
  Author: Sean Bright
  Date:   2024-09-23

  Fixes #895


#### Fix application references to Background
  Author: George Joseph
  Date:   2024-09-20

  The app is actually named "BackGround" but several references
  in XML documentation were spelled "Background" with the lower
  case "g".  This was causing documentation links to return
  "not found" messages.


#### manager.conf.sample: Fix mathcing typo
  Author: George Joseph
  Date:   2024-09-24


#### manager: Enhance event filtering for performance
  Author: George Joseph
  Date:   2024-07-31

  UserNote: You can now perform more granular filtering on events
  in manager.conf using expressions like
  `eventfilter(name(Newchannel),header(Channel),method(starts_with)) = PJSIP/`
  This is much more efficient than
  `eventfilter = Event: Newchannel.*Channel: PJSIP/`
  Full syntax guide is in configs/samples/manager.conf.sample.


#### manager.c: Split XML documentation to manager_doc.xml
  Author: George Joseph
  Date:   2024-08-01


#### db.c: Remove limit on family/key length
  Author: George Joseph
  Date:   2024-09-11

  Consumers like media_cache have been running into issues with
  the previous astdb "/family/key" limit of 253 bytes when needing
  to store things like long URIs.  An Amazon S3 URI is a good example
  of this.  Now, instead of using a static 256 byte buffer for
  "/family/key", we use ast_asprintf() to dynamically create it.

  Both test_db.c and test_media_cache.c were also updated to use
  keys/URIs over the old 253 character limit.

  Resolves: #881

  UserNote: The `ast_db_*()` APIs have had the 253 byte limit on
  "/family/key" removed and will now accept families and keys with a
  total length of up to SQLITE_MAX_LENGTH (currently 1e9!).  This
  affects the `DB*` dialplan applications, dialplan functions,
  manager actions and `databse` CLI commands.  Since the
  media_cache also uses the `ast_db_*()` APIs, you can now store
  resources with URIs longer than 253 bytes.


