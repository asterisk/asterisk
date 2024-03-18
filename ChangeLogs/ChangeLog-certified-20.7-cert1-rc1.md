
Change Log for Release asterisk-certified-20.7-cert1-rc1
========================================

Links:
----------------------------------------

 - [Full ChangeLog](https://downloads.asterisk.org/pub/telephony/certified-asterisk/releases/ChangeLog-certified-20.7-cert1-rc1.md)  
 - [GitHub Diff](https://github.com/asterisk/asterisk/compare/certified-18.9-cert8...certified-20.7-cert1-rc1)  
 - [Tarball](https://downloads.asterisk.org/pub/telephony/certified-asterisk/asterisk-certified-20.7-cert1-rc1.tar.gz)  
 - [Downloads](https://downloads.asterisk.org/pub/telephony/certified-asterisk)  

Summary:
----------------------------------------

- Initial commit for certified-20.7                                               
- res_pjsip_stir_shaken.c:  Add checks for missing parameters                     
- app_dial: Add dial time for progress/ringing.                                   
- app_voicemail: Properly reinitialize config after unit tests.                   
- app_queue.c : fix "queue add member" usage string                               
- app_voicemail: Allow preventing mark messages as urgent.                        
- res_pjsip: Use consistent type for boolean columns.                             
- attestation_config.c: Use ast_free instead of ast_std_free                      
- Makefile: Add stir_shaken/cache to directories created on install               
- Stir/Shaken Refactor                                                            
- alembic: Synchronize alembic heads between supported branches.                  
- translate.c: implement new direct comp table mode                               
- README.md: Removed outdated link                                                
- strings.h: Ensure ast_str_buffer(…) returns a 0 terminated string.              
- res_rtp_asterisk.c: Correct coefficient in MOS calculation.                     
- dsp.c: Fix and improve potentially inaccurate log message.                      
- pjsip show channelstats: Prevent possible segfault when faxing                  
- Reduce startup/shutdown verbose logging                                         
- configure: Rerun bootstrap on modern platform.                                  
- Upgrade bundled pjproject to 2.14.                                              
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
- app_voicemail: add NoOp alembic script to maintain sync                         
- chan_dahdi: Allow MWI to be manually toggled on channels.                       
- chan_rtp.c: MulticastRTP missing refcount without codec option                  
- chan_rtp.c: Change MulticastRTP nameing to avoid memory leak                    
- func_frame_trace: Add CLI command to dump frame queue.                          
- logger: Fix linking regression.                                                 
- Revert "core & res_pjsip: Improve topology change handling."                    
- menuselect: Use more specific error message.                                    
- res_pjsip_nat: Fix potential use of uninitialized transport details             
- app_if: Fix faulty EndIf branching.                                             
- manager.c: Fix regression due to using wrong free function.                     
- config_options.c: Fix truncation of option descriptions.                        
- manager.c: Improve clarity of "manager show connected".                         
- make_xml_documentation: Really collect LOCAL_MOD_SUBDIRS documentation.         
- general: Fix broken links.                                                      
- MergeApproved.yml:  Remove unneeded concurrency                                 
- app_dial: Add option "j" to preserve initial stream topology of caller          
- ast_coredumper: Increase reliability                                            
- logger.c: Move LOG_GROUP documentation to dedicated XML file.                   
- res_odbc.c: Allow concurrent access to request odbc connections                 
- res_pjsip_header_funcs.c: Check URI parameter length before copying.            
- config.c: Log #exec include failures.                                           
- make_xml_documentation: Properly handle absolute LOCAL_MOD_SUBDIRS.             
- app_voicemail.c: Completely resequence mailbox folders.                         
- sig_analog: Fix channel leak when mwimonitor is enabled.                        
- res_rtp_asterisk.c: Update for OpenSSL 3+.                                      
- alembic: Update list of TLS methods available on ps_transports.                 
- func_channel: Expose previously unsettable options.                             
- app.c: Allow ampersands in playback lists to be escaped.                        
- uri.c: Simplify ast_uri_make_host_with_port()                                   
- func_curl.c: Remove CURLOPT() plaintext documentation.                          
- res_http_websocket.c: Set hostname on client for certificate validation.        
- live_ast: Add astcachedir to generated asterisk.conf.                           
- SECURITY.md: Update with correct documentation URL                              
- func_lock: Add missing see-also refs to documentation.                          
- app_followme.c: Grab reference on nativeformats before using it                 
- configs: Improve documentation for bandwidth in iax.conf.                       
- logger: Add channel-based filtering.                                            
- chan_iax2.c: Don't send unsanitized data to the logger.                         
- codec_ilbc: Disable system ilbc if version >= 3.0.0                             
- resource_channels.c: Explicit codec request when creating UnicastRTP.           
- doc: Update IP Quality of Service links.                                        
- chan_pjsip: Add PJSIPHangup dialplan app and manager action                     
- chan_iax2.c: Ensure all IEs are displayed when dumping frame contents.          
- chan_dahdi: Warn if nonexistent cadence is requested.                           
- stasis: Update the snapshot after setting the redirect                          
- ari: Provide the caller ID RDNIS for the channels                               
- main/utils: Implement ast_get_tid() for OpenBSD                                 
- res_rtp_asterisk.c: Fix runtime issue with LibreSSL                             
- app_directory: Add ADSI support to Directory.                                   
- core_local: Fix local channel parsing with slashes.                             
- Remove files that are no longer updated                                         
- app_voicemail: Add AMI event for mailbox PIN changes.                           
- app_queue.c: Emit unpause reason with PauseQueueMember event.                   
- bridge_simple: Suppress unchanged topology change requests                      
- res_pjsip: Include cipher limit in config error message.                        
- res_speech: allow speech to translate input channel                             
- res_rtp_asterisk.c: Fix memory leak in ephemeral certificate creation.          
- res_pjsip_dtmf_info.c: Add 'INFO' to Allow header.                              
- api.wiki.mustache: Fix indentation in generated markdown                        
- pjsip_configuration.c: Disable DTLS renegotiation if WebRTC is enabled.         
- configs: Fix typo in pjsip.conf.sample.                                         
- res_pjsip_exten_state,res_pjsip_mwi: Allow unload on shutdown                   
- res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 cha..
- res_stasis: signal when new command is queued                                   
- ari/stasis: Indicate progress before playback on a bridge                       
- func_curl.c: Ensure channel is locked when manipulating datastores.             
- Update config.yml                                                               
- logger.h: Add ability to change the prefix on SCOPE_TRACE output                
- Add libjwt to third-party                                                       
- res_pjsip: update qualify_timeout documentation with DNS note                   
- chan_dahdi: Clarify scope of callgroup/pickupgroup.                             
- func_json: Fix crashes for some types                                           
- res_speech_aeap: add aeap error handling                                        
- app_voicemail: Disable ADSI if unavailable.                                     
- codec_builtin: Use multiples of 20 for maximum_ms                               
- lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS                            
- asterisk.c: Use the euid's home directory to read/write cli history             
- res_pjsip_transport_websocket: Prevent transport from being destroyed before m..
- cel: add publish user event helper                                              
- chan_console: Fix deadlock caused by unclean thread exit.                       
- file.c: Add ability to search custom dir for sounds                             
- chan_iax2: Improve authentication debugging.                                    
- res_rtp_asterisk: fix wrong counter management in ioqueue objects               
- make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h               
- func_periodic_hook: Add hangup step to avoid timeout                            
- res_stasis_recording.c: Save recording state when unmuted.                      
- res_speech_aeap: check for null format on response                              
- func_periodic_hook: Don't truncate channel name                                 
- safe_asterisk: Change directory permissions to 755                              
- chan_rtp: Implement RTP glue for UnicastRTP channels                            
- app_queue: periodic announcement configurable start time.                       
- variables: Add additional variable dialplan functions.                          
- Restore CHANGES and UPGRADE.txt to allow cherry-picks to work                   
- res_rtp_asterisk: Fix regression issues with DTLS client check                  
- res_pjsip_header_funcs: Duplicate new header value, don't copy.                 
- res_pjsip: disable raw bad packet logging                                       
- res_rtp_asterisk.c: Check DTLS packets against ICE candidate list               
- manager.c: Prevent path traversal with GetConfig.                               
- ari-stubs: Fix more local anchor references                                     
- ari-stubs: Fix more local anchor references                                     
- ari-stubs: Fix broken documentation anchors                                     
- res_pjsip_session: Send Session Interval too small response                     
- app_dial: Fix infinite loop when sending digits.                                
- app_voicemail: Fix for loop declarations                                        
- alembic: Fix quoting of the 100rel column                                       
- pbx.c: Fix gcc 12 compiler warning.                                             
- app_audiosocket: Fixed timeout with -1 to avoid busy loop.                      
- download_externals:  Fix a few version related issues                           
- main/refer.c: Fix double free in refer_data_destructor + potential leak         
- sig_analog: Add Called Subscriber Held capability.                              
- app_macro: Fix locking around datastore access                                  
- Revert "app_stack: Print proper exit location for PBXless channels."            
- install_prereq: Fix dependency install on aarch64.                              
- res_pjsip.c: Set contact_user on incoming call local Contact header             
- extconfig: Allow explicit DB result set ordering to be disabled.                
- rest-api: Run make ari-stubs                                                    
- res_pjsip_header_funcs: Make prefix argument optional.                          
- pjproject_bundled: Increase PJSIP_MAX_MODULE to 38                              
- manager: Tolerate stasis messages with no channel snapshot.                     
- core/ari/pjsip: Add refer mechanism                                             
- chan_dahdi: Allow autoreoriginating after hangup.                               
- audiohook: Unlock channel in mute if no audiohooks present.                     
- sig_analog: Allow three-way flash to time out to silence.                       
- res_prometheus: Do not generate broken metrics                                  
- res_pjsip: Enable TLS v1.3 if present.                                          
- func_cut: Add example to documentation.                                         
- extensions.conf.sample: Remove reference to missing context.                    
- func_export: Use correct function argument as variable name.                    
- app_queue: Add support for applying caller priority change immediately.         
- chan_iax2.c: Avoid crash with IAX2 switch support.                              
- res_geolocation: Ensure required 'location_info' is present.                    
- Adds manager actions to allow move/remove/forward individual messages in a par..
- app_voicemail: add CLI commands for message manipulation                        
- res_rtp_asterisk: Move ast_rtp_rtcp_report_alloc using `rtp->themssrc_valid` i..
- sig_analog: Allow immediate fake ring to be suppressed.                         
- app.h: Move declaration of ast_getdata_result before its first use              
- doc: Remove obsolete CHANGES-staging and UPGRADE-staging                        
- app_voicemail: fix imap compilation errors                                      
- res_musiconhold: avoid moh state access on unlocked chan                        
- utils: add lock timestamps for DEBUG_THREADS                                    
- rest-api: Updates for new documentation site                                    
- app_voicemail_imap: Fix message count when IMAP server is unavailable           
- res_pjsip_rfc3326: Prefer Q.850 cause code over SIP.                            
- res_pjsip_session: Added new function calls to avoid ABI issues.                
- app_queue: Add force_longest_waiting_caller option.                             
- pjsip_transport_events.c: Use %zu printf specifier for size_t.                  
- res_crypto.c: Gracefully handle potential key filename truncation.              
- configure: Remove obsolete and deprecated constructs.                           
- res_fax_spandsp.c: Clean up a spaces/tabs issue                                 
- ast-db-manage: Synchronize revisions between comments and code.                 
- test_statis_endpoints:  Fix channel_messages test again                         
- res_crypto.c: Avoid using the non-portable ALLPERMS macro.                      
- tcptls: when disabling a server port, we should set the accept_fd to -1.        
- AMI: Add parking position parameter to Park action                              
- test_stasis_endpoints.c: Make channel_messages more stable                      
- build: Fix a few gcc 13 issues                                                  
- ast-db-manage: Fix alembic branching error caused by #122.                      
- app_followme: fix issue with enable_callee_prompt=no (#88)                      
- sounds: Update download URL to use HTTPS.                                       
- configure: Makefile downloader enable follow redirects.                         
- res_musiconhold: Add option to loop last file.                                  
- chan_dahdi: Fix Caller ID presentation for FXO ports.                           
- AMI: Add CoreShowChannelMap action.                                             
- sig_analog: Add fuller Caller ID support.                                       
- res_stasis.c: Add new type 'sdp_label' for bridge creation.                     
- app_queue: Preserve reason for realtime queues                                  
- indications: logging changes                                                    
- callerid: Allow specifying timezone for date/time.                              
- logrotate: Fix duplicate log entries.                                           
- chan_pjsip: Allow topology/session refreshes in early media state               
- chan_dahdi: Fix broken hidecallerid setting.                                    
- asterisk.c: Fix option warning for remote console.                              
- configure: fix test code to match gethostbyname_r prototype.                    
- res_pjsip_pubsub.c: Use pjsip version for pending NOTIFY check. (#77)           
- res_sorcery_memory_cache.c: Fix memory leak                                     
- xml.c: Process XML Inclusions recursively.                                      
- apply_patches: Use globbing instead of file/sort.                               
- apply_patches: Sort patch list before applying                                  
- pjsip: Upgrade bundled version to pjproject 2.13.1                              
- Set up new ChangeLogs directory                                                 
- chan_pjsip: also return all codecs on empty re-INVITE for late offers           
- cel: add local optimization begin event                                         
- core: Cleanup gerrit and JIRA references. (#57)                                 
- res_pjsip: mediasec: Add Security-Client headers after 401                      
- LICENSE: Update link to trademark policy.                                       
- chan_dahdi: Add dialmode option for FXS lines.                                  
- Initial GitHub PRs                                                              
- Initial GitHub Issue Templates                                                  
- pbx_dundi: Fix PJSIP endpoint configuration check.                              
- Revert "app_queue: periodic announcement configurable start time."              
- res_pjsip_stir_shaken: Fix JSON field ordering and disallowed TN characters.    
- pbx_dundi: Add PJSIP support.                                                   
- install_prereq: Add Linux Mint support.                                         
- chan_pjsip: fix music on hold continues after INVITE with replaces              
- voicemail.conf: Fix incorrect comment about #include.                           
- app_queue: Fix minor xmldoc duplication and vagueness.                          
- test.c: Fix counting of tests and add 2 new tests                               
- res_calendar: output busy state as part of show calendar.                       
- loader.c: Minor module key check simplification.                                
- ael: Regenerate lexers and parsers.                                             
- bridge_builtin_features: add beep via touch variable                            
- res_mixmonitor: MixMonitorMute by MixMonitor ID                                 
- format_sln: add .slin as supported file extension                               
- res_agi: RECORD FILE plays 2 beeps.                                             
- func_json: Fix JSON parsing issues.                                             
- app_senddtmf: Add SendFlash AMI action.                                         
- app_dial: Fix DTMF not relayed to caller on unanswered calls.                   
- configure: fix detection of re-entrant resolver functions                       
- cli: increase channel column width                                              
- app_queue: periodic announcement configurable start time.                       
- make_version: Strip svn stuff and suppress ref HEAD errors                      
- res_http_media_cache: Introduce options and customize                           
- main/iostream.c: fix build with libressl                                        
- contrib: rc.archlinux.asterisk uses invalid redirect.                           
- res_pjsip_pubsub: subscription cleanup changes                                  
- Revert "pbx_ael: Global variables are not expanded."                            
- res_pjsip: Replace invalid UTF-8 sequences in callerid name                     
- test.c: Avoid passing -1 to FD_* family of functions.                           
- chan_iax2: Fix jitterbuffer regression prior to receiving audio.                
- test_crypto.c: Fix getcwd(…) build error.                                       
- pjproject_bundled: Fix cross-compilation with SSL libs.                         
- app_read: Add an option to return terminator on empty digits.                   
- res_phoneprov.c: Multihomed SERVER cache prevention                             
- app_directory: Add a 'skip call' option.                                        
- app_senddtmf: Add option to answer target channel.                              
- res_pjsip: Prevent SEGV in pjsip_evsub_send_request                             
- app_queue: Minor docs and logging fixes for UnpauseQueueMember.                 
- app_queue: Reset all queue defaults before reload.                              
- res_pjsip: Upgraded bundled pjsip to 2.13                                       
- doxygen: Fix doxygen errors.                                                    
- app_signal: Add signaling applications                                          
- app_directory: add ability to specify configuration file                        
- func_json: Enhance parsing capabilities of JSON_DECODE                          
- res_stasis_snoop: Fix snoop crash                                               
- pbx_ael: Global variables are not expanded.                                     
- res_pjsip_session: Add overlap_context option.                                  
- app_playback.c: Fix PLAYBACKSTATUS regression.                                  
- res_rtp_asterisk: Don't use double math to generate timestamps                  
- format_wav: replace ast_log(LOG_DEBUG, ...) by ast_debug(1, ...)                
- res_pjsip_rfc3326: Add SIP causes support for RFC3326                           
- res_rtp_asterisk: Asterisk Media Experience Score (MES)                         
- Revert "res_rtp_asterisk: Asterisk Media Experience Score (MES)"                
- loader: Allow declined modules to be unloaded.                                  
- app_broadcast: Add Broadcast application                                        
- func_frame_trace: Print text for text frames.                                   
- json.h: Add ast_json_object_real_get.                                           
- manager: Fix appending variables.                                               
- res_pjsip_transport_websocket: Add remote port to transport                     
- http.c: Fix NULL pointer dereference bug                                        
- res_http_media_cache: Do not crash when there is no extension                   
- res_rtp_asterisk: Asterisk Media Experience Score (MES)                         
- pbx_app: Update outdated pbx_exec channel snapshots.                            
- res_pjsip_session: Use Caller ID for extension matching.                        
- res_pjsip_sdp_rtp.c: Use correct timeout when put on hold.                      
- app_voicemail_odbc: Fix string overflow warning.                                
- func_callerid: Warn about invalid redirecting reason.                           
- res_pjsip: Fix path usage in case dialing with '@'                              
- streams:  Ensure that stream is closed in ast_stream_and_wait on error          
- app_sendtext: Remove references to removed applications.                        
- res_geoloc: fix NULL pointer dereference bug                                    
- res_pjsip_aoc: Don't assume a body exists on responses.                         
- app_if: Fix format truncation errors.                                           
- manager: AOC-S support for AOCMessage                                           
- res_pjsip_aoc: New module for sending advice-of-charge with chan_pjsip          
- ari: Destroy body variables in channel create.                                  
- app_voicemail: Fix missing email in msg_create_from_file.                       
- res_pjsip: Fix typo in from_domain documentation                                
- res_hep: Add support for named capture agents.                                  
- app_if: Adds conditional branch applications                                    
- res_pjsip_session.c: Map empty extensions in INVITEs to s.                      
- res_pjsip: Update contact_user to point out default                             
- res_adsi: Fix major regression caused by media format rearchitecture.           
- res_pjsip_header_funcs: Add custom parameter support.                           
- func_presencestate: Fix invalid memory access.                                  
- sig_analog: Fix no timeout duration.                                            
- xmldoc: Allow XML docs to be reloaded.                                          
- rtp_engine.h: Update examples using ast_format_set.                             
- app_mixmonitor: Add option to use real Caller ID for voicemail.                 
- pjproject: 2.13 security fixes                                                  
- pjsip_transport_events: Fix possible use after free on transport                
- manager: prevent file access outside of config dir                              
- ooh323c: not checking for IE minimum length                                     
- pbx_builtins: Allow Answer to return immediately.                               
- chan_dahdi: Allow FXO channels to start immediately.                            
- core & res_pjsip: Improve topology change handling.                             
- sla: Prevent deadlock and crash due to autoservicing.                           
- Build system: Avoid executable stack.                                           
- func_json: Fix memory leak.                                                     
- test_json: Remove duplicated static function.                                   
- res_agi: Respect "transmit_silence" option for "RECORD FILE".                   
- app_mixmonitor: Add option to delete files on exit.                             
- manager: Update ModuleCheck documentation.                                      
- file.c: Don't emit warnings on winks.                                           
- runUnittests.sh:  Save coredumps to proper directory                            
- app_stack: Print proper exit location for PBXless channels.                     
- chan_rtp: Make usage of ast_rtp_instance_get_local_address clearer              
- res_pjsip: prevent crash on websocket disconnect                                
- tcptls: Prevent crash when freeing OpenSSL errors.                              
- res_pjsip_outbound_registration: Allow to use multiple proxies for registration 
- tests: Fix compilation errors on 32-bit.                                        
- res_pjsip: return all codecs on a re-INVITE without SDP                         
- res_pjsip_notify: Add option support for AMI.                                   
- res_pjsip_logger: Add method-based logging option.                              
- Dialing API: Cancel a running async thread, may not cancel all calls            
- chan_dahdi: Fix unavailable channels returning busy.                            
- res_pjsip_pubsub: Prevent removing subscriptions.                               
- say: Don't prepend ampersand erroneously.                                       
- res_crypto: handle unsafe private key files                                     
- audiohook: add directional awareness                                            
- cdr: Allow bridging and dial state changes to be ignored.                       
- res_tonedetect: Add ringback support to TONE_DETECT.                            
- chan_dahdi: Resolve format truncation warning.                                  
- res_crypto: don't modify fname in try_load_key()                                
- res_crypto: use ast_file_read_dirs() to iterate                                 
- res_geolocation: Update wiki documentation                                      
- res_pjsip: Add mediasec capabilities.                                           
- res_prometheus: Do not crash on invisible bridges                               
- res_pjsip_geolocation: Change some notices to debugs.                           
- db: Fix incorrect DB tree count for AMI.                                        
- func_logic: Don't emit warning if both IF branches are empty.                   
- features: Add no answer option to Bridge.                                       
- app_bridgewait: Add option to not answer channel.                               
- app_amd: Add option to play audio during AMD.                                   
- test: initialize capture structure before freeing                               
- func_export: Add EXPORT function                                                
- res_pjsip: Add 100rel option "peer_supported".                                  
- func_scramble: Fix null pointer dereference.                                    
- manager: be more aggressive about purging http sessions.                        
- func_strings: Add trim functions.                                               
- res_crypto: Memory issues and uninitialized variable errors                     
- res_geolocation: Fix issues exposed by compiling with -O2                       
- res_crypto: don't complain about directories                                    
- res_pjsip: Add user=phone on From and PAID for usereqphone=yes                  
- res_geolocation: Fix segfault when there's an empty element                     
- res_musiconhold: Add option to not play music on hold on unanswered channels    
- res_pjsip: Add TEL URI support for basic calls.                                 
- res_crypto: Use EVP API's instead of legacy API's                               
- test: Add coverage for res_crypto                                               
- res_crypto: make keys reloadable on demand for testing                          
- test: Add test coverage for capture child process output                        
- main/utils: allow checking for command in $PATH                                 
- test: Add ability to capture child process output                               
- res_crypto: Don't load non-regular files in keys directory                      
- func_frame_trace: Remove bogus assertion.                                       
- lock.c: Add AMI event for deadlocks.                                            
- app_confbridge: Add end_marked_any option.                                      
- pbx_variables: Use const char if possible.                                      
- res_geolocation: Add two new options to GEOLOC_PROFILE                          
- res_geolocation:  Allow location parameters on the profile object               
- res_geolocation: Add profile parameter suppress_empty_ca_elements               
- res_geolocation:  Add built-in profiles                                         
- res_pjsip_sdp_rtp: Skip formats without SDP details.                            
- cli: Prevent assertions on startup from bad ao2 refs.                           
- pjsip: Add TLS transport reload support for certificate and key.                
- res_tonedetect: Fix typos referring to wrong variables.                         
- alembic: add missing ps_endpoints columns                                       
- chan_dahdi.c: Resolve a format-truncation build warning.                        
- res_pjsip_pubsub: Postpone destruction of old subscriptions on RLS update       
- channel.h: Remove redundant declaration.                                        
- features: Add transfer initiation options.                                      
- CI: Fixing path issue on venv check                                             
- CI: use Python3 virtual environment                                             
- general: Very minor coding guideline fixes.                                     
- res_geolocation: Address user issues, remove complexity, plug leaks             
- chan_iax2: Add missing options documentation.                                   
- app_confbridge: Fix memory leak on updated menu options.                        
- Geolocation: Wiki Documentation                                                 
- manager: Remove documentation for nonexistent action.                           
- general: Improve logging levels of some log messages.                           
- cdr.conf: Remove obsolete app_mysql reference.                                  
- general: Remove obsolete SVN references.                                        
- app_confbridge: Add missing AMI documentation.                                  
- app_meetme: Add missing AMI documentation.                                      
- func_srv: Document field parameter.                                             
- pbx_functions.c: Manually update ast_str strlen.                                
- build: fix bininstall launchd issue on cross-platform build                     
- db: Add AMI action to retrieve DB keys at prefix.                               
- manager: Fix incomplete filtering of AMI events.                                
- Update defaultbranch to 20                                                      
- res_pjsip: delay contact pruning on Asterisk start                              
- chan_dahdi: Fix buggy and missing Caller ID parameters                          
- queues.conf.sample: Correction of typo                                          
- chan_dahdi: Add POLARITY function.                                              
- Makefile: Avoid git-make user conflict                                          
- app_confbridge: Always set minimum video update interval.                       
- pbx.c: Simplify ast_context memory management.                                  
- geoloc_eprofile.c: Fix setting of loc_src in set_loc_src()                      
- Geolocation:  chan_pjsip Capability Preview                                     
- Geolocation:  Core Capability Preview                                           
- general: Fix various typos.                                                     
- cel_odbc & res_config_odbc: Add support for SQL_DATETIME field type             
- chan_iax2: Allow compiling without OpenSSL.                                     
- websocket / aeap: Handle poll() interruptions better.                           
- res_cliexec: Add dialplan exec CLI command.                                     
- features: Update documentation for automon and automixmon                       
- Geolocation: Base Asterisk Prereqs                                              
- pbx_lua: Remove compiler warnings                                               
- res_pjsip_header_funcs: Add functions PJSIP_RESPONSE_HEADER and PJSIP_RESPONSE..
- res_prometheus: Optional load res_pjsip_outbound_registration.so                
- app_dial: Fix dial status regression.                                           
- db: Notify user if deleted DB entry didn't exist.                               
- cli: Fix CLI blocking forever on terminating backslash                          
- app_dial: Propagate outbound hook flashes.                                      
- res_calendar_icalendar: Send user agent in request.                             
- say: Abort play loop if caller hangs up.                                        
- res_pjsip: allow TLS verification of wildcard cert-bearing servers              
- pbx: Add helper function to execute applications.                               
- pjsip: Upgrade bundled version to pjproject 2.12.1                              
- asterisk.c: Fix incompatibility warnings for remote console.                    
- test_aeap_transport: disable part of failing unit test                          
- sig_analog: Fix broken three-way conferencing.                                  
- app_voicemail: Add option to prevent message deletion.                          
- res_parking: Add music on hold override option.                                 
- xmldocs: Improve examples.                                                      
- res_pjsip_outbound_registration: Make max random delay configurable.            
- res_pjsip: Actually enable session timers when timers=always                    
- res_pjsip_pubsub: delete scheduled notification on RLS update                   
- res_pjsip_pubsub: XML sanitized RLS display name                                
- app_sayunixtime: Use correct inflection for German time.                        
- chan_iax2: Prevent deadlock due to duplicate autoservice.                       
- loader: Prevent deadlock using tab completion.                                  
- res_calendar: Prevent assertion if event ends in past.                          
- res_parking: Warn if out of bounds parking spot requested.                      
- chan_pjsip: Only set default audio stream on hold.                              
- res_pjsip_dialog_info_body_generator: Set LOCAL target URI as local URI         
- res_agi: Evaluate dialplan functions and variables in agi exec if enabled       
- ast_pkgconfig.m4: AST_PKG_CONFIG_CHECK() relies on sed.                         
- ari: expose channel driver's unique id to ARI channel resource                  
- loader.c: Use portable printf conversion specifier for int64.                   
- res_pjsip_transport_websocket: Also set the remote name.                        
- res_pjsip_transport_websocket: save the original contact host                   
- res_pjsip_outbound_registration: Show time until expiration                     
- app_confbridge: Add function to retrieve channels.                              
- chan_dahdi: Fix broken operator mode clearing.                                  
- GCC12: Fixes for 16+                                                            
- GCC12: Fixes for 18+.  state_id_by_topic comparing wrong value                  
- core_unreal: Flip stream direction of second channel.                           
- chan_dahdi: Document dial resource options.                                     
- chan_dahdi: Don't allow MWI FSK if channel not idle.                            
- apps/confbridge: Added hear_own_join_sound option to control who hears sound_j..
- chan_dahdi: Don't append cadences on dahdi restart.                             
- chan_iax2: Prevent crash if dialing RSA-only call without outkey.               
- menuselect: Don't erroneously recompile modules.                                
- app_meetme: Don't erroneously set global variables.                             
- asterisk.c: Warn of incompatibilities with remote console.                      
- func_db: Add function to return cardinality at prefix                           
- chan_dahdi: Fix insufficient array size for round robin.                        
- chan_sip.c Session timers get removed on UPDATE                                 
- func_evalexten: Extension evaluation function.                                  
- file.c: Prevent formats from seeking negative offsets.                          
- chan_pjsip: Add ability to send flash events.                                   
- cli: Add command to evaluate dialplan functions.                                
- documentation: Adds versioning information.                                     
- samples: Remove obsolete sample configs.                                        
- chan_pjsip: add allow_sending_180_after_183 option                              
- chan_sip: SIP route header is missing on UPDATE                                 
- manager: Terminate session on write error.                                      
- bridge_simple.c: Unhold channels on join simple bridge.                         
- res_aeap & res_speech_aeap: Add Asterisk External Application Protocol          
- app_dial: Flip stream direction of outgoing channel.                            
- res_pjsip_stir_shaken.c: Fix enabled when not configured.                       
- res_pjsip: Always set async_operations to 1.                                    
- config.h: Don't use C++ keywords as argument names.                             
- cdr_adaptive_odbc: Add support for SQL_DATETIME field type.                     
- pjsip: Increase maximum number of format attributes.                            
- AST-2022-002 - res_stir_shaken/curl: Add ACL checks for Identity header.        
- AST-2022-001 - res_stir_shaken/curl: Limit file size and check start.           
- func_odbc: Add SQL_ESC_BACKSLASHES dialplan function.                           
- app_mf, app_sf: Return -1 if channel hangs up.                                  
- app_queue: Add music on hold option to Queue.                                   
- app_meetme: Emit warning if conference not found.                               
- build: Remove obsolete leftover build references.                               
- res_pjsip_header_funcs: wrong pool used tdata headers                           
- deprecation cleanup: remove leftover files                                      
- pjproject: Update bundled to 2.12 release.                                      
- pbx.c: Warn if there are too many includes in a context.                        
- Makefile:  Disable XML doc validation                                           
- make_xml_documentation: Remove usage of get_sourceable_makeopts                 
- chan_iax2: Fix spacing in netstats command                                      
- openssl: Supress deprecation warnings from OpenSSL 3.0                          
- documentation: Add information on running install_prereq script in readme       
- chan_iax2: Fix perceived showing host address.                                  
- res_pjsip_sdp_rtp: Improve detecting of lack of RTP activity                    
- configure.ac: Use pkg-config to detect libxml2                                  
- time: add support for time64 libcs                                              
- res_pjsip_pubsub: RLS 'uri' list attribute mismatch with SUBSCRIBE request      
- app_dial: Document DIALSTATUS return values.                                    
- stasis_recording: Perform a complete match on requested filename.               
- download_externals: Use HTTPS for downloads                                     
- conversions.c: Specify that we only want to parse decimal numbers.              
- logger: workaround woefully small BUFSIZ in MUSL                                
- pbx_builtins: Add missing options documentation                                 
- res_pjsip_pubsub: update RLS to reflect the changes to the lists                
- res_agi: Fix xmldocs bug with set music.                                        
- res_config_pgsql: Add text-type column check in require_pgsql()                 
- app_queue: Add QueueWithdrawCaller AMI action                                   
- ami: Improve substring parsing for disabled events.                             
- xml.c, config,c:  Add stylesheets and variable list string parsing              
- xmldoc: Fix issue with xmlstarlet validation                                    
- core: Config and XML tweaks needed for geolocation                              
- Makefile: Allow XML documentation to exist outside source files                 
- build: Refactor the earlier "basebranch" commit                                 
- jansson: Update bundled to 2.14 version.                                        
- func_channel: Add lastcontext and lastexten.                                    
- channel.c: Clean up debug level 1.                                              
- configs, LICENSE: remove pbx.digium.com.                                        
- documentation: Add since tag to xmldocs DTD                                     
- asterisk: Add macro for curl user agent.                                        
- res_stir_shaken: refactor utility function                                      
- app_voicemail: Emit warning if asking for nonexistent mailbox.                  
- res_pjsip_pubsub: fix Batched Notifications stop working                        
- res_pjsip_pubsub: provide a display name for RLS subscriptions                  
- func_db: Add validity check for key names when writing.                         
- cli: Add core dump info to core show settings.                                  
- documentation: Adds missing default attributes.                                 
- app_mp3: Document and warn about HTTPS incompatibility.                         
- app_mf: Add max digits option to ReceiveMF.                                     
- ami: Allow events to be globally disabled.                                      
- taskprocessor.c: Prevent crash on graceful shutdown                             
- app_queue: load queues and members from Realtime when needed                    
- manager.c: Simplify AMI ModuleCheck handling                                    
- res_prometheus.c: missing module dependency                                     
- res_pjsip.c: Correct minor typos in 'realm' documentation.                      
- manager.c: Generate valid XML if attribute names have leading digits.           
- build_tools/make_version: Fix bashism in comparison.                            
- bundled_pjproject:  Add additional multipart search utils                       
- chan_sip.c Fix pickup on channel that are in AST_STATE_DOWN                     
- build: Add "basebranch" to .gitreview                                           
- res_pjsip_outbound_authenticator_digest: Prevent ABRT on cleanup                
- cdr: allow disabling CDR by default on new channels                             
- res_tonedetect: Fixes some logic issues and typos                               
- func_frame_drop: Fix typo referencing wrong buffer                              
- res/res_rtp_asterisk: fix skip in rtp sequence numbers after dtmf               
- res_http_websocket: Add a client connection timeout                             
- build: Rebuild configure and autoconfig.h.in                                    
- sched: fix and test a double deref on delete of an executing call back          
- app_queue.c: Queue don't play "thank-you" when here is no hold time announceme..
- res_pjsip_sdp_rtp.c: Support keepalive for video streams.                       
- build_tools/make_version: Fix sed(1) syntax compatibility with NetBSD           
- main/utils: Implement ast_get_tid() for NetBSD                                  
- main: Enable rdtsc support on NetBSD                                            
- BuildSystem: Fix misdetection of gethostbyname_r() on NetBSD                    
- include: Remove unimplemented HMAC declarations                                 
- frame.h: Fix spelling typo                                                      
- res_rtp_asterisk: Fix typo in flag test/set                                     
- bundled_pjproject: Fix srtp detection                                           
- res_pjsip: Make message_filter and session multipart aware                      
- build: Fix issues building pjproject                                            
- res_pjsip: Add utils for checking media types                                   
- bundled_pjproject: Create generic pjsip_hdr_find functions                      
- say.c: Prevent erroneous failures with 'say' family of functions.               
- documentation: Document built-in system and channel vars                        
- pbx_variables: add missing ASTSBINDIR variable                                  
- bundled_pjproject:  Make it easier to hack                                      
- utils.c: Remove all usages of ast_gethostbyname()                               
- say.conf: fix 12pm noon logic                                                   
- pjproject: Fix incorrect unescaping of tokens during parsing                    
- app_queue.c: Support for Nordic syntax in announcements                         
- dsp: Add define macro for DTMF_MATRIX_SIZE                                      
- ami: Add AMI event for Wink                                                     
- cli: Add module refresh command                                                 
- app_mp3: Throw warning on nonexistent stream                                    
- documentation: Add missing AMI documentation                                    
- tcptls.c: refactor client connection to be more robust                          
- app_sf: Add full tech-agnostic SF support                                       
- app_queue: Fix hint updates, allow dup. hints                                   
- say.c: Honor requests for DTMF interruption.                                    
- res_pjsip_sdp_rtp: Preserve order of RTP codecs                                 
- bridge: Unlock channel during Local peer check.                                 
- test_time.c: Tolerate DST transitions                                           
- bundled_pjproject:  Add more support for multipart bodies                       
- ast_coredumper: Fix deleting results when output dir is set                     
- pbx_variables: initialize uninitialized variable                                
- app_queue.c: added DIALEDPEERNUMBER on outgoing channel                         
- http.c: Add ability to create multiple HTTP servers                             
- app.c: Throw warnings for nonexistent options                                   
- app_voicemail.c: Support for Danish syntax in VM                                
- app_sendtext: Add ReceiveText application                                       
- strings: Fix enum names in comment examples                                     
- pbx_variables: Increase parsing capabilities of MSet                            
- chan_sip: Fix crash when accessing RURI before initiating outgoing call         
- func_json: Adds JSON_DECODE function                                            
- configs: Updates to sample configs                                              
- pbx: Add variable substitution API for extensions                               
- CHANGES: Correct reference to configuration file.                               
- app_mf: Add full tech-agnostic MF support                                       
- xmldoc: Avoid whitespace around value for parameter/required.                   
- progdocs: Fix Doxygen left-overs.                                               
- xmldoc: Correct definition for XML element 'matchInfo'.                         
- progdocs: Update Makefile.                                                      
- res_pjsip_sdp_rtp: Do not warn on unknown sRTP crypto suites.                   
- channel: Short-circuit ast_channel_get_by_name() on empty arg.                  
- res_rtp_asterisk: Addressing possible rtp range issues                          
- apps/app_dial.c: HANGUPCAUSE reason code for CANCEL is set to AST_CAUSE_NORMAL..
- res: Fix for Doxygen.                                                           
- res_fax_spandsp: Add spandsp 3.0.0+ compatibility                               
- main: Fix for Doxygen.                                                          
- progdocs: Fix for Doxygen, the hidden parts.                                    
- progdocs: Fix grouping for latest Doxygen.                                      
- documentation: Standardize examples                                             
- config.c: Prevent UB in ast_realtime_require_field.                             
- app_voicemail: Refactor email generation functions                              
- stir/shaken: Avoid a compiler extension of GCC.                                 
- progdocs: Remove outdated references in doxyref.h.                              
- logger: use __FUNCTION__ instead of __PRETTY_FUNCTION__                         
- xmldoc: Fix for Doxygen.                                                        
- astobj2.c: Fix core when ref_log enabled                                        
- channels: Fix for Doxygen.                                                      
- bridge: Deny full Local channel pair in bridge.                                 
- res_tonedetect: Add call progress tone detection                                
- rtp_engine: Add type field for JSON RTCP Report stasis messages                 
- odbc: Fix for Doxygen.                                                          
- parking: Fix for Doxygen.                                                       
- res_ari: Fix for Doxygen.                                                       
- frame: Fix for Doxygen.                                                         
- ari-stubs: Avoid 'is' as comparism with an literal.                             
- BuildSystem: Consistently allow 'ye' even for Jansson.                          
- stasis: Fix for Doxygen.                                                        
- app: Fix for Doxygen.                                                           
- res_xmpp: Fix for Doxygen.                                                      
- channel: Fix for Doxygen.                                                       
- chan_iax2: Fix for Doxygen.                                                     
- res_pjsip: Fix for Doxygen.                                                     
- bridges: Fix for Doxygen.                                                       
- addons: Fix for Doxygen.                                                        
- apps: Fix for Doxygen.                                                          
- tests: Fix for Doxygen.                                                         
- progdocs: Avoid multiple use of section labels.                                 
- progdocs: Use Doxygen \example correctly.                                       
- bridge_channel: Fix for Doxygen.                                                
- progdocs: Avoid 'name' with Doxygen \file.                                      
- app_morsecode: Fix deadlock                                                     
- app_read: Fix custom terminator functionality regression                        
- res_pjsip_callerid: Fix OLI parsing                                             
- build_tools: Spelling fixes                                                     
- contrib: Spelling fixes                                                         
- codecs: Spelling fixes                                                          
- formats: Spelling fixes                                                         
- CREDITS: Spelling fixes                                                         
- addons: Spelling fixes                                                          
- configs: Spelling fixes                                                         
- doc: Spelling fixes                                                             
- menuselect: Spelling fixes                                                      
- include: Spelling fixes                                                         
- UPGRADE.txt: Spelling fixes                                                     
- bridges: Spelling fixes                                                         
- apps: Spelling fixes                                                            
- channels: Spelling fixes                                                        
- tests: Spelling fixes                                                           
- CHANGES: Spelling fixes                                                         
- funcs: Spelling fixes                                                           
- pbx: Spelling fixes                                                             
- main: Spelling fixes                                                            
- utils: Spelling fixes                                                           
- Makefile: Spelling fixes                                                        
- res: Spelling fixes                                                             
- rest-api-templates: Spelling fixes                                              
- agi: Spelling fixes                                                             
- CI: Rename 'master' node to 'built-in'                                          
- BuildSystem: In POSIX sh, == in place of = is undefined.                        
- pbx.c: Don't remove dashes from hints on reload.                                
- sig_analog: Fix truncated buffer copy                                           
- app_voicemail: Fix phantom voicemail bug on rerecord                            
- chan_iax2: Allow both secret and outkey at dial time                            
- res_snmp: As build tool, prefer pkg-config over net-snmp-config.                
- res_config_sqlite: Remove deprecated module.                                    
- stasis: Avoid 'dispatched' as unused variable in normal mode.                   
- various: Fix GCC 11.2 compilation issues.                                       
- ast_coredumper:  Refactor to better find things                                 
- strings/json: Add string delimter match, and object create with vars methods    
- STIR/SHAKEN: Option split and response codes.                                   
- app_queue: Add LoginTime field for member in a queue.                           
- res_speech: Add a type conversion, and new engine unregister methods            
- various: Fix GCC 11 compilation issues.                                         
- apps/app_playback.c: Add 'mix' option to app_playback                           
- BuildSystem: Check for alternate openssl packages                               
- func_talkdetect.c: Fix logical errors in silence detection.                     
- configure: Remove unused OpenSSL SRTP check.                                    
- build: prevent binary downloads for non x86 architectures                       
- main/stun.c: fix crash upon STUN request timeout                                
- Makefile: Use basename in a POSIX-compliant way.                                
- pbx_ael:  Fix crash and lockup issue regarding 'ael reload'                     
- chan_iax2: Add encryption for RSA authentication                                
- res_pjsip_t38: bind UDPTL sessions like RTP                                     
- app_read: Fix null pointer crash                                                
- res_rtp_asterisk: fix memory leak                                               
- main/say.c: Support future dates with Q and q format params                     
- res_pjsip_registrar: Remove unavailable contacts if exceeds max_contacts        
- ari: Ignore invisible bridges when listing bridges.                             
- func_vmcount: Add support for multiple mailboxes                                
- message.c: Support 'To' header override with AMI's MessageSend.                 
- func_channel: Add CHANNEL_EXISTS function.                                      
- app_queue: Fix hint updates for included contexts                               
- res_http_media_cache.c: Compare unaltered MIME types.                           
- logger: Add custom logging capabilities                                         
- app_externalivr.c: Fix mixed leading whitespace in source code.                 
- res_rtp_asterisk.c: Fix build failure when not building with pjproject.         
- pjproject: Add patch to fix trailing whitespace issue in rtpmap                 
- app_mp3: Force output to 16 bits in mpg123                                      
- res_pjsip_caller_id: Add ANI2/OLI parsing                                       
- app_mf: Add channel agnostic MF sender                                          
- app_stack: Include current location if branch fails                             
- test_http_media_cache.c: Fix copy/paste error during test deregistration.       
- resource_channels.c: Fix external media data option                             
- func_strings: Add STRBETWEEN function                                           
- test_abstract_jb.c: Fix put and put_out_of_order memory leaks.                  
- func_env: Add DIRNAME and BASENAME functions                                    
- func_sayfiles: Retrieve say file names                                          
- res_tonedetect: Tone detection module                                           
- res_snmp: Add -fPIC to _ASTCFLAGS                                               
- app_voicemail.c: Ability to silence instructions if greeting is present.        
- term.c: Add support for extended number format terminfo files.                  
- res_srtp: Disable parsing of not enabled cryptos                                
- dns.c: Load IPv6 DNS resolvers if configured.                                   
- bridge_softmix: Suppress error on topology change failure                       
- resource_channels.c: Fix wrong external media parameter parse                   
- config_options: Handle ACO arrays correctly in generated XML docs.              
- chan_iax2: Add ANI2/OLI information element                                     
- pbx_ael:  Fix crash and lockup issue regarding 'ael reload'                     
- app_read: Allow reading # as a digit                                            
- res_rtp_asterisk: Automatically refresh stunaddr from DNS                       
- bridge_basic: Change warning to verbose if transfer cancelled                   
- app_queue: Don't reset queue stats on reload                                    
- res_rtp_asterisk: sqrt(.) requires the header math.h.                           
- dialplan: Add one static and fix two whitespace errors.                         
- sig_analog: Changes to improve electromechanical signalling compatibility       
- media_cache: Don't lock when curl the remote file                               
- res_pjproject: Allow mapping to Asterisk TRACE level                            
- app_milliwatt: Timing fix                                                       
- func_math: Return integer instead of float if possible                          
- app_morsecode: Add American Morse code                                          
- func_scramble: Audio scrambler function                                         
- app_originate: Add ability to set codecs                                        
- BuildSystem: Remove two dead exceptions for compiler Clang.                     
- chan_alsa, chan_sip: Add replacement to moduleinfo                              
- res_monitor: Disable building by default.                                       
- muted: Remove deprecated application.                                           
- conf2ael: Remove deprecated application.                                        
- res_config_sqlite: Remove deprecated module.                                    
- chan_vpb: Remove deprecated module.                                             
- chan_misdn: Remove deprecated module.                                           
- chan_nbs: Remove deprecated module.                                             
- chan_phone: Remove deprecated module.                                           
- chan_oss: Remove deprecated module.                                             
- cdr_syslog: Remove deprecated module.                                           
- app_dahdiras: Remove deprecated module.                                         
- app_nbscat: Remove deprecated module.                                           
- app_image: Remove deprecated module.                                            
- app_url: Remove deprecated module.                                              
- app_fax: Remove deprecated module.                                              
- app_ices: Remove deprecated module.                                             
- app_mysql: Remove deprecated module.                                            
- cdr_mysql: Remove deprecated module.                                            
- mgcp: Remove dead debug code                                                    
- policy: Deprecate modules and add versions to others.                           
- func_frame_drop: New function                                                   
- aelparse: Accept an included context with timings.                              
- format_ogg_speex: Implement a "not supported" write handler                     
- cdr_adaptive_odbc: Prevent filter warnings                                      
- app_queue: Allow streaming multiple announcement files                          
- res_pjsip_header_funcs: Add PJSIP_HEADERS() ability to read header by pattern   
- res_statsd: handle non-standard meter type safely                               
- app_dtmfstore: New application to store digits                                  
- codec_builtin.c: G729 audio gets corrupted by Asterisk due to smoother          
- res_http_media_cache: Cleanup audio format lookup in HTTP requests              
- docs: Remove embedded macro in WaitForCond XML documentation.                   
- Update AMI and ARI versions for Asterisk 20.                                    
- AST-2021-009 - pjproject-bundled: Avoid crash during handshake for TLS          
- AST-2021-008 - chan_iax2: remote crash on unsupported media format              
- AST-2021-007 - res_pjsip_session: Don't offer if no channel exists.             
- res_stasis_playback: Check for chan hangup on play_on_channels                  
- res_http_media_cache.c: Fix merge errors from 18 -> master                      
- res_pjsip_stir_shaken: RFC 8225 compliance and error message cleanup.           
- res_http_media_cache.c: Parse media URLs to find extensions.                    
- main/cdr.c: Correct Party A selection.                                          
- stun: Emit warning message when STUN request times out                          
- app_reload: New Reload application                                              
- res_ari: Fix audiosocket segfault                                               
- res_pjsip_config_wizard.c: Add port matching support.                           
- app_waitforcond: New application                                                
- res_stasis_playback: Send PlaybackFinish event only once for errors             
- jitterbuffer:  Correct signed/unsigned mismatch causing assert                  
- app_dial: Expanded A option to add caller announcement                          
- core: Don't play silence for Busy() and Congestion() applications.              
- res_pjsip_sdp_rtp: Evaluate remotely held for Session Progress                  
- res_pjsip_messaging: Overwrite user in existing contact URI                     
- res_pjsip/pjsip_message_filter: set preferred transport in pjsip_message_filter 
- pbx_builtins: Corrects SayNumber warning                                        
- func_lock: Add "dialplan locks show" cli command.                               
- func_lock: Prevent module unloading in-use module.                              
- func_lock: Fix memory corruption during unload.                                 
- func_lock: Fix requesters counter in error paths.                               
- app_originate: Allow setting Caller ID and variables                            
- menuselect: Fix description of several modules.                                 
- app_confbridge: New ConfKick() application                                      
- res_pjsip_dtmf_info: Hook flash                                                 
- app_confbridge: New option to prevent answer supervision                        
- sip_to_pjsip: Fix missing cases                                                 
- res_pjsip_messaging: Refactor outgoing URI processing                           
- func_math: Three new dialplan functions                                         
- STIR/SHAKEN: Add Date header, dest->tn, and URL checking.                       
- res_pjsip: On partial transport reload also move factories.                     
- func_volume: Add read capability to function.                                   
- stasis: Fix "FRACK!, Failed assertion bad magic number" when unsubscribing      
- res_pjsip.c: Support endpoints with domain info in username                     
- res_rtp_asterisk: Set correct raddr port on RTCP srflx candidates.              
- asterisk: We've moved to Libera Chat!                                           
- res_rtp_asterisk: make it possible to remove SOFTWARE attribute                 
- res_pjsip_outbound_authenticator_digest: Be tolerant of RFC8760 UASs            
- res_pjsip_dialog_info_body_generator: Add LOCAL/REMOTE tags in dialog-info+xml  
- AMI: Add AMI event to expose hook flash events                                  
- app_voicemail: Configurable voicemail beep                                      
- main/file.c: Don't throw error on flash event.                                  
- chan_sip: Expand hook flash recognition.                                        
- pjsip: Add patch for resolving STUN packet lifetime issues.                     
- chan_pjsip: Correct misleading trace message                                    
- STIR/SHAKEN: Switch to base64 URL encoding.                                     
- STIR/SHAKEN: OPENSSL_free serial hex from openssl.                              
- STIR/SHAKEN: Fix certificate type and storage.                                  
- translate.c: Avoid refleak when checking for a translation path                 
- res_rtp_asterisk: More robust timestamp checking                                
- chan_local: Skip filtering audio formats on removed streams.                    
- res_pjsip.c: OPTIONS processing can now optionally skip authentication          
- translate.c: Take sampling rate into account when checking codec's buffer size  
- svn: Switch to https scheme.                                                    
- res_pjsip:  Update documentation for the auth object                            
- res_aeap: Add basic config skeleton and CLI commands.                           
- bridge_channel_write_frame: Check for NULL channel                              
- loader.c: Speed up deprecation metadata lookup                                  
- res_prometheus: Clone containers before iterating                               
- loader: Output warnings for deprecated modules.                                 
- res_rtp_asterisk: Fix standard deviation calculation                            
- res_rtp_asterisk: Don't count 0 as a minimum lost packets                       
- res_rtp_asterisk: Statically declare rtp_drop_packets_data object               
- res_rtp_asterisk: Only raise flash control frame on end.                        
- res_rtp_asterisk: Add a DEVMODE RTP drop packets CLI command                    
- res_pjsip: Give error when TLS transport configured but not supported.          
- time: Add timeval create and unit conversion functions                          
- app_queue: Add alembic migration to add ringinuse to queue_members.             
- modules.conf: Fix more differing usages of assignment operators.                
- logger.conf.sample: Add more debug documentation.                               
- logging: Add .log to samples and update asterisk.logrotate.                     
- app_queue.c: Remove dead 'updatecdr' code.                                      
- queues.conf.sample: Correct 'context' documentation.                            
- logger: Console sessions will now respect logger.conf dateformat= option        
- app_queue.c: Don't crash when realtime queue name is empty.                     
- res_pjsip_session: Make reschedule_reinvite check for NULL topologies           
- app_queue: Only send QueueMemberStatus if status changes.                       
- core_unreal: Fix deadlock with T.38 control frames.                             
- res_pjsip: Add support for partial transport reload.                            
- menuselect: exit non-zero in case of failure on --enable|disable options.       
- res_rtp_asterisk: Force resync on SSRC change.                                  
- menuselect: Add ability to set deprecated and removed versions.                 
- xml: Allow deprecated_in and removed_in for MODULEINFO.                         
- xml: Embed module information into core XML documentation.                      
- documentation: Fix non-matching module support levels.                          
- channel: Fix crash in suppress API.                                             
- func_callerid+res_agi: Fix compile errors related to -Werror=zero-length-bounds 
- app.h: Fix -Werror=zero-length-bounds compile errors in dev mode.               
- app_dial.c: Only send DTMF on first progress event.                             
- res_format_attr_*: Parameter Names are Case-Insensitive.                        
- chan_iax2: System Header strings is included via asterisk.h/compat.h.           
- modules.conf: Fix differing usage of assignment operators.                      
- strings.h: ast_str_to_upper() and _to_lower() are not pure.                     
- res_musiconhold.c: Plug ref leak caused by ao2_replace() misuse.                
- res/res_rtp_asterisk: generate new SSRC on native bridge end                    
- sorcery: Add support for more intelligent reloading.                            
- res_pjsip_refer: Move the progress dlg release to a serializer                  
- res_pjsip_registrar: Include source IP and port in log messages.                
- asterisk: Update copyright.                                                     
- AST-2021-006 - res_pjsip_t38.c: Check for session_media on reinvite.            
- res_format_attr_h263: Generate valid SDP fmtp for H.263+.                       
- res_pjsip_nat: Don't rewrite Contact on REGISTER responses.                     
- channel: Fix memory leak in suppress API.                                       
- res_rtp_asterisk:  Check remote ICE reset and reset local ice attrb             
- pjsip: Generate progress (once) when receiving a 180 with a SDP                 
- main: With Dutch language year after 2020 is not spoken in say.c                
- res_pjsip: dont return early from registration if init auth fails               
- res_fax: validate the remote/local Station ID for UTF-8 format                  
- app_page.c: Don't fail to Page if beep sound file is missing                    
- res_pjsip_refer: Refactor progress locking and serialization                    
- res_rtp_asterisk: Add packet subtype during RTCP debug when relevant            
- res_pjsip_session: Always produce offer on re-INVITE without SDP.               
- res_odbc_transaction: correctly initialise forcecommit value from DSN.          
- res_pjsip_session.c: Check topology on re-invite.                               
- res_config_pgsql: Limit realtime_pgsql() to return one (no more) record.        
- app_queue: Fix conversion of complex extension states into device states        
- app.h: Restore C++ compatibility for macro AST_DECLARE_APP_ARGS                 
- chan_sip: Filter pass-through audio/video formats away, again.                  
- func_odbc:  Introduce minargs config and expose ARGC in addition to ARGn.       
- app_mixmonitor: Add AMI events MixMonitorStart, -Stop and -Mute.                
- AST-2021-002: Remote crash possible when negotiating T.38                       
- rtp:  Enable srtp replay protection                                             
- res_pjsip_diversion: Fix adding more than one histinfo to Supported             
- res_rtp_asterisk.c: Fix signed mismatch that leads to overflow                  
- pjsip: Make modify_local_offer2 tolerate previous failed SDP.                   
- res_pjsip_refer: Always serialize calls to refer_progress_notify                
- core_unreal: Fix T.38 faxing when using local channels.                         
- format_wav: Support of MIME-type for wav16                                      
- chan_sip: Allow [peer] without audio (text+video).                              
- chan_iax2.c: Require secret and auth method if encryption is enabled            
- app_read: Release tone zone reference on early return.                          
- chan_sip: Set up calls without audio (text+video), again.                       
- chan_pjsip, app_transfer: Add TRANSFERSTATUSPROTOCOL variable                   
- channel: Set up calls without audio (text+video), again.                        
- res/res_pjsip.c: allow user=phone when number contain *#                        
- chan_sip: SDP: Reject audio streams correctly.                                  
- main/frame: Add missing control frame names to ast_frame_subclass2str           
- res_musiconhold: Add support of various URL-schemes by MoH.                     
- AC_HEADER_STDC causes a compile failure with autoconf 2.70                      
- pjsip_scheduler: Fix pjsip show scheduled_tasks like for compiler Clang.        
- res_pjsip_session: Avoid sometimes-uninitialized warning with Clang.            
- res_pjsip_pubsub: Fix truncation of persisted SUBSCRIBE packet                  
- chan_pjsip.c: Add parameters to frame in indicate.                              
- res/res_pjsip_session.c: Check that media type matches in function ast_sip_ses..
- Stasis/messaging: tech subscriptions conflict with endpoint subscriptions.      
- chan_sip: SDP: Sidestep stream parsing when its media is disabled.              
- chan_pjsip: Assign SIPDOMAIN after creating a channel                           
- chan_pjsip: Stop queueing control frames twice on outgoing channels             
- contrib/systemd: Added note on common issues with systemd and asterisk          
- Revert "res_pjsip_outbound_registration.c:  Use our own scheduler and other st..
- func_lock: fix multiple-channel-grant problems.                                 
- pbx_lua:  Add LUA_VERSIONS environment variable to ./configure.                 
- app_mixmonitor: cleanup datastore when monitor thread fails to launch           
- app_voicemail: Prevent deadlocks when out of ODBC database connections          
- chan_pjsip: Incorporate channel reference count into transfer_refer().          
- pbx_realtime: wrong type stored on publish of ast_channel_snapshot_type         
- asterisk: Export additional manager functions                                   
- res_pjsip: Prevent segfault in UDP registration with flow transports            
- codecs: Remove test-law.                                                        
- res/res_pjsip_diversion: prevent crash on tel: uri in History-Info              
- chan_vpb.cc: Fix compile errors.                                                
- res_pjsip_session.c: Fix compiler warnings.                                     
- res_pjsip_session: Fixed NULL active media topology handle                      
- app_chanspy: Spyee information missing in ChanSpyStop AMI Event                 
- res_ari: Fix wrong media uri handle for channel play                            
- logger.c: Automatically add a newline to formats that don't have one            
- res_pjsip_nat.c: Create deep copies of strings when appropriate                 
- res_musiconhold: Don't crash when real-time doesn't return any entries          
- res_pjsip_pidf_digium_body_supplement: Support Sangoma user agent.              
- pjsip: Match lifetime of INVITE session to our session.                         
- res_http_media_cache.c: Set reasonable number of redirects                      
- Introduce astcachedir, to be used for temporary bucket files                    
- media_cache: Fix reference leak with bucket file metadata                       
- res_pjsip_stir_shaken: Fix module description                                   
- voicemail: add option 'e' to play greetings as early media                      
- loader: Sync load- and build-time deps.                                         
- CHANGES: Remove already applied CHANGES update                                  
- res_pjsip: set Accept-Encoding to identity in OPTIONS response                  
- chan_sip: Remove unused sip_socket->port.                                       
- bridge_basic: Fixed setup of recall channels                                    
- modules.conf: Align the comments for more conclusiveness.                       
- app_queue: Fix deadlock between update and show queues                          
- res_pjsip_outbound_registration.c:  Use our own scheduler and other stuff       
- pjsip_scheduler.c: Add type ONESHOT and enhance cli show command                
- sched: AST_SCHED_REPLACE_UNREF can lead to use after free of data               
- res_pjsip/config_transport: Load and run without OpenSSL.                       
- res_stir_shaken: Include OpenSSL headers where used actually.                   
- func_curl.c: Allow user to set what return codes constitute a failure.          
- AST-2020-001 - res_pjsip: Return dialog locked and referenced                   
- AST-2020-002 - res_pjsip: Stop sending INVITEs after challenge limit.           
- sip_to_pjsip.py: Handle #include globs and other fixes                          
- Compiler fixes for GCC with -Og                                                 
- Compiler fixes for GCC when printf %s is NULL                                   
- Compiler fixes for GCC with -Os                                                 
- chan_sip: On authentication, pick MD5 for sure.                                 
- main/say: Work around gcc 9 format-truncation false positive                    
- res_pjsip, res_pjsip_session: initialize local variables                        
- install_prereq: Add GMime 3.0.                                                  
- BuildSystem: Enable Lua 5.4.                                                    
- res_pjsip_session: Restore calls to ast_sip_message_apply_transport()           
- features.conf.sample: Sample sound files incorrectly quoted                     
- logger.conf.sample: add missing comment mark                                    
- res_pjsip: Adjust outgoing offer call pref.                                     
- tcptls.c: Don't close TCP client file descriptors more than once                
- resource_endpoints.c: memory leak when providing a 404 response                 
- Logging: Add debug logging categories                                           
- pbx.c: On error, ast_add_extension2_lockopt should always free 'data'           
- app_confbridge/bridge_softmix:  Add ability to force estimated bitrate          
- app_voicemail.c: Document VMSayName interruption behavior                       
- res_pjsip_sdp_rtp: Fix accidentally native bridging calls                       
- res_musiconhold: Load all realtime entries, not just the first                  
- channels: Don't dereference NULL pointer                                        
- res_pjsip_diversion: fix double 181                                             
- res_musiconhold: Clarify that playlist mode only supports HTTP(S) URLs          
- dsp.c: Update calls to ast_format_cmp to check result properly                  
- res_pjsip_session: Fix stream name memory leak.                                 
- func_curl.c: Prevent crash when using CURLOPT(httpheader)                       
- res_musiconhold: Start playlist after initial announcement                      
- res_pjsip_session: Fix session reference leak.                                  
- res_stasis.c: Add compare function for bridges moh container                    
- logger.h: Fix ast_trace to respect scope_level                                  
- chan_sip.c: Don't build by default                                              
- audiosocket: Fix module menuselect descriptions                                 
- bridge_softmix/sfu_topologies_on_join: Ignore topology change failures          
- res_pjsip_session.c: Fix build when TEST_FRAMEWORK is not defined               
- res_pjsip_diversion: implement support for History-Info                         
- format_cap: Perform codec lookups by pointer instead of name                    
- res_pjsip_session: Fix issue with COLP and 491                                  
- debugging:  Add enough to choke a mule                                          
- res_pjsip_session:  Handle multi-stream re-invites better                       
- realtime: Increased reg_server character size                                   
- res_stasis.c: Added video_single option for bridge creation                     
- Bridging: Use a ref to bridge_channel's channel to prevent crash.               
- res_pjsip_session: Deferred re-INVITE without SDP send a=sendrecv instead of a..
- conversions: Add string to signed integer conversion functions                  
- app_queue: Fix leave-empty not recording a call as abandoned                    
- ast_coredumper: Fix issues with naming                                          
- parking: Copy parker UUID as well.                                              
- sip_nat_settings: Update script for latest Linux.                               
- samples: Fix keep_alive_interval default in pjsip.conf.                         
- chan_pjsip: disallow PJSIP_SEND_SESSION_REFRESH pre-answer execution            
- pbx: Fix hints deadlock between reload and ExtensionState.                      
- logger.c: Added a new log formatter called "plain"                              
- res_speech: Bump reference on format object                                     
- res_pjsip_diversion: handle 181                                                 
- app_voicemail: Process urgent messages with mailcmd                             
- app_queue: Member lastpause time reseting                                       
- res_pjsip_session: Don't aggressively terminate on failed re-INVITE.            
- bridge_channel: Ensure text messages are zero terminated                        
- res_musiconhold.c: Use ast_file_read_dir to scan MoH directory                  
- scope_trace: Added debug messages and added additional macros                   
- stream.c:  Added 2 more debugging utils and added pos to stream string          
- chan_sip: Clear ToHost property on peer when changing to dynamic host           
- ACN: Changes specific to the core                                               
- Makefile: Fix certified version numbers                                         
- res_musiconhold.c: Prevent crash with realtime MoH                              
- res_pjsip: Fix codec preference defaults.                                       
- vector.h: Fix implementation of AST_VECTOR_COMPACT() for empty vectors          
- pjproject: clone sdp to protect against (nat) modifications                     
- utils.c: NULL terminate ast_base64decode_string.                                
- ACN: Configuration renaming for pjsip endpoint                                  
- res_stir_shaken: Fix memory allocation error in curl.c                          
- res_pjsip_session: Ensure reused streams have correct bundle group              
- res_pjsip_registrar: Don't specify an expiration for static contacts.           
- utf8.c: Add UTF-8 validation and utility functions                              
- stasis_bridge.c: Fixed wrong video_mode shown                                   
- vector.h: Add AST_VECTOR_SORT()                                                 
- CI: Force publishAsteriskDocs to use python2                                    
- websocket / pjsip: Increase maximum packet size.                                
- Prepare master for the next Asterisk version                                    
- acl.c: Coerce a NULL pointer into the empty string                              
- pjsip: Include timer patch to prevent cancelling timer 0.                       

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

- ### chan_dahdi: Allow MWI to be manually toggled on channels.                       
  The 'dahdi set mwi' now allows MWI on channels
  to be manually toggled if needed for troubleshooting.
  Resolves: #440

- ### app_dial: Add option "j" to preserve initial stream topology of caller          
  The option "j" is now available for the Dial application which
  uses the initial stream topology of the caller to create the outgoing
  channels.

- ### logger: Add channel-based filtering.                                            
  The console log can now be filtered by
  channels or groups of channels, using the
  logger filter CLI commands.

- ### chan_pjsip: Add PJSIPHangup dialplan app and manager action                     
  A new dialplan app PJSIPHangup and AMI action allows you
  to hang up an unanswered incoming PJSIP call with a specific SIP
  response code in the 400 -> 699 range.

- ### app_voicemail: Add AMI event for mailbox PIN changes.                           
  The VoicemailPasswordChange event is
  now emitted whenever a mailbox password is updated,
  containing the mailbox information and the new
  password.
  Resolves: #398

- ### res_speech: allow speech to translate input channel                             
  res_speech now supports translation of an input channel
  to a format supported by the speech provider, provided a translation
  path is available between the source format and provider capabilites.

- ### res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 cha..
  With this update, the PJSIP realm lengths have been extended
  to support up to 255 characters.

- ### res_stasis: signal when new command is queued                                   
  Call setup times should be significantly improved
  when using ARI.

- ### lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS                            
  You no longer need to select DEBUG_THREADS to use
  DETECT_DEADLOCKS.  This removes a significant amount of overhead
  if you just want to detect possible deadlocks vs needing full
  lock tracing.

- ### file.c: Add ability to search custom dir for sounds                             
  A new option "sounds_search_custom_dir" has been added to
  asterisk.conf that allows asterisk to search
  AST_DATA_DIR/sounds/custom for sounds files before searching the
  standard AST_DATA_DIR/sounds/<lang> directory.

- ### make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h               
  The "Build Options" entry in the "core show settings"
  CLI command has been renamed to "ABI related Build Options" and
  a new entry named "All Build Options" has been added that shows
  both breaking and non-breaking options.

- ### chan_rtp: Implement RTP glue for UnicastRTP channels                            
  The dial string option 'g' was added to the UnicastRTP channel
  which enables RTP glue and therefore native RTP bridges with those
  channels.

- ### app_queue: periodic announcement configurable start time.                       
  Introduce a new queue configuration option called
  'periodic-announce-startdelay' which will vary the normal (historic)
  behavior of starting the periodic announcement cycle at
  periodic-announce-frequency seconds after entering the queue to start
  the periodic announcement cycle at period-announce-startdelay seconds
  after joining the queue.  The default behavior if this config option is
  not set remains unchanged.
  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### variables: Add additional variable dialplan functions.                          
  Four new dialplan functions have been added.
  GLOBAL_DELETE and DELETE have been added which allows
  the deletion of global and channel variables.
  GLOBAL_EXISTS and VARIABLE_EXISTS have been added
  which checks whether a global or channel variable has
  been set.

- ### sig_analog: Add Called Subscriber Held capability.                              
  Called Subscriber Held is now supported for analog
  FXS channels, using the calledsubscriberheld option. This allows
  a station  user to go on hook when receiving an incoming call
  and resume from another phone on the same line by going on hook,
  without disconnecting the call.

- ### res_pjsip_header_funcs: Make prefix argument optional.                          
  The prefix argument to PJSIP_HEADERS is now
  optional. If not specified, all header names will be
  returned.

- ### core/ari/pjsip: Add refer mechanism                                             
  There is a new ARI endpoint `/endpoints/refer` for referring
  an endpoint to some URI or endpoint.

- ### chan_dahdi: Allow autoreoriginating after hangup.                               
  The autoreoriginate setting now allows for kewlstart FXS
  channels to automatically reoriginate and provide dial tone to the
  user again after all calls on the line have cleared. This saves users
  from having to manually hang up and pick up the receiver again before
  making another call.

- ### sig_analog: Allow three-way flash to time out to silence.                       
  The threewaysilenthold option now allows the three-way
  dial tone to time out to silence, rather than continuing forever.

- ### res_pjsip: Enable TLS v1.3 if present.                                          
  res_pjsip now allows TLS v1.3 to be enabled if supported by
  the underlying PJSIP library. The bundled version of PJSIP supports
  TLS v1.3.

- ### app_queue: Add support for applying caller priority change immediately.         
  The 'queue priority caller' CLI command and
  'QueueChangePriorityCaller' AMI action now have an 'immediate'
  argument which allows the caller priority change to be reflected
  immediately, causing the position of a caller to move within the
  queue depending on the priorities of the other callers.

- ### Adds manager actions to allow move/remove/forward individual messages in a par..
  The following manager actions have been added
  VoicemailBoxSummary - Generate message list for a given mailbox
  VoicemailRemove - Remove a message from a mailbox folder
  VoicemailMove - Move a message from one folder to another within a mailbox
  VoicemailForward - Copy a message from one folder in one mailbox
  to another folder in another or the same mailbox.

- ### app_voicemail: add CLI commands for message manipulation                        
  The following CLI commands have been added to app_voicemail
  voicemail show mailbox <mailbox> <context>
  Show contents of mailbox <mailbox>@<context>
  voicemail remove <mailbox> <context> <from_folder> <messageid>
  Remove message <messageid> from <from_folder> in mailbox <mailbox>@<context>
  voicemail move <mailbox> <context> <from_folder> <messageid> <to_folder>
  Move message <messageid> in mailbox <mailbox>&<context> from <from_folder> to <to_folder>
  voicemail forward <from_mailbox> <from_context> <from_folder> <messageid> <to_mailbox> <to_context> <to_folder>
  Forward message <messageid> in mailbox <mailbox>@<context> <from_folder> to
  mailbox <mailbox>@<context> <to_folder>

- ### sig_analog: Allow immediate fake ring to be suppressed.                         
  The immediatering option can now be set to no to suppress
  the fake audible ringback provided when immediate=yes on FXS channels.

- ### AMI: Add parking position parameter to Park action                              
  New ParkingSpace parameter has been added to AMI action Park.

- ### res_musiconhold: Add option to loop last file.                                  
  The loop_last option in musiconhold.conf now
  allows the last file in the directory to be looped once reached.

- ### AMI: Add CoreShowChannelMap action.                                             
  New AMI action CoreShowChannelMap has been added.

- ### sig_analog: Add fuller Caller ID support.                                       
  Additional Caller ID properties are now supported on
  incoming calls to FXS stations, namely the
  redirecting reason and call qualifier.

- ### res_stasis.c: Add new type 'sdp_label' for bridge creation.                     
  When creating a bridge using the ARI the 'type' argument now
  accepts a new value 'sdp_label' which will configure the bridge to add
  labels for each stream in the SDP with the corresponding channel id.

- ### app_queue: Preserve reason for realtime queues                                  
  Make paused reason in realtime queues persist an
  Asterisk restart. This was fixed for non-realtime
  queues in ASTERISK_25732.

- ### cel: add local optimization begin event                                         
  The new AST_CEL_LOCAL_OPTIMIZE_BEGIN can be used
  by itself or in conert with the existing
  AST_CEL_LOCAL_OPTIMIZE to book-end local channel optimizaion.

- ### chan_dahdi: Add dialmode option for FXS lines.                                  
  A "dialmode" option has been added which allows
  specifying, on a per-channel basis, what methods of
  subscriber dialing (pulse and/or tone) are permitted.
  Additionally, this can be changed on a channel
  at any point during a call using the CHANNEL
  function.


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

- ### app.c: Allow ampersands in playback lists to be escaped.                        
  Ampersands in URLs passed to the `Playback()`,
  `Background()`, `SpeechBackground()`, `Read()`, `Authenticate()`, or
  `Queue()` applications as filename arguments can now be escaped by
  single quoting the filename. Additionally, this is also possible when
  using the `CONFBRIDGE` dialplan function, or configuring various
  features in `confbridge.conf` and `queues.conf`.

- ### pjsip_configuration.c: Disable DTLS renegotiation if WebRTC is enabled.         
  The dtls_rekey will be disabled if webrtc support is
  requested on an endpoint. A warning will also be emitted.

- ### res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 cha..
  As part of this update, the maximum allowable length
  for PJSIP endpoints and relevant resources has been increased from
  40 to 255 characters. To take advantage of this enhancement, it is
  recommended to run the necessary procedures (e.g., Alembic) to
  update your schemas.

- ### app_queue: Preserve reason for realtime queues                                  
  Add a new column to the queue_member table:
  reason_paused VARCHAR(80) so the reason can be preserved.

- ### cel: add local optimization begin event                                         
  The existing AST_CEL_LOCAL_OPTIMIZE can continue
  to be used as-is and the AST_CEL_LOCAL_OPTIMIZE_BEGIN event
  can be ignored if desired.


Closed Issues:
----------------------------------------

  - #35: [New Feature]: chan_dahdi: Allow disabling pulse or tone dialing
  - #37: [Bug]: contrib/scripts/install_prereq tries to install armhf packages on aarch64 Debian platforms
  - #39: [Bug]: Remove .gitreview from repository.
  - #43: [Bug]: Link to trademark policy is no longer correct
  - #45: [bug]: Non-bundled PJSIP check for evsub pending NOTIFY check is insufficient/ineffective
  - #46: [bug]: Stir/Shaken: Wrong CID used when looking up certificates
  - #48: [bug]: res_pjsip: Mediasec requires different headers on 401 response
  - #52: [improvement]: Add local optimization begin cel event
  - #55: [bug]: res_sorcery_memory_cache: Memory leak when calling sorcery_memory_cache_open
  - #64: [bug]: app_voicemail_imap wrong behavior when losing IMAP connection
  - #65: [bug]: heap overflow by default at startup
  - #66: [improvement]: Fix preserve reason of pause when Asterisk is restared for realtime queues
  - #71: [new-feature]: core/ari/pjsip: Add refer mechanism to refer endpoints to some resource
  - #73: [new-feature]: pjsip: Allow topology/session refreshes in early media state
  - #84: [bug]: codec_ilbc:  Fails to build with ilbc version 3.0.4
  - #87: [bug]: app_followme: Setting enable_callee_prompt=no breaks timeout
  - #89: [improvement]:  indications: logging changes
  - #91: [improvement]: Add parameter on ARI bridge create to allow it to send SDP labels
  - #94: [new-feature]: sig_analog: Add full Caller ID support for incoming calls
  - #96: [bug]: make install-logrotate causes logrotate to fail on service restart
  - #98: [new-feature]: callerid: Allow timezone to be specified at runtime
  - #100: [bug]: sig_analog: hidecallerid setting is broken
  - #102: [bug]: Strange warning - 'T' option is not compatible with remote console mode and has no effect.
  - #104: [improvement]: Add AMI action to get a list of connected channels
  - #108: [new-feature]: fair handling of calls in multi-queue scenarios
  - #110: [improvement]: utils - add lock timing information with DEBUG_THREADS
  - #116: [bug]: SIP Reason: "Call completed elsewhere" no longer propagating
  - #118: [new-feature]: chan_dahdi: Allow fake ringing to be inhibited when immediate=yes
  - #120: [bug]: chan_dahdi: Fix broken presentation for FXO caller ID
  - #122: [new-feature]: res_musiconhold: Add looplast option
  - #129: [bug]: res_speech_aeap: Crash due to NULL format on setup
  - #133: [bug]: unlock channel after moh state access
  - #136: [bug]: Makefile downloader does not follow redirects.
  - #145: [bug]: ABI issue with pjproject and pjsip_inv_session
  - #155: [bug]: GCC 13 is catching a few new trivial issues
  - #158: [bug]: test_stasis_endpoints.c: Unit test channel_messages is unstable
  - #170: [improvement]: app_voicemail - add CLI commands to manipulate messages
  - #174: [bug]: app_voicemail imap compile errors
  - #179: [bug]: Queue strategy “Linear” with Asterisk 20 on Realtime
  - #181: [improvement]: app_voicemail - add manager actions to display and manipulate messages
  - #193: [bug]: third-party/apply-patches doesn't sort the patch file list before applying
  - #200: [bug]: Regression: In app.h an enum is used before its declaration.
  - #202: [improvement]: app_queue: Add support for immediately applying queue caller priority change
  - #205: [new-feature]: sig_analog: Allow flash to time out to silent hold
  - #224: [new-feature]: chan_dahdi: Allow automatic reorigination on hangup
  - #226: [improvement]: Apply contact_user to incoming calls
  - #230: [bug]: PJSIP_RESPONSE_HEADERS function documentation is misleading
  - #233: [bug]: Deadlock with MixMonitorMute AMI action
  - #240: [new-feature]: sig_analog: Add Called Subscriber Held capability
  - #242: [new-feature]: logger: Allow filtering logs in CLI by channel
  - #248: [bug]: core_local: Local channels cannot have slashes in the destination
  - #253: app_gosub patch appear to have broken predial handlers that utilize macros that call gosubs
  - #255: [bug]: pjsip_endpt_register_module: Assertion "Too many modules registered"
  - #260: [bug]: maxptime must be changed to multiples of 20
  - #263: [bug]: download_externals doesn't always handle versions correctly
  - #265: [bug]: app_macro isn't locking around channel datastore access
  - #267: [bug]: ari: refer with display_name key in request body leads to crash
  - #274: [bug]: Syntax Error in SQL Code
  - #275: [bug]:Asterisk make now requires ASTCFLAGS='-std=gnu99 -Wdeclaration-after-statement'
  - #277: [bug]: pbx.c: Compiler error with gcc 12.2
  - #281: [bug]: app_dial: Infinite loop if called channel hangs up while receiving digits
  - #286: [improvement]: chan_iax2: Improve authentication debugging
  - #289: [new-feature]: Add support for deleting channel and global variables
  - #294: [improvement]: chan_dahdi: Improve call pickup documentation
  - #298: [improvement]: chan_rtp: Implement RTP glue
  - #301: [bug]: Number of ICE TURN threads continually growing
  - #303: [bug]: SpeechBackground never exits
  - #308: [bug]: chan_console: Deadlock when hanging up console channels
  - #315: [improvement]: Search /var/lib/asterisk/sounds/custom for sound files before  /var/lib/asterisk/sounds/<lang>
  - #316: [bug]: Privilege Escalation in Astrisk's Group permissions.
  - #319: [bug]: func_periodic_hook truncates long channel names when setting EncodedChannel
  - #321: [bug]: Performance suffers unnecessarily when debugging deadlocks
  - #325: [bug]: hangup after beep to avoid waiting for timeout
  - #330: [improvement]: Add cel user event helper function
  - #337: [bug]: asterisk.c: The CLI history file is written to the wrong directory in some cases
  - #341: [bug]: app_if.c : nested EndIf incorrectly exits parent If
  - #345: [improvement]: Increase pj_sip Realm Size to 255 Characters for Improved Functionality
  - #349: [improvement]: Add libjwt to third-party
  - #351: [improvement]: Refactor res_stir_shaken to use libjwt
  - #352: [bug]: Update qualify_timeout documentation to include DNS note
  - #354: [improvement]: app_voicemail: Disable ADSI if unavailable on a line
  - #356: [new-feature]: app_directory: Add ADSI support.
  - #360: [improvement]: Update documentation for CHANGES/UPGRADE files
  - #362: [improvement]: Speed up ARI command processing
  - #379: [bug]: Orphaned taskprocessors cause shutdown delays
  - #384: [bug]: Unnecessary re-INVITE after answer
  - #388: [bug]: Crash in app_followme.c due to not acquiring a reference to nativeformats
  - #396: [improvement]: res_pjsip: Specify max ciphers allowed if too many provided
  - #398: [new-feature]: app_voicemail: Add AMI event for password change
  - #406: [improvement]: pjsip: Upgrade bundled version to pjproject 2.14
  - #409: [improvement]: chan_dahdi: Emit warning if specifying nonexistent cadence
  - #423: [improvement]: func_lock: Add missing see-also refs
  - #425: [improvement]: configs: Improve documentation for bandwidth in iax.conf.sample
  - #428: [bug]: cli: Output is truncated from "config show help"
  - #430: [bug]: Fix broken links
  - #440: [new-feature]: chan_dahdi: Allow manually toggling MWI on channels
  - #442: [bug]: func_channel: Some channel options are not settable
  - #445: [bug]: ast_coredumper isn't figuring out file locations properly in all cases
  - #458: [bug]: Memory leak in chan_dahdi when mwimonitor=yes on FXO
  - #462: [new-feature]: app_dial: Add new option to preserve initial stream topology of caller
  - #465: [improvement]: Change res_odbc connection pool request logic to not lock around blocking operations
  - #482: [improvement]: manager.c: Improve clarity of "manager show connected" output
  - #492: [improvement]: res_calendar_icalendar: Print icalendar error if available on parsing failure
  - #500: [bug regression]: res_rtp_asterisk doesn't build if pjproject isn't used
  - #503: [bug]: The res_rtp_asterisk DTLS check against ICE candidates fails when it shouldn't
  - #505: [bug]: res_pjproject: ast_sockaddr_cmp() always fails on sockaddrs created by ast_sockaddr_from_pj_sockaddr()
  - #509: [bug]: res_pjsip: Crash when looking up transport state in use
  - #513: [bug]: manager.c: Crash due to regression using wrong free function when built with MALLOC_DEBUG
  - #520: [improvement]: menuselect: Use more specific error message.
  - #527: [bug]: app_voicemail_odbc no longer working after removal of macrocontext.
  - #529: [bug]: MulticastRTP without selected codec leeds to "FRACK!, Failed assertion bad magic number 0x0 for object" after ~30 calls
  - #530: [bug]: bridge_channel.c: Stream topology change amplification with multiple layers of Local channels
  - #533: [improvement]: channel.c, func_frame_trace.c: Improve debuggability of channel frame queue
  - #539: [bug]: Existence of logger.xml causes linking failure
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

 An additional 751 ASTERISK-* issues were closed.

Commits By Author:
----------------------------------------

- ### Alexander Greiner-Baer (1):
  - res_pjsip: set Accept-Encoding to identity in OPTIONS response

- ### Alexander Traud (67):
  - samples: Fix keep_alive_interval default in pjsip.conf.
  - sip_nat_settings: Update script for latest Linux.
  - BuildSystem: Enable Lua 5.4.
  - install_prereq: Add GMime 3.0.
  - chan_sip: On authentication, pick MD5 for sure.
  - Compiler fixes for GCC with -Os
  - Compiler fixes for GCC when printf %s is NULL
  - Compiler fixes for GCC with -Og
  - res_stir_shaken: Include OpenSSL headers where used actually.
  - res_pjsip/config_transport: Load and run without OpenSSL.
  - modules.conf: Align the comments for more conclusiveness.
  - chan_sip: Remove unused sip_socket->port.
  - loader: Sync load- and build-time deps.
  - codecs: Remove test-law.
  - chan_sip: SDP: Sidestep stream parsing when its media is disabled.
  - res_pjsip_session: Avoid sometimes-uninitialized warning with Clang.
  - pjsip_scheduler: Fix pjsip show scheduled_tasks like for compiler Clang.
  - chan_sip: SDP: Reject audio streams correctly.
  - channel: Set up calls without audio (text+video), again.
  - chan_sip: Set up calls without audio (text+video), again.
  - chan_sip: Allow [peer] without audio (text+video).
  - rtp:  Enable srtp replay protection
  - chan_sip: Filter pass-through audio/video formats away, again.
  - res_format_attr_h263: Generate valid SDP fmtp for H.263+.
  - chan_iax2: System Header strings is included via asterisk.h/compat.h.
  - res_format_attr_*: Parameter Names are Case-Insensitive.
  - aelparse: Accept an included context with timings.
  - BuildSystem: Remove two dead exceptions for compiler Clang.
  - dialplan: Add one static and fix two whitespace errors.
  - res_rtp_asterisk: sqrt(.) requires the header math.h.
  - stasis: Avoid 'dispatched' as unused variable in normal mode.
  - res_config_sqlite: Remove deprecated module.
  - res_snmp: As build tool, prefer pkg-config over net-snmp-config.
  - BuildSystem: In POSIX sh, == in place of = is undefined.
  - progdocs: Avoid 'name' with Doxygen \file.
  - bridge_channel: Fix for Doxygen.
  - progdocs: Use Doxygen \example correctly.
  - progdocs: Avoid multiple use of section labels.
  - tests: Fix for Doxygen.
  - apps: Fix for Doxygen.
  - addons: Fix for Doxygen.
  - bridges: Fix for Doxygen.
  - res_pjsip: Fix for Doxygen.
  - chan_iax2: Fix for Doxygen.
  - channel: Fix for Doxygen.
  - res_xmpp: Fix for Doxygen.
  - app: Fix for Doxygen.
  - stasis: Fix for Doxygen.
  - BuildSystem: Consistently allow 'ye' even for Jansson.
  - ari-stubs: Avoid 'is' as comparism with an literal.
  - frame: Fix for Doxygen.
  - res_ari: Fix for Doxygen.
  - parking: Fix for Doxygen.
  - odbc: Fix for Doxygen.
  - channels: Fix for Doxygen.
  - xmldoc: Fix for Doxygen.
  - progdocs: Remove outdated references in doxyref.h.
  - stir/shaken: Avoid a compiler extension of GCC.
  - progdocs: Fix grouping for latest Doxygen.
  - progdocs: Fix for Doxygen, the hidden parts.
  - main: Fix for Doxygen.
  - res: Fix for Doxygen.
  - res_pjsip_sdp_rtp: Do not warn on unknown sRTP crypto suites.
  - progdocs: Update Makefile.
  - xmldoc: Correct definition for XML element 'matchInfo'.
  - progdocs: Fix Doxygen left-overs.
  - xmldoc: Avoid whitespace around value for parameter/required.

- ### Alexandre Fournier (1):
  - res_geoloc: fix NULL pointer dereference bug

- ### Alexei Gradinari (12):
  - sched: AST_SCHED_REPLACE_UNREF can lead to use after free of data
  - res_fax: validate the remote/local Station ID for UTF-8 format
  - app_queue: load queues and members from Realtime when needed
  - res_pjsip_pubsub: provide a display name for RLS subscriptions
  - res_pjsip_pubsub: fix Batched Notifications stop working
  - res_pjsip_pubsub: update RLS to reflect the changes to the lists
  - res_pjsip_pubsub: RLS 'uri' list attribute mismatch with SUBSCRIBE request
  - res_pjsip_dialog_info_body_generator: Set LOCAL target URI as local URI
  - res_pjsip_pubsub: XML sanitized RLS display name
  - res_pjsip_pubsub: delete scheduled notification on RLS update
  - res_pjsip_pubsub: Postpone destruction of old subscriptions on RLS update
  - format_wav: replace ast_log(LOG_DEBUG, ...) by ast_debug(1, ...)

- ### Andre Barbosa (3):
  - res_stasis_playback: Send PlaybackFinish event only once for errors
  - res_stasis_playback: Check for chan hangup on play_on_channels
  - media_cache: Don't lock when curl the remote file

- ### Andrew Siplas (1):
  - logger.conf.sample: add missing comment mark

- ### Bastian Triller (2):
  - res_pjsip_session: Send Session Interval too small response
  - func_json: Fix crashes for some types

- ### Ben Ford (27):
  - res_stir_shaken: Fix memory allocation error in curl.c
  - utils.c: NULL terminate ast_base64decode_string.
  - Bridging: Use a ref to bridge_channel's channel to prevent crash.
  - AST-2020-002 - res_pjsip: Stop sending INVITEs after challenge limit.
  - chan_pjsip.c: Add parameters to frame in indicate.
  - core_unreal: Fix T.38 faxing when using local channels.
  - res_pjsip_session.c: Check topology on re-invite.
  - AST-2021-006 - res_pjsip_t38.c: Check for session_media on reinvite.
  - logging: Add .log to samples and update asterisk.logrotate.
  - logger.conf.sample: Add more debug documentation.
  - res_aeap: Add basic config skeleton and CLI commands.
  - STIR/SHAKEN: Fix certificate type and storage.
  - STIR/SHAKEN: OPENSSL_free serial hex from openssl.
  - STIR/SHAKEN: Switch to base64 URL encoding.
  - STIR/SHAKEN: Add Date header, dest->tn, and URL checking.
  - Update AMI and ARI versions for Asterisk 20.
  - STIR/SHAKEN: Option split and response codes.
  - AST-2022-001 - res_stir_shaken/curl: Limit file size and check start.
  - AST-2022-002 - res_stir_shaken/curl: Add ACL checks for Identity header.
  - res_pjsip_stir_shaken.c: Fix enabled when not configured.
  - res_pjsip: Add TEL URI support for basic calls.
  - pjproject: 2.13 security fixes
  - res_pjsip_sdp_rtp.c: Use correct timeout when put on hold.
  - AMI: Add CoreShowChannelMap action.
  - res_pjsip_session: Added new function calls to avoid ABI issues.
  - manager.c: Prevent path traversal with GetConfig.
  - Upgrade bundled pjproject to 2.14.

- ### Bernd Zobl (2):
  - res_pjsip/pjsip_message_filter: set preferred transport in pjsip_message_filter
  - res_pjsip_sdp_rtp: Evaluate remotely held for Session Progress

- ### Boris P. Korzun (10):
  - bridge_basic: Fixed setup of recall channels
  - res_musiconhold: Add support of various URL-schemes by MoH.
  - format_wav: Support of MIME-type for wav16
  - res_config_pgsql: Limit realtime_pgsql() to return one (no more) record.
  - rtp_engine: Add type field for JSON RTCP Report stasis messages
  - res_config_pgsql: Add text-type column check in require_pgsql()
  - res_pjsip_sdp_rtp: Improve detecting of lack of RTP activity
  - res_prometheus: Optional load res_pjsip_outbound_registration.so
  - pbx_lua: Remove compiler warnings
  - http.c: Fix NULL pointer dereference bug

- ### Brad Smith (4):
  - res_rtp_asterisk.c: Fix runtime issue with LibreSSL
  - main/utils: Implement ast_get_tid() for OpenBSD
  - main/utils: Simplify the FreeBSD ast_get_tid() handling
  - BuildSystem: Bump autotools versions on OpenBSD.

- ### Carlos Oliva (1):
  - app_mp3: Force output to 16 bits in mpg123

- ### Christof Efkemann (1):
  - app_sayunixtime: Use correct inflection for German time.

- ### Dan Cropp (2):
  - chan_pjsip: Incorporate channel reference count into transfer_refer().
  - chan_pjsip, app_transfer: Add TRANSFERSTATUSPROTOCOL variable

- ### Dennis Buteyn (1):
  - chan_sip: Clear ToHost property on peer when changing to dynamic host

- ### Dovid Bender (1):
  - func_curl.c: Allow user to set what return codes constitute a failure.

- ### Dustin Marquess (1):
  - res_fax_spandsp: Add spandsp 3.0.0+ compatibility

- ### Eduardo (1):
  - codec_builtin: Use multiples of 20 for maximum_ms

- ### Evandro César Arruda (1):
  - app_queue: Member lastpause time reseting

- ### Evgenios_Greek (1):
  - stasis: Fix "FRACK!, Failed assertion bad magic number" when unsubscribing

- ### Fabrice Fontaine (2):
  - main/iostream.c: fix build with libressl
  - configure: fix detection of re-entrant resolver functions

- ### Florentin Mayer (1):
  - res_pjsip_sdp_rtp: Preserve order of RTP codecs

- ### Frederic LE FOLL (1):
  - Dialing API: Cancel a running async thread, may not cancel all calls

- ### Frederic Van Espen (1):
  - ast_coredumper: Fix deleting results when output dir is set

- ### George Joseph (128):
  - Prepare master for the next Asterisk version
  - CI: Force publishAsteriskDocs to use python2
  - res_pjsip_session: Ensure reused streams have correct bundle group
  - ACN: Configuration renaming for pjsip endpoint
  - ACN: Changes specific to the core
  - stream.c:  Added 2 more debugging utils and added pos to stream string
  - scope_trace: Added debug messages and added additional macros
  - logger.c: Added a new log formatter called "plain"
  - ast_coredumper: Fix issues with naming
  - res_pjsip_session:  Handle multi-stream re-invites better
  - debugging:  Add enough to choke a mule
  - res_pjsip_session: Fix issue with COLP and 491
  - bridge_softmix/sfu_topologies_on_join: Ignore topology change failures
  - logger.h: Fix ast_trace to respect scope_level
  - app_confbridge/bridge_softmix:  Add ability to force estimated bitrate
  - pjsip_scheduler.c: Add type ONESHOT and enhance cli show command
  - res_pjsip_outbound_registration.c:  Use our own scheduler and other stuff
  - app_queue: Fix deadlock between update and show queues
  - logger.c: Automatically add a newline to formats that don't have one
  - Revert "res_pjsip_outbound_registration.c:  Use our own scheduler and other st..
  - chan_iax2.c: Require secret and auth method if encryption is enabled
  - res_pjsip_refer: Always serialize calls to refer_progress_notify
  - res_pjsip_refer: Refactor progress locking and serialization
  - res_pjsip_refer: Move the progress dlg release to a serializer
  - res_pjsip_session: Make reschedule_reinvite check for NULL topologies
  - res_prometheus: Clone containers before iterating
  - bridge_channel_write_frame: Check for NULL channel
  - res_pjsip:  Update documentation for the auth object
  - res_pjsip_outbound_authenticator_digest: Be tolerant of RFC8760 UASs
  - res_pjsip_messaging: Refactor outgoing URI processing
  - res_pjsip_messaging: Overwrite user in existing contact URI
  - jitterbuffer:  Correct signed/unsigned mismatch causing assert
  - res_pjproject: Allow mapping to Asterisk TRACE level
  - bridge_softmix: Suppress error on topology change failure
  - res_snmp: Add -fPIC to _ASTCFLAGS
  - pjproject: Add patch to fix trailing whitespace issue in rtpmap
  - BuildSystem: Check for alternate openssl packages
  - ast_coredumper:  Refactor to better find things
  - CI: Rename 'master' node to 'built-in'
  - bundled_pjproject:  Add more support for multipart bodies
  - bundled_pjproject:  Make it easier to hack
  - bundled_pjproject: Create generic pjsip_hdr_find functions
  - res_pjsip: Add utils for checking media types
  - build: Fix issues building pjproject
  - res_pjsip: Make message_filter and session multipart aware
  - bundled_pjproject: Fix srtp detection
  - res_pjsip_outbound_authenticator_digest: Prevent ABRT on cleanup
  - build: Add "basebranch" to .gitreview
  - bundled_pjproject:  Add additional multipart search utils
  - build: Refactor the earlier "basebranch" commit
  - Makefile: Allow XML documentation to exist outside source files
  - core: Config and XML tweaks needed for geolocation
  - xmldoc: Fix issue with xmlstarlet validation
  - xml.c, config,c:  Add stylesheets and variable list string parsing
  - make_xml_documentation: Remove usage of get_sourceable_makeopts
  - Makefile:  Disable XML doc validation
  - GCC12: Fixes for 18+.  state_id_by_topic comparing wrong value
  - GCC12: Fixes for 16+
  - Geolocation: Base Asterisk Prereqs
  - Geolocation:  Core Capability Preview
  - Geolocation:  chan_pjsip Capability Preview
  - geoloc_eprofile.c: Fix setting of loc_src in set_loc_src()
  - Update defaultbranch to 20
  - Geolocation: Wiki Documentation
  - res_geolocation: Address user issues, remove complexity, plug leaks
  - res_geolocation:  Add built-in profiles
  - res_geolocation: Add profile parameter suppress_empty_ca_elements
  - res_geolocation:  Allow location parameters on the profile object
  - res_geolocation: Add two new options to GEOLOC_PROFILE
  - res_geolocation: Fix segfault when there's an empty element
  - res_geolocation: Fix issues exposed by compiling with -O2
  - res_crypto: Memory issues and uninitialized variable errors
  - res_geolocation: Update wiki documentation
  - chan_rtp: Make usage of ast_rtp_instance_get_local_address clearer
  - runUnittests.sh:  Save coredumps to proper directory
  - pjsip_transport_events: Fix possible use after free on transport
  - res_rtp_asterisk: Asterisk Media Experience Score (MES)
  - res_pjsip_transport_websocket: Add remote port to transport
  - Revert "res_rtp_asterisk: Asterisk Media Experience Score (MES)"
  - res_rtp_asterisk: Asterisk Media Experience Score (MES)
  - res_rtp_asterisk: Don't use double math to generate timestamps
  - res_pjsip: Replace invalid UTF-8 sequences in callerid name
  - make_version: Strip svn stuff and suppress ref HEAD errors
  - test.c: Fix counting of tests and add 2 new tests
  - Initial GitHub Issue Templates
  - Initial GitHub PRs
  - Set up new ChangeLogs directory
  - apply_patches: Sort patch list before applying
  - build: Fix a few gcc 13 issues
  - test_stasis_endpoints.c: Make channel_messages more stable
  - test_statis_endpoints:  Fix channel_messages test again
  - rest-api: Updates for new documentation site
  - doc: Remove obsolete CHANGES-staging and UPGRADE-staging
  - app.h: Move declaration of ast_getdata_result before its first use
  - pjproject_bundled: Increase PJSIP_MAX_MODULE to 38
  - rest-api: Run make ari-stubs
  - download_externals:  Fix a few version related issues
  - alembic: Fix quoting of the 100rel column
  - ari-stubs: Fix broken documentation anchors
  - ari-stubs: Fix more local anchor references
  - ari-stubs: Fix more local anchor references
  - res_rtp_asterisk.c: Check DTLS packets against ICE candidate list
  - res_rtp_asterisk: Fix regression issues with DTLS client check
  - Restore CHANGES and UPGRADE.txt to allow cherry-picks to work
  - safe_asterisk: Change directory permissions to 755
  - func_periodic_hook: Don't truncate channel name
  - make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h
  - file.c: Add ability to search custom dir for sounds
  - asterisk.c: Use the euid's home directory to read/write cli history
  - lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS
  - Add libjwt to third-party
  - logger.h: Add ability to change the prefix on SCOPE_TRACE output
  - res_pjsip_exten_state,res_pjsip_mwi: Allow unload on shutdown
  - api.wiki.mustache: Fix indentation in generated markdown
  - bridge_simple: Suppress unchanged topology change requests
  - chan_pjsip: Add PJSIPHangup dialplan app and manager action
  - codec_ilbc: Disable system ilbc if version >= 3.0.0
  - SECURITY.md: Update with correct documentation URL
  - ast_coredumper: Increase reliability
  - MergeApproved.yml:  Remove unneeded concurrency
  - Revert "core & res_pjsip: Improve topology change handling."
  - Reduce startup/shutdown verbose logging
  - pjsip show channelstats: Prevent possible segfault when faxing
  - Stir/Shaken Refactor
  - Makefile: Add stir_shaken/cache to directories created on install
  - attestation_config.c: Use ast_free instead of ast_std_free
  - res_pjsip_stir_shaken.c:  Add checks for missing parameters
  - Initial commit for certified-20.7

- ### Gitea (1):
  - res_pjsip_header_funcs: Duplicate new header value, don't copy.

- ### Guido Falsi (1):
  - res_rtp_asterisk.c: Fix build failure when not building with pjproject.

- ### Henning Westerholt (3):
  - res_pjsip: return all codecs on a re-INVITE without SDP
  - chan_pjsip: fix music on hold continues after INVITE with replaces
  - chan_pjsip: also return all codecs on empty re-INVITE for late offers

- ### Holger Hans Peter Freyther (9):
  - res_pjsip_sdp_rtp: Fix accidentally native bridging calls
  - pjsip: Generate progress (once) when receiving a 180 with a SDP
  - res_prometheus: Do not crash on invisible bridges
  - res_http_media_cache: Do not crash when there is no extension
  - res_http_media_cache: Introduce options and customize
  - res_prometheus: Do not generate broken metrics
  - ari/stasis: Indicate progress before playback on a bridge
  - ari: Provide the caller ID RDNIS for the channels
  - stasis: Update the snapshot after setting the redirect

- ### Hugh McMaster (1):
  - configure.ac: Use pkg-config to detect libxml2

- ### Igor Goncharovsky (5):
  - res_ari: Fix audiosocket segfault
  - res_pjsip_header_funcs: Add PJSIP_HEADERS() ability to read header by pattern
  - res_pjsip_outbound_registration: Allow to use multiple proxies for registration
  - res_pjsip: Fix path usage in case dialing with '@'
  - res_pjsip_rfc3326: Add SIP causes support for RFC3326

- ### Ivan Poddubnyi (5):
  - chan_pjsip: Stop queueing control frames twice on outgoing channels
  - chan_pjsip: Assign SIPDOMAIN after creating a channel
  - main/frame: Add missing control frame names to ast_frame_subclass2str
  - res_pjsip_diversion: Fix adding more than one histinfo to Supported
  - app_queue: Fix conversion of complex extension states into device states

- ### Jaco Kroon (22):
  - pbx_lua:  Add LUA_VERSIONS environment variable to ./configure.
  - func_lock: fix multiple-channel-grant problems.
  - contrib/systemd: Added note on common issues with systemd and asterisk
  - AC_HEADER_STDC causes a compile failure with autoconf 2.70
  - func_odbc:  Introduce minargs config and expose ARGC in addition to ARGn.
  - app.h: Restore C++ compatibility for macro AST_DECLARE_APP_ARGS
  - res_odbc_transaction: correctly initialise forcecommit value from DSN.
  - app.h: Fix -Werror=zero-length-bounds compile errors in dev mode.
  - func_callerid+res_agi: Fix compile errors related to -Werror=zero-length-bounds
  - menuselect: exit non-zero in case of failure on --enable|disable options.
  - func_lock: Fix requesters counter in error paths.
  - func_lock: Fix memory corruption during unload.
  - func_lock: Prevent module unloading in-use module.
  - func_lock: Add "dialplan locks show" cli command.
  - logger: use __FUNCTION__ instead of __PRETTY_FUNCTION__
  - manager: be more aggressive about purging http sessions.
  - Build system: Avoid executable stack.
  - app_queue: periodic announcement configurable start time.
  - res_calendar: output busy state as part of show calendar.
  - configure: fix test code to match gethostbyname_r prototype.
  - tcptls: when disabling a server port, we should set the accept_fd to -1.
  - app_queue: periodic announcement configurable start time.

- ### Jason D. McCormick (1):
  - install_prereq: Fix dependency install on aarch64.

- ### Jasper Hafkenscheid (1):
  - res_srtp: Disable parsing of not enabled cryptos

- ### Jasper van der Neut (1):
  - channels: Don't dereference NULL pointer

- ### Jean Aunis (4):
  - resource_endpoints.c: memory leak when providing a 404 response
  - Stasis/messaging: tech subscriptions conflict with endpoint subscriptions.
  - translate.c: Take sampling rate into account when checking codec's buffer size
  - res_rtp_asterisk: fix memory leak

- ### Jeremy Lainé (1):
  - res_rtp_asterisk: make it possible to remove SOFTWARE attribute

- ### Jiajian Zhou (1):
  - AMI: Add parking position parameter to Park action

- ### Joe Searle (1):
  - res_stasis.c: Add new type 'sdp_label' for bridge creation.

- ### Jose Lopes (1):
  - res_pjsip_header_funcs: Add functions PJSIP_RESPONSE_HEADER and PJSIP_RESPONSE..

- ### Joseph Nadiv (3):
  - res_pjsip_dialog_info_body_generator: Add LOCAL/REMOTE tags in dialog-info+xml
  - res_pjsip.c: Support endpoints with domain info in username
  - res_pjsip_registrar: Remove unavailable contacts if exceeds max_contacts

- ### Josh Soref (25):
  - agi: Spelling fixes
  - rest-api-templates: Spelling fixes
  - res: Spelling fixes
  - Makefile: Spelling fixes
  - utils: Spelling fixes
  - main: Spelling fixes
  - pbx: Spelling fixes
  - funcs: Spelling fixes
  - CHANGES: Spelling fixes
  - tests: Spelling fixes
  - channels: Spelling fixes
  - apps: Spelling fixes
  - bridges: Spelling fixes
  - UPGRADE.txt: Spelling fixes
  - include: Spelling fixes
  - menuselect: Spelling fixes
  - doc: Spelling fixes
  - configs: Spelling fixes
  - addons: Spelling fixes
  - CREDITS: Spelling fixes
  - formats: Spelling fixes
  - codecs: Spelling fixes
  - contrib: Spelling fixes
  - build_tools: Spelling fixes
  - test_time.c: Tolerate DST transitions

- ### Joshua C. Colp (85):
  - pjsip: Include timer patch to prevent cancelling timer 0.
  - websocket / pjsip: Increase maximum packet size.
  - res_pjsip_registrar: Don't specify an expiration for static contacts.
  - res_pjsip: Fix codec preference defaults.
  - res_pjsip_session: Don't aggressively terminate on failed re-INVITE.
  - pbx: Fix hints deadlock between reload and ExtensionState.
  - parking: Copy parker UUID as well.
  - res_pjsip_session: Fix session reference leak.
  - res_pjsip_session: Fix stream name memory leak.
  - res_pjsip: Adjust outgoing offer call pref.
  - voicemail: add option 'e' to play greetings as early media
  - pjsip: Match lifetime of INVITE session to our session.
  - res_pjsip_pidf_digium_body_supplement: Support Sangoma user agent.
  - pjsip: Make modify_local_offer2 tolerate previous failed SDP.
  - res_pjsip_session: Always produce offer on re-INVITE without SDP.
  - channel: Fix memory leak in suppress API.
  - res_pjsip_nat: Don't rewrite Contact on REGISTER responses.
  - asterisk: Update copyright.
  - res_pjsip_registrar: Include source IP and port in log messages.
  - sorcery: Add support for more intelligent reloading.
  - channel: Fix crash in suppress API.
  - documentation: Fix non-matching module support levels.
  - xml: Embed module information into core XML documentation.
  - xml: Allow deprecated_in and removed_in for MODULEINFO.
  - menuselect: Add ability to set deprecated and removed versions.
  - res_rtp_asterisk: Force resync on SSRC change.
  - res_pjsip: Add support for partial transport reload.
  - core_unreal: Fix deadlock with T.38 control frames.
  - app_queue: Only send QueueMemberStatus if status changes.
  - res_pjsip: Give error when TLS transport configured but not supported.
  - res_rtp_asterisk: Only raise flash control frame on end.
  - loader: Output warnings for deprecated modules.
  - svn: Switch to https scheme.
  - chan_local: Skip filtering audio formats on removed streams.
  - pjsip: Add patch for resolving STUN packet lifetime issues.
  - asterisk: We've moved to Libera Chat!
  - res_rtp_asterisk: Set correct raddr port on RTCP srflx candidates.
  - res_pjsip: On partial transport reload also move factories.
  - core: Don't play silence for Busy() and Congestion() applications.
  - AST-2021-007 - res_pjsip_session: Don't offer if no channel exists.
  - docs: Remove embedded macro in WaitForCond XML documentation.
  - policy: Deprecate modules and add versions to others.
  - cdr_mysql: Remove deprecated module.
  - app_mysql: Remove deprecated module.
  - app_ices: Remove deprecated module.
  - app_fax: Remove deprecated module.
  - app_url: Remove deprecated module.
  - app_image: Remove deprecated module.
  - app_nbscat: Remove deprecated module.
  - app_dahdiras: Remove deprecated module.
  - cdr_syslog: Remove deprecated module.
  - chan_oss: Remove deprecated module.
  - chan_phone: Remove deprecated module.
  - chan_nbs: Remove deprecated module.
  - chan_misdn: Remove deprecated module.
  - chan_vpb: Remove deprecated module.
  - res_config_sqlite: Remove deprecated module.
  - conf2ael: Remove deprecated application.
  - muted: Remove deprecated application.
  - res_monitor: Disable building by default.
  - ari: Ignore invisible bridges when listing bridges.
  - bridge: Deny full Local channel pair in bridge.
  - bridge: Unlock channel during Local peer check.
  - jansson: Update bundled to 2.14 version.
  - pjproject: Update bundled to 2.12 release.
  - func_odbc: Add SQL_ESC_BACKSLASHES dialplan function.
  - pjsip: Increase maximum number of format attributes.
  - cdr_adaptive_odbc: Add support for SQL_DATETIME field type.
  - res_pjsip: Always set async_operations to 1.
  - manager: Terminate session on write error.
  - res_pjsip_transport_websocket: Also set the remote name.
  - websocket / aeap: Handle poll() interruptions better.
  - pjsip: Add TLS transport reload support for certificate and key.
  - res_pjsip_sdp_rtp: Skip formats without SDP details.
  - res_agi: Respect "transmit_silence" option for "RECORD FILE".
  - ari: Destroy body variables in channel create.
  - res_pjsip_aoc: Don't assume a body exists on responses.
  - pbx_dundi: Fix PJSIP endpoint configuration check.
  - LICENSE: Update link to trademark policy.
  - app_queue: Add support for applying caller priority change immediately.
  - audiohook: Unlock channel in mute if no audiohooks present.
  - manager: Tolerate stasis messages with no channel snapshot.
  - variables: Add additional variable dialplan functions.
  - Update config.yml
  - utils: Make behavior of ast_strsep* match strsep.

- ### Joshua Colp (1):
  - Revert "app_queue: periodic announcement configurable start time."

- ### Kevin Harwell (28):
  - chan_pjsip: disallow PJSIP_SEND_SESSION_REFRESH pre-answer execution
  - conversions: Add string to signed integer conversion functions
  - Logging: Add debug logging categories
  - res_pjsip, res_pjsip_session: initialize local variables
  - AST-2020-001 - res_pjsip: Return dialog locked and referenced
  - pbx_realtime: wrong type stored on publish of ast_channel_snapshot_type
  - app_mixmonitor: cleanup datastore when monitor thread fails to launch
  - AST-2021-002: Remote crash possible when negotiating T.38
  - res_rtp_asterisk: Add packet subtype during RTCP debug when relevant
  - time: Add timeval create and unit conversion functions
  - res_rtp_asterisk: Add a DEVMODE RTP drop packets CLI command
  - res_rtp_asterisk: Statically declare rtp_drop_packets_data object
  - res_rtp_asterisk: Don't count 0 as a minimum lost packets
  - res_rtp_asterisk: Fix standard deviation calculation
  - AST-2021-008 - chan_iax2: remote crash on unsupported media format
  - AST-2021-009 - pjproject-bundled: Avoid crash during handshake for TLS
  - format_ogg_speex: Implement a "not supported" write handler
  - res_speech: Add a type conversion, and new engine unregister methods
  - strings/json: Add string delimter match, and object create with vars methods
  - http.c: Add ability to create multiple HTTP servers
  - tcptls.c: refactor client connection to be more robust
  - res_http_websocket: Add a client connection timeout
  - deprecation cleanup: remove leftover files
  - res_pjsip_header_funcs: wrong pool used tdata headers
  - res_aeap & res_speech_aeap: Add Asterisk External Application Protocol
  - test_aeap_transport: disable part of failing unit test
  - res_pjsip: allow TLS verification of wildcard cert-bearing servers
  - cel_odbc & res_config_odbc: Add support for SQL_DATETIME field type

- ### Kfir Itzhak (2):
  - app_queue: Fix leave-empty not recording a call as abandoned
  - app_queue: Add QueueWithdrawCaller AMI action

- ### Luke Escude (1):
  - res_pjsip_sdp_rtp.c: Support keepalive for video streams.

- ### Marcel Wagner (3):
  - documentation: Add information on running install_prereq script in readme
  - res_pjsip: Update contact_user to point out default
  - res_pjsip: Fix typo in from_domain documentation

- ### Mark Murawski (4):
  - logger: Console sessions will now respect logger.conf dateformat= option
  - pbx_ael:  Fix crash and lockup issue regarding 'ael reload'
  - pbx_ael:  Fix crash and lockup issue regarding 'ael reload'
  - Remove files that are no longer updated

- ### Mark Petersen (10):
  - apps/app_dial.c: HANGUPCAUSE reason code for CANCEL is set to AST_CAUSE_NORMAL..
  - app_voicemail.c: Support for Danish syntax in VM
  - app_queue.c: added DIALEDPEERNUMBER on outgoing channel
  - app_queue.c: Support for Nordic syntax in announcements
  - app_queue.c: Queue don't play "thank-you" when here is no hold time announceme..
  - chan_sip.c Fix pickup on channel that are in AST_STATE_DOWN
  - res_prometheus.c: missing module dependency
  - chan_sip: SIP route header is missing on UPDATE
  - chan_pjsip: add allow_sending_180_after_183 option
  - chan_sip.c Session timers get removed on UPDATE

- ### Matthew Fredrickson (4):
  - Revert "app_stack: Print proper exit location for PBXless channels."
  - app_macro: Fix locking around datastore access
  - app_followme.c: Grab reference on nativeformats before using it
  - res_odbc.c: Allow concurrent access to request odbc connections

- ### Matthew Kern (1):
  - res_pjsip_t38: bind UDPTL sessions like RTP

- ### Maximilian Fridrich (13):
  - app_dial: Flip stream direction of outgoing channel.
  - core_unreal: Flip stream direction of second channel.
  - chan_pjsip: Only set default audio stream on hold.
  - res_pjsip: Add 100rel option "peer_supported".
  - res_pjsip: Add mediasec capabilities.
  - core & res_pjsip: Improve topology change handling.
  - res_pjsip: mediasec: Add Security-Client headers after 401
  - chan_pjsip: Allow topology/session refreshes in early media state
  - core/ari/pjsip: Add refer mechanism
  - main/refer.c: Fix double free in refer_data_destructor + potential leak
  - chan_rtp: Implement RTP glue for UnicastRTP channels
  - app_dial: Add option "j" to preserve initial stream topology of caller
  - res_pjsip_nat: Fix potential use of uninitialized transport details

- ### Michael Cargile (1):
  - apps/confbridge: Added hear_own_join_sound option to control who hears sound_j..

- ### Michael Kuron (2):
  - res_pjsip_aoc: New module for sending advice-of-charge with chan_pjsip
  - manager: AOC-S support for AOCMessage

- ### Michael Neuhauser (2):
  - pjproject: clone sdp to protect against (nat) modifications
  - res_pjsip: delay contact pruning on Asterisk start

- ### Michal Hajek (1):
  - res_stasis.c: Add compare function for bridges moh container

- ### Michał Górny (5):
  - include: Remove unimplemented HMAC declarations
  - BuildSystem: Fix misdetection of gethostbyname_r() on NetBSD
  - main: Enable rdtsc support on NetBSD
  - main/utils: Implement ast_get_tid() for NetBSD
  - build_tools/make_version: Fix sed(1) syntax compatibility with NetBSD

- ### Miguel Angel Nubla (1):
  - configure: Makefile downloader enable follow redirects.

- ### Mike Bradeen (44):
  - build: prevent binary downloads for non x86 architectures
  - various: Fix GCC 11 compilation issues.
  - astobj2.c: Fix core when ref_log enabled
  - res_rtp_asterisk: Addressing possible rtp range issues
  - sched: fix and test a double deref on delete of an executing call back
  - taskprocessor.c: Prevent crash on graceful shutdown
  - Makefile: Avoid git-make user conflict
  - CI: use Python3 virtual environment
  - CI: Fixing path issue on venv check
  - alembic: add missing ps_endpoints columns
  - res_pjsip: Add user=phone on From and PAID for usereqphone=yes
  - audiohook: add directional awareness
  - res_pjsip: prevent crash on websocket disconnect
  - ooh323c: not checking for IE minimum length
  - manager: prevent file access outside of config dir
  - app_directory: add ability to specify configuration file
  - res_pjsip: Upgraded bundled pjsip to 2.13
  - res_pjsip: Prevent SEGV in pjsip_evsub_send_request
  - app_senddtmf: Add option to answer target channel.
  - app_directory: Add a 'skip call' option.
  - app_read: Add an option to return terminator on empty digits.
  - res_pjsip_pubsub: subscription cleanup changes
  - cli: increase channel column width
  - format_sln: add .slin as supported file extension
  - res_mixmonitor: MixMonitorMute by MixMonitor ID
  - bridge_builtin_features: add beep via touch variable
  - cel: add local optimization begin event
  - indications: logging changes
  - utils: add lock timestamps for DEBUG_THREADS
  - res_musiconhold: avoid moh state access on unlocked chan
  - app_voicemail: fix imap compilation errors
  - app_voicemail: add CLI commands for message manipulation
  - Adds manager actions to allow move/remove/forward individual messages in a par..
  - app_voicemail: Fix for loop declarations
  - res_pjsip: disable raw bad packet logging
  - res_speech_aeap: check for null format on response
  - func_periodic_hook: Add hangup step to avoid timeout
  - cel: add publish user event helper
  - res_speech_aeap: add aeap error handling
  - res_pjsip: update qualify_timeout documentation with DNS note
  - res_stasis: signal when new command is queued
  - res_speech: allow speech to translate input channel
  - app_voicemail: add NoOp alembic script to maintain sync
  - app_chanspy: Add 'D' option for dual-channel audio

- ### MikeNaso (1):
  - res_pjsip.c: Set contact_user on incoming call local Contact header

- ### Moritz Fain (1):
  - ari: expose channel driver's unique id to ARI channel resource

- ### Nathan Bruning (2):
  - res_musiconhold: Don't crash when real-time doesn't return any entries
  - app_queue: Add force_longest_waiting_caller option.

- ### Naveen Albert (267):
  - chan_sip: Expand hook flash recognition.
  - main/file.c: Don't throw error on flash event.
  - app_voicemail: Configurable voicemail beep
  - AMI: Add AMI event to expose hook flash events
  - func_volume: Add read capability to function.
  - func_math: Three new dialplan functions
  - sip_to_pjsip: Fix missing cases
  - app_confbridge: New option to prevent answer supervision
  - res_pjsip_dtmf_info: Hook flash
  - app_confbridge: New ConfKick() application
  - app_originate: Allow setting Caller ID and variables
  - pbx_builtins: Corrects SayNumber warning
  - app_dial: Expanded A option to add caller announcement
  - app_waitforcond: New application
  - app_reload: New Reload application
  - app_dtmfstore: New application to store digits
  - app_queue: Allow streaming multiple announcement files
  - cdr_adaptive_odbc: Prevent filter warnings
  - func_frame_drop: New function
  - chan_alsa, chan_sip: Add replacement to moduleinfo
  - app_originate: Add ability to set codecs
  - func_scramble: Audio scrambler function
  - app_morsecode: Add American Morse code
  - func_math: Return integer instead of float if possible
  - app_milliwatt: Timing fix
  - app_queue: Don't reset queue stats on reload
  - bridge_basic: Change warning to verbose if transfer cancelled
  - app_read: Allow reading # as a digit
  - chan_iax2: Add ANI2/OLI information element
  - res_tonedetect: Tone detection module
  - func_sayfiles: Retrieve say file names
  - func_env: Add DIRNAME and BASENAME functions
  - func_strings: Add STRBETWEEN function
  - app_stack: Include current location if branch fails
  - app_mf: Add channel agnostic MF sender
  - res_pjsip_caller_id: Add ANI2/OLI parsing
  - logger: Add custom logging capabilities
  - app_queue: Fix hint updates for included contexts
  - func_channel: Add CHANNEL_EXISTS function.
  - func_vmcount: Add support for multiple mailboxes
  - app_read: Fix null pointer crash
  - chan_iax2: Add encryption for RSA authentication
  - chan_iax2: Allow both secret and outkey at dial time
  - app_voicemail: Fix phantom voicemail bug on rerecord
  - sig_analog: Fix truncated buffer copy
  - res_pjsip_callerid: Fix OLI parsing
  - app_read: Fix custom terminator functionality regression
  - app_morsecode: Fix deadlock
  - res_tonedetect: Add call progress tone detection
  - app_voicemail: Refactor email generation functions
  - documentation: Standardize examples
  - app_mf: Add full tech-agnostic MF support
  - pbx: Add variable substitution API for extensions
  - configs: Updates to sample configs
  - func_json: Adds JSON_DECODE function
  - chan_sip: Fix crash when accessing RURI before initiating outgoing call
  - pbx_variables: Increase parsing capabilities of MSet
  - strings: Fix enum names in comment examples
  - app_sendtext: Add ReceiveText application
  - app.c: Throw warnings for nonexistent options
  - pbx_variables: initialize uninitialized variable
  - app_sf: Add full tech-agnostic SF support
  - documentation: Add missing AMI documentation
  - app_mp3: Throw warning on nonexistent stream
  - cli: Add module refresh command
  - ami: Add AMI event for Wink
  - dsp: Add define macro for DTMF_MATRIX_SIZE
  - say.conf: fix 12pm noon logic
  - pbx_variables: add missing ASTSBINDIR variable
  - documentation: Document built-in system and channel vars
  - res_rtp_asterisk: Fix typo in flag test/set
  - frame.h: Fix spelling typo
  - func_frame_drop: Fix typo referencing wrong buffer
  - res_tonedetect: Fixes some logic issues and typos
  - cdr: allow disabling CDR by default on new channels
  - ami: Allow events to be globally disabled.
  - app_mf: Add max digits option to ReceiveMF.
  - app_mp3: Document and warn about HTTPS incompatibility.
  - documentation: Adds missing default attributes.
  - cli: Add core dump info to core show settings.
  - func_db: Add validity check for key names when writing.
  - app_voicemail: Emit warning if asking for nonexistent mailbox.
  - res_stir_shaken: refactor utility function
  - asterisk: Add macro for curl user agent.
  - documentation: Add since tag to xmldocs DTD
  - configs, LICENSE: remove pbx.digium.com.
  - channel.c: Clean up debug level 1.
  - func_channel: Add lastcontext and lastexten.
  - ami: Improve substring parsing for disabled events.
  - res_agi: Fix xmldocs bug with set music.
  - pbx_builtins: Add missing options documentation
  - app_dial: Document DIALSTATUS return values.
  - chan_iax2: Fix perceived showing host address.
  - chan_iax2: Fix spacing in netstats command
  - pbx.c: Warn if there are too many includes in a context.
  - build: Remove obsolete leftover build references.
  - app_meetme: Emit warning if conference not found.
  - app_queue: Add music on hold option to Queue.
  - app_mf, app_sf: Return -1 if channel hangs up.
  - samples: Remove obsolete sample configs.
  - documentation: Adds versioning information.
  - cli: Add command to evaluate dialplan functions.
  - chan_pjsip: Add ability to send flash events.
  - file.c: Prevent formats from seeking negative offsets.
  - func_evalexten: Extension evaluation function.
  - chan_dahdi: Fix insufficient array size for round robin.
  - func_db: Add function to return cardinality at prefix
  - asterisk.c: Warn of incompatibilities with remote console.
  - app_meetme: Don't erroneously set global variables.
  - menuselect: Don't erroneously recompile modules.
  - chan_iax2: Prevent crash if dialing RSA-only call without outkey.
  - chan_dahdi: Don't append cadences on dahdi restart.
  - chan_dahdi: Don't allow MWI FSK if channel not idle.
  - chan_dahdi: Document dial resource options.
  - chan_dahdi: Fix broken operator mode clearing.
  - app_confbridge: Add function to retrieve channels.
  - res_pjsip_outbound_registration: Show time until expiration
  - res_parking: Warn if out of bounds parking spot requested.
  - res_calendar: Prevent assertion if event ends in past.
  - loader: Prevent deadlock using tab completion.
  - chan_iax2: Prevent deadlock due to duplicate autoservice.
  - res_pjsip_outbound_registration: Make max random delay configurable.
  - xmldocs: Improve examples.
  - res_parking: Add music on hold override option.
  - app_voicemail: Add option to prevent message deletion.
  - sig_analog: Fix broken three-way conferencing.
  - asterisk.c: Fix incompatibility warnings for remote console.
  - pbx: Add helper function to execute applications.
  - say: Abort play loop if caller hangs up.
  - res_calendar_icalendar: Send user agent in request.
  - app_dial: Propagate outbound hook flashes.
  - cli: Fix CLI blocking forever on terminating backslash
  - db: Notify user if deleted DB entry didn't exist.
  - app_dial: Fix dial status regression.
  - res_cliexec: Add dialplan exec CLI command.
  - chan_iax2: Allow compiling without OpenSSL.
  - general: Fix various typos.
  - app_confbridge: Always set minimum video update interval.
  - chan_dahdi: Add POLARITY function.
  - chan_dahdi: Fix buggy and missing Caller ID parameters
  - manager: Fix incomplete filtering of AMI events.
  - db: Add AMI action to retrieve DB keys at prefix.
  - pbx_functions.c: Manually update ast_str strlen.
  - func_srv: Document field parameter.
  - app_meetme: Add missing AMI documentation.
  - app_confbridge: Add missing AMI documentation.
  - general: Remove obsolete SVN references.
  - cdr.conf: Remove obsolete app_mysql reference.
  - general: Improve logging levels of some log messages.
  - manager: Remove documentation for nonexistent action.
  - app_confbridge: Fix memory leak on updated menu options.
  - chan_iax2: Add missing options documentation.
  - general: Very minor coding guideline fixes.
  - features: Add transfer initiation options.
  - res_tonedetect: Fix typos referring to wrong variables.
  - cli: Prevent assertions on startup from bad ao2 refs.
  - pbx_variables: Use const char if possible.
  - app_confbridge: Add end_marked_any option.
  - lock.c: Add AMI event for deadlocks.
  - func_frame_trace: Remove bogus assertion.
  - func_strings: Add trim functions.
  - func_scramble: Fix null pointer dereference.
  - func_export: Add EXPORT function
  - app_amd: Add option to play audio during AMD.
  - app_bridgewait: Add option to not answer channel.
  - features: Add no answer option to Bridge.
  - func_logic: Don't emit warning if both IF branches are empty.
  - db: Fix incorrect DB tree count for AMI.
  - res_pjsip_geolocation: Change some notices to debugs.
  - chan_dahdi: Resolve format truncation warning.
  - res_tonedetect: Add ringback support to TONE_DETECT.
  - cdr: Allow bridging and dial state changes to be ignored.
  - say: Don't prepend ampersand erroneously.
  - res_pjsip_pubsub: Prevent removing subscriptions.
  - chan_dahdi: Fix unavailable channels returning busy.
  - res_pjsip_logger: Add method-based logging option.
  - res_pjsip_notify: Add option support for AMI.
  - tests: Fix compilation errors on 32-bit.
  - tcptls: Prevent crash when freeing OpenSSL errors.
  - app_stack: Print proper exit location for PBXless channels.
  - file.c: Don't emit warnings on winks.
  - manager: Update ModuleCheck documentation.
  - app_mixmonitor: Add option to delete files on exit.
  - test_json: Remove duplicated static function.
  - func_json: Fix memory leak.
  - sla: Prevent deadlock and crash due to autoservicing.
  - chan_dahdi: Allow FXO channels to start immediately.
  - pbx_builtins: Allow Answer to return immediately.
  - app_mixmonitor: Add option to use real Caller ID for voicemail.
  - rtp_engine.h: Update examples using ast_format_set.
  - xmldoc: Allow XML docs to be reloaded.
  - sig_analog: Fix no timeout duration.
  - func_presencestate: Fix invalid memory access.
  - res_pjsip_header_funcs: Add custom parameter support.
  - res_adsi: Fix major regression caused by media format rearchitecture.
  - res_pjsip_session.c: Map empty extensions in INVITEs to s.
  - app_if: Adds conditional branch applications
  - res_hep: Add support for named capture agents.
  - app_voicemail: Fix missing email in msg_create_from_file.
  - app_if: Fix format truncation errors.
  - app_sendtext: Remove references to removed applications.
  - func_callerid: Warn about invalid redirecting reason.
  - app_voicemail_odbc: Fix string overflow warning.
  - res_pjsip_session: Use Caller ID for extension matching.
  - pbx_app: Update outdated pbx_exec channel snapshots.
  - manager: Fix appending variables.
  - json.h: Add ast_json_object_real_get.
  - func_frame_trace: Print text for text frames.
  - app_broadcast: Add Broadcast application
  - loader: Allow declined modules to be unloaded.
  - res_pjsip_session: Add overlap_context option.
  - func_json: Enhance parsing capabilities of JSON_DECODE
  - app_signal: Add signaling applications
  - chan_iax2: Fix jitterbuffer regression prior to receiving audio.
  - app_dial: Fix DTMF not relayed to caller on unanswered calls.
  - app_senddtmf: Add SendFlash AMI action.
  - func_json: Fix JSON parsing issues.
  - app_queue: Fix minor xmldoc duplication and vagueness.
  - voicemail.conf: Fix incorrect comment about #include.
  - pbx_dundi: Add PJSIP support.
  - res_pjsip_stir_shaken: Fix JSON field ordering and disallowed TN characters.
  - chan_dahdi: Add dialmode option for FXS lines.
  - asterisk.c: Fix option warning for remote console.
  - chan_dahdi: Fix broken hidecallerid setting.
  - logrotate: Fix duplicate log entries.
  - callerid: Allow specifying timezone for date/time.
  - sig_analog: Add fuller Caller ID support.
  - chan_dahdi: Fix Caller ID presentation for FXO ports.
  - res_musiconhold: Add option to loop last file.
  - sig_analog: Allow immediate fake ring to be suppressed.
  - sig_analog: Allow three-way flash to time out to silence.
  - chan_dahdi: Allow autoreoriginating after hangup.
  - res_pjsip_header_funcs: Make prefix argument optional.
  - sig_analog: Add Called Subscriber Held capability.
  - pbx.c: Fix gcc 12 compiler warning.
  - app_dial: Fix infinite loop when sending digits.
  - chan_iax2: Improve authentication debugging.
  - chan_console: Fix deadlock caused by unclean thread exit.
  - app_voicemail: Disable ADSI if unavailable.
  - chan_dahdi: Clarify scope of callgroup/pickupgroup.
  - res_pjsip: Include cipher limit in config error message.
  - app_voicemail: Add AMI event for mailbox PIN changes.
  - core_local: Fix local channel parsing with slashes.
  - app_directory: Add ADSI support to Directory.
  - chan_dahdi: Warn if nonexistent cadence is requested.
  - logger: Add channel-based filtering.
  - configs: Improve documentation for bandwidth in iax.conf.
  - func_lock: Add missing see-also refs to documentation.
  - func_channel: Expose previously unsettable options.
  - sig_analog: Fix channel leak when mwimonitor is enabled.
  - general: Fix broken links.
  - manager.c: Improve clarity of "manager show connected".
  - config_options.c: Fix truncation of option descriptions.
  - manager.c: Fix regression due to using wrong free function.
  - app_if: Fix faulty EndIf branching.
  - menuselect: Use more specific error message.
  - logger: Fix linking regression.
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

- ### Nick French (4):
  - res_pjsip_session: Restore calls to ast_sip_message_apply_transport()
  - res_pjsip: Prevent segfault in UDP registration with flow transports
  - res_pjsip: dont return early from registration if init auth fails
  - pjproject_bundled: Fix cross-compilation with SSL libs.

- ### Nickolay Shmyrev (1):
  - res_speech: Bump reference on format object

- ### Nico Kooijman (1):
  - main: With Dutch language year after 2020 is not spoken in say.c

- ### Niklas Larsson (1):
  - app_queue: Preserve reason for realtime queues

- ### Olaf Titz (1):
  - app_voicemail_imap: Fix message count when IMAP server is unavailable

- ### Patrick Verzele (1):
  - res_pjsip_session: Deferred re-INVITE without SDP send a=sendrecv instead of a..

- ### Peter Fern (1):
  - streams:  Ensure that stream is closed in ast_stream_and_wait on error

- ### PeterHolik (2):
  - chan_rtp.c: Change MulticastRTP nameing to avoid memory leak
  - chan_rtp.c: MulticastRTP missing refcount without codec option

- ### Philip Prindeville (14):
  - logger: workaround woefully small BUFSIZ in MUSL
  - time: add support for time64 libcs
  - res_crypto: Don't load non-regular files in keys directory
  - test: Add ability to capture child process output
  - main/utils: allow checking for command in $PATH
  - test: Add test coverage for capture child process output
  - res_crypto: make keys reloadable on demand for testing
  - test: Add coverage for res_crypto
  - res_crypto: Use EVP API's instead of legacy API's
  - res_crypto: don't complain about directories
  - test: initialize capture structure before freeing
  - res_crypto: use ast_file_read_dirs() to iterate
  - res_crypto: don't modify fname in try_load_key()
  - res_crypto: handle unsafe private key files

- ### Pirmin Walthert (1):
  - res_pjsip_nat.c: Create deep copies of strings when appropriate

- ### Richard Mudgett (2):
  - res_pjsip_session.c: Fix compiler warnings.
  - chan_vpb.cc: Fix compile errors.

- ### Rijnhard Hessel (1):
  - res_statsd: handle non-standard meter type safely

- ### Robert Cripps (1):
  - res/res_pjsip_session.c: Check that media type matches in function ast_sip_ses..

- ### Rodrigo Ramírez Norambuena (1):
  - app_queue: Add LoginTime field for member in a queue.

- ### Salah Ahmed (1):
  - res_rtp_asterisk:  Check remote ICE reset and reset local ice attrb

- ### Sam Banks (1):
  - queues.conf.sample: Correction of typo

- ### Samuel Olaechea (1):
  - configs: Fix typo in pjsip.conf.sample.

- ### Sarah Autumn (1):
  - sig_analog: Changes to improve electromechanical signalling compatibility

- ### Sean Bright (149):
  - acl.c: Coerce a NULL pointer into the empty string
  - vector.h: Add AST_VECTOR_SORT()
  - utf8.c: Add UTF-8 validation and utility functions
  - vector.h: Fix implementation of AST_VECTOR_COMPACT() for empty vectors
  - res_musiconhold.c: Prevent crash with realtime MoH
  - res_musiconhold.c: Use ast_file_read_dir to scan MoH directory
  - bridge_channel: Ensure text messages are zero terminated
  - app_voicemail: Process urgent messages with mailcmd
  - format_cap: Perform codec lookups by pointer instead of name
  - res_pjsip_session.c: Fix build when TEST_FRAMEWORK is not defined
  - audiosocket: Fix module menuselect descriptions
  - chan_sip.c: Don't build by default
  - res_musiconhold: Start playlist after initial announcement
  - func_curl.c: Prevent crash when using CURLOPT(httpheader)
  - dsp.c: Update calls to ast_format_cmp to check result properly
  - res_musiconhold: Clarify that playlist mode only supports HTTP(S) URLs
  - app_voicemail.c: Document VMSayName interruption behavior
  - pbx.c: On error, ast_add_extension2_lockopt should always free 'data'
  - tcptls.c: Don't close TCP client file descriptors more than once
  - features.conf.sample: Sample sound files incorrectly quoted
  - sip_to_pjsip.py: Handle #include globs and other fixes
  - CHANGES: Remove already applied CHANGES update
  - media_cache: Fix reference leak with bucket file metadata
  - res_http_media_cache.c: Set reasonable number of redirects
  - app_chanspy: Spyee information missing in ChanSpyStop AMI Event
  - asterisk: Export additional manager functions
  - app_voicemail: Prevent deadlocks when out of ODBC database connections
  - res_pjsip_pubsub: Fix truncation of persisted SUBSCRIBE packet
  - app_read: Release tone zone reference on early return.
  - res_rtp_asterisk.c: Fix signed mismatch that leads to overflow
  - app_page.c: Don't fail to Page if beep sound file is missing
  - res_musiconhold.c: Plug ref leak caused by ao2_replace() misuse.
  - strings.h: ast_str_to_upper() and _to_lower() are not pure.
  - modules.conf: Fix differing usage of assignment operators.
  - app_dial.c: Only send DTMF on first progress event.
  - app_queue.c: Don't crash when realtime queue name is empty.
  - queues.conf.sample: Correct 'context' documentation.
  - app_queue.c: Remove dead 'updatecdr' code.
  - modules.conf: Fix more differing usages of assignment operators.
  - app_queue: Add alembic migration to add ringinuse to queue_members.
  - loader.c: Speed up deprecation metadata lookup
  - res_pjsip.c: OPTIONS processing can now optionally skip authentication
  - res_rtp_asterisk: More robust timestamp checking
  - translate.c: Avoid refleak when checking for a translation path
  - chan_pjsip: Correct misleading trace message
  - menuselect: Fix description of several modules.
  - res_pjsip_config_wizard.c: Add port matching support.
  - main/cdr.c: Correct Party A selection.
  - res_http_media_cache.c: Parse media URLs to find extensions.
  - res_pjsip_stir_shaken: RFC 8225 compliance and error message cleanup.
  - res_http_media_cache.c: Fix merge errors from 18 -> master
  - res_http_media_cache: Cleanup audio format lookup in HTTP requests
  - mgcp: Remove dead debug code
  - config_options: Handle ACO arrays correctly in generated XML docs.
  - dns.c: Load IPv6 DNS resolvers if configured.
  - term.c: Add support for extended number format terminfo files.
  - app_voicemail.c: Ability to silence instructions if greeting is present.
  - test_abstract_jb.c: Fix put and put_out_of_order memory leaks.
  - test_http_media_cache.c: Fix copy/paste error during test deregistration.
  - app_externalivr.c: Fix mixed leading whitespace in source code.
  - res_http_media_cache.c: Compare unaltered MIME types.
  - message.c: Support 'To' header override with AMI's MessageSend.
  - Makefile: Use basename in a POSIX-compliant way.
  - configure: Remove unused OpenSSL SRTP check.
  - func_talkdetect.c: Fix logical errors in silence detection.
  - various: Fix GCC 11.2 compilation issues.
  - pbx.c: Don't remove dashes from hints on reload.
  - config.c: Prevent UB in ast_realtime_require_field.
  - channel: Short-circuit ast_channel_get_by_name() on empty arg.
  - CHANGES: Correct reference to configuration file.
  - say.c: Honor requests for DTMF interruption.
  - pjproject: Fix incorrect unescaping of tokens during parsing
  - utils.c: Remove all usages of ast_gethostbyname()
  - say.c: Prevent erroneous failures with 'say' family of functions.
  - build: Rebuild configure and autoconfig.h.in
  - build_tools/make_version: Fix bashism in comparison.
  - manager.c: Generate valid XML if attribute names have leading digits.
  - res_pjsip.c: Correct minor typos in 'realm' documentation.
  - manager.c: Simplify AMI ModuleCheck handling
  - conversions.c: Specify that we only want to parse decimal numbers.
  - download_externals: Use HTTPS for downloads
  - stasis_recording: Perform a complete match on requested filename.
  - openssl: Supress deprecation warnings from OpenSSL 3.0
  - config.h: Don't use C++ keywords as argument names.
  - loader.c: Use portable printf conversion specifier for int64.
  - ast_pkgconfig.m4: AST_PKG_CONFIG_CHECK() relies on sed.
  - pbx.c: Simplify ast_context memory management.
  - channel.h: Remove redundant declaration.
  - chan_dahdi.c: Resolve a format-truncation build warning.
  - app_playback.c: Fix PLAYBACKSTATUS regression.
  - pbx_ael: Global variables are not expanded.
  - doxygen: Fix doxygen errors.
  - app_queue: Reset all queue defaults before reload.
  - app_queue: Minor docs and logging fixes for UnpauseQueueMember.
  - test_crypto.c: Fix getcwd(…) build error.
  - test.c: Avoid passing -1 to FD_* family of functions.
  - Revert "pbx_ael: Global variables are not expanded."
  - contrib: rc.archlinux.asterisk uses invalid redirect.
  - res_agi: RECORD FILE plays 2 beeps.
  - ael: Regenerate lexers and parsers.
  - loader.c: Minor module key check simplification.
  - core: Cleanup gerrit and JIRA references. (#57)
  - apply_patches: Use globbing instead of file/sort.
  - xml.c: Process XML Inclusions recursively.
  - res_pjsip_pubsub.c: Use pjsip version for pending NOTIFY check. (#77)
  - sounds: Update download URL to use HTTPS.
  - ast-db-manage: Fix alembic branching error caused by #122.
  - res_crypto.c: Avoid using the non-portable ALLPERMS macro.
  - ast-db-manage: Synchronize revisions between comments and code.
  - configure: Remove obsolete and deprecated constructs.
  - res_crypto.c: Gracefully handle potential key filename truncation.
  - pjsip_transport_events.c: Use %zu printf specifier for size_t.
  - res_pjsip_rfc3326: Prefer Q.850 cause code over SIP.
  - res_geolocation: Ensure required 'location_info' is present.
  - chan_iax2.c: Avoid crash with IAX2 switch support.
  - func_export: Use correct function argument as variable name.
  - extensions.conf.sample: Remove reference to missing context.
  - res_pjsip: Enable TLS v1.3 if present.
  - extconfig: Allow explicit DB result set ordering to be disabled.
  - res_stasis_recording.c: Save recording state when unmuted.
  - func_curl.c: Ensure channel is locked when manipulating datastores.
  - pjsip_configuration.c: Disable DTLS renegotiation if WebRTC is enabled.
  - res_pjsip_dtmf_info.c: Add 'INFO' to Allow header.
  - res_rtp_asterisk.c: Fix memory leak in ephemeral certificate creation.
  - app_queue.c: Emit unpause reason with PauseQueueMember event.
  - chan_iax2.c: Ensure all IEs are displayed when dumping frame contents.
  - doc: Update IP Quality of Service links.
  - resource_channels.c: Explicit codec request when creating UnicastRTP.
  - chan_iax2.c: Don't send unsanitized data to the logger.
  - live_ast: Add astcachedir to generated asterisk.conf.
  - res_http_websocket.c: Set hostname on client for certificate validation.
  - func_curl.c: Remove CURLOPT() plaintext documentation.
  - uri.c: Simplify ast_uri_make_host_with_port()
  - app.c: Allow ampersands in playback lists to be escaped.
  - alembic: Update list of TLS methods available on ps_transports.
  - res_rtp_asterisk.c: Update for OpenSSL 3+.
  - app_voicemail.c: Completely resequence mailbox folders.
  - make_xml_documentation: Properly handle absolute LOCAL_MOD_SUBDIRS.
  - config.c: Log #exec include failures.
  - res_pjsip_header_funcs.c: Check URI parameter length before copying.
  - logger.c: Move LOG_GROUP documentation to dedicated XML file.
  - make_xml_documentation: Really collect LOCAL_MOD_SUBDIRS documentation.
  - app_confbridge: Don't emit warnings on valid configurations.
  - rtp_engine.c: Correct sample rate typo for L16/44100.
  - res_pjsip_session.c: Correctly format SDP connection addresses.
  - res_pjsip_t38.c: Permit IPv6 SDP connection addresses.
  - strings.h: Ensure ast_str_buffer(…) returns a 0 terminated string.
  - alembic: Synchronize alembic heads between supported branches.
  - res_pjsip: Use consistent type for boolean columns.

- ### Sebastian Jennen (1):
  - translate.c: implement new direct comp table mode

- ### Sebastien Duthil (4):
  - app_mixmonitor: Add AMI events MixMonitorStart, -Stop and -Mute.
  - stun: Emit warning message when STUN request times out
  - res_rtp_asterisk: Automatically refresh stunaddr from DNS
  - main/stun.c: fix crash upon STUN request timeout

- ### Sergey V. Lobanov (1):
  - build: fix bininstall launchd issue on cross-platform build

- ### Shaaah (1):
  - app_queue.c : fix "queue add member" usage string

- ### Shloime Rosenblum (3):
  - main/say.c: Support future dates with Q and q format params
  - apps/app_playback.c: Add 'mix' option to app_playback
  - res_agi: Evaluate dialplan functions and variables in agi exec if enabled

- ### Shyju Kanaprath (1):
  - README.md: Removed outdated link

- ### Stanislav (1):
  - res_pjsip_stir_shaken: Fix module description

- ### Stanislav Abramenkov (2):
  - pjsip: Upgrade bundled version to pjproject 2.12.1
  - pjsip: Upgrade bundled version to pjproject 2.13.1

- ### Steve Davies (1):
  - app_queue: Fix hint updates, allow dup. hints

- ### Sungtae Kim (5):
  - res_stasis.c: Added video_single option for bridge creation
  - realtime: Increased reg_server character size
  - res_ari: Fix wrong media uri handle for channel play
  - res_pjsip_session: Fixed NULL active media topology handle
  - resource_channels.c: Fix external media data option

- ### The_Blode (1):
  - install_prereq: Add Linux Mint support.

- ### Thomas Guebels (1):
  - res_pjsip_transport_websocket: save the original contact host

- ### Tinet-mucw (1):
  - res_pjsip_transport_websocket: Prevent transport from being destroyed before m..

- ### Torrey Searle (6):
  - res_pjsip_diversion: handle 181
  - res_pjsip_diversion: implement support for History-Info
  - res_pjsip_diversion: fix double 181
  - res/res_pjsip_diversion: prevent crash on tel: uri in History-Info
  - res/res_rtp_asterisk: generate new SSRC on native bridge end
  - res/res_rtp_asterisk: fix skip in rtp sequence numbers after dtmf

- ### Trevor Peirce (2):
  - res_pjsip: Actually enable session timers when timers=always
  - features: Update documentation for automon and automixmon

- ### Vitezslav Novy (1):
  - res_rtp_asterisk: fix wrong counter management in ioqueue objects

- ### Walter Doekes (1):
  - main/say: Work around gcc 9 format-truncation false positive

- ### Yury Kirsanov (1):
  - bridge_simple.c: Unhold channels on join simple bridge.

- ### alex2grad (1):
  - app_followme: fix issue with enable_callee_prompt=no (#88)

- ### cmaj (3):
  - Makefile: Fix certified version numbers
  - res_phoneprov.c: Multihomed SERVER cache prevention
  - app_speech_utils.c: Allow partial speech results.

- ### lvl (2):
  - res_musiconhold: Load all realtime entries, not just the first
  - Introduce astcachedir, to be used for temporary bucket files

- ### phoneben (1):
  - func_cut: Add example to documentation.

- ### roadkill (1):
  - res/res_pjsip.c: allow user=phone when number contain *#

- ### romryz (1):
  - res_rtp_asterisk.c: Correct coefficient in MOS calculation.

- ### sungtae kim (5):
  - stasis_bridge.c: Fixed wrong video_mode shown
  - resource_channels.c: Fix wrong external media parameter parse
  - res_musiconhold: Add option to not play music on hold on unanswered channels
  - res_stasis_snoop: Fix snoop crash
  - res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 cha..

- ### under (1):
  - codec_builtin.c: G729 audio gets corrupted by Asterisk due to smoother

- ### zhengsh (3):
  - res_sorcery_memory_cache.c: Fix memory leak
  - res_rtp_asterisk: Move ast_rtp_rtcp_report_alloc using `rtp->themssrc_valid` i..
  - app_audiosocket: Fixed timeout with -1 to avoid busy loop.

- ### zhou_jiajian (1):
  - res_fax_spandsp.c: Clean up a spaces/tabs issue


Detail:
----------------------------------------

- ### Initial commit for certified-20.7                                               
  Author: George Joseph  
  Date:   2024-03-18  


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


- ### alembic: Synchronize alembic heads between supported branches.                  
  Author: Sean Bright  
  Date:   2024-02-28  

  This adds a dummy migration to 18 and 20 so that our alembic heads are
  synchronized across all supported branches.

  In this case the migration we are stubbing (24c12d8e9014) is:

  https://github.com/asterisk/asterisk/commit/775352ee6c2a5bcd4f0e3df51aee5d1b0abf4cbe

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


- ### app_voicemail: add NoOp alembic script to maintain sync                         
  Author: Mike Bradeen  
  Date:   2024-01-17  

  Adding a NoOp alembic script for the voicemail database to maintain
  version sync with other branches.

  Fixes: #527

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

- ### logger: Fix linking regression.                                                 
  Author: Naveen Albert  
  Date:   2024-01-16  

  Commit 008731b0a4b96c4e6c340fff738cc12364985b64
  caused a regression by resulting in logger.xml
  being compiled and linked into the asterisk
  binary in lieu of logger.c on certain platforms
  if Asterisk was compiled in dev mode.

  To fix this, we ensure the file has a unique
  name without the extension. Most existing .xml
  files have been named differently from any
  .c files in the same directory or did not
  pose this issue.

  channels/pjsip/dialplan_functions.xml does not
  pose this issue but is also being renamed
  to adhere to this policy.

  Resolves: #539

- ### Revert "core & res_pjsip: Improve topology change handling."                    
  Author: George Joseph  
  Date:   2024-01-12  

  This reverts commit 315eb551dbd18ecd424a2f32179d4c1f6f6edd26.

  Over the past year, we've had several reports of "topology storms"
  occurring where 2 external facing channels connected by one or more
  local channels and bridges will get themselves in a state where
  they continually send each other topology change requests.  This
  usually manifests itself in no-audio calls and a flood of
  "Exceptionally long queue length" messages.  It appears that this
  commit is the cause so we're reverting it for now until we can
  determine a more appropriate solution.

  Resolves: #530

- ### menuselect: Use more specific error message.                                    
  Author: Naveen Albert  
  Date:   2024-01-04  

  Instead of using the same error message for
  missing dependencies and conflicts, be specific
  about what actually went wrong.

  Resolves: #520

- ### res_pjsip_nat: Fix potential use of uninitialized transport details             
  Author: Maximilian Fridrich  
  Date:   2024-01-08  

  The ast_sip_request_transport_details must be zero initialized,
  otherwise this could lead to a SEGV.

  Resolves: #509

- ### app_if: Fix faulty EndIf branching.                                             
  Author: Naveen Albert  
  Date:   2023-12-23  

  This fixes faulty branching logic for the
  EndIf application. Instead of computing
  the next priority, which should be done
  for false conditionals or ExitIf, we should
  simply advance to the next priority.

  Resolves: #341

- ### manager.c: Fix regression due to using wrong free function.                     
  Author: Naveen Albert  
  Date:   2023-12-26  

  Commit 424be345639d75c6cb7d0bd2da5f0f407dbd0bd5 introduced
  a regression by calling ast_free on memory allocated by
  realpath. This causes Asterisk to abort when executing this
  function. Since the memory is allocated by glibc, it should
  be freed using ast_std_free.

  Resolves: #513

- ### config_options.c: Fix truncation of option descriptions.                        
  Author: Naveen Albert  
  Date:   2023-11-09  

  This increases the format width of option descriptions
  to avoid needless truncation for longer descriptions.

  Resolves: #428

- ### manager.c: Improve clarity of "manager show connected".                         
  Author: Naveen Albert  
  Date:   2023-12-05  

  Improve the "manager show connected" CLI command
  to clarify that the last two columns are permissions
  related, not counts, and use sufficient widths
  to consistently display these values.

  ASTERISK-30143 #close
  Resolves: #482


- ### make_xml_documentation: Really collect LOCAL_MOD_SUBDIRS documentation.         
  Author: Sean Bright  
  Date:   2023-12-01  

  Although `make_xml_documentation`'s `print_dependencies` command was
  corrected by the previous fix (#461) for #142, the `create_xml` was
  not properly handling `LOCAL_MOD_SUBDIRS` XML documentation.


- ### general: Fix broken links.                                                      
  Author: Naveen Albert  
  Date:   2023-11-09  

  This fixes a number of broken links throughout the
  tree, mostly caused by wiki.asterisk.org being replaced
  with docs.asterisk.org, which should eliminate the
  need for sporadic fixes as in f28047db36a70e81fe373a3d19132c43adf3f74b.

  Resolves: #430

- ### MergeApproved.yml:  Remove unneeded concurrency                                 
  Author: George Joseph  
  Date:   2023-12-06  

  The concurrency parameter on the MergeAndCherryPick job has
  been rmeoved.  It was a hold-over from earlier days.


- ### app_dial: Add option "j" to preserve initial stream topology of caller          
  Author: Maximilian Fridrich  
  Date:   2023-11-30  

  Resolves: #462

  UserNote: The option "j" is now available for the Dial application which
  uses the initial stream topology of the caller to create the outgoing
  channels.


- ### ast_coredumper: Increase reliability                                            
  Author: George Joseph  
  Date:   2023-11-11  

  Instead of searching for the asterisk binary and the modules in the
  filesystem, we now get their locations, along with libdir, from
  the coredump itself...

  For the binary, we can use `gdb -c <coredump> ... "info proc exe"`.
  gdb can print this even without having the executable and symbols.

  Once we have the binary, we can get the location of the modules with
  `gdb ... "print ast_config_AST_MODULE_DIR`

  If there was no result then either it's not an asterisk coredump
  or there were no symbols loaded.  Either way, it's not usable.

  For libdir, we now run "strings" on the note0 section of the
  coredump (which has the shared library -> memory address xref) and
  search for "libasteriskssl|libasteriskpj", then take the dirname.

  Since we're now getting everything from the coredump, it has to be
  correct as long as we're not crossing namespace boundaries like
  running asterisk in a docker container but trying to run
  ast_coredumper from the host using a shared file system (which you
  shouldn't be doing).

  There is still a case for using --asterisk-bin and/or --libdir: If
  you've updated asterisk since the coredump was taken, the binary,
  libraries and modules won't match the coredump which will render it
  useless.  If you can restore or rebuild the original files that
  match the coredump and place them in a temporary directory, you can
  use --asterisk-bin, --libdir, and a new --moddir option to point to
  them and they'll be correctly captured in a tarball created
  with --tarball-coredumps.  If you also use --tarball-config, you can
  use a new --etcdir option to point to what normally would be the
  /etc/asterisk directory.

  Also addressed many "shellcheck" findings.

  Resolves: #445

- ### logger.c: Move LOG_GROUP documentation to dedicated XML file.                   
  Author: Sean Bright  
  Date:   2023-12-01  

  The `get_documentation` awk script will only extract the first
  DOCUMENTATION block that it finds in a given file. This is by design
  (9bc2127) to prevent AMI event documentation from being pulled in to
  the core.xml documentation file.

  Because of this, the `LOG_GROUP` documentation added in 89709e2 was
  not being properly extracted and was missing fom the resulting XML
  documentation file. This commit moves the `LOG_GROUP` documentation to
  a separate `logger.xml` file.


- ### res_odbc.c: Allow concurrent access to request odbc connections                 
  Author: Matthew Fredrickson  
  Date:   2023-11-30  

  There are valid scenarios where res_odbc's connection pool might have some dead
  or stuck connections while others are healthy (imagine network
  elements/firewalls/routers silently timing out connections to a single DB and a
  single IP address, or a heterogeneous connection pool connected to potentially
  multiple IPs/instances of a replicated DB using a DNS front end for load
  balancing and one replica fails).

  In order to time out those unhealthy connections without blocking access to
  other parts of Asterisk that may attempt access to the connection pool, it would
  be beneficial to not lock/block access around the entire pool in
  _ast_odbc_request_obj2 while doing potentially blocking operations on connection
  pool objects such as the connection_dead() test, odbc_obj_connect(), or by
  dereferencing a struct odbc_obj for the last time and triggering a
  odbc_obj_disconnect().

  This would facilitate much quicker and concurrent timeout of dead connections
  via the connection_dead() test, which could block potentially for a long period
  of time depending on odbc.ini or other odbc connector specific timeout settings.

  This also would make rapid failover (in the clustered DB scenario) much quicker.

  This patch changes the locking in _ast_odbc_request_obj2() to not lock around
  odbc_obj_connect(), _disconnect(), and connection_dead(), while continuing to
  lock around truly shared, non-immutable state like the connection_cnt member and
  the connections list on struct odbc_class.

  Fixes: #465

- ### res_pjsip_header_funcs.c: Check URI parameter length before copying.            
  Author: Sean Bright  
  Date:   2023-12-04  

  Fixes #477


- ### config.c: Log #exec include failures.                                           
  Author: Sean Bright  
  Date:   2023-11-22  

  If the script referenced by `#exec` does not exist, writes anything to
  stderr, or exits abnormally or with a non-zero exit status, we log
  that to Asterisk's error logging channel.

  Additionally, write out a warning if the script produces no output.

  Fixes #259


- ### make_xml_documentation: Properly handle absolute LOCAL_MOD_SUBDIRS.             
  Author: Sean Bright  
  Date:   2023-11-27  

  If LOCAL_MOD_SUBDIRS contains absolute paths, do not prefix them with
  the path to Asterisk's source tree.

  Fixes #142


- ### app_voicemail.c: Completely resequence mailbox folders.                         
  Author: Sean Bright  
  Date:   2023-11-27  

  Resequencing is a process that occurs when we open a voicemail folder
  and discover that there are gaps between messages (e.g. `msg0000.txt`
  is missing but `msg0001.txt` exists). Resequencing involves shifting
  the existing messages down so we end up with a sequential list of
  messages.

  Currently, this process stops after reaching a threshold based on the
  message limit (`maxmsg`) configured on the current folder. However, if
  `maxmsg` is lowered when a voicemail folder contains more than
  `maxmsg + 10` messages, resequencing will not run completely leaving
  the mailbox in an inconsistent state.

  We now resequence up to the maximum number of messages permitted by
  `app_voicemail` (currently hard-coded at 9999 messages).

  Fixes #86


- ### sig_analog: Fix channel leak when mwimonitor is enabled.                        
  Author: Naveen Albert  
  Date:   2023-11-24  

  When mwimonitor=yes is enabled for an FXO port,
  the do_monitor thread will launch mwi_thread if it thinks
  there could be MWI on an FXO channel, due to the noise
  threshold being satisfied. This, in turns, calls
  analog_ss_thread_start in sig_analog. However, unlike
  all other instances where __analog_ss_thread is called
  in sig_analog, this call path does not properly set
  pvt->ss_astchan to the Asterisk channel, which means
  that the Asterisk channel is NULL when __analog_ss_thread
  starts executing. As a result, the thread exits and the
  channel is never properly cleaned up by calling ast_hangup.

  This caused issues with do_monitor on incoming calls,
  as it would think the channel was still owned even while
  receiving events, leading to an infinite barrage of
  warning messages; additionally, the channel would persist
  improperly.

  To fix this, the assignment is added to the call path
  where it is missing (which is only used for mwi_thread).
  A warning message is also added since previously there
  was no indication that __analog_ss_thread was exiting
  abnormally. This resolves both the channel leak and the
  condition that led to the warning messages.

  Resolves: #458

- ### res_rtp_asterisk.c: Update for OpenSSL 3+.                                      
  Author: Sean Bright  
  Date:   2023-11-20  

  In 5ac5c2b0 we defined `OPENSSL_SUPPRESS_DEPRECATED` to silence
  deprecation warnings. This commit switches over to using
  non-deprecated API.


- ### alembic: Update list of TLS methods available on ps_transports.                 
  Author: Sean Bright  
  Date:   2023-11-14  

  Related to #221 and #222.

  Also adds `*.ini` to the `.gitignore` file in ast-db-manage for
  convenience.


- ### func_channel: Expose previously unsettable options.                             
  Author: Naveen Albert  
  Date:   2023-11-11  

  Certain channel options are not set anywhere or
  exposed in any way to users, making them unusable.
  This exposes some of these options which make sense
  for users to manipulate at runtime.

  Resolves: #442

- ### app.c: Allow ampersands in playback lists to be escaped.                        
  Author: Sean Bright  
  Date:   2023-11-07  

  Any function or application that accepts a `&`-separated list of
  filenames can now include a literal `&` in a filename by wrapping the
  entire filename in single quotes, e.g.:

  ```
  exten = _X.,n,Playback('https://example.com/sound.cgi?a=b&c=d'&hello-world)
  ```

  Fixes #172

  UpgradeNote: Ampersands in URLs passed to the `Playback()`,
  `Background()`, `SpeechBackground()`, `Read()`, `Authenticate()`, or
  `Queue()` applications as filename arguments can now be escaped by
  single quoting the filename. Additionally, this is also possible when
  using the `CONFBRIDGE` dialplan function, or configuring various
  features in `confbridge.conf` and `queues.conf`.


- ### uri.c: Simplify ast_uri_make_host_with_port()                                   
  Author: Sean Bright  
  Date:   2023-11-09  


- ### func_curl.c: Remove CURLOPT() plaintext documentation.                          
  Author: Sean Bright  
  Date:   2023-11-13  

  I assume this was missed when initially converting to XML
  documentation and we've been kicking the can down the road since.


- ### res_http_websocket.c: Set hostname on client for certificate validation.        
  Author: Sean Bright  
  Date:   2023-11-09  

  Additionally add a `assert()` to in the TLS client setup code to
  ensure that hostname is set when it is supposed to be.

  Fixes #433


- ### live_ast: Add astcachedir to generated asterisk.conf.                           
  Author: Sean Bright  
  Date:   2023-11-09  

  `astcachedir` (added in b0842713) was not added to `live_ast` so
  continued to point to the system `/var/cache` directory instead of the
  one in the live environment.


- ### SECURITY.md: Update with correct documentation URL                              
  Author: George Joseph  
  Date:   2023-11-09  


- ### func_lock: Add missing see-also refs to documentation.                          
  Author: Naveen Albert  
  Date:   2023-11-09  

  Resolves: #423

- ### app_followme.c: Grab reference on nativeformats before using it                 
  Author: Matthew Fredrickson  
  Date:   2023-10-25  

  Fixes a crash due to a lack of proper reference on the nativeformats
  object before passing it into ast_request().  Also found potentially
  similar use case bugs in app_chanisavail.c, bridge.c, and bridge_basic.c

  Fixes: #388

- ### configs: Improve documentation for bandwidth in iax.conf.                       
  Author: Naveen Albert  
  Date:   2023-11-09  

  This improves the documentation for the bandwidth setting
  in iax.conf by making it clearer what the ramifications
  of this setting are. It also changes the sample default
  from low to high, since only high is compatible with good
  codecs that people will want to use in the vast majority
  of cases, and this is a common gotcha that trips up new users.

  Resolves: #425

- ### logger: Add channel-based filtering.                                            
  Author: Naveen Albert  
  Date:   2023-08-09  

  This adds the ability to filter console
  logging by channel or groups of channels.
  This can be useful on busy systems where
  an administrator would like to analyze certain
  calls in detail. A dialplan function is also
  included for the purpose of assigning a channel
  to a group (e.g. by tenant, or some other metric).

  ASTERISK-30483 #close

  Resolves: #242

  UserNote: The console log can now be filtered by
  channels or groups of channels, using the
  logger filter CLI commands.


- ### chan_iax2.c: Don't send unsanitized data to the logger.                         
  Author: Sean Bright  
  Date:   2023-11-08  

  This resolves an issue where non-printable characters could be sent to
  the console/log files.


- ### codec_ilbc: Disable system ilbc if version >= 3.0.0                             
  Author: George Joseph  
  Date:   2023-11-07  

  Fedora 37 started shipping ilbc 3.0.4 which we don't yet support.
  configure.ac now checks the system for "libilbc < 3" instead of
  just "libilbc".  If true, the system version of ilbc will be used.
  If not, the version included at codecs/ilbc will be used.

  Resolves: #84

- ### resource_channels.c: Explicit codec request when creating UnicastRTP.           
  Author: Sean Bright  
  Date:   2023-11-06  

  Fixes #394


- ### doc: Update IP Quality of Service links.                                        
  Author: Sean Bright  
  Date:   2023-11-07  

  Fixes #328


- ### chan_pjsip: Add PJSIPHangup dialplan app and manager action                     
  Author: George Joseph  
  Date:   2023-10-31  

  See UserNote below.

  Exposed the existing Hangup AMI action in manager.c so we can use
  all of it's channel search and AMI protocol handling without
  duplicating that code in dialplan_functions.c.

  Added a lookup function to res_pjsip.c that takes in the
  string represenation of the pjsip_status_code enum and returns
  the actual status code.  I.E.  ast_sip_str2rc("DECLINE") returns
  603.  This allows the caller to specify PJSIPHangup(decline) in
  the dialplan, just like Hangup(call_rejected).

  Also extracted the XML documentation to its own file since it was
  almost as large as the code itself.

  UserNote: A new dialplan app PJSIPHangup and AMI action allows you
  to hang up an unanswered incoming PJSIP call with a specific SIP
  response code in the 400 -> 699 range.


- ### chan_iax2.c: Ensure all IEs are displayed when dumping frame contents.          
  Author: Sean Bright  
  Date:   2023-11-06  

  When IAX2 debugging was enabled (`iax2 set debug on`), if the last IE
  in a frame was one that may not have any data - such as the CALLTOKEN
  IE in an NEW request - it was not getting displayed.


- ### chan_dahdi: Warn if nonexistent cadence is requested.                           
  Author: Naveen Albert  
  Date:   2023-11-02  

  If attempting to ring a channel using a nonexistent cadence,
  emit a warning, before falling back to the default cadence.

  Resolves: #409

- ### stasis: Update the snapshot after setting the redirect                          
  Author: Holger Hans Peter Freyther  
  Date:   2023-10-21  

  The previous commit added the caller_rdnis attribute. Make it
  avialble during a possible ChanngelHangupRequest.


- ### ari: Provide the caller ID RDNIS for the channels                               
  Author: Holger Hans Peter Freyther  
  Date:   2023-10-14  

  Provide the caller ID RDNIS when available. This will allow an
  application to follow the redirect.


- ### main/utils: Implement ast_get_tid() for OpenBSD                                 
  Author: Brad Smith  
  Date:   2023-11-01  

  Implement the ast_get_tid() function for OpenBSD. OpenBSD supports
  getting the TID via getthrid().


- ### res_rtp_asterisk.c: Fix runtime issue with LibreSSL                             
  Author: Brad Smith  
  Date:   2023-11-02  

  The module will fail to load. Use proper function DTLS_method() with LibreSSL.


- ### app_directory: Add ADSI support to Directory.                                   
  Author: Naveen Albert  
  Date:   2023-09-27  

  This adds optional ADSI support to the Directory
  application, which allows callers with ADSI CPE
  to navigate the Directory system significantly
  faster than is possible using the audio prompts.
  Callers can see the directory name (and optionally
  extension) on their screenphone and confirm or
  reject a match immediately rather than waiting
  for it to be spelled out, enhancing usability.

  Resolves: #356

- ### core_local: Fix local channel parsing with slashes.                             
  Author: Naveen Albert  
  Date:   2023-08-09  

  Currently, trying to call a Local channel with a slash
  in the extension will fail due to the parsing of characters
  after such a slash as being dial modifiers. Additionally,
  core_local is inconsistent and incomplete with
  its parsing of Local dial strings in that sometimes it
  uses the first slash and at other times it uses the last.

  For instance, something like DAHDI/5 or PJSIP/device
  is a perfectly usable extension in the dialplan, but Local
  channels in particular prevent these from being called.

  This creates inconsistent behavior for users, since using
  a slash in an extension is perfectly acceptable, and using
  a Goto to accomplish this works fine, but if specified
  through a Local channel, the parsing prevents this.

  This fixes this by explicitly parsing options from the
  last slash in the extension, rather than the first one,
  which doesn't cause an issue for extensions with slashes.

  ASTERISK-30013 #close

  Resolves: #248

- ### Remove files that are no longer updated                                         
  Author: Mark Murawski  
  Date:   2023-10-30  

  Fixes: #360

- ### app_voicemail: Add AMI event for mailbox PIN changes.                           
  Author: Naveen Albert  
  Date:   2023-10-30  

  This adds an AMI event that is emitted whenever a
  mailbox password is successfully changed, allowing
  AMI consumers to process these.

  UserNote: The VoicemailPasswordChange event is
  now emitted whenever a mailbox password is updated,
  containing the mailbox information and the new
  password.

  Resolves: #398

- ### app_queue.c: Emit unpause reason with PauseQueueMember event.                   
  Author: Sean Bright  
  Date:   2023-10-30  

  Fixes #395


- ### bridge_simple: Suppress unchanged topology change requests                      
  Author: George Joseph  
  Date:   2023-10-30  

  In simple_bridge_join, we were sending topology change requests
  even when the new and old topologies were the same.  In some
  circumstances, this can cause unnecessary re-invites and even
  a re-invite flood.  We now suppress those.

  Resolves: #384

- ### res_pjsip: Include cipher limit in config error message.                        
  Author: Naveen Albert  
  Date:   2023-10-30  

  If too many ciphers are specified in the PJSIP config,
  include the maximum number of ciphers that may be
  specified in the user-facing error message.

  Resolves: #396

- ### res_speech: allow speech to translate input channel                             
  Author: Mike Bradeen  
  Date:   2023-09-07  

  * Allow res_speech to translate the input channel if the
    format is translatable to a format suppored by the
    speech provider.

  Resolves: #129

  UserNote: res_speech now supports translation of an input channel
  to a format supported by the speech provider, provided a translation
  path is available between the source format and provider capabilites.


- ### res_rtp_asterisk.c: Fix memory leak in ephemeral certificate creation.          
  Author: Sean Bright  
  Date:   2023-10-25  

  Fixes #386


- ### res_pjsip_dtmf_info.c: Add 'INFO' to Allow header.                              
  Author: Sean Bright  
  Date:   2023-10-17  

  Fixes #376


- ### api.wiki.mustache: Fix indentation in generated markdown                        
  Author: George Joseph  
  Date:   2023-10-25  

  The '*' list indicator for default values and allowable values for
  path, query and POST parameters need to be indented 4 spaces
  instead of 2.

  Should resolve issue 38 in the documentation repo.


- ### pjsip_configuration.c: Disable DTLS renegotiation if WebRTC is enabled.         
  Author: Sean Bright  
  Date:   2023-10-23  

  Per RFC8827:

      Implementations MUST NOT implement DTLS renegotiation and MUST
      reject it with a "no_renegotiation" alert if offered.

  So we disable it when webrtc=yes is set.

  Fixes #378

  UpgradeNote: The dtls_rekey will be disabled if webrtc support is
  requested on an endpoint. A warning will also be emitted.


- ### configs: Fix typo in pjsip.conf.sample.                                         
  Author: Samuel Olaechea  
  Date:   2023-10-12  


- ### res_pjsip_exten_state,res_pjsip_mwi: Allow unload on shutdown                   
  Author: George Joseph  
  Date:   2023-10-19  

  Commit f66f77f last year prevents the res_pjsip_exten_state and
  res_pjsip_mwi modules from unloading due to possible pjproject
  asserts if the modules are reloaded. A side effect of the
  implementation is that the taskprocessors these modules use aren't
  being released. When asterisk is doing a graceful shutdown, it
  waits AST_TASKPROCESSOR_SHUTDOWN_MAX_WAIT seconds for all
  taskprocessors to stop but since those 2 modules don't release
  theirs, the shutdown hangs for that amount of time.

  This change allows the modules to be unloaded and their resources to
  be released when ast_shutdown_final is true.

  Resolves: #379

- ### res_pjsip: Expanding PJSIP endpoint ID and relevant resource length to 255 cha..
  Author: sungtae kim  
  Date:   2023-09-23  

  This commit introduces an extension to the endpoint and relevant
  resource sizes for PJSIP, transitioning from its current 40-character
  constraint to a more versatile 255-character capacity. This enhancement
  significantly overcomes limitations related to domain qualification and
  practical usage, ultimately delivering improved functionality. In
  addition, it includes adjustments to accommodate the expanded realm size
  within the ARI, specifically enhancing the maximum realm length.

  Resolves: #345

  UserNote: With this update, the PJSIP realm lengths have been extended
  to support up to 255 characters.

  UpgradeNote: As part of this update, the maximum allowable length
  for PJSIP endpoints and relevant resources has been increased from
  40 to 255 characters. To take advantage of this enhancement, it is
  recommended to run the necessary procedures (e.g., Alembic) to
  update your schemas.


- ### res_stasis: signal when new command is queued                                   
  Author: Mike Bradeen  
  Date:   2023-10-02  

  res_statsis's app loop sleeps for up to .2s waiting on input
  to a channel before re-checking the command queue. This can
  cause delays between channel setup and bridge.

  This change is to send a SIGURG on the sleeping thread when
  a new command is enqueued. This exits the sleeping thread out
  of the ast_waitfor() call triggering the new command being
  processed on the channel immediately.

  Resolves: #362

  UserNote: Call setup times should be significantly improved
  when using ARI.


- ### ari/stasis: Indicate progress before playback on a bridge                       
  Author: Holger Hans Peter Freyther  
  Date:   2023-10-02  

  Make it possible to start a playback and the calling party
  to receive audio on a bridge before the call is connected.

  Model the implementation after play_on_channel and deliver a
  AST_CONTROL_PROGRESS before starting the playback.

  For a PJSIP channel this will result in sending a SIP 183
  Session Progress.


- ### func_curl.c: Ensure channel is locked when manipulating datastores.             
  Author: Sean Bright  
  Date:   2023-10-09  


- ### Update config.yml                                                               
  Author: Joshua C. Colp  
  Date:   2023-06-15  


- ### logger.h: Add ability to change the prefix on SCOPE_TRACE output                
  Author: George Joseph  
  Date:   2023-10-05  

  You can now define the _TRACE_PREFIX_ macro to change the
  default trace line prefix of "file:line function" to
  something else.  Full documentation in logger.h.


- ### Add libjwt to third-party                                                       
  Author: George Joseph  
  Date:   2023-09-21  

  The current STIR/SHAKEN implementation is not currently usable due
  to encryption issues. Rather than trying to futz with OpenSSL and
  the the current code, we can take advantage of the existing
  capabilities of libjwt but we first need to add it to the
  third-party infrastructure already in place for jansson and
  pjproject.

  A few tweaks were also made to the third-party infrastructure as
  a whole.  The jansson "dest" install directory was renamed "dist"
  to better match convention, and the third-party Makefile was updated
  to clean all product directories not just the ones currently in
  use.

  Resolves: #349

- ### res_pjsip: update qualify_timeout documentation with DNS note                   
  Author: Mike Bradeen  
  Date:   2023-09-26  

  The documentation on qualify_timeout does not explicitly state that the timeout
  includes any time required to perform any needed DNS queries on the endpoint.

  If the OPTIONS response is delayed due to the DNS query, it can still render an
  endpoint as Unreachable if the net time is enough for qualify_timeout to expire.

  Resolves: #352

- ### chan_dahdi: Clarify scope of callgroup/pickupgroup.                             
  Author: Naveen Albert  
  Date:   2023-09-04  

  Internally, chan_dahdi only applies callgroup and
  pickupgroup to FXO signalled channels, but this is
  not documented anywhere. This is now documented in
  the sample config, and a warning is emitted if a
  user tries configuring these settings for channel
  types that do not support these settings, since they
  will not have any effect.

  Resolves: #294

- ### func_json: Fix crashes for some types                                           
  Author: Bastian Triller  
  Date:   2023-09-21  

  This commit fixes crashes in JSON_DECODE() for types null, true, false
  and real numbers.

  In addition it ensures that a path is not deeper than 32 levels.

  Also allow root object to be an array.

  Add unit tests for above cases.


- ### res_speech_aeap: add aeap error handling                                        
  Author: Mike Bradeen  
  Date:   2023-09-21  

  res_speech_aeap previously did not register an error handler
  with aeap, so it was not notified of a disconnect. This resulted
  in SpeechBackground never exiting upon a websocket disconnect.

  Resolves: #303

- ### app_voicemail: Disable ADSI if unavailable.                                     
  Author: Naveen Albert  
  Date:   2023-09-27  

  If ADSI is available on a channel, app_voicemail will repeatedly
  try to use ADSI, even if there is no CPE that supports it. This
  leads to many unnecessary delays during the session. If ADSI is
  available but ADSI setup fails, we now disable it to prevent
  further attempts to use ADSI during the session.

  Resolves: #354

- ### codec_builtin: Use multiples of 20 for maximum_ms                               
  Author: Eduardo  
  Date:   2023-07-28  

  Some providers require a multiple of 20 for the maxptime or fail to complete calls,
  e.g. Vivo in Brazil. To increase compatibility, only multiples of 20 are now used.

  Resolves: #260

- ### lock.c: Separate DETECT_DEADLOCKS from DEBUG_THREADS                            
  Author: George Joseph  
  Date:   2023-09-13  

  Previously, DETECT_DEADLOCKS depended on DEBUG_THREADS.
  Unfortunately, DEBUG_THREADS adds a lot of lock tracking overhead
  to all of the lock lifecycle calls whereas DETECT_DEADLOCKS just
  causes the lock calls to loop over trylock in 200us intervals until
  the lock is obtained and spits out log messages if it takes more
  than 5 seconds.  From a code perspective, the only reason they were
  tied together was for logging.  So... The ifdefs in lock.c were
  refactored to allow DETECT_DEADLOCKS to be enabled without
  also enabling DEBUG_THREADS.

  Resolves: #321

  UserNote: You no longer need to select DEBUG_THREADS to use
  DETECT_DEADLOCKS.  This removes a significant amount of overhead
  if you just want to detect possible deadlocks vs needing full
  lock tracing.


- ### asterisk.c: Use the euid's home directory to read/write cli history             
  Author: George Joseph  
  Date:   2023-09-15  

  The CLI .asterisk_history file is read from/written to the directory
  specified by the HOME environment variable. If the root user starts
  asterisk with the -U/-G options, or with runuser/rungroup set in
  asterisk.conf, the asterisk process is started as root but then it
  calls setuid/setgid to set the new user/group. This does NOT reset
  the HOME environment variable to the new user's home directory
  though so it's still left as "/root". In this case, the new user
  will almost certainly NOT have access to read from or write to the
  history file.

  * Added function process_histfile() which calls
    getpwuid(geteuid()) and uses pw->dir as the home directory
    instead of the HOME environment variable.
  * ast_el_read_default_histfile() and ast_el_write_default_histfile()
    have been modified to use the new process_histfile()
    function.

  Resolves: #337

- ### res_pjsip_transport_websocket: Prevent transport from being destroyed before m..
  Author: Tinet-mucw  
  Date:   2023-09-13  

  From the gdb information, ast_websocket_read reads a message successfully,
  then transport_read is called in the serializer. During execution of pjsip_transport_down,
  ws_session->stream->fd is closed; ast_websocket_read encounters an error and exits the while loop.
  After executing transport_shutdown, the transport's reference count becomes 0, causing a crash when sending SIP messages.
  This was due to pjsip_transport_dec_ref executing earlier than pjsip_rx_data_clone, leading to this issue.
  In websocket_cb executeing pjsip_transport_add_ref, this we now ensure the transport is not destroyed while in the loop.

  Resolves: asterisk#299

- ### cel: add publish user event helper                                              
  Author: Mike Bradeen  
  Date:   2023-09-14  

  Add a wrapper function around ast_cel_publish_event that
  packs event and extras into a blob before publishing

  Resolves:#330

- ### chan_console: Fix deadlock caused by unclean thread exit.                       
  Author: Naveen Albert  
  Date:   2023-09-09  

  To terminate a console channel, stop_stream causes pthread_cancel
  to make stream_monitor exit. However, commit 5b8fea93d106332bc0faa4b7fa8a6ea71e546cac
  added locking to this function which results in deadlock due to
  the stream_monitor thread being killed while it's holding the pvt lock.

  To resolve this, a flag is now set and read to indicate abort, so
  the use of pthread_cancel and pthread_kill can be avoided altogether.

  Resolves: #308

- ### file.c: Add ability to search custom dir for sounds                             
  Author: George Joseph  
  Date:   2023-09-11  

  To better co-exist with sounds files that may be managed by
  packages, custom sound files may now be placed in
  AST_DATA_DIR/sounds/custom instead of the standard
  AST_DATA_DIR/sounds/<lang> directory.  If the new
  "sounds_search_custom_dir" option in asterisk.conf is set
  to "true", asterisk will search the custom directory for sounds
  files before searching the standard directory.  For performance
  reasons, the "sounds_search_custom_dir" defaults to "false".

  Resolves: #315

  UserNote: A new option "sounds_search_custom_dir" has been added to
  asterisk.conf that allows asterisk to search
  AST_DATA_DIR/sounds/custom for sounds files before searching the
  standard AST_DATA_DIR/sounds/<lang> directory.


- ### chan_iax2: Improve authentication debugging.                                    
  Author: Naveen Albert  
  Date:   2023-08-30  

  Improves and adds some logging to make it easier
  for users to debug authentication issues.

  Resolves: #286

- ### res_rtp_asterisk: fix wrong counter management in ioqueue objects               
  Author: Vitezslav Novy  
  Date:   2023-09-05  

  In function  rtp_ioqueue_thread_remove counter in ioqueue object is not decreased
  which prevents unused ICE TURN threads from being removed.

  Resolves: #301

- ### make_buildopts_h, et. al.  Allow adding all cflags to buildopts.h               
  Author: George Joseph  
  Date:   2023-09-13  

  The previous behavior of make_buildopts_h was to not add the
  non-ABI-breaking MENUSELECT_CFLAGS like DETECT_DEADLOCKS,
  REF_DEBUG, etc. to the buildopts.h file because "it caused
  ccache to invalidate files and extended compile times". They're
  only defined by passing them on the gcc command line with '-D'
  options.   In practice, including them in the include file rarely
  causes any impact because the only time ccache cares is if you
  actually change an option so the hit occurrs only once after
  you change it.

  OK so why would we want to include them?  Many IDEs follow the
  include files to resolve defines and if the options aren't in an
  include file, it can cause the IDE to mark blocks of "ifdeffed"
  code as unused when they're really not.

  So...

  * Added a new menuselect compile option ADD_CFLAGS_TO_BUILDOPTS_H
    which tells make_buildopts_h to include the non-ABI-breaking
    flags in buildopts.h as well as the ABI-breaking ones. The default
    is disabled to preserve current behavior.  As before though,
    only the ABI-breaking flags appear in AST_BUILDOPTS and only
    those are used to calculate AST_BUILDOPT_SUM.
    A new AST_BUILDOPT_ALL define was created to capture all of the
    flags.

  * make_version_c was streamlined to use buildopts.h and also to
    create asterisk_build_opts_all[] and ast_get_build_opts_all(void)

  * "core show settings" now shows both AST_BUILDOPTS and
    AST_BUILDOPTS_ALL.

  UserNote: The "Build Options" entry in the "core show settings"
  CLI command has been renamed to "ABI related Build Options" and
  a new entry named "All Build Options" has been added that shows
  both breaking and non-breaking options.


- ### func_periodic_hook: Add hangup step to avoid timeout                            
  Author: Mike Bradeen  
  Date:   2023-09-12  

  func_periodic_hook does not hangup after playback, relying on hangup
  which keeps the channel alive longer than necessary.

  Resolves: #325

- ### res_stasis_recording.c: Save recording state when unmuted.                      
  Author: Sean Bright  
  Date:   2023-09-12  

  Fixes #322


- ### res_speech_aeap: check for null format on response                              
  Author: Mike Bradeen  
  Date:   2023-09-08  

  * Fixed issue in res_speech_aeap when unable to provide an
    input format to check against.


- ### func_periodic_hook: Don't truncate channel name                                 
  Author: George Joseph  
  Date:   2023-09-11  

  func_periodic_hook was truncating long channel names which
  causes issues when you need to run other dialplan functions/apps
  on the channel.

  Resolves: #319

- ### safe_asterisk: Change directory permissions to 755                              
  Author: George Joseph  
  Date:   2023-09-11  

  If the safe_asterisk script detects that the /var/lib/asterisk
  directory doesn't exist, it now creates it with 755 permissions
  instead of 770.  safe_asterisk needing to create that directory
  should be extremely rare though because it's normally created
  by 'make install' which already sets the permissions to 755.

  Resolves: #316

- ### chan_rtp: Implement RTP glue for UnicastRTP channels                            
  Author: Maximilian Fridrich  
  Date:   2023-09-05  

  Resolves: #298

  UserNote: The dial string option 'g' was added to the UnicastRTP channel
  which enables RTP glue and therefore native RTP bridges with those
  channels.


- ### app_queue: periodic announcement configurable start time.                       
  Author: Jaco Kroon  
  Date:   2023-02-21  

  This newly introduced periodic-announce-startdelay makes it possible to
  configure the initial start delay of the first periodic announcement
  after which periodic-announce-frequency takes over.

  UserNote: Introduce a new queue configuration option called
  'periodic-announce-startdelay' which will vary the normal (historic)
  behavior of starting the periodic announcement cycle at
  periodic-announce-frequency seconds after entering the queue to start
  the periodic announcement cycle at period-announce-startdelay seconds
  after joining the queue.  The default behavior if this config option is
  not set remains unchanged.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### variables: Add additional variable dialplan functions.                          
  Author: Joshua C. Colp  
  Date:   2023-08-31  

  Using the Set dialplan application does not actually
  delete channel or global variables. Instead the
  variables are set to an empty value.

  This change adds two dialplan functions,
  GLOBAL_DELETE and DELETE which can be used to
  delete global and channel variables instead
  of just setting them to empty.

  There is also no ability within the dialplan to
  determine if a global or channel variable has
  actually been set or not.

  This change also adds two dialplan functions,
  GLOBAL_EXISTS and VARIABLE_EXISTS which can be
  used to determine if a global or channel variable
  has been set or not.

  Resolves: #289

  UserNote: Four new dialplan functions have been added.
  GLOBAL_DELETE and DELETE have been added which allows
  the deletion of global and channel variables.
  GLOBAL_EXISTS and VARIABLE_EXISTS have been added
  which checks whether a global or channel variable has
  been set.


- ### Restore CHANGES and UPGRADE.txt to allow cherry-picks to work                   
  Author: George Joseph  
  Date:   2024-01-12  


- ### res_rtp_asterisk: Fix regression issues with DTLS client check                  
  Author: George Joseph  
  Date:   2023-12-15  

  * Since ICE candidates are used for the check and pjproject is
    required to use ICE, res_rtp_asterisk was failing to compile
    when pjproject wasn't available.  The check is now wrapped
    with an #ifdef HAVE_PJPROJECT.

  * The rtp->ice_active_remote_candidates container was being
    used to check the address on incoming packets but that
    container doesn't contain peer reflexive candidates discovered
    during negotiation. This was causing the check to fail
    where it shouldn't.  We now check against pjproject's
    real_ice->rcand array which will contain those candidates.

  * Also fixed a bug in ast_sockaddr_from_pj_sockaddr() where
    we weren't zeroing out sin->sin_zero before returning.  This
    was causing ast_sockaddr_cmp() to always return false when
    one of the inputs was converted from a pj_sockaddr, even
    if both inputs had the same address and port.

  Resolves: #500
  Resolves: #503
  Resolves: #505

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

- ### res_pjsip: disable raw bad packet logging                                       
  Author: Mike Bradeen  
  Date:   2023-07-25  

  Add patch to split the log level for invalid packets received on the
  signaling port.  The warning regarding the packet will move to level 2
  so that it can still be displayed, while the raw packet will be at level
  4.

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

- ### ari-stubs: Fix more local anchor references                                     
  Author: George Joseph  
  Date:   2023-09-05  

  Also allow CreateDocs job to be run manually with default branches.


- ### ari-stubs: Fix more local anchor references                                     
  Author: George Joseph  
  Date:   2023-09-05  

  Also allow CreateDocs job to be run manually with default branches.


- ### ari-stubs: Fix broken documentation anchors                                     
  Author: George Joseph  
  Date:   2023-09-05  

  All of the links that reference page anchors with capital letters in
  the ids (#Something) have been changed to lower case to match the
  anchors that are generated by mkdocs.


- ### res_pjsip_session: Send Session Interval too small response                     
  Author: Bastian Triller  
  Date:   2023-08-28  

  Handle session interval lower than endpoint's configured minimum timer
  when sending first answer. Timer setting is checked during this step and
  needs to handled appropriately.
  Before this change, no response was sent at all. After this change a
  response with 422 Session Interval too small is sent to UAC.


- ### app_dial: Fix infinite loop when sending digits.                                
  Author: Naveen Albert  
  Date:   2023-08-28  

  If the called party hangs up while digits are being
  sent, -1 is returned to indicate so, but app_dial
  was not checking the return value, resulting in
  the hangup being lost and looping forever until
  the caller manually hangs up the channel. We now
  abort if digit sending fails.

  ASTERISK-29428 #close

  Resolves: #281

- ### app_voicemail: Fix for loop declarations                                        
  Author: Mike Bradeen  
  Date:   2023-08-29  

  Resolve for loop initial declarations added in cli changes.

  Resolves: #275

- ### alembic: Fix quoting of the 100rel column                                       
  Author: George Joseph  
  Date:   2023-08-28  

  Add quoting around the ps_endpoints 100rel column in the ALTER
  statements.  Although alembic doesn't complain when generating
  sql statements, postgresql does (rightly so).

  Resolves: #274

- ### pbx.c: Fix gcc 12 compiler warning.                                             
  Author: Naveen Albert  
  Date:   2023-08-27  

  Resolves: #277

- ### app_audiosocket: Fixed timeout with -1 to avoid busy loop.                      
  Author: zhengsh  
  Date:   2023-08-24  

  Resolves: asterisk#234

- ### download_externals:  Fix a few version related issues                           
  Author: George Joseph  
  Date:   2023-08-18  

  * Fixed issue with the script not parsing the new tag format for
    certified releases.  The format changed from certified/18.9-cert5
    to certified-18.9-cert5.

  * Fixed issue where the asterisk version wasn't being considered
    when looking for cached versions.

  Resolves: #263

- ### main/refer.c: Fix double free in refer_data_destructor + potential leak         
  Author: Maximilian Fridrich  
  Date:   2023-08-21  

  Resolves: #267

- ### sig_analog: Add Called Subscriber Held capability.                              
  Author: Naveen Albert  
  Date:   2023-08-09  

  This adds support for Called Subscriber Held for FXS
  lines, which allows users to go on hook when receiving
  a call and resume the call later from another phone on
  the same line, without disconnecting the call. This is
  a convenience mechanism that most real PSTN telephone
  switches support.

  ASTERISK-30372 #close

  Resolves: #240

  UserNote: Called Subscriber Held is now supported for analog
  FXS channels, using the calledsubscriberheld option. This allows
  a station  user to go on hook when receiving an incoming call
  and resume from another phone on the same line by going on hook,
  without disconnecting the call.


- ### app_macro: Fix locking around datastore access                                  
  Author: Matthew Fredrickson  
  Date:   2023-08-21  

  app_macro sometimes would crash due to datastore list corruption on the
  channel because of lack of locking around find and create process for
  the macro datastore. This patch locks the channel lock prior to protect
  against this problem.

  Resolves: #265

- ### Revert "app_stack: Print proper exit location for PBXless channels."            
  Author: Matthew Fredrickson  
  Date:   2023-08-10  

  This reverts commit 617dad4cba1513dddce87b8e95a61415fb587cf1.

  apps/app_stack.c: Revert buggy gosub patch

  This seems to break the case when a predial macro calls a gosub.
  When the gosub calls return, the Return function outputs:

  app_stack.c:423 return_exec: Return without Gosub: stack is empty

  This returns -1 to the calling macro, which returns to app_dial
  and causes the call to hangup instead of proceeding with the macro
  that invoked the gosub.

  Resolves: #253

- ### install_prereq: Fix dependency install on aarch64.                              
  Author: Jason D. McCormick  
  Date:   2023-04-28  

  Fixes dependency solutions in install_prereq for Debian aarch64
  platforms. install_prereq was attempting to forcibly install 32-bit
  armhf packages due to the aptitude search for dependencies.

  Resolves: #37

- ### res_pjsip.c: Set contact_user on incoming call local Contact header             
  Author: MikeNaso  
  Date:   2023-08-08  

  If the contact_user is configured on the endpoint it will now be set on the local Contact header URI for incoming calls. The contact_user has already been set on the local Contact header URI for outgoing calls.

  Resolves: #226

- ### extconfig: Allow explicit DB result set ordering to be disabled.                
  Author: Sean Bright  
  Date:   2023-07-12  

  Added a new boolean configuration flag -
  `order_multi_row_results_by_initial_column` - to both res_pgsql.conf
  and res_config_odbc.conf that allows the administrator to disable the
  explicit `ORDER BY` that was previously being added to all generated
  SQL statements that returned multiple rows.

  Fixes: #179

- ### rest-api: Run make ari-stubs                                                    
  Author: George Joseph  
  Date:   2023-08-09  

  An earlier cherry-pick that involved rest-api somehow didn't include
  a comment change in res/ari/resource_endpoints.h.  This commit
  corrects that.  No changes other than the comment.


- ### res_pjsip_header_funcs: Make prefix argument optional.                          
  Author: Naveen Albert  
  Date:   2023-08-09  

  The documentation for PJSIP_HEADERS claims that
  prefix is optional, but in the code it is actually not.
  However, there is no inherent reason for this, as users
  may want to retrieve all header names, not just those
  beginning with a certain prefix.

  This makes the prefix optional for this function,
  simply fetching all header names if not specified.
  As a result, the documentation is now correct.

  Resolves: #230

  UserNote: The prefix argument to PJSIP_HEADERS is now
  optional. If not specified, all header names will be
  returned.


- ### pjproject_bundled: Increase PJSIP_MAX_MODULE to 38                              
  Author: George Joseph  
  Date:   2023-08-11  

  The default is 32 with 8 being used by pjproject itself.  Recent
  commits have put us over the limit resulting in assertions in
  pjproject.  Since this value is used in invites, dialogs,
  transports and subscriptions as well as the global pjproject
  endpoint, we don't want to increase it too much.

  Resolves: #255

- ### manager: Tolerate stasis messages with no channel snapshot.                     
  Author: Joshua C. Colp  
  Date:   2023-08-09  

  In some cases I have yet to determine some stasis messages may
  be created without a channel snapshot. This change adds some
  tolerance to this scenario, preventing a crash from occurring.


- ### core/ari/pjsip: Add refer mechanism                                             
  Author: Maximilian Fridrich  
  Date:   2023-05-10  

  This change adds support for refers that are not session based. It
  includes a refer implementation for the PJSIP technology which results
  in out-of-dialog REFERs being sent to a PJSIP endpoint. These can be
  triggered using the new ARI endpoint `/endpoints/refer`.

  Resolves: #71

  UserNote: There is a new ARI endpoint `/endpoints/refer` for referring
  an endpoint to some URI or endpoint.


- ### chan_dahdi: Allow autoreoriginating after hangup.                               
  Author: Naveen Albert  
  Date:   2023-08-04  

  Currently, if an FXS channel is still off hook when
  all calls on the line have hung up, the user is provided
  reorder tone until going back on hook again.

  In addition to not reflecting what most commercial switches
  actually do, it's very common for switches to automatically
  reoriginate for the user so that dial tone is provided without
  the user having to depress and release the hookswitch manually.
  This can increase convenience for users.

  This behavior is now supported for kewlstart FXS channels.
  It's supported only for kewlstart (FXOKS) mainly because the
  behavior doesn't make any sense for ground start channels,
  and loop start signalling doesn't provide the necessary DAHDI
  event that makes this easy to implement. Likely almost everyone
  is using FXOKS over FXOLS anyways since FXOLS is pretty useless
  these days.

  ASTERISK-30357 #close

  Resolves: #224

  UserNote: The autoreoriginate setting now allows for kewlstart FXS
  channels to automatically reoriginate and provide dial tone to the
  user again after all calls on the line have cleared. This saves users
  from having to manually hang up and pick up the receiver again before
  making another call.


- ### audiohook: Unlock channel in mute if no audiohooks present.                     
  Author: Joshua C. Colp  
  Date:   2023-08-09  

  In the case where mute was called on a channel that had no
  audiohooks the code was not unlocking the channel, resulting
  in a deadlock.

  Resolves: #233

- ### sig_analog: Allow three-way flash to time out to silence.                       
  Author: Naveen Albert  
  Date:   2023-07-10  

  sig_analog allows users to flash and use the three-way dial
  tone as a primitive hold function, simply by never timing
  it out.

  Some systems allow this dial tone to time out to silence,
  so the user is not annoyed by a persistent dial tone.
  This option allows the dial tone to time out normally to
  silence.

  ASTERISK-30004 #close
  Resolves: #205

  UserNote: The threewaysilenthold option now allows the three-way
  dial tone to time out to silence, rather than continuing forever.


- ### res_prometheus: Do not generate broken metrics                                  
  Author: Holger Hans Peter Freyther  
  Date:   2023-04-07  

  In 8d6fdf9c3adede201f0ef026dab201b3a37b26b6 invisible bridges were
  skipped but that lead to producing metrics with no name and no help.

  Keep track of the number of metrics configured and then only emit these.
  Add a basic testcase that verifies that there is no '(NULL)' in the
  output.

  ASTERISK-30474


- ### res_pjsip: Enable TLS v1.3 if present.                                          
  Author: Sean Bright  
  Date:   2023-08-02  

  Fixes #221

  UserNote: res_pjsip now allows TLS v1.3 to be enabled if supported by
  the underlying PJSIP library. The bundled version of PJSIP supports
  TLS v1.3.


- ### func_cut: Add example to documentation.                                         
  Author: phoneben  
  Date:   2023-07-19  

  This adds an example to the XML documentation clarifying usage
  of the CUT function to address a common misusage.


- ### extensions.conf.sample: Remove reference to missing context.                    
  Author: Sean Bright  
  Date:   2023-07-16  

  c3ff4648 removed the [iaxtel700] context but neglected to remove
  references to it.

  This commit addresses that and also removes iaxtel and freeworlddialup
  references from other config files.


- ### func_export: Use correct function argument as variable name.                    
  Author: Sean Bright  
  Date:   2023-07-12  

  Fixes #208


- ### app_queue: Add support for applying caller priority change immediately.         
  Author: Joshua C. Colp  
  Date:   2023-07-07  

  The app_queue module provides both an AMI action and a CLI command
  to change the priority of a caller in a queue. Up to now this change
  of priority has only been reflected to new callers into the queue.

  This change adds an "immediate" option to both the AMI action and
  CLI command which immediately applies the priority change respective
  to the other callers already in the queue. This can allow, for example,
  a caller to be placed at the head of the queue immediately if their
  priority is sufficient.

  Resolves: #202

  UserNote: The 'queue priority caller' CLI command and
  'QueueChangePriorityCaller' AMI action now have an 'immediate'
  argument which allows the caller priority change to be reflected
  immediately, causing the position of a caller to move within the
  queue depending on the priorities of the other callers.


- ### chan_iax2.c: Avoid crash with IAX2 switch support.                              
  Author: Sean Bright  
  Date:   2023-07-07  

  A change made in 82cebaa0 did not properly handle the case when a
  channel was not provided, triggering a crash. ast_check_hangup(...)
  does not protect against NULL pointers.

  Fixes #180


- ### res_geolocation: Ensure required 'location_info' is present.                    
  Author: Sean Bright  
  Date:   2023-07-07  

  Fixes #189


- ### Adds manager actions to allow move/remove/forward individual messages in a par..
  Author: Mike Bradeen  
  Date:   2023-06-29  

  Resolves: #181

  UserNote: The following manager actions have been added

  VoicemailBoxSummary - Generate message list for a given mailbox

  VoicemailRemove - Remove a message from a mailbox folder

  VoicemailMove - Move a message from one folder to another within a mailbox

  VoicemailForward - Copy a message from one folder in one mailbox
  to another folder in another or the same mailbox.


- ### app_voicemail: add CLI commands for message manipulation                        
  Author: Mike Bradeen  
  Date:   2023-06-20  

  Adds CLI commands to allow move/remove/forward individual messages
  from a particular mailbox folder. The forward command can be used
  to copy a message within a mailbox or to another mailbox. Also adds
  a show mailbox, required to retrieve message ID's.

  Resolves: #170

  UserNote: The following CLI commands have been added to app_voicemail

  voicemail show mailbox <mailbox> <context>
  Show contents of mailbox <mailbox>@<context>

  voicemail remove <mailbox> <context> <from_folder> <messageid>
  Remove message <messageid> from <from_folder> in mailbox <mailbox>@<context>

  voicemail move <mailbox> <context> <from_folder> <messageid> <to_folder>
  Move message <messageid> in mailbox <mailbox>&<context> from <from_folder> to <to_folder>

  voicemail forward <from_mailbox> <from_context> <from_folder> <messageid> <to_mailbox> <to_context> <to_folder>
  Forward message <messageid> in mailbox <mailbox>@<context> <from_folder> to
  mailbox <mailbox>@<context> <to_folder>


- ### res_rtp_asterisk: Move ast_rtp_rtcp_report_alloc using `rtp->themssrc_valid` i..
  Author: zhengsh  
  Date:   2023-06-30  

  From the gdb information, it was found that when calling __ast_free, the size of the
  allocated space pointed to by the pointer matches the size created when rtp->themssrc_valid
  is equal to 0. However, in reality, when reading the value of rtp->themssrc_valid in gdb,
  it is found to be 1.

  Within ast_rtcp_write(), the call to ast_rtp_rtcp_report_alloc() uses rtp->themssrc_valid,
  which is outside the protection of the rtp_instance lock. However,
  ast_rtcp_generate_report(), which is called by ast_rtcp_generate_compound_prefix(), uses
  rtp->themssrc_valid within the protection of the rtp_instance lock.

  This can lead to the possibility that the value of rtp->themssrc_valid used in the call to
  ast_rtp_rtcp_report_alloc() may be different from the value of rtp->themssrc_valid used
  within ast_rtcp_generate_report().

  Resolves: asterisk#63

- ### sig_analog: Allow immediate fake ring to be suppressed.                         
  Author: Naveen Albert  
  Date:   2023-06-08  

  When immediate=yes on an FXS channel, sig_analog will
  start fake audible ringback that continues until the
  channel is answered. Even if it answers immediately,
  the ringback is still audible for a brief moment.
  This can be disruptive and unwanted behavior.

  This adds an option to disable this behavior, though
  the default behavior remains unchanged.

  ASTERISK-30003 #close
  Resolves: #118

  UserNote: The immediatering option can now be set to no to suppress
  the fake audible ringback provided when immediate=yes on FXS channels.


- ### app.h: Move declaration of ast_getdata_result before its first use              
  Author: George Joseph  
  Date:   2023-07-10  

  The ast_app_getdata() and ast_app_getdata_terminator() declarations
  in app.h were changed recently to return enum ast_getdata_result
  (which is how they were defined in app.c).  The existing
  declaration of ast_getdata_result in app.h was about 1000 lines
  after those functions however so under certain circumstances,
  a "use before declaration" error was thrown by the compiler.
  The declaration of the enum was therefore moved to before those
  functions.

  Resolves: #200

- ### doc: Remove obsolete CHANGES-staging and UPGRADE-staging                        
  Author: George Joseph  
  Date:   2023-07-10  


- ### app_voicemail: fix imap compilation errors                                      
  Author: Mike Bradeen  
  Date:   2023-06-26  

  Fixes two compilation errors in app_voicemail_imap, one due to an unsed
  variable and one due to a new variable added in the incorrect location
  in _163.

  Resolves: #174

- ### res_musiconhold: avoid moh state access on unlocked chan                        
  Author: Mike Bradeen  
  Date:   2023-05-31  

  Move channel unlock to after moh state access to avoid
  potential unlocked access to state.

  Resolves: #133

- ### utils: add lock timestamps for DEBUG_THREADS                                    
  Author: Mike Bradeen  
  Date:   2023-05-23  

  Adds last locked and unlocked timestamps as well as a
  counter for the number of times the lock has been
  attempted (vs locked/unlocked) to debug output printed
  using the DEBUG_THREADS option.

  Resolves: #110

- ### rest-api: Updates for new documentation site                                    
  Author: George Joseph  
  Date:   2023-06-26  

  The new documentation site uses traditional markdown instead
  of the Confluence flavored version.  This required changes in
  the mustache templates and the python that generates the files.


- ### app_voicemail_imap: Fix message count when IMAP server is unavailable           
  Author: Olaf Titz  
  Date:   2023-06-15  

  Some callers of __messagecount did not correctly handle error return,
  instead returning a -1 message count.
  This caused a notification with "Messages-Waiting: yes" and
  "Voice-Message: -1/0 (0/0)" if the IMAP server was unavailable.

  Fixes: #64

- ### res_pjsip_rfc3326: Prefer Q.850 cause code over SIP.                            
  Author: Sean Bright  
  Date:   2023-06-12  

  Resolves: #116

- ### res_pjsip_session: Added new function calls to avoid ABI issues.                
  Author: Ben Ford  
  Date:   2023-06-05  

  Added two new functions (ast_sip_session_get_dialog and
  ast_sip_session_get_pjsip_inv_state) that retrieve the dialog and the
  pjsip_inv_state respectively from the pjsip_inv_session on the
  ast_sip_session struct. This is due to pjproject adding a new field to
  the pjsip_inv_session struct that caused crashes when trying to access
  fields that were no longer where they were expected to be if a module
  was compiled against a different version of pjproject.

  Resolves: #145

- ### app_queue: Add force_longest_waiting_caller option.                             
  Author: Nathan Bruning  
  Date:   2023-01-24  

  This adds an option 'force_longest_waiting_caller' which changes the
  global behavior of the queue engine to prevent queue callers from
  'jumping ahead' when an agent is in multiple queues.

  Resolves: #108

  Also closes old asterisk issues:
  - ASTERISK-17732
  - ASTERISK-17570


- ### pjsip_transport_events.c: Use %zu printf specifier for size_t.                  
  Author: Sean Bright  
  Date:   2023-06-05  

  Partially resolves #143.


- ### res_crypto.c: Gracefully handle potential key filename truncation.              
  Author: Sean Bright  
  Date:   2023-06-05  

  Partially resolves #143.


- ### configure: Remove obsolete and deprecated constructs.                           
  Author: Sean Bright  
  Date:   2023-06-01  

  These were uncovered when trying to run `bootstrap.sh` with Autoconf
  2.71:

  * AC_CONFIG_HEADER() is deprecated in favor of AC_CONFIG_HEADERS().
  * AC_HEADER_TIME is obsolete.
  * $as_echo is deprecated in favor of AS_ECHO() which requires an update
    to ax_pthread.m4.

  Note that the generated artifacts in this commit are from Autoconf 2.69.

  Resolves #139


- ### res_fax_spandsp.c: Clean up a spaces/tabs issue                                 
  Author: zhou_jiajian  
  Date:   2023-05-26  


- ### ast-db-manage: Synchronize revisions between comments and code.                 
  Author: Sean Bright  
  Date:   2023-06-06  

  In a handful of migrations, the comment header that indicates the
  current and previous revisions has drifted from the identifiers
  revision and down_revision variables. This updates the comment headers
  to match the code.


- ### test_statis_endpoints:  Fix channel_messages test again                         
  Author: George Joseph  
  Date:   2023-06-12  


- ### res_crypto.c: Avoid using the non-portable ALLPERMS macro.                      
  Author: Sean Bright  
  Date:   2023-06-05  

  ALLPERMS is not POSIX and it's trivial enough to not jump through
  autoconf hoops to check for it.

  Fixes #149.


- ### tcptls: when disabling a server port, we should set the accept_fd to -1.        
  Author: Jaco Kroon  
  Date:   2023-06-02  

  If we don't set this to -1 if the structure can be potentially re-used
  later then it's possible that we'll issue a close() on an unrelated file
  descriptor, breaking asterisk in other interesting ways.

  I believe this to be an unlikely scenario, but it costs nothing to be
  safe.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### AMI: Add parking position parameter to Park action                              
  Author: Jiajian Zhou  
  Date:   2023-05-19  

  Add a parking space extension parameter (ParkingSpace) to the Park action.
  Park action will attempt to park the call to that extension.
  If the extension is already in use, then execution will continue at the next priority.

  UserNote: New ParkingSpace parameter has been added to AMI action Park.

- ### test_stasis_endpoints.c: Make channel_messages more stable                      
  Author: George Joseph  
  Date:   2023-06-09  

  The channel_messages test was assuming that stasis would return
  messages in a specific order.  This is an incorrect assumption as
  message ordering was never guaranteed.  This was causing the test
  to fail occasionally.  We now test all the messages for the
  required message types instead of testing one by one.

  Resolves: #158

- ### build: Fix a few gcc 13 issues                                                  
  Author: George Joseph  
  Date:   2023-06-09  

  * gcc 13 is now catching when a function is declared as returning
    an enum but defined as returning an int or vice versa.  Fixed
    a few in app.h, loader.c, stasis_message.c.

  * gcc 13 is also now (incorrectly) complaining of dangling pointers
    when assigning a pointer to a local char array to a char *. Had
    to change that to an ast_alloca.

  Resolves: #155

- ### ast-db-manage: Fix alembic branching error caused by #122.                      
  Author: Sean Bright  
  Date:   2023-06-05  

  Fixes #147.


- ### app_followme: fix issue with enable_callee_prompt=no (#88)                      
  Author: alex2grad  
  Date:   2023-06-05  

  * app_followme: fix issue with enable_callee_prompt=no

  If the FollowMe option 'enable_callee_prompt' is set to 'no' then Asterisk
  incorrectly sets a winner channel to the channel from which any control frame was read.

  This fix sets the winner channel only to the answered channel.

  Resolves: #87

  ASTERISK-30326


- ### sounds: Update download URL to use HTTPS.                                       
  Author: Sean Bright  
  Date:   2023-06-01  

  Related to #136


- ### configure: Makefile downloader enable follow redirects.                         
  Author: Miguel Angel Nubla  
  Date:   2023-06-01  

  If curl is used for building, any download such as a sounds package
  will fail to follow HTTP redirects and will download wrong data.

  Resolves: #136

- ### res_musiconhold: Add option to loop last file.                                  
  Author: Naveen Albert  
  Date:   2023-05-25  

  Adds the loop_last option to res_musiconhold,
  which allows the last audio file in the directory
  to be looped perpetually once reached, rather than
  circling back to the beginning again.

  Resolves: #122
  ASTERISK-30462

  UserNote: The loop_last option in musiconhold.conf now
  allows the last file in the directory to be looped once reached.


- ### chan_dahdi: Fix Caller ID presentation for FXO ports.                           
  Author: Naveen Albert  
  Date:   2023-05-25  

  Currently, the presentation for incoming channels is
  always available, because it is never actually set,
  meaning the channel presentation can be nonsensical.
  If the presentation from the incoming Caller ID spill
  is private or unavailable, we now update the channel
  presentation to reflect this.

  Resolves: #120
  ASTERISK-30333
  ASTERISK-21741


- ### AMI: Add CoreShowChannelMap action.                                             
  Author: Ben Ford  
  Date:   2023-05-18  

  Adds a new AMI action (CoreShowChannelMap) that takes in a channel name
  and provides a list of all channels that are connected to that channel,
  following local channel connections as well.

  Resolves: #104

  UserNote: New AMI action CoreShowChannelMap has been added.

- ### sig_analog: Add fuller Caller ID support.                                       
  Author: Naveen Albert  
  Date:   2023-05-18  

  A previous change, ASTERISK_29991, made it possible
  to send additional Caller ID parameters that were
  not previously supported.

  This change adds support for analog DAHDI channels
  to now be able to receive these parameters for
  on-hook Caller ID, in order to enhance the usability
  of CPE that support these parameters.

  Resolves: #94
  ASTERISK-30331

  UserNote: Additional Caller ID properties are now supported on
  incoming calls to FXS stations, namely the
  redirecting reason and call qualifier.


- ### res_stasis.c: Add new type 'sdp_label' for bridge creation.                     
  Author: Joe Searle  
  Date:   2023-05-25  

  Add new type 'sdp_label' when creating a bridge using the ARI. This will
  add labels to the SDP for each stream, the label is set to the
  corresponding channel id.

  Resolves: #91

  UserNote: When creating a bridge using the ARI the 'type' argument now
  accepts a new value 'sdp_label' which will configure the bridge to add
  labels for each stream in the SDP with the corresponding channel id.


- ### app_queue: Preserve reason for realtime queues                                  
  Author: Niklas Larsson  
  Date:   2023-05-05  

  When Asterisk is restarted it does not preserve paused reason for
  members of realtime queues. This was fixed for non-realtime queues in
  ASTERISK_25732

  Resolves: #66

  UpgradeNote: Add a new column to the queue_member table:
  reason_paused VARCHAR(80) so the reason can be preserved.

  UserNote: Make paused reason in realtime queues persist an
  Asterisk restart. This was fixed for non-realtime
  queues in ASTERISK_25732.


- ### indications: logging changes                                                    
  Author: Mike Bradeen  
  Date:   2023-05-16  

  Increase verbosity to indicate failure due to missing country
  and to specify default on CLI dump

  Resolves: #89

- ### callerid: Allow specifying timezone for date/time.                              
  Author: Naveen Albert  
  Date:   2023-05-18  

  The Caller ID generation routine currently is hardcoded
  to always use the system time zone. This makes it possible
  to optionally specify any TZ-format time zone.

  Resolves: #98
  ASTERISK-30330


- ### logrotate: Fix duplicate log entries.                                           
  Author: Naveen Albert  
  Date:   2023-05-18  

  The Asterisk logrotate script contains explicit
  references to files with the .log extension,
  which are also included when *log is expanded.
  This causes issues with newer versions of logrotate.
  This fixes this by ensuring that a log file cannot
  be referenced multiple times after expansion occurs.

  Resolves: #96
  ASTERISK-30442
  Reported by: EN Barnett
  Tested by: EN Barnett


- ### chan_pjsip: Allow topology/session refreshes in early media state               
  Author: Maximilian Fridrich  
  Date:   2023-05-10  

  With this change, session modifications in the early media state are
  possible if the SDP was sent reliably and confirmed by a PRACK. For
  details, see RFC 6337, escpecially section 3.2.

  Resolves: #73

- ### chan_dahdi: Fix broken hidecallerid setting.                                    
  Author: Naveen Albert  
  Date:   2023-05-18  

  The hidecallerid setting in chan_dahdi.conf currently
  is broken for a couple reasons.

  First, the actual code in sig_analog to "allow" or "block"
  Caller ID depending on this setting improperly used
  ast_set_callerid instead of updating the presentation.
  This issue was mostly fixed in ASTERISK_29991, and that
  fix is carried forward to this code as well.

  Secondly, the hidecallerid setting is set on the DAHDI
  pvt but not carried forward to the analog pvt properly.
  This is because the chan_dahdi config loading code improperly
  set permhidecallerid to permhidecallerid from the config file,
  even though hidecallerid is what is actually set from the config
  file. (This is done correctly for call waiting, a few lines above.)
  This is fixed to read the proper value.

  Thirdly, in sig_analog, hidecallerid is set to permhidecallerid
  only on hangup. This can lead to potential security vulnerabilities
  as an allowed Caller ID from an initial call can "leak" into subsequent
  calls if no hangup occurs between them. This is fixed by setting
  hidecallerid to permcallerid when calls begin, rather than when they end.
  This also means we don't need to also set hidecallerid in chan_dahdi.c
  when copying from the config, as we would have to otherwise.

  Fourthly, sig_analog currently only allows dialing *67 or *82 if
  that would actually toggle the presentation. A comment is added
  clarifying that this behavior is okay.

  Finally, a couple log messages are updated to be more accurate.

  Resolves: #100
  ASTERISK-30349 #close


- ### asterisk.c: Fix option warning for remote console.                              
  Author: Naveen Albert  
  Date:   2023-05-18  

  Commit 09e989f972e2583df4e9bf585c246c37322d8d2f
  categorized the T option as not being compatible
  with remote consoles, but they do affect verbose
  messages with remote console. This fixes this.

  Resolves: #102

- ### configure: fix test code to match gethostbyname_r prototype.                    
  Author: Jaco Kroon  
  Date:   2023-05-10  

  This enables the test to work with CC=clang.

  Without this the test for 6 args would fail with:

  utils.c:99:12: error: static declaration of 'gethostbyname_r' follows non-static declaration
  static int gethostbyname_r (const char *name, struct hostent *ret, char *buf,
             ^
  /usr/include/netdb.h:177:12: note: previous declaration is here
  extern int gethostbyname_r (const char *__restrict __name,
             ^

  Fixing the expected return type to int sorts this out.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### res_pjsip_pubsub.c: Use pjsip version for pending NOTIFY check. (#77)           
  Author: Sean Bright  
  Date:   2023-05-11  

  The functionality we are interested in is present only in pjsip 2.13
  and newer.

  Resolves: #45

- ### res_sorcery_memory_cache.c: Fix memory leak                                     
  Author: zhengsh  
  Date:   2023-05-03  

  Replace the original call to ast_strdup with a call to ast_strdupa to fix the leak issue.

  Resolves: #55
  ASTERISK-30429


- ### xml.c: Process XML Inclusions recursively.                                      
  Author: Sean Bright  
  Date:   2023-05-09  

  If processing an XInclude results in new <xi:include> elements, we
  need to run XInclude processing again. This continues until no
  replacement occurs or an error is encountered.

  There is a separate issue with dynamic strings (ast_str) that will be
  addressed separately.

  Resolves: #65

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


- ### Set up new ChangeLogs directory                                                 
  Author: George Joseph  
  Date:   2023-05-09  


- ### chan_pjsip: also return all codecs on empty re-INVITE for late offers           
  Author: Henning Westerholt  
  Date:   2023-05-03  

  We should also return all codecs on an re-INVITE without SDP for a
  call that used late offer (e.g. no SDP in the initial INVITE, SDP
  in the ACK). Bugfix for feature introduced in ASTERISK-30193
  (https://issues.asterisk.org/jira/browse/ASTERISK-30193)

  Migration from previous gerrit change that was not merged.


- ### cel: add local optimization begin event                                         
  Author: Mike Bradeen  
  Date:   2023-05-02  

  The current AST_CEL_LOCAL_OPTIMIZE event is and has been
  triggered on a local optimization end to serve as a flag
  indicating the event occurred.  This change adds a second
  AST_CEL_LOCAL_OPTIMIZE_BEGIN event for further detail.

  Resolves: #52

  UpgradeNote: The existing AST_CEL_LOCAL_OPTIMIZE can continue
  to be used as-is and the AST_CEL_LOCAL_OPTIMIZE_BEGIN event
  can be ignored if desired.

  UserNote: The new AST_CEL_LOCAL_OPTIMIZE_BEGIN can be used
  by itself or in conert with the existing
  AST_CEL_LOCAL_OPTIMIZE to book-end local channel optimizaion.


- ### core: Cleanup gerrit and JIRA references. (#57)                                 
  Author: Sean Bright  
  Date:   2023-05-03  

  * Remove .gitreview and switch to pulling the main asterisk branch
    version from configure.ac instead.

  * Replace references to JIRA with GitHub.

  * Other minor cleanup found along the way.

  Resolves: #39

- ### res_pjsip: mediasec: Add Security-Client headers after 401                      
  Author: Maximilian Fridrich  
  Date:   2023-05-02  

  When using mediasec, requests sent after a 401 must still contain the
  Security-Client header according to
  draft-dawes-sipcore-mediasec-parameter.

  Resolves: #48

- ### LICENSE: Update link to trademark policy.                                       
  Author: Joshua C. Colp  
  Date:   2023-05-01  

  Resolves: #43

- ### chan_dahdi: Add dialmode option for FXS lines.                                  
  Author: Naveen Albert  
  Date:   2023-04-28  

  Currently, both pulse and tone dialing are always enabled
  on all FXS lines, with no way of disabling one or the other.

  In some circumstances, it is desirable or necessary to
  disable one of these, and this behavior can be problematic.

  A new "dialmode" option is added which allows setting the
  methods to support on a per channel basis for FXS (FXO
  signalled lines). The four options are "both", "pulse",
  "dtmf"/"tone", and "none".

  Additionally, integration with the CHANNEL function is
  added so that this setting can be updated for a channel
  during a call.

  Resolves: #35
  ASTERISK-29992

  UserNote: A "dialmode" option has been added which allows
  specifying, on a per-channel basis, what methods of
  subscriber dialing (pulse and/or tone) are permitted.

  Additionally, this can be changed on a channel
  at any point during a call using the CHANNEL
  function.


- ### Initial GitHub PRs                                                              
  Author: George Joseph  
  Date:   2023-04-28  


- ### Initial GitHub Issue Templates                                                  
  Author: George Joseph  
  Date:   2023-04-28  


- ### pbx_dundi: Fix PJSIP endpoint configuration check.                              
  Author: Joshua C. Colp  
  Date:   2023-04-13  

  ASTERISK-28233


- ### Revert "app_queue: periodic announcement configurable start time."              
  Author: Joshua Colp  
  Date:   2023-04-11  

  This reverts commit 3fd0b65bae4b1b14434737ffcf0da4aa9ff717f6.

  Reason for revert: Causes segmentation fault.


- ### res_pjsip_stir_shaken: Fix JSON field ordering and disallowed TN characters.    
  Author: Naveen Albert  
  Date:   2023-02-17  

  The current STIR/SHAKEN signing process is inconsistent with the
  RFCs in a couple ways that can cause interoperability issues.

  RFC8225 specifies that the keys must be ordered lexicographically, but
  currently the fields are simply ordered according to the order
  in which they were added to the JSON object, which is not
  compliant with the RFC and can cause issues with some carriers.

  To fix this, we now leverage libjansson's ability to dump a JSON
  object sorted by key value, yielding the correct field ordering.

  Additionally, telephone numbers must have any leading + prefix removed
  and must not contain characters outside of 0-9, *, and # in order
  to comply with the RFCs. Numbers are now properly formatted as such.

  ASTERISK-30407 #close


- ### pbx_dundi: Add PJSIP support.                                                   
  Author: Naveen Albert  
  Date:   2022-12-09  

  Adds PJSIP as a supported technology to DUNDi.

  To facilitate this, we now allow an endpoint to be specified
  for outgoing PJSIP calls. We also allow users to force a specific
  channel technology for outgoing SIP-protocol calls.

  ASTERISK-28109 #close
  ASTERISK-28233 #close


- ### install_prereq: Add Linux Mint support.                                         
  Author: The_Blode  
  Date:   2023-03-17  

  ASTERISK-30359 #close


- ### chan_pjsip: fix music on hold continues after INVITE with replaces              
  Author: Henning Westerholt  
  Date:   2023-03-21  

  In a three party scenario with INVITE with replaces, we need to
  unhold the call, otherwise one party continues to get music on
  hold, and the call is not properly bridged between them.

  ASTERISK-30428


- ### voicemail.conf: Fix incorrect comment about #include.                           
  Author: Naveen Albert  
  Date:   2023-03-28  

  A comment at the top of voicemail.conf says that #include
  cannot be used in voicemail.conf because this breaks
  the ability for app_voicemail to auto-update passwords.
  This is factually incorrect, since Asterisk has no problem
  updating files that are #include'd in the main configuration
  file, and this does work in voicemail.conf as well.

  ASTERISK-30479 #close


- ### app_queue: Fix minor xmldoc duplication and vagueness.                          
  Author: Naveen Albert  
  Date:   2023-04-03  

  The F option in the xmldocs for the Queue application
  was erroneously duplicated, causing it to display
  twice on the wiki. The two sections are now merged into one.

  Additionally, the description for the d option was quite
  vague. Some more details are added to provide context
  as to what this actually does.

  ASTERISK-30486 #close


- ### test.c: Fix counting of tests and add 2 new tests                               
  Author: George Joseph  
  Date:   2023-03-28  

  The unit test XML output was counting all registered tests as "run"
  even when only a subset were actually requested to be run and
  the "failures" attribute was missing.

  * The "tests" attribute of the "testsuite" element in the
    output XML now reflects only the tests actually requested
    to be executed instead of all the tests registered.

  * The "failures" attribute was added to the "testsuite"
    element.

  Also added 2 new unit tests that just pass and fail to be
  used for CI testing.


- ### res_calendar: output busy state as part of show calendar.                       
  Author: Jaco Kroon  
  Date:   2023-03-23  

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### loader.c: Minor module key check simplification.                                
  Author: Sean Bright  
  Date:   2023-03-23  


- ### ael: Regenerate lexers and parsers.                                             
  Author: Sean Bright  
  Date:   2023-03-21  

  Various changes to ensure that the lexers and parsers can be correctly
  generated when REBUILD_PARSERS is enabled.

  Some notes:

  * Because of the version of flex we are using to generate the lexers
    (2.5.35) some post-processing in the Makefile is still required.

  * The generated lexers do not contain the problematic C99 check that
    was being replaced by the call to sed in the respective Makefiles so
    it was removed.

  * Since these files are generated, they will include trailing
    whitespace in some places. This does not need to be corrected.


- ### bridge_builtin_features: add beep via touch variable                            
  Author: Mike Bradeen  
  Date:   2023-03-01  

  Add periodic beep option to one-touch recording by setting
  the touch variable TOUCH_MONITOR_BEEP or
  TOUCH_MIXMONITOR_BEEP to the desired interval in seconds.

  If the interval is less than 5 seconds, a minimum of 5
  seconds will be imposed.  If the interval is set to an
  invalid value, it will default to 15 seconds.

  A new test event PERIODIC_HOOK_ENABLED was added to the
  func_periodic_hook hook_on function to indicate when
  a hook is started.  This is so we can test that the touch
  variable starts the hook as expected.

  ASTERISK-30446


- ### res_mixmonitor: MixMonitorMute by MixMonitor ID                                 
  Author: Mike Bradeen  
  Date:   2023-03-13  

  While it is possible to create multiple mixmonitor instances
  on a channel, it was not previously possible to mute individual
  instances.

  This change includes the ability to specify the MixMonitorID
  when calling the manager action: MixMonitorMute.  This will
  allow an individual MixMonitor instance to be muted via id.
  This id can be stored as a channel variable using the 'i'
  MixMonitor option.

  As part of this change, if no MixMonitorID is specified in
  the manager action MixMonitorMute, Asterisk will set the mute
  flag on all MixMonitor spy-type audiohooks on the channel.
  This is done via the new audiohook function:
  ast_audiohook_set_mute_all.

  ASTERISK-30464


- ### format_sln: add .slin as supported file extension                               
  Author: Mike Bradeen  
  Date:   2023-03-14  

  Adds '.slin' to existing supported file extensions:
  .sln and .raw

  ASTERISK-30465


- ### res_agi: RECORD FILE plays 2 beeps.                                             
  Author: Sean Bright  
  Date:   2023-03-08  

  Sending the "RECORD FILE" command without the optional
  `offset_samples` argument can result in two beeps playing on the
  channel.

  This bug has been present since Asterisk 0.3.0 (2003-02-06).

  ASTERISK-30457 #close


- ### func_json: Fix JSON parsing issues.                                             
  Author: Naveen Albert  
  Date:   2023-02-26  

  Fix issue with returning empty instead of dumping
  the JSON string when recursing.

  Also adds a unit test to capture this fix.

  ASTERISK-30441 #close


- ### app_senddtmf: Add SendFlash AMI action.                                         
  Author: Naveen Albert  
  Date:   2023-02-26  

  Adds an AMI action to send a flash event
  on a channel.

  ASTERISK-30440 #close


- ### app_dial: Fix DTMF not relayed to caller on unanswered calls.                   
  Author: Naveen Albert  
  Date:   2023-03-04  

  DTMF frames are not handled in app_dial when sent towards the
  caller. This means that if DTMF is sent to the calling party
  and the call has not yet been answered, the DTMF is not audible.
  This is now fixed by relaying DTMF frames if only a single
  destination is being dialed.

  ASTERISK-29516 #close


- ### configure: fix detection of re-entrant resolver functions                       
  Author: Fabrice Fontaine  
  Date:   2023-03-08  

  uClibc does not provide res_nsearch:
  asterisk-16.0.0/main/dns.c:506: undefined reference to `res_nsearch'

  Patch coded by Yann E. MORIN:
  http://lists.busybox.net/pipermail/buildroot/2018-October/232630.html

  ASTERISK-21795 #close

  Signed-off-by: Bernd Kuhls <bernd.kuhls@t-online.de>
  [Retrieved from:
  https: //git.buildroot.net/buildroot/tree/package/asterisk/0005-configure-fix-detection-of-re-entrant-resolver-funct.patch]
  Signed-off-by: Fabrice Fontaine <fontaine.fabrice@gmail.com>

- ### cli: increase channel column width                                              
  Author: Mike Bradeen  
  Date:   2023-03-06  

  For 'core show channels', the Channel name field is increased
  to 64 characters and the Location name field is increased to
  32 characters.

  For 'core show channels verbose', the Channel name field is
  increased to 80 characters, the Context is increased to 24
  characters and the Extension is increased to 24 characters.

  ASTERISK-30455


- ### app_queue: periodic announcement configurable start time.                       
  Author: Jaco Kroon  
  Date:   2023-02-21  

  This newly introduced periodic-announce-startdelay makes it possible to
  configure the initial start delay of the first periodic announcement
  after which periodic-announce-frequency takes over.

  ASTERISK-30437 #close
  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### make_version: Strip svn stuff and suppress ref HEAD errors                      
  Author: George Joseph  
  Date:   2023-03-13  

  * All of the code that used subversion has been removed.

  * When Asterisk is checked out from a tag or commit instead
    of one of the regular branches, git would emit messages like
    "fatal: ref HEAD is not a symbolic ref" which weren't fatal
    at all.  Those are now suppressed.


- ### res_http_media_cache: Introduce options and customize                           
  Author: Holger Hans Peter Freyther  
  Date:   2022-10-16  

  Make the existing CURL parameters configurable and allow
  to specify the usable protocols, proxy and DNS timeout.

  ASTERISK-30340


- ### main/iostream.c: fix build with libressl                                        
  Author: Fabrice Fontaine  
  Date:   2023-02-25  

  Fix the following build failure with libressl by using SSL_is_server
  which is available since version 2.7.0 and
  https://github.com/libressl-portable/openbsd/commit/d7ec516916c5eaac29b02d7a8ac6570f63b458f7:

  iostream.c: In function 'ast_iostream_close':
  iostream.c:559:41: error: invalid use of incomplete typedef 'SSL' {aka 'struct ssl_st'}
    559 |                         if (!stream->ssl->server) {
        |                                         ^~

  ASTERISK-30107 #close

  Fixes: - http://autobuild.buildroot.org/results/ce4d62d00bb77ba5b303cacf6be7e350581a62f9

- ### contrib: rc.archlinux.asterisk uses invalid redirect.                           
  Author: Sean Bright  
  Date:   2023-03-02  

  `rc.archlinux.asterisk`, which explicitly requests bash in its
  shebang, uses the following command syntax:

    ${DAEMON} -rx "core stop now" > /dev/null 2&>1

  The intent of which is to execute:

    ${DAEMON} -rx "core stop now"

  While sending both stdout and stderr to `/dev/null`. Unfortunately,
  because the `&` is in the wrong place, bash is interpreting the `2` as
  just an additional argument to the `$DAEMON` command and not as a file
  descriptor and proceeds to use the bashism `&>` to send stderr and
  stdout to a file named `1`.

  So we clean it up and just use bash's shortcut syntax.

  Issue raised and a fix suggested (but not used) by peutch on GitHub¹.

  ASTERISK-30449 #close

  1. https://github.com/asterisk/asterisk/pull/31


- ### res_pjsip_pubsub: subscription cleanup changes                                  
  Author: Mike Bradeen  
  Date:   2023-03-29  

  There are two main parts of the change associated with this
  commit. These are driven by the change in call order of
  pubsub_on_rx_refresh and pubsub_on_evsub_state by pjproject
  when an in-dialog SUBSCRIBE is received.

  First, the previous behavior was for pjproject to call
  pubsub_on_rx_refresh before calling pubsub_on_evsub_state
  when an in-dialog SUBSCRIBE was received that changes the
  subscription state.

  If that change was a termination due to a re-SUBSCRIBE with
  an expires of 0, we used to use the call to pubsub_on_rx_refresh
  to set the substate of the evsub to TERMINATE_PENDING before
  pjproject could call pubsub_on_evsub_state.

  This substate let pubsub_on_evsub_state know that the
  subscription TERMINATED event could be ignored as there was
  still a subsequent NOTIFY that needed to be generated and
  another call to pubsub_on_evsub_state to come with it.

  That NOTIFY was sent via serialized_pubsub_on_refresh_timeout
  which would see the TERMINATE_PENDING state and transition it
  to TERMINATE_IN_PROGRESS before triggering another call to
  pubsub_on_evsub_state (which now would clean up the evsub.)

  The new pjproject behavior is to call pubsub_on_evsub_state
  before pubsub_on_rx_refresh. This means we no longer can set
  the state to TERMINATE_PENDING to tell pubsub_on_evsub_state
  that it can ignore the first TERMINATED event.

  To handle this, we now look directly at the event type,
  method type and the expires value to determine whether we
  want to ignore the event or use it to trigger the evsub
  cleanup.

  Second, pjproject now expects the NOTIFY to actually be sent
  during pubsub_on_rx_refresh and avoids the protocol violation
  inherent in sending a NOTIFY before the SUBSCRIBE is
  acknowledged by caching the sent NOTIFY then sending it
  after responding to the SUBSCRIBE.

  This requires we send the NOTIFY using the non-serialized
  pubsub_on_refresh_timeout directly and let pjproject handle
  the protocol violation.

  ASTERISK-30469


- ### Revert "pbx_ael: Global variables are not expanded."                            
  Author: Sean Bright  
  Date:   2023-03-19  

  This reverts commit 56051d1ac5115ff8c55b920fc441613c487fb512.

  Reason for revert: Behavior change that breaks existing dialplan.

  ASTERISK-30472 #close


- ### res_pjsip: Replace invalid UTF-8 sequences in callerid name                     
  Author: George Joseph  
  Date:   2023-02-16  

  * Added a new function ast_utf8_replace_invalid_chars() to
    utf8.c that copies a string replacing any invalid UTF-8
    sequences with the Unicode specified U+FFFD replacement
    character.  For example:  "abc\xffdef" becomes "abc\uFFFDdef".
    Any UTF-8 compliant implementation will show that character
    as a � character.

  * Updated res_pjsip:set_id_from_hdr() to use
    ast_utf8_replace_invalid_chars and print a warning if any
    invalid sequences were found during the copy.

  * Updated stasis_channels:ast_channel_publish_varset to use
    ast_utf8_replace_invalid_chars and print a warning if any
    invalid sequences were found during the copy.

  ASTERISK-27830


- ### test.c: Avoid passing -1 to FD_* family of functions.                           
  Author: Sean Bright  
  Date:   2023-02-27  

  This avoids buffer overflow errors when running tests that capture
  output from child processes.

  This also corrects a copypasta in an off-nominal error message.


- ### chan_iax2: Fix jitterbuffer regression prior to receiving audio.                
  Author: Naveen Albert  
  Date:   2022-12-14  

  ASTERISK_29392 (a security fix) introduced a regression by
  not processing frames when we don't have an audio format.

  Currently, chan_iax2 only calls jb_get to read frames from
  the jitterbuffer when the voiceformat has been set on the pvt.
  However, this only happens when we receive a voice frame, which
  means that prior to receiving voice frames, other types of frames
  get stalled completely in the jitterbuffer.

  To fix this, we now fallback to using the format negotiated during
  call setup until we've actually received a voice frame with a format.
  This ensures we're always able to read from the jitterbuffer.

  ASTERISK-30354 #close
  ASTERISK-30162 #close


- ### test_crypto.c: Fix getcwd(…) build error.                                       
  Author: Sean Bright  
  Date:   2023-02-27  

  `getcwd(…)` is decorated with the `warn_unused_result` attribute and
  therefore needs its return value checked.


- ### pjproject_bundled: Fix cross-compilation with SSL libs.                         
  Author: Nick French  
  Date:   2023-02-11  

  Asterisk makefiles auto-detect SSL library availability,
  then they assume that pjproject makefiles will also autodetect
  an SSL library at the same time, so they do not pass on the
  autodetection result to pjproject.

  This normally works, except the pjproject makefiles disables
  autodetection when cross-compiling.

  Fix by explicitly configuring pjproject to use SSL if we
  have been told to use it or it was autodetected

  ASTERISK-30424 #close


- ### app_read: Add an option to return terminator on empty digits.                   
  Author: Mike Bradeen  
  Date:   2023-01-30  

  Adds 'e' option to allow Read() to return the terminator as the
  dialed digits in the case where only the terminator is entered.

  ie; if "#" is entered, return "#" if the 'e' option is set and ""
  if it is not.

  ASTERISK-30411


- ### res_phoneprov.c: Multihomed SERVER cache prevention                             
  Author: cmaj  
  Date:   2023-01-07  

  Phones moving between subnets on multi-homed server have their
  initially connected interface IP cached in the SERVER variable,
  even when it is not specified in the configuration files. This
  prevents phones from obtaining the correct SERVER variable value
  when they move to another subnet.

  ASTERISK-30388 #close
  Reported-by: cmaj


- ### app_directory: Add a 'skip call' option.                                        
  Author: Mike Bradeen  
  Date:   2023-01-27  

  Adds 's' option to skip calling the extension and instead set the
  extension as DIRECTORY_EXTEN channel variable.

  ASTERISK-30405


- ### app_senddtmf: Add option to answer target channel.                              
  Author: Mike Bradeen  
  Date:   2023-02-06  

  Adds a new option to SendDTMF() which will answer the specified
  channel if it is not already up. If no channel is specified, the
  current channel will be answered instead.

  ASTERISK-30422


- ### res_pjsip: Prevent SEGV in pjsip_evsub_send_request                             
  Author: Mike Bradeen  
  Date:   2023-02-21  

  contributed pjproject - patch to check sub->pending_notify
  in evsub.c:on_tsx_state before calling
  pjsip_evsub_send_request()

  res_pjsip_pubsub - change post pjsip 2.13 behavior to use
  pubsub_on_refresh_timeout to avoid the ao2_cleanup call on
  the sub_tree. This is is because the final NOTIFY send is no
  longer the last place the sub_tree is referenced.

  ASTERISK-30419


- ### app_queue: Minor docs and logging fixes for UnpauseQueueMember.                 
  Author: Sean Bright  
  Date:   2023-02-02  

  ASTERISK-30417 #close


- ### app_queue: Reset all queue defaults before reload.                              
  Author: Sean Bright  
  Date:   2023-01-31  

  Several queue fields were not being set to their default value during
  a reload.

  Additionally added some sample configuration options that were missing
  from queues.conf.sample.


- ### res_pjsip: Upgraded bundled pjsip to 2.13                                       
  Author: Mike Bradeen  
  Date:   2023-01-20  

  Removed multiple patches.

  Code chages in res_pjsip_pubsub due to changes in evsub.

  Pjsip now calls on_evsub_state() before on_rx_refresh(),
  so the sub tree deletion that used to take place in
  on_evsub_state() now must take place in on_rx_refresh().

  Additionally, pjsip now requires that you send the NOTIFY
  from within on_rx_refresh(), otherwise it will assert
  when going to send the 200 OK. The idea is that it will
  look for this NOTIFY and cache it until after sending the
  response in order to deal with the self-imposed message
  mis-order. Asterisk previously dealt with this by pushing
  the NOTIFY in on_rx_refresh(), but pjsip now forces us
  to use it's method.

  Changes were required to configure in order to detect
  which way pjsip handles this as the two are not
  compatible for the reasons mentioned above.

  A corresponding change in testsuite is required in order
  to deal with the small interal timing changes caused by
  moving the NOTIFY send.

  ASTERISK-30325


- ### doxygen: Fix doxygen errors.                                                    
  Author: Sean Bright  
  Date:   2023-01-30  


- ### app_signal: Add signaling applications                                          
  Author: Naveen Albert  
  Date:   2022-01-06  

  Adds the Signal and WaitForSignal
  applications, which can be used for inter-channel
  signaling in the dialplan.

  Signal supports sending a signal to other channels
  listening for a signal of the same name, with an
  optional data payload. The signal is received by
  all channels waiting for that named signal.

  ASTERISK-29810 #close


- ### app_directory: add ability to specify configuration file                        
  Author: Mike Bradeen  
  Date:   2023-01-25  

  Adds option to app_directory to specify a filename from which to
  read configuration instead of voicemail.conf ie;

  same => n,Directory(,,c(directory.conf))

  This configuration should contain a list of extensions using the
  voicemail.conf format, ie;

  2020=2020,Dog Dog,,,,attach=no|saycid=no|envelope=no|delete=no

  ASTERISK-30404


- ### func_json: Enhance parsing capabilities of JSON_DECODE                          
  Author: Naveen Albert  
  Date:   2022-02-12  

  Adds support for arrays to JSON_DECODE by allowing the
  user to print out entire arrays or index a particular
  key or print the number of keys in a JSON array.

  Additionally, adds support for recursively iterating a
  JSON tree in a single function call, making it easier
  to parse JSON results with multiple levels. A maximum
  depth is imposed to prevent potentially blowing
  the stack.

  Also fixes a bug with the unit tests causing an empty
  string to be printed instead of the actual test result.

  ASTERISK-29913 #close


- ### res_stasis_snoop: Fix snoop crash                                               
  Author: sungtae kim  
  Date:   2023-01-04  

  Added NULL pointer check and channel lock to prevent resource release
  while the chanspy is processing.

  ASTERISK-29604


- ### pbx_ael: Global variables are not expanded.                                     
  Author: Sean Bright  
  Date:   2023-01-26  

  Variable references within global variable assignments are now
  expanded rather than being included literally.

  ASTERISK-30406 #close


- ### res_pjsip_session: Add overlap_context option.                                  
  Author: Naveen Albert  
  Date:   2022-10-13  

  Adds the overlap_context option, which can be used
  to explicitly specify a context to use for overlap
  dialing extension matches, rather than forcibly
  using the context configured for the endpoint.

  ASTERISK-30262 #close


- ### app_playback.c: Fix PLAYBACKSTATUS regression.                                  
  Author: Sean Bright  
  Date:   2023-01-05  

  In Asterisk 11, if a channel was redirected away during Playback(),
  the PLAYBACKSTATUS variable would be set to SUCCESS. In Asterisk 12
  (specifically commit 7d9871b3940fa50e85039aef6a8fb9870a7615b9) that
  behavior was inadvertently changed and the same operation would result
  in the PLAYBACKSTATUS variable being set to FAILED. The Asterisk 11
  behavior has been restored.

  Partial fix for ASTERISK~25661.


- ### res_rtp_asterisk: Don't use double math to generate timestamps                  
  Author: George Joseph  
  Date:   2023-01-11  

  Rounding issues with double math were causing rtp timestamp
  slips in outgoing packets.  We're now back to integer math
  and are getting no more slips.

  ASTERISK-30391


- ### format_wav: replace ast_log(LOG_DEBUG, ...) by ast_debug(1, ...)                
  Author: Alexei Gradinari  
  Date:   2023-01-06  

  Each playback of WAV files results in logging
  "Skipping unknown block 'LIST'".

  To prevent unnecessary flooding of this DEBUG log this patch replaces
  ast_log(LOG_DEBUG, ...) by ast_debug(1, ...).


- ### res_pjsip_rfc3326: Add SIP causes support for RFC3326                           
  Author: Igor Goncharovsky  
  Date:   2022-11-18  

  Add ability to set HANGUPCAUSE when SIP causecode received in BYE (in addition to currently supported Q.850).

  ASTERISK-30319 #close


- ### res_rtp_asterisk: Asterisk Media Experience Score (MES)                         
  Author: George Joseph  
  Date:   2022-10-28  

  -----------------

  This commit reinstates MES with some casting fixes to the
  functions in time.h that convert between doubles and timeval
  structures.  The casting issues were causing incorrect
  timestamps to be calculated which caused transcoding from/to
  G722 to produce bad or no audio.

  ASTERISK-30391

  -----------------

  This module has been updated to provide additional
  quality statistics in the form of an Asterisk
  Media Experience Score.  The score is avilable using
  the same mechanisms you'd use to retrieve jitter, loss,
  and rtt statistics.  For more information about the
  score and how to retrieve it, see
  https://wiki.asterisk.org/wiki/display/AST/Media+Experience+Score

  * Updated chan_pjsip to set quality channel variables when a
    call ends.
  * Updated channels/pjsip/dialplan_functions.c to add the ability
    to retrieve the MES along with the existing rtcp stats when
    using the CHANNEL dialplan function.
  * Added the ast_debug_rtp_is_allowed and ast_debug_rtcp_is_allowed
    checks for debugging purposes.
  * Added several function to time.h for manipulating time-in-samples
    and times represented as double seconds.
  * Updated rtp_engine.c to pass through the MES when stats are
    requested.  Also debug output that dumps the stats when an
    rtp instance is destroyed.
  * Updated res_rtp_asterisk.c to implement the calculation of the
    MES.  In the process, also had to update the calculation of
    jitter.  Many debugging statements were also changed to be
    more informative.
  * Added a unit test for internal testing.  The test should not be
    run during normal operation and is disabled by default.


- ### Revert "res_rtp_asterisk: Asterisk Media Experience Score (MES)"                
  Author: George Joseph  
  Date:   2023-01-09  

  This reverts commit d454801c2ddba89f7925c847012db2866e271f68.

  Reason for revert: Issue when transcoding to/from g722


- ### loader: Allow declined modules to be unloaded.                                  
  Author: Naveen Albert  
  Date:   2022-12-08  

  Currently, if a module declines to load, dlopen is called
  to register the module but dlclose never gets called.
  Furthermore, loader.c currently doesn't allow dlclose
  to ever get called on the module, since it declined to
  load and the unload function bails early in this case.

  This can be problematic if a module is updated, since the
  new module cannot be loaded into memory since we haven't
  closed all references to it. To fix this, we now allow
  modules to be unloaded, even if they never "loaded" in
  Asterisk itself, so that dlclose is called and the module
  can be properly cleaned up, allowing the updated module
  to be loaded from scratch next time.

  ASTERISK-30345 #close


- ### app_broadcast: Add Broadcast application                                        
  Author: Naveen Albert  
  Date:   2022-08-15  

  Adds a new application, Broadcast, which can be used for
  one-to-many transmission and many-to-one reception of
  channel audio in Asterisk. This is similar to ChanSpy,
  except it is designed for multiple channel targets instead
  of a single one. This can make certain kinds of audio
  manipulation more efficient and streamlined. New kinds
  of audio injection impossible with ChanSpy are also made
  possible.

  ASTERISK-30180 #close


- ### func_frame_trace: Print text for text frames.                                   
  Author: Naveen Albert  
  Date:   2022-12-13  

  Since text frames contain a text body, make FRAME_TRACE
  more useful for text frames by actually printing the text.

  ASTERISK-30353 #close


- ### json.h: Add ast_json_object_real_get.                                           
  Author: Naveen Albert  
  Date:   2022-12-16  

  json.h contains macros to get a string and an integer
  from a JSON object. However, the macro to do this for
  JSON reals is missing. This adds that.

  ASTERISK-30361 #close


- ### manager: Fix appending variables.                                               
  Author: Naveen Albert  
  Date:   2022-12-22  

  The if statement here is always false after the for
  loop finishes, so variables are never appended.
  This removes that to properly append to the end
  of the variable list.

  ASTERISK-30351 #close
  Reported by: Sebastian Gutierrez


- ### res_pjsip_transport_websocket: Add remote port to transport                     
  Author: George Joseph  
  Date:   2022-12-23  

  When Asterisk receives a new websocket conenction, it creates a new
  pjsip transport for it and copies connection data into it.  The
  transport manager then uses the remote IP address and port on the
  transport to create a monitor for each connection.  However, the
  remote port wasn't being copied, only the IP address which meant
  that the transport manager was creating only 1 monitoring entry for
  all websocket connections from the same IP address. Therefore, if
  one of those connections failed, it deleted the transport taking
  all the the connections from that same IP address with it.

  * We now copy the remote port into the created transport and the
    transport manager behaves correctly.

  ASTERISK-30369


- ### http.c: Fix NULL pointer dereference bug                                        
  Author: Boris P. Korzun  
  Date:   2022-12-28  

  If native HTTP is disabled but HTTPS is enabled and status page enabled
  too, Core/HTTP crashes while loading. 'global_http_server' references
  to NULL, but the status page tries to dereference it.

  The patch adds a check for HTTP is enabled.

  ASTERISK-30379 #close


- ### res_http_media_cache: Do not crash when there is no extension                   
  Author: Holger Hans Peter Freyther  
  Date:   2022-12-16  

  Do not crash when a URL has no path component as in this case the
  ast_uri_path function will return NULL. Make the code cope with not
  having a path.

  The below would crash
  > media cache create http://google.com /tmp/foo.wav

  Thread 1 "asterisk" received signal SIGSEGV, Segmentation fault.
  0x0000ffff836616cc in strrchr () from /lib/aarch64-linux-gnu/libc.so.6
  (gdb) bt
   #0  0x0000ffff836616cc in strrchr () from /lib/aarch64-linux-gnu/libc.so.6
   #1  0x0000ffff43d43a78 in file_extension_from_string (str=<optimized out>, buffer=buffer@entry=0xffffca9973c0 "",
      capacity=capacity@entry=64) at res_http_media_cache.c:288
   #2  0x0000ffff43d43bac in file_extension_from_url_path (bucket_file=bucket_file@entry=0x3bf96568,
      buffer=buffer@entry=0xffffca9973c0 "", capacity=capacity@entry=64) at res_http_media_cache.c:378
   #3  0x0000ffff43d43c74 in bucket_file_set_extension (bucket_file=bucket_file@entry=0x3bf96568) at res_http_media_cache.c:392
   #4  0x0000ffff43d43d10 in bucket_file_run_curl (bucket_file=0x3bf96568) at res_http_media_cache.c:555
   #5  0x0000ffff43d43f74 in bucket_http_wizard_create (sorcery=<optimized out>, data=<optimized out>, object=<optimized out>)
      at res_http_media_cache.c:613
   #6  0x0000000000487638 in bucket_file_wizard_create (sorcery=<optimized out>, data=<optimized out>, object=<optimized out>)
      at bucket.c:191
   #7  0x0000000000554408 in sorcery_wizard_create (object_wizard=object_wizard@entry=0x3b9f0718,
      details=details@entry=0xffffca9974a8) at sorcery.c:2027
   #8  0x0000000000559698 in ast_sorcery_create (sorcery=<optimized out>, object=object@entry=0x3bf96568) at sorcery.c:2077
   #9  0x00000000004893a4 in ast_bucket_file_create (file=file@entry=0x3bf96568) at bucket.c:727
   #10 0x00000000004f877c in ast_media_cache_create_or_update (uri=0x3bfa1103 "https://google.com",
      file_path=0x3bfa1116 "/tmp/foo.wav", metadata=metadata@entry=0x0) at media_cache.c:335
   #11 0x00000000004f88ec in media_cache_handle_create_item (e=<optimized out>, cmd=<optimized out>, a=0xffffca9976b8)
      at media_cache.c:640

  ASTERISK-30375 #close


- ### res_rtp_asterisk: Asterisk Media Experience Score (MES)                         
  Author: George Joseph  
  Date:   2022-10-28  

  This module has been updated to provide additional
  quality statistics in the form of an Asterisk
  Media Experience Score.  The score is avilable using
  the same mechanisms you'd use to retrieve jitter, loss,
  and rtt statistics.  For more information about the
  score and how to retrieve it, see
  https://wiki.asterisk.org/wiki/display/AST/Media+Experience+Score

  * Updated chan_pjsip to set quality channel variables when a
    call ends.
  * Updated channels/pjsip/dialplan_functions.c to add the ability
    to retrieve the MES along with the existing rtcp stats when
    using the CHANNEL dialplan function.
  * Added the ast_debug_rtp_is_allowed and ast_debug_rtcp_is_allowed
    checks for debugging purposes.
  * Added several function to time.h for manipulating time-in-samples
    and times represented as double seconds.
  * Updated rtp_engine.c to pass through the MES when stats are
    requested.  Also debug output that dumps the stats when an
    rtp instance is destroyed.
  * Updated res_rtp_asterisk.c to implement the calculation of the
    MES.  In the process, also had to update the calculation of
    jitter.  Many debugging statements were also changed to be
    more informative.
  * Added a unit test for internal testing.  The test should not be
    run during normal operation and is disabled by default.

  ASTERISK-30280


- ### pbx_app: Update outdated pbx_exec channel snapshots.                            
  Author: Naveen Albert  
  Date:   2022-12-21  

  pbx_exec makes a channel snapshot before executing applications.
  This doesn't cause an issue during normal dialplan execution
  where pbx_exec is called over and over again in succession.
  However, if pbx_exec is called "one off", e.g. using
  ast_pbx_exec_application, then a channel snapshot never ends
  up getting made after the executed application returns, and
  inaccurate snapshot information will linger for a while, causing
  "core show channels", etc. to show erroneous info.

  This is fixed by manually making a channel snapshot at the end
  of ast_pbx_exec_application, since we anticipate that pbx_exec
  might not get called again immediately.

  ASTERISK-30367 #close


- ### res_pjsip_session: Use Caller ID for extension matching.                        
  Author: Naveen Albert  
  Date:   2022-11-26  

  Currently, there is no Caller ID available to us when
  checking for an extension match when handling INVITEs.
  As a result, extension patterns that depend on the Caller ID
  are not matched and calls may be incorrectly rejected.

  The Caller ID is not available because the supplement that
  adds Caller ID to the session does not execute until after
  this check. Supplement callbacks cannot yet be executed
  at this point since the session is not yet in the appropriate
  state.

  To fix this without impacting existing behavior, the Caller ID
  number is now retrieved before attempting to pattern match.
  This ensures pattern matching works correctly and there is
  no behavior change to the way supplements are called.

  ASTERISK-28767 #close


- ### res_pjsip_sdp_rtp.c: Use correct timeout when put on hold.                      
  Author: Ben Ford  
  Date:   2022-12-12  

  When a call is put on hold and it has moh_passthrough and rtp_timeout
  set on the endpoint, the wrong timeout will be used. rtp_timeout_hold is
  expected to be used, but rtp_timeout is used instead. This change adds a
  couple of checks for locally_held to determine if rtp_timeout_hold needs
  to be used instead of rtp_timeout.

  ASTERISK-30350


- ### app_voicemail_odbc: Fix string overflow warning.                                
  Author: Naveen Albert  
  Date:   2022-11-14  

  Fixes a negative offset warning by initializing
  the buffer to empty.

  Additionally, although it doesn't currently complain
  about it, the size of a buffer is increased to
  accomodate the maximum size contents it could have.

  ASTERISK-30240 #close


- ### func_callerid: Warn about invalid redirecting reason.                           
  Author: Naveen Albert  
  Date:   2022-11-26  

  Currently, if a user attempts to set a Caller ID related
  function to an invalid value, a warning is emitted,
  except for when setting the redirecting reason.
  We now emit a warning if we were unable to successfully
  parse the user-provided reason.

  ASTERISK-30332 #close


- ### res_pjsip: Fix path usage in case dialing with '@'                              
  Author: Igor Goncharovsky  
  Date:   2022-11-04  

  Fix aor lookup on sip path addition. Issue happens in case of dialing
  with @ and overriding user part of RURI.

  ASTERISK-30100 #close
  Reported-by: Yury Kirsanov


- ### streams:  Ensure that stream is closed in ast_stream_and_wait on error          
  Author: Peter Fern  
  Date:   2022-11-22  

  When ast_stream_and_wait returns an error (for example, when attempting
  to stream to a channel after hangup) the stream is not closed, and
  callers typically do not check the return code. This results in leaking
  file descriptors, leading to resource exhaustion.

  This change ensures that the stream is closed in case of error.

  ASTERISK-30198 #close
  Reported-by: Julien Alie


- ### app_sendtext: Remove references to removed applications.                        
  Author: Naveen Albert  
  Date:   2022-12-10  

  Removes see-also references to applications that don't
  exist anymore (removed in Asterisk 19),
  so these dead links don't show up on the wiki.

  ASTERISK-30347 #close


- ### res_geoloc: fix NULL pointer dereference bug                                    
  Author: Alexandre Fournier  
  Date:   2022-12-09  

  The `ast_geoloc_datastore_add_eprofile` function does not return 0 on
  success, it returns the size of the underlying datastore. This means
  that the datastore will be freed and its pointer set to NULL when no
  error occured at all.

  ASTERISK-30346


- ### res_pjsip_aoc: Don't assume a body exists on responses.                         
  Author: Joshua C. Colp  
  Date:   2022-12-13  

  When adding AOC to an outgoing response the code
  assumed that a body would exist for comparing the
  Content-Type. This isn't always true.

  The code now checks to make sure the response has
  a body before checking the Content-Type.

  ASTERISK-21502


- ### app_if: Fix format truncation errors.                                           
  Author: Naveen Albert  
  Date:   2022-12-12  

  Fixes format truncation warnings in gcc 12.2.1.

  ASTERISK-30349 #close


- ### manager: AOC-S support for AOCMessage                                           
  Author: Michael Kuron  
  Date:   2022-11-01  

  ASTERISK-21502


- ### res_pjsip_aoc: New module for sending advice-of-charge with chan_pjsip          
  Author: Michael Kuron  
  Date:   2022-10-23  

  chan_sip supported sending AOC-D and AOC-E information in SIP INFO
  messages in an "AOC" header in a format that was originally defined by
  Snom. In the meantime, ETSI TS 124 647 introduced an XML-based AOC
  format that is supported by devices from multiple vendors, including
  Snom phones with firmware >= 8.4.2 (released in 2010).

  This commit adds a new res_pjsip_aoc module that inserts AOC information
  into outgoing messages or sends SIP INFO messages as described below.
  It also fixes a small issue in res_pjsip_session which didn't always
  call session supplements on outgoing_response.

  * AOC-S in the 180/183/200 responses to an INVITE request
  * AOC-S in SIP INFO (if a 200 response has already been sent or if the
    INVITE was sent by Asterisk)
  * AOC-D in SIP INFO
  * AOC-D in the 200 response to a BYE request (if the client hangs up)
  * AOC-D in a BYE request (if Asterisk hangs up)
  * AOC-E in the 200 response to a BYE request (if the client hangs up)
  * AOC-E in a BYE request (if Asterisk hangs up)

  The specification defines one more, AOC-S in an INVITE request, which
  is not implemented here because it is not currently possible in
  Asterisk to have AOC data ready at this point in call setup. Once
  specifying AOC-S via the dialplan or passing it through from another
  SIP channel's INVITE is possible, that might be added.

  The SIP INFO requests are sent out immediately when the AOC indication
  is received. The others are inserted into an appropriate outgoing
  message whenever that is ready to be sent. In the latter case, the XML
  is stored in a channel variable at the time the AOC indication is
  received. Depending on where the AOC indications are coming from (e.g.
  PRI or AMI), it may not always be possible to guarantee that the AOC-E
  is available in time for the BYE.

  Successfully tested AOC-D and both variants of AOC-E with a Snom D735
  running firmware 10.1.127.10. It does not appear to properly support
  AOC-S however, so that could only be tested by inspecting SIP traces.

  ASTERISK-21502 #close
  Reported-by: Matt Jordan <mjordan@digium.com>


- ### ari: Destroy body variables in channel create.                                  
  Author: Joshua C. Colp  
  Date:   2022-12-08  

  When passing a JSON body to the 'create' channel route
  it would be converted into Asterisk variables, but never
  freed resulting in a memory leak.

  This change makes it so that the variables are freed in
  all cases.

  ASTERISK-30344


- ### app_voicemail: Fix missing email in msg_create_from_file.                       
  Author: Naveen Albert  
  Date:   2022-11-03  

  msg_create_from_file currently does not dispatch emails,
  which means that applications using this function, such
  as MixMonitor, will not trigger notifications to users
  (only AMI events are sent our currently). This is inconsistent
  with other ways users can receive voicemail.

  This is fixed by adding an option that attempts to send
  an email and falling back to just the notifications as
  done now if that fails. The existing behavior remains
  the default.

  ASTERISK-30283 #close


- ### res_pjsip: Fix typo in from_domain documentation                                
  Author: Marcel Wagner  
  Date:   2022-11-25  

  This fixes a small typo in the from_domain documentation on the endpoint documentation

  ASTERISK-30328 #close


- ### res_hep: Add support for named capture agents.                                  
  Author: Naveen Albert  
  Date:   2022-11-21  

  Adds support for the capture agent name field
  of the Homer protocol to Asterisk by allowing
  users to specify a name that will be sent to
  the HEP server.

  ASTERISK-30322 #close


- ### app_if: Adds conditional branch applications                                    
  Author: Naveen Albert  
  Date:   2021-06-28  

  Adds the If, ElseIf, Else, ExitIf, and EndIf
  applications for conditional execution
  of a block of dialplan, similar to the While,
  EndWhile, and ExitWhile applications. The
  appropriate branch is executed at most once
  if available and may be broken out of while
  inside.

  ASTERISK-29497


- ### res_pjsip_session.c: Map empty extensions in INVITEs to s.                      
  Author: Naveen Albert  
  Date:   2022-10-17  

  Some SIP devices use an empty extension for PLAR functionality.

  Rather than rejecting these empty extensions, we now use the s
  extension for such calls to mirror the existing PLAR functionality
  in Asterisk (e.g. chan_dahdi).

  ASTERISK-30265 #close


- ### res_pjsip: Update contact_user to point out default                             
  Author: Marcel Wagner  
  Date:   2022-11-17  

  Updates the documentation for the 'contact_user' field to point out the
  default outbound contact if no contact_user is specified 's'

  ASTERISK-30316 #close


- ### res_adsi: Fix major regression caused by media format rearchitecture.           
  Author: Naveen Albert  
  Date:   2022-11-23  

  The commit that rearchitected media formats,
  a2c912e9972c91973ea66902d217746133f96026 (ASTERISK_23114)
  introduced a regression by improperly translating code in res_adsi.c.
  In particular, the pointer to the frame buffer was initialized
  at the top of adsi_careful_send, rather than dynamically updating it
  for each frame, as is required.

  This resulted in the first frame being repeatedly sent,
  rather than advancing through the frames.
  This corrupted the transmission of the CAS to the CPE,
  which meant that CPE would never respond with the DTMF acknowledgment,
  effectively completely breaking ADSI functionality.

  This issue is now fixed, and ADSI now works properly again.

  ASTERISK-29793 #close


- ### res_pjsip_header_funcs: Add custom parameter support.                           
  Author: Naveen Albert  
  Date:   2022-07-21  

  Adds support for custom URI and header parameters
  in the From header in PJSIP. Parameters can be
  both set and read using this function.

  ASTERISK-30150 #close


- ### func_presencestate: Fix invalid memory access.                                  
  Author: Naveen Albert  
  Date:   2022-11-13  

  When parsing information from AstDB while loading,
  it is possible that certain pointers are never
  set, which leads to invalid memory access and
  then, fatally, invalid free attempts on this memory.
  We now initialize to NULL to prevent this.

  ASTERISK-30311 #close


- ### sig_analog: Fix no timeout duration.                                            
  Author: Naveen Albert  
  Date:   2022-12-01  

  ASTERISK_28702 previously attempted to fix an
  issue with flash hook hold timing out after
  just under 17 minutes, when it should have never
  been timing out. It fixed this by changing 999999
  to INT_MAX, but it did so in chan_dahdi, which
  is the wrong place since ss_thread is now in
  sig_analog and the one in chan_dahdi is mostly
  dead code.

  This fixes this by porting the fix to sig_analog.

  ASTERISK-30336 #close


- ### xmldoc: Allow XML docs to be reloaded.                                          
  Author: Naveen Albert  
  Date:   2022-11-05  

  The XML docs are currently only loaded on
  startup with no way to update them during runtime.
  This makes it impossible to load modules that
  use ACO/Sorcery (which require documentation)
  if they are added to the source tree and built while
  Asterisk is running (e.g. external modules).

  This adds a CLI command to reload the XML docs
  during runtime so that documentation can be updated
  without a full restart of Asterisk.

  ASTERISK-30289 #close


- ### rtp_engine.h: Update examples using ast_format_set.                             
  Author: Naveen Albert  
  Date:   2022-11-24  

  This file includes some doxygen comments referencing
  ast_format_set. This is an obsolete API that was
  removed years back, but documentation was not fully
  updated to reflect that. These examples are
  updated to the current way of doing things
  (using the format cache).

  ASTERISK-30327 #close


- ### app_mixmonitor: Add option to use real Caller ID for voicemail.                 
  Author: Naveen Albert  
  Date:   2022-11-04  

  MixMonitor currently uses the Connected Line as the Caller ID
  for voicemails. This is due to the implementation being written
  this way for use with Digium phones. However, in general this
  is not correct for generic usage in the dialplan, and people
  may need the real Caller ID instead. This adds an option to do that.

  ASTERISK-30286 #close


- ### pjproject: 2.13 security fixes                                                  
  Author: Ben Ford  
  Date:   2022-11-29  

  Backports two security fixes (c4d3498 and 450baca) from pjproject 2.13.

  ASTERISK-30338


- ### pjsip_transport_events: Fix possible use after free on transport                
  Author: George Joseph  
  Date:   2022-10-10  

  It was possible for a module that registered for transport monitor
  events to pass in a pjsip_transport that had already been freed.
  This caused pjsip_transport_events to crash when looking up the
  monitor for the transport.  The fix is a two pronged approach.

  1. We now increment the reference count on pjsip_transports when we
  create monitors for them, then decrement the count when the
  transport is going to be destroyed.

  2. There are now APIs to register and unregister monitor callbacks
  by "transport key" which is a string concatenation of the remote ip
  address and port.  This way the module needing to monitor the
  transport doesn't have to hold on to the transport object itself to
  unregister.  It just has to save the transport_key.

  * Added the pjsip_transport reference increment and decrement.

  * Changed the internal transport monitor container key from the
    transport->obj_name (which may not be unique anyway) to the
    transport_key.

  * Added a helper macro AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR() that
    fills a buffer with the transport_key using a passed-in
    pjsip_transport.

  * Added the following functions:
    ast_sip_transport_monitor_register_key
    ast_sip_transport_monitor_register_replace_key
    ast_sip_transport_monitor_unregister_key
    and marked their non-key counterparts as deprecated.

  * Updated res_pjsip_pubsub and res_pjsip_outbound_register to use
    the new "key" monitor functions.

  NOTE: res_pjsip_registrar also uses the transport monitor
  functionality but doesn't have a persistent object other than
  contact to store a transport key.  At this time, it continues to
  use the non-key monitor functions.

  ASTERISK-30244


- ### manager: prevent file access outside of config dir                              
  Author: Mike Bradeen  
  Date:   2022-10-03  

  Add live_dangerously flag to manager and use this flag to
  determine if a configuation file outside of AST_CONFIG_DIR
  should be read.

  ASTERISK-30176


- ### ooh323c: not checking for IE minimum length                                     
  Author: Mike Bradeen  
  Date:   2022-06-06  

  When decoding q.931 encoded calling/called number
  now checking for length being less than minimum required.

  ASTERISK-30103


- ### pbx_builtins: Allow Answer to return immediately.                               
  Author: Naveen Albert  
  Date:   2022-11-11  

  The Answer application currently waits for up to 500ms
  for media, even if users specify a different timeout.

  This adds an option to not wait for media on the channel
  by doing a raw answer instead. The default 500ms threshold
  is also documented.

  ASTERISK-30308 #close


- ### chan_dahdi: Allow FXO channels to start immediately.                            
  Author: Naveen Albert  
  Date:   2022-11-11  

  Currently, chan_dahdi will wait for at least one
  ring before an incoming call can enter the dialplan.
  This is generally necessary in order to receive
  the Caller ID spill and/or distinctive ringing
  detection.

  However, if neither of these is required, then there
  is nothing gained by waiting for one ring and this
  unnecessarily delays call setup. Users can now
  use immediate=yes to make FXO channels (FXS signaled)
  begin processing dialplan as soon as Asterisk receives
  the call.

  ASTERISK-30305 #close


- ### core & res_pjsip: Improve topology change handling.                             
  Author: Maximilian Fridrich  
  Date:   2022-09-07  

  This PR contains two relatively separate changes in channel.c and
  res_pjsip_session.c which ensure that topology changes are not ignored
  in cases where they should be handled.

  For channel.c:

  The function ast_channel_request_stream_topology_change only triggers a
  stream topology request change indication, if the channel's topology
  does not equal the requested topology. However, a channel could be in a
  state where it is currently "negotiating" a new topology but hasn't
  updated it yet, so the topology request change would be lost. Channels
  need to be able to handle such situations internally and stream
  topology requests should therefore always be passed on.

  In the case of chan_pjsip for example, it queues a session refresh
  (re-INVITE) if it is currently in the middle of a transaction or has
  pending requests (among other reasons).

  Now, ast_channel_request_stream_topology_change always indicates a
  stream topology request change even if the requested topology equals the
  channel's topology.

  For res_pjsip_session.c:

  The function resolve_refresh_media_states does not process stream state
  changes if the delayed active state differs from the current active
  state. I.e. if the currently active stream state has changed between the
  time the sip session refresh request was queued and the time it is being
  processed, the session refresh is ignored. However, res_pjsip_session
  contains logic that ensures that session refreshes are queued and
  re-queued correctly if a session refresh is currently not possible. So
  this check is not necessary and led to some session refreshes being
  lost.

  Now, a session refresh is done even if the delayed active state differs
  from the current active state and it is checked whether the delayed
  pending state differs from the current active - because that means a
  refresh is necessary.

  Further, the unit test of resolve_refresh_media_states was adapted to
  reflect the new behavior. I.e. the changes to delayed pending are
  prioritized over the changes to current active because we want to
  preserve the original intention of the pending state.

  ASTERISK-30184


- ### sla: Prevent deadlock and crash due to autoservicing.                           
  Author: Naveen Albert  
  Date:   2022-09-24  

  SLAStation currently autoservices the station channel before
  creating a thread to actually dial the trunk. This leads
  to duplicate servicing of the channel which causes assertions,
  deadlocks, crashes, and moreover not the correct behavior.

  Removing the autoservice prevents the crash, but if the station
  hangs up before the trunk answers, the call hangs since the hangup
  was never serviced on the channel.

  This is fixed by not autoservicing the channel, but instead
  servicing it in the thread dialing the trunk, since it is doing
  so synchronously to begin with. Instead of sleeping for 100ms
  in a loop, we simply use the channel for timing, and abort
  if it disappears.

  The same issue also occurs with SLATrunk when a call is answered,
  because ast_answer invokes ast_waitfor_nandfds. Thus, we use
  ast_raw_answer instead which does not cause any conflict and allows
  the call to be answered normally without thread blocking issues.

  ASTERISK-29998 #close


- ### Build system: Avoid executable stack.                                           
  Author: Jaco Kroon  
  Date:   2022-11-07  

  Found in res_geolocation, but I believe others may have similar issues,
  thus not linking to a specific issue.

  Essentially gcc doesn't mark the stack for being non-executable unless
  it's compiling the source, this informs ld via gcc to mark the object as
  not requiring an executable stack (which a binary blob obviously
  doesn't).

  ASTERISK-30321

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### func_json: Fix memory leak.                                                     
  Author: Naveen Albert  
  Date:   2022-11-10  

  A memory leak was present in func_json due to
  using ast_json_free, which just calls ast_free,
  as opposed to recursively freeing the JSON
  object as needed. This is now fixed to use the
  right free functions.

  ASTERISK-30293 #close


- ### test_json: Remove duplicated static function.                                   
  Author: Naveen Albert  
  Date:   2022-11-10  

  Removes the function mkstemp_file and uses
  ast_file_mkftemp from file.h instead.

  ASTERISK-30295 #close


- ### res_agi: Respect "transmit_silence" option for "RECORD FILE".                   
  Author: Joshua C. Colp  
  Date:   2022-11-16  

  The "RECORD FILE" command in res_agi has its own
  implementation for actually doing the recording. This
  has resulted in it not actually obeying the option
  "transmit_silence" when recording.

  This change causes it to now send silence if the
  option is enabled.

  ASTERISK-30314


- ### app_mixmonitor: Add option to delete files on exit.                             
  Author: Naveen Albert  
  Date:   2022-11-03  

  Adds an option that allows MixMonitor to delete
  its copy of any recording files before exiting.

  This can be handy in conjunction with options
  like m, which copy the file elsewhere, and the
  original files may no longer be needed.

  ASTERISK-30284 #close


- ### manager: Update ModuleCheck documentation.                                      
  Author: Naveen Albert  
  Date:   2022-11-03  

  The ModuleCheck XML documentation falsely
  claims that the module's version number is returned.
  This has not been the case since 14, since the version
  number is not available anymore, but the documentation
  was not changed at the time. It is now updated to
  reflect this.

  ASTERISK-30285 #close


- ### file.c: Don't emit warnings on winks.                                           
  Author: Naveen Albert  
  Date:   2022-11-06  

  Adds an ignore case for wink since it should
  pass through with no warning.

  ASTERISK-30290 #close


- ### runUnittests.sh:  Save coredumps to proper directory                            
  Author: George Joseph  
  Date:   2022-11-02  

  Fixed the specification of "outputdir" when calling ast_coredumper
  so the txt files are saved in the correct place.

  ASTERISK-30282


- ### app_stack: Print proper exit location for PBXless channels.                     
  Author: Naveen Albert  
  Date:   2022-10-01  

  When gosub is executed on channels without a PBX, the context,
  extension, and priority are initialized to the channel driver's
  default location for that endpoint. As a result, the last Return
  will restore this location and the Gosub logs will print out bogus
  information about our exit point.

  To fix this, on channels that don't have a PBX, the execution
  location is left intact on the last return if there are no
  further stack frames left. This allows the correct location
  to be printed out to the user, rather than the bogus default
  context.

  ASTERISK-30076 #close


- ### chan_rtp: Make usage of ast_rtp_instance_get_local_address clearer              
  Author: George Joseph  
  Date:   2022-11-02  

  unicast_rtp_request() was setting the channel variables like this:

  pbx_builtin_setvar_helper(chan, "UNICASTRTP_LOCAL_ADDRESS",
      ast_sockaddr_stringify_addr(&local_address));
  ast_rtp_instance_get_local_address(instance, &local_address);
  pbx_builtin_setvar_helper(chan, "UNICASTRTP_LOCAL_PORT",
      ast_sockaddr_stringify_port(&local_address));

  ...which made it appear that UNICASTRTP_LOCAL_ADDRESS was being
  set before local_address was set.  In fact, the address part of
  local_address was set earlier in the function, just not the port.
  This was confusing however so ast_rtp_instance_get_local_address()
  is now being called before setting UNICASTRTP_LOCAL_ADDRESS.

  ASTERISK-30281


- ### res_pjsip: prevent crash on websocket disconnect                                
  Author: Mike Bradeen  
  Date:   2022-10-13  

  When a websocket (or potentially any stateful connection) is quickly
  created then destroyed, it is possible that the qualify thread will
  destroy the transaction before the initialzing thread is finished
  with it.

  Depending on the timing, this can cause an assertion within pjsip.

  To prevent this, ast_send_stateful_response will now create the group
  lock and add a reference to it before creating the transaction.

  While this should resolve the crash, there is still the potential that
  the contact will not be cleaned up properly, see:ASTERISK~29286. As a
  result, the contact has to 'time out' before it will be removed.

  ASTERISK-28689


- ### tcptls: Prevent crash when freeing OpenSSL errors.                              
  Author: Naveen Albert  
  Date:   2022-10-27  

  write_openssl_error_to_log has been erroneously
  using ast_free instead of free, which will
  cause a crash when MALLOC_DEBUG is enabled since
  the memory was not allocated by Asterisk's memory
  manager. This changes it to use the actual free
  function directly to avoid this.

  ASTERISK-30278 #close


- ### res_pjsip_outbound_registration: Allow to use multiple proxies for registration 
  Author: Igor Goncharovsky  
  Date:   2022-09-09  

  Current registration code use pjsip_parse_uri to verify outbound_proxy
  that is different from the reading this option for the endpoint. This
  made value with multiple proxies invalid for registration pjsip settings.
  Removing URI validation helps to use registration through multiple proxies.

  ASTERISK-30217 #close


- ### tests: Fix compilation errors on 32-bit.                                        
  Author: Naveen Albert  
  Date:   2022-10-23  

  Fix compilation errors caused by using size_t
  instead of uintmax_t and non-portable format
  specifiers.

  ASTERISK-30273 #close


- ### res_pjsip: return all codecs on a re-INVITE without SDP                         
  Author: Henning Westerholt  
  Date:   2022-08-26  

  Currently chan_pjsip on receiving a re-INVITE without SDP will only
  return the codecs that are previously negotiated and not offering
  all enabled codecs.

  This causes interoperability issues with different equipment (e.g.
  from Cisco) for some of our customers and probably also in other
  scenarios involving 3PCC infrastructure.

  According to RFC 3261, section 14.2 we SHOULD return all codecs
  on a re-INVITE without SDP

  The PR proposes a new parameter to configure this behaviour:
  all_codecs_on_empty_reinvite. It includes the code, documentation,
  alembic migrations, CHANGES file and example configuration additions.

  ASTERISK-30193 #close


- ### res_pjsip_notify: Add option support for AMI.                                   
  Author: Naveen Albert  
  Date:   2022-10-14  

  The PJSIP notify CLI commands allow for using
  "options" configured in pjsip_notify.conf.

  This allows these same options to be used in
  AMI actions as well.

  Additionally, as part of this improvement,
  some repetitive common code is refactored.

  ASTERISK-30263 #close


- ### res_pjsip_logger: Add method-based logging option.                              
  Author: Naveen Albert  
  Date:   2022-07-21  

  Expands the pjsip logger to support the ability to filter
  by SIP message method. This can make certain types of SIP debugging
  easier by only logging messages of particular method(s).

  ASTERISK-30146 #close

  Co-authored-by: Sean Bright <sean@seanbright.com>

- ### Dialing API: Cancel a running async thread, may not cancel all calls            
  Author: Frederic LE FOLL  
  Date:   2022-10-06  

  race condition: ast_dial_join() may not cancel outgoing call, if
  function is called just after called party answer and before
  application execution (bit is_running_app not yet set).

  This fix adds ast_softhangup() calls in addition to existing
  pthread_kill() when is_running_app is not set.

  ASTERISK-30258


- ### chan_dahdi: Fix unavailable channels returning busy.                            
  Author: Naveen Albert  
  Date:   2022-10-23  

  This fixes dahdi_request to properly set the cause
  code to CONGESTION instead of BUSY if no channels
  were actually available.

  Currently, the cause is erroneously set to busy
  if the channel itself is found, regardless of its
  current state. However, if the channel is not available
  (e.g. T1 down, card not operable, etc.), then the
  channel itself may not be in a functional state,
  in which case CHANUNAVAIL is the correct cause to use.

  This adds a simple check to ensure that busy tone
  is only returned if a channel is encountered that
  has an owner, since that is the only possible way
  that a channel could actually be busy.

  ASTERISK-30274 #close


- ### res_pjsip_pubsub: Prevent removing subscriptions.                               
  Author: Naveen Albert  
  Date:   2022-10-16  

  pjproject does not provide any mechanism of removing
  event packages, which means that once a subscription
  handler is registered, it is effectively permanent.

  pjproject will assert if the same event package is
  ever registered again, so currently unloading and
  loading any Asterisk modules that use subscriptions
  will cause a crash that is beyond our control.

  For that reason, we now prevent users from being
  able to unload these modules, to prevent them
  from ever being loaded twice.

  ASTERISK-30264 #close


- ### say: Don't prepend ampersand erroneously.                                       
  Author: Naveen Albert  
  Date:   2022-09-28  

  Some logic in say.c for determining if we need
  to also add an ampersand for file seperation was faulty,
  as non-successful files would increment the count, causing
  a leading ampersand to be added improperly.

  This is fixed, and a unit test that captures this regression
  is also added.

  ASTERISK-30248 #close


- ### res_crypto: handle unsafe private key files                                     
  Author: Philip Prindeville  
  Date:   2022-09-16  

  ASTERISK-30213 #close


- ### audiohook: add directional awareness                                            
  Author: Mike Bradeen  
  Date:   2022-09-29  

  Add enum to allow setting optional direction. If set to only one
  direction, only feed matching-direction frames to the associated
  slin factory.

  This prevents mangling the transcoder on non-mixed frames when the
  READ and WRITE frames would have otherwise required it.  Also
  removes the need to mute or discard the un-wanted frames as they
  are no longer added in the first place.

  res_stasis_snoop is changed to use this addition to set direction
  on audiohook based on spy direction.

  If no direction is set, the ast_audiohook_init will init this enum
  to BOTH which maintains existing functionality.

  ASTERISK-30252


- ### cdr: Allow bridging and dial state changes to be ignored.                       
  Author: Naveen Albert  
  Date:   2022-06-01  

  Allows bridging, parking, and dial messages to be globally
  ignored for all CDRs such that only a single CDR record
  is generated per channel.

  This is useful when CDRs should endure for the lifetime of
  an entire channel and bridging and dial updates in the
  dialplan should not result in multiple CDR records being
  created for the call. With the ignore bridging option,
  bridging changes have no impact on the channel's CDRs.
  With the ignore dial state option, multiple Dials and their
  outcomes have no impact on the channel's CDRs. The
  last disposition on the channel is preserved in the CDR,
  so the actual disposition of the call remains available.

  These two options can reduce the amount of "CDR hacks" that
  have hitherto been necessary to ensure that CDR was not
  "spoiled" by these messages if that was undesired, such as
  putting a dummy optimization-disabled local channel between
  the caller and the actual call and putting the CDR on the channel
  in the middle to ensure that CDR would persist for the entire
  call and properly record start, answer, and end times.
  Enabling these options is desirable when calls correspond
  to the entire lifetime of channels and the CDR should
  reflect that.

  Current default behavior remains unchanged.

  ASTERISK-30091 #close


- ### res_tonedetect: Add ringback support to TONE_DETECT.                            
  Author: Naveen Albert  
  Date:   2022-09-30  

  Adds support for detecting audible ringback tone
  to the TONE_DETECT function using the p option.

  ASTERISK-30254 #close


- ### chan_dahdi: Resolve format truncation warning.                                  
  Author: Naveen Albert  
  Date:   2022-10-01  

  Fixes a format truncation warning in notify_message.

  ASTERISK-30256 #close


- ### res_crypto: don't modify fname in try_load_key()                                
  Author: Philip Prindeville  
  Date:   2022-09-16  

  "fname" is passed in as a const char *, but strstr() mangles that
  into a char *, and we were attempting to modify the string in place.
  This is an unwanted (and undocumented) side-effect.

  ASTERISK-30213


- ### res_crypto: use ast_file_read_dirs() to iterate                                 
  Author: Philip Prindeville  
  Date:   2022-09-15  

  ASTERISK-30213


- ### res_geolocation: Update wiki documentation                                      
  Author: George Joseph  
  Date:   2022-09-27  

  Also added a note to the geolocation.conf.sample file
  and added a README to the res/res_geolocation/wiki
  directory.


- ### res_pjsip: Add mediasec capabilities.                                           
  Author: Maximilian Fridrich  
  Date:   2022-07-26  

  This patch adds support for mediasec SIP headers and SDP attributes.
  These are defined in RFC 3329, 3GPP TS 24.229 and
  draft-dawes-sipcore-mediasec-parameter. The new features are
  implemented so that a backbone for RFC 3329 is present to streamline
  future work on RFC 3329.

  With this patch, Asterisk can communicate with Deutsche Telekom trunks
  which require these fields.

  ASTERISK-30032


- ### res_prometheus: Do not crash on invisible bridges                               
  Author: Holger Hans Peter Freyther  
  Date:   2022-09-20  

  Avoid crashing by skipping invisible bridges and checking the
  snapshot for a null pointer. In effect this is how the bridges
  are enumerated in res/ari/resource_bridges.c already.

  ASTERISK-30239
  ASTERISK-30237


- ### res_pjsip_geolocation: Change some notices to debugs.                           
  Author: Naveen Albert  
  Date:   2022-09-19  

  If geolocation is not in use for an endpoint, the NOTICE
  log level is currently spammed with messages about this,
  even though nothing is wrong and these messages provide
  no real value. These log messages are therefore changed
  to debugs.

  ASTERISK-30241 #close


- ### db: Fix incorrect DB tree count for AMI.                                        
  Author: Naveen Albert  
  Date:   2022-09-24  

  The DBGetTree AMI action's ListItem previously
  always reported 1, regardless of the count. This
  is corrected to report the actual count.

  ASTERISK-30245 #close
  patches:
    gettreecount.diff submitted by Birger Harzenetter (license 5870)


- ### func_logic: Don't emit warning if both IF branches are empty.                   
  Author: Naveen Albert  
  Date:   2022-09-21  

  The IF function currently emits warnings if both IF branches
  are empty. However, there is no actual necessity that either
  branch be non-empty as, unlike other conditional applications/
  functions, nothing is inherently done with IF, and both
  sides could legitimately be empty. The warning is thus turned
  into a debug message.

  ASTERISK-30243 #close


- ### features: Add no answer option to Bridge.                                       
  Author: Naveen Albert  
  Date:   2022-09-11  

  Adds the n "no answer" option to the Bridge application
  so that answer supervision can not automatically
  be provided when Bridge is executed.

  Additionally, a mechanism (dialplan variable)
  is added to prevent bridge targets (typically the
  target of a masquerade) from answering the channel
  when they enter the bridge.

  ASTERISK-30223 #close


- ### app_bridgewait: Add option to not answer channel.                               
  Author: Naveen Albert  
  Date:   2022-09-09  

  Adds the n option to not answer the channel when calling
  BridgeWait, so the application can be used without
  forcing answer supervision.

  ASTERISK-30216 #close


- ### app_amd: Add option to play audio during AMD.                                   
  Author: Naveen Albert  
  Date:   2022-08-15  

  Adds an option that will play an audio file
  to the party while AMD is running on the
  channel, so the called party does not just
  hear silence.

  ASTERISK-30179 #close


- ### test: initialize capture structure before freeing                               
  Author: Philip Prindeville  
  Date:   2022-09-15  

  ASTERISK-30232 #close


- ### func_export: Add EXPORT function                                                
  Author: Naveen Albert  
  Date:   2021-05-17  

  Adds the EXPORT function, which allows write
  access to variables and functions on other
  channels.

  ASTERISK-29432 #close


- ### res_pjsip: Add 100rel option "peer_supported".                                  
  Author: Maximilian Fridrich  
  Date:   2022-07-26  

  This patch adds a new option to the 100rel parameter for pjsip
  endpoints called "peer_supported". When an endpoint with this option
  receives an incoming request and the request indicated support for the
  100rel extension, then Asterisk will send 1xx responses reliably. If
  the request did not indicate 100rel support, Asterisk sends 1xx
  responses normally.

  ASTERISK-30158


- ### func_scramble: Fix null pointer dereference.                                    
  Author: Naveen Albert  
  Date:   2022-09-10  

  Fix segfault due to null pointer dereference
  inside the audiohook callback.

  ASTERISK-30220 #close


- ### manager: be more aggressive about purging http sessions.                        
  Author: Jaco Kroon  
  Date:   2022-09-05  

  If we find that n_max (currently hard wired to 1) sessions were purged,
  schedule the next purge for 1ms into the future rather than 5000ms (as
  per current).  This way we will purge up to 1000 sessions per second
  rather than 1 every 5 seconds.

  This mitigates a build-up of sessions should http sessions gets
  established faster than 1 per 5 seconds.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### func_strings: Add trim functions.                                               
  Author: Naveen Albert  
  Date:   2022-09-11  

  Adds TRIM, LTRIM, and RTRIM, which can be used
  for trimming leading and trailing whitespace
  from strings.

  ASTERISK-30222 #close


- ### res_crypto: Memory issues and uninitialized variable errors                     
  Author: George Joseph  
  Date:   2022-09-16  

  ASTERISK-30235


- ### res_geolocation: Fix issues exposed by compiling with -O2                       
  Author: George Joseph  
  Date:   2022-09-16  

  Fixed "may be used uninitialized" errors in geoloc_config.c.

  ASTERISK-30234


- ### res_crypto: don't complain about directories                                    
  Author: Philip Prindeville  
  Date:   2022-09-13  

  ASTERISK-30226 #close


- ### res_pjsip: Add user=phone on From and PAID for usereqphone=yes                  
  Author: Mike Bradeen  
  Date:   2022-08-15  

  Adding user=phone to local-side uri's when user_eq_phone=yes is set for
  an endpoint. Previously this would only add the header to the To and R-URI.

  ASTERISK-30178


- ### res_geolocation: Fix segfault when there's an empty element                     
  Author: George Joseph  
  Date:   2022-09-13  

  Fixed a segfault caused by var_list_from_loc_info() encountering
  an empty location info element.

  Fixed an issue in ast_strsep() where a value with only whitespace
  wasn't being preserved.

  Fixed an issue in ast_variable_list_from_quoted_string() where
  an empty value was considered a failure.

  ASTERISK-30215
  Reported by: Dan Cropp


- ### res_musiconhold: Add option to not play music on hold on unanswered channels    
  Author: sungtae kim  
  Date:   2022-08-14  

  This change adds an option, answeredonly, that will prevent music on
  hold on channels that are not answered.

  ASTERISK-30135


- ### res_pjsip: Add TEL URI support for basic calls.                                 
  Author: Ben Ford  
  Date:   2022-08-02  

  This change allows TEL URI requests to come through for basic calls. The
  allowed requests are INVITE, ACK, BYE, and CANCEL. The From and To
  headers will now allow TEL URIs, as well as the request URI.

  Support is only for TEL URIs present in traffic from a remote party.
  Asterisk does not generate any TEL URIs on its own.

  ASTERISK-26894


- ### res_crypto: Use EVP API's instead of legacy API's                               
  Author: Philip Prindeville  
  Date:   2022-03-24  

  ASTERISK-30046 #close


- ### test: Add coverage for res_crypto                                               
  Author: Philip Prindeville  
  Date:   2022-05-03  

  We're validating the following functionality:

  encrypting a block of data with RSA
  decrypting a block of data with RSA
  signing a block of data with RSA
  verifying a signature with RSA
  encrypting a block of data with AES-ECB
  encrypting a block of data with AES-ECB

  as well as accessing test keys from the keystore.

  ASTERISK-30045 #close


- ### res_crypto: make keys reloadable on demand for testing                          
  Author: Philip Prindeville  
  Date:   2022-07-26  

  ASTERISK-30045


- ### test: Add test coverage for capture child process output                        
  Author: Philip Prindeville  
  Date:   2022-05-03  

  ASTERISK-30037 #close


- ### main/utils: allow checking for command in $PATH                                 
  Author: Philip Prindeville  
  Date:   2022-07-26  

  ASTERISK-30037


- ### test: Add ability to capture child process output                               
  Author: Philip Prindeville  
  Date:   2022-05-02  

  ASTERISK-30037


- ### res_crypto: Don't load non-regular files in keys directory                      
  Author: Philip Prindeville  
  Date:   2022-04-26  

  ASTERISK-30046


- ### func_frame_trace: Remove bogus assertion.                                       
  Author: Naveen Albert  
  Date:   2022-09-08  

  The FRAME_TRACE function currently asserts if it sees
  a MASQUERADE_NOTIFY. However, this is a legitimate thing
  that can happen so asserting is inappropriate, as there
  are no clear negative ramifications of such a thing. This
  is adjusted to be like the other frames to print out
  the subclass.

  ASTERISK-30210 #close


- ### lock.c: Add AMI event for deadlocks.                                            
  Author: Naveen Albert  
  Date:   2022-07-27  

  Adds an AMI event to indicate that a deadlock
  has likely started, when Asterisk is compiled
  with DETECT_DEADLOCKS enabled. This can make
  it easier to perform automated deadlock detection
  and take appropriate action (such as doing a core
  dump). Unlike the deadlock warnings, the AMI event
  is emitted only once per deadlock.

  ASTERISK-30161 #close


- ### app_confbridge: Add end_marked_any option.                                      
  Author: Naveen Albert  
  Date:   2022-09-04  

  Adds the end_marked_any option, which can be used
  to kick a user from a conference if any marked user
  leaves.

  ASTERISK-30211 #close


- ### pbx_variables: Use const char if possible.                                      
  Author: Naveen Albert  
  Date:   2022-09-03  

  Use const char for char arguments to
  pbx_substitute_variables_helper_full_location
  that can do so (context and exten).

  ASTERISK-30209 #close


- ### res_geolocation: Add two new options to GEOLOC_PROFILE                          
  Author: George Joseph  
  Date:   2022-08-25  

  Added an 'a' option to the GEOLOC_PROFILE function to allow
  variable lists like location_info_refinement to be appended
  to instead of replacing the entire list.

  Added an 'r' option to the GEOLOC_PROFILE function to resolve all
  variables before a read operation and after a Set operation.

  Added a few missing parameters to the ones allowed for writing
  with GEOLOC_PROFILE.

  Fixed a bug where calling GEOLOC_PROFILE to read a parameter
  might actually update the profile object.

  Cleaned up XML documentation a bit.

  ASTERISK-30190


- ### res_geolocation:  Allow location parameters on the profile object               
  Author: George Joseph  
  Date:   2022-08-18  

  You can now specify the location object's format, location_info,
  method, location_source and confidence parameters directly on
  a profile object for simple scenarios where the location
  information isn't common with any other profiles.  This is
  mutually exclusive with setting location_reference on the
  profile.

  Updated appdocsxml.dtd to allow xi:include in a configObject
  element.  This makes it easier to link to complete configOptions
  in another object.  This is used to add the above fields to the
  profile object without having to maintain the option descriptions
  in two places.

  ASTERISK-30185


- ### res_geolocation: Add profile parameter suppress_empty_ca_elements               
  Author: George Joseph  
  Date:   2022-08-17  

  Added profile parameter "suppress_empty_ca_elements" that
  will cause Civic Address elements that are empty to be
  suppressed from the outgoing PIDF-LO document.

  Fixed a possible SEGV if a sub-parameter value didn't have a
  value.

  ASTERISK-30177


- ### res_geolocation:  Add built-in profiles                                         
  Author: George Joseph  
  Date:   2022-08-16  

  The trigger to perform outgoing geolocation processing is the
  presence of a geoloc_outgoing_call_profile on an endpoint. This
  is intentional so as to not leak location information to
  destinations that shouldn't receive it.   In a totally dynamic
  configuration scenario however, there may not be any profiles
  defined in geolocation.conf.  This makes it impossible to do
  outgoing processing without defining a "dummy" profile in the
  config file.

  This commit adds 4 built-in profiles:
    "<prefer_config>"
    "<discard_config>"
    "<prefer_incoming>"
    "<discard_incoming>"
  The profiles are empty except for having their precedence
  set and can be set on an endpoint to allow processing without
  entries in geolocation.conf.  "<discard_config>" is actually the
  best one to use in this situation.

  ASTERISK-30182


- ### res_pjsip_sdp_rtp: Skip formats without SDP details.                            
  Author: Joshua C. Colp  
  Date:   2022-08-30  

  When producing an outgoing SDP we iterate through the configured
  formats and produce SDP information. It is possible for some
  configured formats to not have SDP information available. If this
  is the case we skip over them to allow the SDP to still be
  produced.

  ASTERISK-29185


- ### cli: Prevent assertions on startup from bad ao2 refs.                           
  Author: Naveen Albert  
  Date:   2022-05-03  

  If "core show channels" is run before startup has completed, it
  is possible for bad ao2 refs to occur because the system is not
  yet fully initialized. This will lead to an assertion failing.

  To prevent this, initialization of CLI builtins is moved to be
  later along in the main load sequence. Core CLI commands are
  loaded at the same time, but channel-related commands are loaded
  later on.

  ASTERISK-29846 #close


- ### pjsip: Add TLS transport reload support for certificate and key.                
  Author: Joshua C. Colp  
  Date:   2022-08-19  

  This change adds support using the pjsip_tls_transport_restart
  function for reloading the TLS certificate and key, if the filenames
  remain unchanged. This is useful for Let's Encrypt and other
  situations. Note that no restart of the transport will occur if
  the certificate and key remain unchanged.

  ASTERISK-30186


- ### res_tonedetect: Fix typos referring to wrong variables.                         
  Author: Naveen Albert  
  Date:   2022-08-25  

  Fixes two typos that cause fax detection to not work.
  One refers to the wrong frame variable, and the other
  refers to the subclass.integer instead of the frametype
  as it should.

  ASTERISK-30192 #close


- ### alembic: add missing ps_endpoints columns                                       
  Author: Mike Bradeen  
  Date:   2022-08-17  

  The following required columns were missing,
  now added to the ps_endpoints table:

  incoming_call_offer_pref
  outgoing_call_offer_pref
  stir_shaken_profile

  ASTERISK-29453


- ### chan_dahdi.c: Resolve a format-truncation build warning.                        
  Author: Sean Bright  
  Date:   2022-08-19  

  With gcc (Ubuntu 11.2.0-19ubuntu1) 11.2.0:

  > chan_dahdi.c:4129:18: error: ‘%s’ directive output may be truncated
  >   writing up to 255 bytes into a region of size between 242 and 252
  >   [-Werror=format-truncation=]

  This removes the error-prone sizeof(...) calculations in favor of just
  doubling the size of the base buffer.


- ### res_pjsip_pubsub: Postpone destruction of old subscriptions on RLS update       
  Author: Alexei Gradinari  
  Date:   2022-08-03  

  Set termination state to old subscriptions to prevent queueing and sending
  NOTIFY messages on exten/device state changes.

  Postpone destruction of old subscriptions until all already queued tasks
  that may be using old subscriptions have completed.

  ASTERISK-29906


- ### channel.h: Remove redundant declaration.                                        
  Author: Sean Bright  
  Date:   2022-08-15  

  The DECLARE_STRINGFIELD_SETTERS_FOR() declares ast_channel_name_set()
  for us, so no need to declare it separately.


- ### features: Add transfer initiation options.                                      
  Author: Naveen Albert  
  Date:   2022-02-05  

  Adds additional control options over the transfer
  feature functionality to give users more control
  in how the transfer feature sounds and works.

  First, the "transfer" sound that plays when a transfer is
  initiated can now be customized by the user in
  features.conf, just as with the other transfer sounds.

  Secondly, the user can now specify the transfer extension
  in advance by using the TRANSFER_EXTEN variable. If
  a valid extension is contained in this variable, the call
  will automatically be transferred to this destination.
  Otherwise, it will fall back to collecting the extension
  from the user as is always done now.

  ASTERISK-29899 #close


- ### CI: Fixing path issue on venv check                                             
  Author: Mike Bradeen  
  Date:   2022-08-31  

  ASTERISK-26826


- ### CI: use Python3 virtual environment                                             
  Author: Mike Bradeen  
  Date:   2022-08-11  

  Requires Python3 testsuite changes

  ASTERISK-26826


- ### general: Very minor coding guideline fixes.                                     
  Author: Naveen Albert  
  Date:   2022-07-28  

  Fixes a few coding guideline violations:
  * Use of C99 comments
  * Opening brace on same line as function prototype

  ASTERISK-30163 #close


- ### res_geolocation: Address user issues, remove complexity, plug leaks             
  Author: George Joseph  
  Date:   2022-08-05  

  * Added processing for the 'confidence' element.
  * Added documentation to some APIs.
  * removed a lot of complex code related to the very-off-nominal
    case of needing to process multiple location info sources.
  * Create a new 'ast_geoloc_eprofile_to_pidf' API that just takes
    one eprofile instead of a datastore of multiples.
  * Plugged a huge leak in XML processing that arose from
    insufficient documentation by the libxml/libxslt authors.
  * Refactored stylesheets to be more efficient.
  * Renamed 'profile_action' to 'profile_precedence' to better
    reflect it's purpose.
  * Added the config option for 'allow_routing_use' which
    sets the value of the 'Geolocation-Routing' header.
  * Removed the GeolocProfileCreate and GeolocProfileDelete
    dialplan apps.
  * Changed the GEOLOC_PROFILE dialplan function as follows:
    * Removed the 'profile' argument.
    * Automatically create a profile if it doesn't exist.
    * Delete a profile if 'inheritable' is set to no.
  * Fixed various bugs and leaks
  * Updated Asterisk WiKi documentation.

  ASTERISK-30167


- ### chan_iax2: Add missing options documentation.                                   
  Author: Naveen Albert  
  Date:   2022-07-30  

  Adds missing dial resource option documentation.

  ASTERISK-30164 #close


- ### app_confbridge: Fix memory leak on updated menu options.                        
  Author: Naveen Albert  
  Date:   2022-08-01  

  If the CONFBRIDGE function is used to dynamically set
  menu options, a memory leak occurs when a menu option
  that has been set is overridden, since the menu entry
  is not destroyed before being freed. This ensures that
  it is.

  Additionally, logic that duplicates the destroy function
  is removed in lieu of the destroy function itself.

  ASTERISK-28422 #close


- ### Geolocation: Wiki Documentation                                                 
  Author: George Joseph  
  Date:   2022-07-19  


- ### manager: Remove documentation for nonexistent action.                           
  Author: Naveen Albert  
  Date:   2022-07-28  

  The manager XML documentation documents a "FilterList"
  action, but there is no such action. Therefore, this can
  lead to confusion when people try to use a documented
  action that does not, in fact, exist. This is removed
  as the action never did exist in the past, nor would it
  be trivial to add since we only store the regex_t
  objects, so the filter list can't actually be provided
  without storing that separately. Most likely, the
  documentation was originally added (around version 10)
  in anticipation of something that never happened.

  ASTERISK-29917 #close


- ### general: Improve logging levels of some log messages.                           
  Author: Naveen Albert  
  Date:   2022-07-22  

  Adjusts some logging levels to be more or less important,
  that is more prominent when actual problems occur and less
  prominent for less noteworthy things.

  ASTERISK-30153 #close


- ### cdr.conf: Remove obsolete app_mysql reference.                                  
  Author: Naveen Albert  
  Date:   2022-07-27  

  The CDR sample config still mentions that app_mysql
  is available in the addons directory, but this is
  incorrect as it was removed as of 19. This removes
  that to avoid confusion.

  ASTERISK-30160 #close


- ### general: Remove obsolete SVN references.                                        
  Author: Naveen Albert  
  Date:   2022-07-27  

  There are a handful of files in the tree that
  reference an SVN link for the coding guidelines.

  This removes these because the links are dead
  and the vast majority of source files do not
  contain these links, so this is more consistent.

  app_skel still maintains an (up to date) link
  to the coding guidelines.

  ASTERISK-30159 #close


- ### app_confbridge: Add missing AMI documentation.                                  
  Author: Naveen Albert  
  Date:   2022-07-23  

  Documents the ConfbridgeListRooms AMI response,
  which is currently not documented.

  ASTERISK-30020 #close


- ### app_meetme: Add missing AMI documentation.                                      
  Author: Naveen Albert  
  Date:   2022-07-23  

  The MeetmeList and MeetmeListRooms AMI
  responses are currently completely undocumented.
  This adds documentation for these responses.

  ASTERISK-30018 #close


- ### func_srv: Document field parameter.                                             
  Author: Naveen Albert  
  Date:   2022-07-23  

  Adds missing documentation for the field parameter
  for the SRVRESULT function.

  ASTERISK-30151
  Reported by: Chris Young


- ### pbx_functions.c: Manually update ast_str strlen.                                
  Author: Naveen Albert  
  Date:   2022-07-23  

  When ast_func_read2 is used to read a function using
  its read function (as opposed to a native ast_str read2
  function), the result is copied directly by the function
  into the ast_str buffer. As a result, the ast_str length
  remains initialized to 0, which is a bug because this is
  not the real string length.

  This can cascade and have issues elsewhere, such as when
  reading substrings of functions that only register read
  as opposed to read2 callbacks. In this case, since reading
  ast_str_strlen returns 0, the returned substring is empty
  as opposed to the actual substring. This has caused
  the ast_str family of functions to behave inconsistently
  and erroneously, in contrast to the pbx_variables substitution
  functions which work correctly.

  This fixes this issue by manually updating the ast_str length
  when the result is copied directly into the ast_str buffer.

  Additionally, an assertion and a unit test that previously
  exposed these issues are added, now that the issue is fixed.

  ASTERISK-29966 #close


- ### build: fix bininstall launchd issue on cross-platform build                     
  Author: Sergey V. Lobanov  
  Date:   2022-02-19  

  configure script detects /sbin/launchd, but the result of this
  check is not used in Makefile (bininstall). Makefile also detects
  /sbin/launchd file to decide if it is required to install
  safe_asterisk.

  configure script correctly detects cross compile build and sets
  PBX_LAUNCHD=0

  In case of building asterisk on MacOS host for Linux target using
  external toolchain (e.g. OpenWrt toolchain), bininstall does not
  install safe_asterisk (due to /sbin/launchd detection in Makefile),
  but it is required on target (Linux).

  This patch adds HAVE_SBIN_LAUNCHD=@PBX_LAUNCHD@ to makeopts.in to
  use the result of /sbin/launchd detection from configure script in
  Makefile.
  Also this patch uses HAVE_SBIN_LAUNCHD in Makefile (bininstall) to
  decide if it is required to install safe_asterisk.

  ASTERISK-29905 #close


- ### db: Add AMI action to retrieve DB keys at prefix.                               
  Author: Naveen Albert  
  Date:   2022-07-11  

  Adds the DBGetTree action, which can be used to
  retrieve all of the DB keys beginning with a
  particular prefix, similar to the capability
  provided by the database show CLI command.

  ASTERISK-30136 #close


- ### manager: Fix incomplete filtering of AMI events.                                
  Author: Naveen Albert  
  Date:   2022-07-12  

  The global event filtering code was only in one
  possible execution path, so not all events were
  being properly filtered out if requested. This moves
  that into the universal AMI handling code so all
  events are properly handled.

  Additionally, the CLI listing of disabled events can
  also get truncated, so we now print out everything.

  ASTERISK-30137 #close


- ### Update defaultbranch to 20                                                      
  Author: George Joseph  
  Date:   2022-07-20  


- ### res_pjsip: delay contact pruning on Asterisk start                              
  Author: Michael Neuhauser  
  Date:   2022-06-14  

  Move the call to ast_sip_location_prune_boot_contacts() *after* the call
  to ast_res_pjsip_init_options_handling() so that
  res/res_pjsip/pjsip_options.c is informed about the contact deletion and
  updates its sip_options_contact_statuses list. This allows for an AMI
  event to be sent by res/res_pjsip/pjsip_options.c if the endpoint
  registers again from the same remote address and port (i.e., same URI)
  as used before the Asterisk restart.

  ASTERISK-30109
  Reported-by: Michael Neuhauser


- ### chan_dahdi: Fix buggy and missing Caller ID parameters                          
  Author: Naveen Albert  
  Date:   2022-03-29  

  There are several things wrong with analog Caller ID
  handling that are fixed by this commit:

  callerid.c's Caller ID generation function contains the
  logic to use the presentation to properly send the proper
  Caller ID. However, currently, DAHDI does not pass any
  presentation information to the Caller ID module, which
  means that presentation is completely ignored on all calls.
  This means that lines could be getting Caller ID information
  they aren't supposed to.

  Part of the reason this has been obscured is because the
  simple switch logic for handling the built in *67 and *82
  is completely wrong. Rather than modifying the presentation
  for the call accordingly (which is what it's supposed to do),
  it simply blanks out the Caller ID or fills it in. This is
  wrong, so wrong that it makes a mockery of the specification.
  Additionally, it would leave to the "UNAVAILABLE" disposition
  being used for Caller ID generation as opposed to the "PRIVATE"
  disposition that it should have been using. This is now fixed
  to only update the presentation and not modify the number and
  name, so that the simple switch *67/*82 work correctly.

  Next, sig_analog currently only copies over the name and number,
  nothing else, when it is filling in a duplicated caller id
  structure. Thus, we also now copy over the presentation
  information so that is available for the Caller ID spill.
  Additionally, this meant that "valid" was implicitly 0,
  and as such presentation would always fail to "Unavailable".
  The validity is therefore also copied over so it can be used
  by ast_party_id_presentation.

  As part of this fix, new API is added so that all the relevant
  Caller ID information can be passed in to the Caller ID generation
  functions. Parameters that are also completely missing from the
  Caller ID spill have also been added, to enhance the compatibility,
  correctness, and completeness of the Asterisk Caller ID implementation.

  ASTERISK-29991 #close


- ### queues.conf.sample: Correction of typo                                          
  Author: Sam Banks  
  Date:   2022-07-11  

  ASTERISK-30126 #close


- ### chan_dahdi: Add POLARITY function.                                              
  Author: Naveen Albert  
  Date:   2022-04-01  

  Adds a POLARITY function which can be used to
  retrieve the current polarity of an FXS channel
  as well as set the polarity of an FXS channel
  to idle or reverse at any point during a call.

  ASTERISK-30000 #close


- ### Makefile: Avoid git-make user conflict                                          
  Author: Mike Bradeen  
  Date:   2022-06-01  

  make_version now silently checks if the required git commands will
  fail.  If they do, then return UNKNOWN__git_check_fail to
  distinguish this failure from other UNKNOWN__ version failures

  Makefile checks for this value on install and exits out with
  instructions

  ASTERISK-30029


- ### app_confbridge: Always set minimum video update interval.                       
  Author: Naveen Albert  
  Date:   2022-06-18  

  Currently, if multiple video-enabled ConfBridges are
  conferenced together, we immediately get into a scenario
  where an infinite sequence of video updates fills up
  the taskprocessor queue and causes memory consumption
  to climb unabated until Asterisk is killed. This is due
  to the core bridging mechanism that provides video updates
  (softmix_bridge_write_control in bridge_softmix.c)
  continously updating all the channels in the bridge with
  video updates.

  The logic to do so in the core is that the video updates
  should be provided if the video_update_discard property
  for the bridge is 0, or if enough time has elapsed since
  the last video update. Thus, we already have a safeguard
  built in to ensure the scenario described above does not
  happen. Currently, however, this safeguard is not being
  adequately ensured.

  In app_confbridge, the video_update_discard property
  defaults to 2000, which is a healthy value that should
  completely prevent this issue. However, this value is
  only set onto the bridge in the SFU video mode. This
  leaves video modes such as follow_talker completely
  vulnerable, since video_update_discard will actually
  be 0, since the default or set value was never applied.
  As a result, the core bridging mechanism will always
  try to provide video updates regardless of when the last
  one was sent.

  To prevent this issue from happening, we now always
  set the video_update_discard property on the bridge
  with the value from the bridge profile. The app_confbridge
  defaults will thus ensure that infinite video updates
  no longer happen in any video mode.

  ASTERISK-29907 #close


- ### pbx.c: Simplify ast_context memory management.                                  
  Author: Sean Bright  
  Date:   2022-07-05  

  Allocate all of the ast_context's character data in the structure's
  flexible array member and eliminate the clunky fake_context. This will
  simplify future changes to ast_context.


- ### geoloc_eprofile.c: Fix setting of loc_src in set_loc_src()                      
  Author: George Joseph  
  Date:   2022-07-13  

  line 196:    loc_src = '\0';
  should have been
  line 196:    *loc_src = '\0';

  The issue was caught by the gcc optimizer complaining that
  loc_src had a zero length because the pointer itself was being
  set to NULL instead of the _contents_ of the pointer being set
  to the NULL terminator.

  ASTERISK-30138
  Reported-by: Sean Bright


- ### Geolocation:  chan_pjsip Capability Preview                                     
  Author: George Joseph  
  Date:   2022-07-07  

  This commit adds res_pjsip_geolocation which gives chan_pjsip
  the ability to use the core geolocation capabilities.

  This commit message is intentionally short because this isn't
  a simple capability.  See the documentation at
  https://wiki.asterisk.org/wiki/display/AST/Geolocation
  for more information.

  THE CAPABILITIES IMPLEMENTED HERE MAY CHANGE BASED ON
  USER FEEDBACK!

  ASTERISK-30128


- ### Geolocation:  Core Capability Preview                                           
  Author: George Joseph  
  Date:   2022-02-15  

  This commit adds res_geolocation which creates the core capabilities
  to manipulate Geolocation information on SIP INVITEs.

  An upcoming commit will add res_pjsip_geolocation which will
  allow the capabilities to be used with the pjsip channel driver.

  This commit message is intentionally short because this isn't
  a simple capability.  See the documentation at
  https://wiki.asterisk.org/wiki/display/AST/Geolocation
  for more information.

  THE CAPABILITIES IMPLEMENTED HERE MAY CHANGE BASED ON
  USER FEEDBACK!

  ASTERISK-30127


- ### general: Fix various typos.                                                     
  Author: Naveen Albert  
  Date:   2022-06-01  

  ASTERISK-30089 #close


- ### cel_odbc & res_config_odbc: Add support for SQL_DATETIME field type             
  Author: Kevin Harwell  
  Date:   2022-06-17  

  See also: ASTERISK_30023

  ASTERISK-30096 #close
  patches:
    inline on issue - submitted by Morvai Szabolcs


- ### chan_iax2: Allow compiling without OpenSSL.                                     
  Author: Naveen Albert  
  Date:   2022-07-04  

  ASTERISK_30007 accidentally made OpenSSL a
  required depdendency. This adds an ifdef so
  the relevant code is compiled only if OpenSSL
  is available, since it only needs to be executed
  if OpenSSL is available anyways.

  ASTERISK-30083 #close


- ### websocket / aeap: Handle poll() interruptions better.                           
  Author: Joshua C. Colp  
  Date:   2022-06-28  

  A sporadic test failure was happening when executing the AEAP
  Websocket transport tests. It was originally thought this was
  due to things not getting cleaned up fast enough, but upon further
  investigation I determined the underlying cause was poll()
  getting interrupted and this not being handled in all places.

  This change adds EINTR and EAGAIN handling to the Websocket
  client connect code as well as the AEAP Websocket transport code.
  If either occur then the code will just go back to waiting
  for data.

  The originally disabled failure test case has also been
  re-enabled.

  ASTERISK-30099


- ### res_cliexec: Add dialplan exec CLI command.                                     
  Author: Naveen Albert  
  Date:   2022-05-14  

  Adds a CLI command similar to "dialplan eval function" except for
  applications: "dialplan exec application", useful for quickly
  testing certain application behavior directly from the CLI
  without writing any dialplan.

  ASTERISK-30062 #close


- ### features: Update documentation for automon and automixmon                       
  Author: Trevor Peirce  
  Date:   2022-07-03  

  The current documentation is out of date and does not reflect actual
  behaviour.  This change makes documentation clearer and accurately
  reflect the purpose of relevant channel variables.

  ASTERISK-30123


- ### Geolocation: Base Asterisk Prereqs                                              
  Author: George Joseph  
  Date:   2022-06-27  

  * Added ast_variable_list_from_quoted_string()
    Parse a quoted string into an ast_variable list.

  * Added ast_str_substitute_variables_full2()
    Perform variable/function/expression substitution on an ast_str.

  * Added ast_strsep_quoted()
    Like ast_strsep except you can specify a specific quote character.
    Also added unit test.

  * Added ast_xml_find_child_element()
    Find a direct child element by name.

  * Added ast_xml_doc_dump_memory()
    Dump the specified document to a buffer

  * ast_datastore_free() now checks for a NULL datastore
    before attempting to destroy it.


- ### pbx_lua: Remove compiler warnings                                               
  Author: Boris P. Korzun  
  Date:   2022-06-24  

  Improved variable definitions (specified correct type) for avoiding
  compiler warnings.

  ASTERISK-30117 #close


- ### res_pjsip_header_funcs: Add functions PJSIP_RESPONSE_HEADER and PJSIP_RESPONSE..
  Author: Jose Lopes  
  Date:   2022-04-08  

  These new functions allow retrieving information from headers on 200 OK
  INVITE response.

  ASTERISK-29999


- ### res_prometheus: Optional load res_pjsip_outbound_registration.so                
  Author: Boris P. Korzun  
  Date:   2022-06-09  

  Switched res_pjsip_outbound_registration.so dep to optional. Added
  module loaded check before using it.

  ASTERISK-30101 #close


- ### app_dial: Fix dial status regression.                                           
  Author: Naveen Albert  
  Date:   2022-04-30  

  ASTERISK_28638 caused a regression by incorrectly aborting
  early and overwriting the status on certain calls.
  This was exhibited by certain technologies such as DAHDI,
  where DAHDI returns NULL for the request if a line is busy.
  This caused the BUSY condition to be incorrectly treated
  as CHANUNAVAIL because the DIALSTATUS was getting incorrectly
  overwritten and call handling was aborted early.

  This is fixed by instead checking if any valid peers have been
  specified, as opposed to checking the list size of successful
  requests. This is because the latter could be empty but this
  does not indicate any kind of problem. This restores the
  previous working behavior.

  ASTERISK-29989 #close


- ### db: Notify user if deleted DB entry didn't exist.                               
  Author: Naveen Albert  
  Date:   2022-04-01  

  Currently, if using the CLI to delete a DB entry,
  "Database entry removed" is always returned,
  regardless of whether or not the entry actually
  existed in the first place. This meant that users
  were never told if entries did not exist.

  The same issue occurs if trying to delete a DB key
  using AMI.

  To address this, new API is added that is more stringent
  in deleting values from AstDB, which will not return
  success if the value did not exist in the first place,
  and will print out specific error details if available.

  ASTERISK-30001 #close


- ### cli: Fix CLI blocking forever on terminating backslash                          
  Author: Naveen Albert  
  Date:   2022-02-05  

  A corner case exists in CLI parsing where if
  a CLI user in a remote console ends with
  a backslash and then invokes command completion
  (using TAB or ?), then the console will freeze
  forever until a SIGQUIT signal is sent to the
  process, due to getting blocked forever
  reading the command completion. CTRL+C
  and other key combinations have no impact on
  the CLI session.

  This occurs because, in such cases, the CLI
  process is waiting for AST_CLI_COMPLETE_EOF
  to appear in the buffer from the main process,
  but instead the main process is confused by
  the funny syntax and thus prints out the CLI help.
  As a result, the CLI process is stuck on the
  read call, waiting for the completion that
  will never come.

  This prevents blocking forever by checking
  if the data from the main process starts with
  "Usage:". If it does, that means that CLI help
  was sent instead of the tab complete vector,
  and thus the CLI should bail out and not wait
  any longer.

  ASTERISK-29822 #close


- ### app_dial: Propagate outbound hook flashes.                                      
  Author: Naveen Albert  
  Date:   2022-06-18  

  The Dial application currently stops hook flashes
  dead in their tracks from propagating through on
  outbound calls. This fixes that so they can go
  down the wire.

  ASTERISK-30115 #close


- ### res_calendar_icalendar: Send user agent in request.                             
  Author: Naveen Albert  
  Date:   2022-06-20  

  Microsoft recently began rejecting all requests for
  ICS calendars on Office 365 with 400 errors if
  the request doesn't contain a user agent. See:

  https://docs.microsoft.com/en-us/answers/questions/883904/34the-remote-server-returned-an-error-400-bad-requ.html

  Accordingly, we now send a user agent on requests for
  ICS files so that requests to Office 365 will work as
  they did before.

  ASTERISK-30106


- ### say: Abort play loop if caller hangs up.                                        
  Author: Naveen Albert  
  Date:   2022-05-22  

  If the caller has hung up, break out of the play loop so we don't try
  to play remaining files and fail to do so.

  ASTERISK-30075 #close


- ### res_pjsip: allow TLS verification of wildcard cert-bearing servers              
  Author: Kevin Harwell  
  Date:   2022-06-08  

  Rightly the use of wildcards in certificates is disallowed in accordance
  with RFC5922. However, RFC2818 does make some allowances with regards to
  their use when using subject alt names with DNS name types.

  As such this patch creates a new setting for TLS transports called
  'allow_wildcard_certs', which when it and 'verify_server' are both enabled
  allows DNS name types, as well as the common name that start with '*.'
  to match as a wildcard.

  For instance: *.example.com
  will match for: foo.example.com

  Partial matching is not allowed, e.g. f*.example.com, foo.*.com, etc...
  And the starting wildcard only matches for a single level.

  For instance: *.example.com
  will NOT match for: foo.bar.example.com

  The new setting is disabled by default.

  ASTERISK-30072 #close


- ### pbx: Add helper function to execute applications.                               
  Author: Naveen Albert  
  Date:   2022-05-15  

  Finding an application and executing it if found is
  a common task throughout Asterisk. This adds a helper
  function around pbx_exec to do this, to eliminate
  redundant code and make it easier for modules to
  substitute variables and execute applications by name.

  ASTERISK-30061 #close


- ### pjsip: Upgrade bundled version to pjproject 2.12.1                              
  Author: Stanislav Abramenkov  
  Date:   2022-05-10  

  More information:
  https://github.com/pjsip/pjproject/releases/tag/2.12.1

  Pull request to third-party
  https://github.com/asterisk/third-party/pull/11

  ASTERISK-30050


- ### asterisk.c: Fix incompatibility warnings for remote console.                    
  Author: Naveen Albert  
  Date:   2022-06-11  

  A previous review fixing ASTERISK_22246 and ASTERISK_26582
  got a couple of the options mixed up as to whether or not
  they are compatible with the remote console. This fixes
  those to the best of my knowledge.

  ASTERISK-30097 #close


- ### test_aeap_transport: disable part of failing unit test                          
  Author: Kevin Harwell  
  Date:   2022-06-07  

  The 'transport_binary' test sporadically fails, but on a theory that the
  problem is caused by a previously executed test, transport_connect_fail,
  part of that test has been disabled until a solution is found.

  ASTERISK_30099


- ### sig_analog: Fix broken three-way conferencing.                                  
  Author: Naveen Albert  
  Date:   2022-05-13  

  Three-way calling for analog lines is currently broken.
  If party A is on a call with party B and initiates a
  three-way call to party C, the behavior differs depending
  on whether the call is conferenced prior to party C
  answering. The post-answer case is correct. However,
  if A flashes before C answers, then the next flash
  disconnects B rather than C, which is incorrect.

  This error occurs because the subs are not swapped
  in the misbehaving case. This is because the flash
  handler only swaps the subs if C has answered already,
  which is wrong. To fix this, we swap the subs regardless
  of whether C has answered or not when the call is
  conferenced. This ensures that C is disconnected
  on the next hook flash, rather than B as can happen
  currently.

  ASTERISK-30043 #close


- ### app_voicemail: Add option to prevent message deletion.                          
  Author: Naveen Albert  
  Date:   2022-05-15  

  Adds an option to VoiceMailMain that prevents the user
  from deleting messages during that application invocation.
  This can be useful for public or shared mailboxes, where
  some users should be able to listen to messages but not
  delete them.

  ASTERISK-30063 #close


- ### res_parking: Add music on hold override option.                                 
  Author: Naveen Albert  
  Date:   2022-05-31  

  An m option to Park and ParkAndAnnounce now allows
  specifying a music on hold class override.

  ASTERISK-30087


- ### xmldocs: Improve examples.                                                      
  Author: Naveen Albert  
  Date:   2022-06-01  

  Use example tags instead of regular para tags
  where possible.

  ASTERISK-30090


- ### res_pjsip_outbound_registration: Make max random delay configurable.            
  Author: Naveen Albert  
  Date:   2022-03-12  

  Currently, PJSIP will randomly wait up to 10 seconds for each
  outbound registration's initial attempt. The reason for this
  is to avoid having all outbound registrations attempt to register
  simultaneously.

  This can create limitations with the test suite where we need to
  be able to receive inbound calls potentially within 10 seconds of
  starting up. For instance, we might register to another server
  and then try to receive a call through the registration, but if
  the registration hasn't happened yet, this will fail, and hence
  this inconsistent behavior can cause tests to fail. Ultimately,
  this requires a smaller random value because there may be no good
  reason to wait for up to 10 seconds in these circumstances.

  To address this, a new config option is introduced which makes this
  maximum delay configurable. This allows, for instance, this to be
  set to a very small value in test systems to ensure that registrations
  happen immediately without an unnecessary delay, and can be used more
  generally to control how "tight" the initial outbound registrations
  are.

  ASTERISK-29965 #close


- ### res_pjsip: Actually enable session timers when timers=always                    
  Author: Trevor Peirce  
  Date:   2022-06-07  

  When a pjsip endpoint is defined with timers=always, this has been a
  functional noop.  This patch correctly sets the feature bitmap to both
  enable support for session timers and to enable them even when the
  endpoint itself does not request or support timers.

  ASTERISK-29603
  Reported-By: Ray Crumrine


- ### res_pjsip_pubsub: delete scheduled notification on RLS update                   
  Author: Alexei Gradinari  
  Date:   2022-06-06  

  If there is scheduled notification, we must delete it
  to avoid using destroyed subscriptions.

  ASTERISK-29906


- ### res_pjsip_pubsub: XML sanitized RLS display name                                
  Author: Alexei Gradinari  
  Date:   2022-06-07  

  ASTERISK-29891


- ### app_sayunixtime: Use correct inflection for German time.                        
  Author: Christof Efkemann  
  Date:   2022-06-01  

  In function ast_say_date_with_format_de(), take special
  care when the hour is one o'clock. In this case, the
  German number "eins" must be inflected to its neutrum form,
  "ein". This is achieved by playing "digits/1N" instead of
  "digits/1". Fixes both 12- and 24-hour formats.

  ASTERISK-30092


- ### chan_iax2: Prevent deadlock due to duplicate autoservice.                       
  Author: Naveen Albert  
  Date:   2022-05-16  

  If a switch is invoked using chan_iax2, deadlock can result
  because the PBX core is autoservicing the channel while chan_iax2
  also then attempts to service it while waiting for the result
  of the switch. This removes servicing of the channel to prevent
  any conflicts.

  ASTERISK-30064 #close


- ### loader: Prevent deadlock using tab completion.                                  
  Author: Naveen Albert  
  Date:   2022-05-03  

  If tab completion using ast_module_helper is attempted
  during startup, deadlock will ensue because the CLI
  will attempt to lock the module list while it is already
  locked by the loader. This causes deadlock because when
  the loader tries to acquire the CLI lock, they are blocked
  on each other.

  Waiting for startup to complete is not feasible because
  the CLI lock is acquired while waiting, so deadlock will
  ensure regardless of whether or not a lock on the module
  list is attempted.

  To prevent deadlock, we immediately abort if tab completion
  is attempted on the module list before Asterisk is fully
  booted.

  ASTERISK-30039 #close


- ### res_calendar: Prevent assertion if event ends in past.                          
  Author: Naveen Albert  
  Date:   2022-03-23  

  res_calendar will trigger an assertion currently
  if the ending time is calculated to be in the past.
  Unlike the reminder and start times, however, there
  is currently no check to catch non-positive times
  and set them to 1. As a result, if we get a negative
  value by happenstance, this can cause a crash.

  To prevent the assertion from begin triggered, we now
  use the same logic as the reminder and start events
  to catch this issue before it can cause a problem.

  ASTERISK-29981 #close


- ### res_parking: Warn if out of bounds parking spot requested.                      
  Author: Naveen Albert  
  Date:   2022-05-30  

  Emits a warning if the user has requested a parking spot that
  is out of bounds for the requested parking lot.

  ASTERISK-30086


- ### chan_pjsip: Only set default audio stream on hold.                              
  Author: Maximilian Fridrich  
  Date:   2022-05-19  

  When a PJSIP channel is set on hold or off hold, all streams were set
  on/off hold. This is not the desired behaviour and caused issues
  when there were multiple streams in the topology.

  Now, only the default audio stream is set on/off hold when a hold is
  indicated.

  ASTERISK-30051


- ### res_pjsip_dialog_info_body_generator: Set LOCAL target URI as local URI         
  Author: Alexei Gradinari  
  Date:   2022-05-26  

  The change "Add LOCAL/REMOTE tags in dialog-info+xml" set both "local"
  Identity Element URI and Target Element URI to the same value -
  the channel Caller Number.
  For Identity Element it's ok to set as Caller ID.
  But Local Target URI should be set as local URI.

  In this case the Local Target URI can be used for Directed Call Pickup
  by Polycom ip-phones (parameter useLocalTargetUriforLegacyPickup).

  Also XML sanitized Display names.

  ASTERISK-24601


- ### res_agi: Evaluate dialplan functions and variables in agi exec if enabled       
  Author: Shloime Rosenblum  
  Date:   2022-05-11  

  Agi commnad exec can now evaluate dialplan functions and
  variables if variable AGIEXECFULL is set to yes. this can
  be useful when executing Playback or Read from agi.

  ASTERISK-30058 #close


- ### ast_pkgconfig.m4: AST_PKG_CONFIG_CHECK() relies on sed.                         
  Author: Sean Bright  
  Date:   2022-05-17  

  Make sure that we have a working sed before trying to use it.

  ASTERISK-30059 #close


- ### ari: expose channel driver's unique id to ARI channel resource                  
  Author: Moritz Fain  
  Date:   2022-04-26  

  This change exposes the channel driver's unique id (i.e. the Call-ID
  for chan_sip/chan_pjsip based channels) to ARI channel resources
  as `protocol_id`.

  ASTERISK-30027
  Reported by: Moritz Fain
  Tested by: Moritz Fain


- ### loader.c: Use portable printf conversion specifier for int64.                   
  Author: Sean Bright  
  Date:   2022-05-17  

  ASTERISK-30060 #close


- ### res_pjsip_transport_websocket: Also set the remote name.                        
  Author: Joshua C. Colp  
  Date:   2022-05-17  

  As part of PJSIP 2.11 a behavior change was done to require
  a matching remote hostname on an established transport for
  secure transports. Since the Websocket transport is considered
  a secure transport this caused the existing connection to not
  be found and used.

  We now set the remote hostname and the transport can be found.

  ASTERISK-30065


- ### res_pjsip_transport_websocket: save the original contact host                   
  Author: Thomas Guebels  
  Date:   2022-05-04  

  This is needed to be able to restore it in REGISTER responses,
  otherwise the client won't be able to find the contact it created.

  ASTERISK-30042


- ### res_pjsip_outbound_registration: Show time until expiration                     
  Author: Naveen Albert  
  Date:   2022-01-07  

  Adjusts the pjsip show registration(s) commands to show
  the amount of seconds remaining until a registration
  expires.

  ASTERISK-29845 #close


- ### app_confbridge: Add function to retrieve channels.                              
  Author: Naveen Albert  
  Date:   2022-04-29  

  Adds the CONFBRIDGE_CHANNELS function which can be used
  to retrieve a comma-separated list of channels, filtered
  by a particular type of participant category. This output
  can then be used with functions like UNSHIFT, SHIFT, POP,
  etc.

  ASTERISK-30036 #close


- ### chan_dahdi: Fix broken operator mode clearing.                                  
  Author: Naveen Albert  
  Date:   2022-04-26  

  Currently, the operator services mode in DAHDI is broken and unusable.
  The actual operator recall functionality works properly; however,
  when the operator hangs up (which is the only way that such a call
  is allowed to end), both lines are permanently taken out of service
  until "dahdi restart" is run. This prevents this feature from being
  used.

  Operator mode is one of the few factors that can cause the general
  analog event handling in sig_analog not to be used. Several years
  back, much of the analog handling was moved from chan_dahdi to
  sig_analog. However, this was not done fully or consistently at
  the time, and when operator mode is active, sig_analog does not
  get used. Generally this is correct, but in the case of hangup
  it should be using sig_analog regardless of the operator mode;
  otherwise, the lines do not properly clear and they become unusable.

  This bug is fixed so the operator can now hang up and properly
  release the call. It is treated just like any other hangup. The
  operator mode functionality continues to work as it did before.

  ASTERISK-29993 #close


- ### GCC12: Fixes for 16+                                                            
  Author: George Joseph  
  Date:   2022-05-03  

  Most issues were in stringfields and had to do with comparing
  a pointer to an constant/interned string with NULL.  Since the
  string was a constant, a pointer to it could never be NULL so
  the comparison was always "true".  gcc now complains about that.

  There were also a few issues where determining if there was
  enough space for a memcpy or s(n)printf which were fixed
  by defining some of the involved variables as "volatile".

  There were also a few other miscellaneous fixes.

  ASTERISK-30044


- ### GCC12: Fixes for 18+.  state_id_by_topic comparing wrong value                  
  Author: George Joseph  
  Date:   2022-05-04  

  GCC 12 caught an issue in state_id_by_topic where we were
  checking a pointer for NULL instead of the contents of
  the pointer for '\0'.

  ASTERISK-30044


- ### core_unreal: Flip stream direction of second channel.                           
  Author: Maximilian Fridrich  
  Date:   2022-04-29  

  When a new unreal (local) channel is created, a second (;2) channel is
  created as a counterpart which clones the topology of the first
  channel. This creates issues when an outgoing stream is sendonly or
  recvonly as the stream state of the inbound channel will be the same
  as the stream state of the outbound channel.

  Now the stream state is flipped for the streams of the 2nd channel in
  ast_unreal_new_channels if the outgoing stream topology is recvonly or
  sendonly.

  ASTERISK-29655
  Reported by: Michael Auracher

  ASTERISK-29638
  Reported by: Michael Auracher


- ### chan_dahdi: Document dial resource options.                                     
  Author: Naveen Albert  
  Date:   2022-03-27  

  Documents the Dial syntax for DAHDI, namely the channel group,
  distinctive ring, answer confirmation, and digital call options
  that are specified in the resource itself.

  ASTERISK-24827 #close


- ### chan_dahdi: Don't allow MWI FSK if channel not idle.                            
  Author: Naveen Albert  
  Date:   2022-03-29  

  For lines that have mailboxes configured on them, with
  FSK MWI, DAHDI will periodically try to dispatch FSK
  to update MWI. However, this is never supposed to be
  done when a channel is not idle.

  There is currently an edge case where MWI FSK can
  extraneously get spooled for the channel if a caller
  hook flashes and hangs up, which triggers a recall ring.
  After one ring, the on hook time threshold in this if
  condition has been satisfied and an MWI update is spooled.
  This means that when the phone is picked up again, the
  answerer gets an FSK spill before being reconnected to
  the party on hold.

  To prevent this, we now explicitly check to ensure that
  subchannel 0 has no owner. There is no owner when DAHDI
  channels are idle, but if the channel is "in use" in some
  way (such as in the aforementioned scenario), then there
  is an owner, and we shouldn't process MWI at this time.

  ASTERISK-28518 #close


- ### apps/confbridge: Added hear_own_join_sound option to control who hears sound_j..
  Author: Michael Cargile  
  Date:   2022-02-23  

  Added the hear_own_join_sound option to the confbridge user profile to
  control who hears the sound_join audio file. When set to 'yes' the user
  entering the conference and the participants already in the conference
  will hear the sound_join audio file. When set to 'no' the user entering
  the conference will not hear the sound_join audio file, but the
  participants already in the conference will hear the sound_join audio
  file.

  ASTERISK-29931
  Added by Michael Cargile


- ### chan_dahdi: Don't append cadences on dahdi restart.                             
  Author: Naveen Albert  
  Date:   2022-03-27  

  Currently, if any custom ring cadences are specified, they are
  appended to the array of cadences from wherever we left off
  last time. This works properly the first time, but on subsequent
  dahdi restarts, it means that the existing cadences are left
  alone and (most likely) the same cadences are then re-added
  afterwards. In short order, the cadence array gets maxed out
  and the user begins seeing warnings that the array is full
  and no more cadences may be added.

  This buggy behavior persists until Asterisk is completely
  restarted; however, if and when dahdi restart is run again,
  then the same problem is reintroduced.

  This fixes this behavior so that cadence parsing is more
  idempotent, that is so running dahdi restart multiple times
  starts adding cadences from the beginning, rather than from
  wherever the last cadence was added.

  As before, it is still not possible to revert to the default
  cadences by simply removing all cadences in this manner, nor
  is it possible to delete existing cadences. However, this
  does make it possible to update existing cadences, which
  was not possible before, and also ensures that the cadences
  remain unchanged if the config remains unchanged.

  ASTERISK-29990 #close


- ### chan_iax2: Prevent crash if dialing RSA-only call without outkey.               
  Author: Naveen Albert  
  Date:   2022-04-02  

  Currently, if attempting to place a call to a peer that only allows
  RSA authentication, if we fail to provide an outkey when placing
  the call, Asterisk will crash.

  This exposes the broader issue that IAX2 is prone to causing a crash
  if encryption or decryption is attempted but we never initialized
  the encryption and decryption keys. In other words, if the logic
  to use encryption in chan_iax2 is not perfectly aligned with the
  decision to build keys in the first place, then a crash is not
  only possible but probable. This was demonstrated by ASTERISK_29264,
  for instance.

  This permanently prevents such events from causing a crash by explicitly
  checking that keys are initialized properly before setting the flags
  to use encryption for the call. Instead of crashing, the call will
  now abort.

  ASTERISK-30007 #close


- ### menuselect: Don't erroneously recompile modules.                                
  Author: Naveen Albert  
  Date:   2022-02-05  

  A bug in menuselect can cause modules that are disabled
  by default to be recompiled every time a recompilation
  occurs. This occurs for module categories that are NOT
  positive output, as for these categories, the modules
  contained in the makeopts file indicate modules which
  should NOT be selected. The existing procedure of iterating
  through these modules to mark modules as present is thus
  insufficient. This has led to modules with a default_enabled
  tag of "no" to get deleted and recompiled every time, even
  when they haven't changed.

  To fix this, we now modify the mark as present behavior
  for module categories that are not positive output. For
  these, we start by iterating through the module tree
  and marking all modules as present, then go back and
  mark anything contained in the makeopts file as not
  present. This ensures that makeopt selections are actually
  used properly, regardless of whether a module category
  uses positive output or not.

  ASTERISK-29728 #close


- ### app_meetme: Don't erroneously set global variables.                             
  Author: Naveen Albert  
  Date:   2022-03-31  

  The admin_exec function in app_meetme is used by the SLA
  applications for internal bridging. However, in these cases,
  chan is NULL. Currently, this function will set some status
  variables that are intended for a channel, but since channel
  is NULL, this is erroneously creating meaningless global
  variables, which shouldn't be happening. This sets these
  variables only if chan is not NULL.

  ASTERISK-30002 #close


- ### asterisk.c: Warn of incompatibilities with remote console.                      
  Author: Naveen Albert  
  Date:   2022-03-05  

  Some command line options to Asterisk only apply when Asterisk
  is started and cannot be used with remote console mode. If a
  user tries to use any of these, they are currently simply
  silently ignored.

  This prints out a warning if incompatible options are used,
  informing users that an option used cannot be used with remote
  console mode. Additionally, some clarifications are added to
  the help text and man page.

  ASTERISK-22246
  ASTERISK-26582


- ### func_db: Add function to return cardinality at prefix                           
  Author: Naveen Albert  
  Date:   2022-03-15  

  Adds the DB_KEYCOUNT function, which can be used to retrieve
  the number of keys at a given prefix in AstDB.

  ASTERISK-29968 #close


- ### chan_dahdi: Fix insufficient array size for round robin.                        
  Author: Naveen Albert  
  Date:   2022-03-30  

  According to chan_dahdi.conf, up to 64 groups (numbered
  0 through 63) can be used when dialing DAHDI channels.

  However, currently dialing round robin with a group number
  greater than 31 fails because the array for the round robin
  structure is only size 32, instead of 64 as it should be.

  This fixes that so the round robin array size is consistent
  with the actual groups capacity.

  ASTERISK-29994


- ### chan_sip.c Session timers get removed on UPDATE                                 
  Author: Mark Petersen  
  Date:   2022-02-26  

  If Asterisk receives a SIP REFER with Session-Timers UAC
  maintain Session-Timers when sending UPDATE"

  ASTERISK-29843


- ### func_evalexten: Extension evaluation function.                                  
  Author: Naveen Albert  
  Date:   2021-06-21  

  This adds the EVAL_EXTEN function, which may be used to retrieve
  the variable-substituted data at any extension.

  ASTERISK-29486


- ### file.c: Prevent formats from seeking negative offsets.                          
  Author: Naveen Albert  
  Date:   2022-03-01  

  Currently, if a user uses an application like ControlPlayback
  to try to rewind a file past the beginning, this can throw
  warnings when the file format (e.g. PCM) tries to seek to
  a negative offset.

  Instead of letting file formats try (and fail) to seek a
  negative offset, we instead now catch this in the rewind
  function to ensure that we never seek an offset less than 0.
  This prevents legitimate user actions from triggering warnings
  from any particular file formats.

  ASTERISK-29943 #close


- ### chan_pjsip: Add ability to send flash events.                                   
  Author: Naveen Albert  
  Date:   2022-02-26  

  PJSIP currently is capable of receiving flash events
  and converting them to FLASH control frames, but it
  currently lacks support for doing the reverse: taking
  a FLASH control frame and converting it into a flash
  event in the SIP domain.

  This adds the ability for PJSIP to process flash control
  frames by converting them into the appropriate SIP INFO
  message, which can then be sent to the peer. This allows,
  for example, flash events to be sent between Asterisk
  systems using PJSIP.

  ASTERISK-29941 #close


- ### cli: Add command to evaluate dialplan functions.                                
  Author: Naveen Albert  
  Date:   2021-12-26  

  Adds the dialplan eval function commands to evaluate a dialplan
  function from the CLI. The return value and function result are
  printed out and can be used for testing or debugging.

  ASTERISK-29820 #close


- ### documentation: Adds versioning information.                                     
  Author: Naveen Albert  
  Date:   2022-02-25  

  Adds version information for applications, functions,
  and manager events/actions.

  This is not completely exhaustive by any means but
  covers most new things added that have release
  versioning information in the issue tracker.

  ASTERISK-29940 #close


- ### samples: Remove obsolete sample configs.                                        
  Author: Naveen Albert  
  Date:   2022-04-02  

  Removes a couple sample config files for modules
  which have since been removed from Asterisk.

  ASTERISK-30008 #close


- ### chan_pjsip: add allow_sending_180_after_183 option                              
  Author: Mark Petersen  
  Date:   2022-02-21  

  added new global config option "allow_sending_180_after_183"
  that if enabled will preserve 180 after a 183

  ASTERISK-29842


- ### chan_sip: SIP route header is missing on UPDATE                                 
  Author: Mark Petersen  
  Date:   2022-03-07  

  if Asterisk need to send an UPDATE before answer
  on a channel that uses Record-Route:
  it will not include a Route header

  ASTERISK-29955


- ### manager: Terminate session on write error.                                      
  Author: Joshua C. Colp  
  Date:   2022-04-25  

  On a write error to an AMI session a flag was set to
  indicate that the write error had occurred, with the
  expected result being that the session be terminated.
  This was not actually happening and instead writing
  would continue to be attempted.

  This change adds a check for the write error and causes
  the session to actually terminate.

  ASTERISK-29948


- ### bridge_simple.c: Unhold channels on join simple bridge.                         
  Author: Yury Kirsanov  
  Date:   2022-04-21  

  Patch provided inline by Yury Kirsanov on the linked issue and
  approved by Josh Colp.

  ASTERISK-29253 #close


- ### res_aeap & res_speech_aeap: Add Asterisk External Application Protocol          
  Author: Kevin Harwell  
  Date:   2021-06-18  

  Add framework to connect to, and read and write protocol based
  messages from and to an external application using an Asterisk
  External Application Protocol (AEAP). This has been divided into
  several abstractions:

   1. transport - base communication layer (currently websocket only)
   2. message - AEAP description and data (currently JSON only)
   3. transaction - links/binds requests and responses
   4. aeap - transport, message, and transaction handler/manager

  This patch also adds an AEAP implementation for speech to text.
  Existing speech API callbacks for speech to text have been completed
  making it possible for Asterisk to connect to a configured external
  translator service and provide audio for STT. Results can also be
  received from the external translator, and made available as speech
  results in Asterisk.

  Unit tests have also been created that test the AEAP framework, and
  also the speech to text implementation.

  ASTERISK-29726 #close


- ### app_dial: Flip stream direction of outgoing channel.                            
  Author: Maximilian Fridrich  
  Date:   2022-04-13  

  When executing dial, the topology of the incoming channel is cloned and
  used for the outgoing channel. This creates issues when an incoming
  stream is sendonly or recvonly as the stream state of the outgoing
  channel will be the same as the stream state of the incoming channel.

  Now the stream state is flipped for the outgoing stream in
  dial_exec_full if the incoming stream topology is recvonly or sendonly.

  ASTERISK-29655
  Reported by: Michael Auracher

  ASTERISK-29638
  Reported by: Michael Auracher


- ### res_pjsip_stir_shaken.c: Fix enabled when not configured.                       
  Author: Ben Ford  
  Date:   2022-04-21  

  There was an issue with the conditional where STIR/SHAKEN would be
  enabled even when not configured. It has been changed to ensure that if
  a profile does not exist and stir_shaken is not set in pjsip.conf, then
  the conditional will return from the function without performing
  STIR/SHAKEN operations.

  ASTERISK-30024


- ### res_pjsip: Always set async_operations to 1.                                    
  Author: Joshua C. Colp  
  Date:   2022-04-06  

  The async_operations setting on a transport configures how
  many simultaneous incoming packets the transport can handle
  when multiple threads are polling and waiting on the transport.
  As we only use a single thread this was needlessly creating
  incoming packets when set to a non-default value, wasting memory.

  ASTERISK-30006


- ### config.h: Don't use C++ keywords as argument names.                             
  Author: Sean Bright  
  Date:   2022-04-19  

  ASTERISK-30021 #close


- ### cdr_adaptive_odbc: Add support for SQL_DATETIME field type.                     
  Author: Joshua C. Colp  
  Date:   2022-04-20  

  ASTERISK-30023


- ### pjsip: Increase maximum number of format attributes.                            
  Author: Joshua C. Colp  
  Date:   2022-04-11  

  Chrome has added more attributes, causing the limit to be
  exceeded. This raises it up some more.

  ASTERISK-30015


- ### AST-2022-002 - res_stir_shaken/curl: Add ACL checks for Identity header.        
  Author: Ben Ford  
  Date:   2022-02-28  

  Adds a new configuration option, stir_shaken_profile, in pjsip.conf that
  can be specified on a per endpoint basis. This option will reference a
  stir_shaken_profile that can be configured in stir_shaken.conf. The type
  of this option must be 'profile'. The stir_shaken option can be
  specified on this object with the same values as before (attest, verify,
  on), but it cannot be off since having the profile itself implies wanting
  STIR/SHAKEN support. You can also specify an ACL from acl.conf (along
  with permit and deny lines in the object itself) that will be used to
  limit what interfaces Asterisk will attempt to retrieve information from
  when reading the Identity header.

  ASTERISK-29476


- ### AST-2022-001 - res_stir_shaken/curl: Limit file size and check start.           
  Author: Ben Ford  
  Date:   2022-01-07  

  Put checks in place to limit how much we will actually download, as well
  as a check for the data we receive at the start to ensure it begins with
  what we would expect a certificate to begin with.

  ASTERISK-29872


- ### func_odbc: Add SQL_ESC_BACKSLASHES dialplan function.                           
  Author: Joshua C. Colp  
  Date:   2022-02-10  

  Some databases depending on their configuration using backslashes
  for escaping. When combined with the use of ' this can result in
  a broken func_odbc query.

  This change adds a SQL_ESC_BACKSLASHES dialplan function which can
  be used to escape the backslashes.

  This is done as a dialplan function instead of being always done
  as some databases do not require this, and always doing it would
  result in incorrect data being put into the database.

  ASTERISK-29838


- ### app_mf, app_sf: Return -1 if channel hangs up.                                  
  Author: Naveen Albert  
  Date:   2022-03-05  

  The ReceiveMF and ReceiveSF applications currently always
  return 0, even if a channel has hung up. The call will still
  end but generally applications are expected to return -1 if
  the channel has hung up.

  We now return -1 if a hangup occured to bring this behavior
  in line with this norm. This has no functional impact, but
  merely increases conformity with how these modules interact
  with the PBX core.

  ASTERISK-29951 #close


- ### app_queue: Add music on hold option to Queue.                                   
  Author: Naveen Albert  
  Date:   2022-01-22  

  Adds the m option to the Queue application, which allows a
  music on hold class to be specified at runtime which will
  override the class configured in queues.conf.

  This option functions like the m option to Dial.

  ASTERISK-29876 #close


- ### app_meetme: Emit warning if conference not found.                               
  Author: Naveen Albert  
  Date:   2022-03-05  

  Currently, if a user tries to access a non-dynamic
  MeetMe conference and the conference is not found,
  the call simply silent hangs up. There is no indication
  to the user that anything went wrong at all.

  This changes the relevant debug message to a warning
  so that the user is notified of this invalidity.

  ASTERISK-29954 #close


- ### build: Remove obsolete leftover build references.                               
  Author: Naveen Albert  
  Date:   2022-02-24  

  Removes some leftover build and config references to
  modules that have since been removed from Asterisk.

  ASTERISK-29935 #close


- ### res_pjsip_header_funcs: wrong pool used tdata headers                           
  Author: Kevin Harwell  
  Date:   2022-03-23  

  When adding headers to an outgoing request the headers were cloned using
  the dialog's pool when they should have been cloned using tdata's pool.
  Under certain circumstances it was possible for the dialog object, and
  its pool to be freed while tdata is still active and available. Thus the
  cloned header "disappeared", and when tdata tried to later access it a
  crash would occur.

  This patch makes it so all added headers are cloned appropriately using
  tdata's pool.

  ASTERISK-29411 #close
  ASTERISK-29535 #close


- ### deprecation cleanup: remove leftover files                                      
  Author: Kevin Harwell  
  Date:   2022-03-25  

  Several modules removal and deprecations occurred in 19.0.0 (initial
  19 release), but associated UPGRADE files were not removed from
  staging for some reason in the master branch.

  This patch removes those files, and also removes a spurious leftover
  header, chan_phone.h (associated module removed in 19).


- ### pjproject: Update bundled to 2.12 release.                                      
  Author: Joshua C. Colp  
  Date:   2022-02-24  

  This change removes patches which have been merged into
  upstream and updates some existing ones. It also adds
  some additional config_site.h changes to restore previous
  behavior, as well as a patch to allow multiple Authorization
  headers. There seems to be some confusion or disagreement
  on language in RFC 8760 in regards to whether multiple
  Authorization headers are supported. The RFC implies it
  is allowed, as does some past sipcore discussion. There is
  also the catch all of "local policy" to allow it. In
  the case of Asterisk we allow it.

  ASTERISK-29351


- ### pbx.c: Warn if there are too many includes in a context.                        
  Author: Naveen Albert  
  Date:   2022-03-05  

  The PBX core uses the stack when it comes to includes, which
  means that a context can only contain strictly fewer than
  AST_PBX_MAX_STACK includes. If this is exceeded, then warnings
  will be emitted for each number of includes beyond this if
  searching for an extension in the including context, and if
  the extension's inclusion is beyond the stack size, it will
  simply not be found.

  To address this, we now check if there are too many includes
  in a context when the dialplan is reloaded so that if there
  is an issue, the user is aware of at "compile time" as opposed
  to "run time" only. Secondly, more details are printed out
  when this message is encountered so it's clear what has happened.

  ASTERISK-26719


- ### Makefile:  Disable XML doc validation                                           
  Author: George Joseph  
  Date:   2022-03-25  

  make_xml_documentation was being called with the --validate
  flag set when it shouldn't have been.  This was causing
  build failures if neither xmllint nor xmlstarlet were installed.
  The correct behavior is to simply print a message that either
  one of those tools should be installed for validation and
  continue with the build.

  ASTERISK-29988


- ### make_xml_documentation: Remove usage of get_sourceable_makeopts                 
  Author: George Joseph  
  Date:   2022-03-25  

  get_sourceable_makeopts wasn't handling variables with embedded
  double quotes in them very well.  One example was the DOWNLOAD
  variable when curl was being used instead of wget.  Rather than
  trying to fix get_sourceable_makeopts, it's just been removed.

  ASTERISK-29986
  Reported by: Stefan Ruijsenaars


- ### chan_iax2: Fix spacing in netstats command                                      
  Author: Naveen Albert  
  Date:   2022-02-05  

  The iax2 show netstats command previously didn't contain
  enough spacing in the header to properly align the table
  header with the table body. This caused column headers
  to not align with the values on longer channel names.

  Some spacing is added to account for the longest channel
  names that display (before truncation occurs) so that
  columns are always properly aligned.

  ASTERISK-29895 #close
  patches:
    61205_misaligned2.patch submitted by Birger Harzenetter (license 5870)


- ### openssl: Supress deprecation warnings from OpenSSL 3.0                          
  Author: Sean Bright  
  Date:   2022-03-25  

  There is work going on to update our OpenSSL usage to avoid the
  deprecated functions but in the meantime make it possible to compile
  in devmode.


- ### documentation: Add information on running install_prereq script in readme       
  Author: Marcel Wagner  
  Date:   2022-03-23  

  Adding information in the readme about running the install_preqreq script to install components that the ./configure script might indicate as missing.

  ASTERISK-29976 #close


- ### chan_iax2: Fix perceived showing host address.                                  
  Author: Naveen Albert  
  Date:   2022-03-13  

  ASTERISK_22025 introduced a regression that shows
  the host IP and port as the perceived IP and port
  again, as opposed to showing the actual perceived
  address. This fixes this by showing the correct
  information.

  ASTERISK-29048 #close


- ### res_pjsip_sdp_rtp: Improve detecting of lack of RTP activity                    
  Author: Boris P. Korzun  
  Date:   2022-02-22  

  Change RTP timer behavior for detecting RTP only after two-way
  SDP channel establishment. Ignore detecting after receiving 183
  with SDP or while direct media is used.
  Make rtp_timeout and rtp_timeout_hold options consistent to rtptimeout
  and rtpholdtimeout options in chan_sip.

  ASTERISK-26689 #close
  ASTERISK-29929 #close


- ### configure.ac: Use pkg-config to detect libxml2                                  
  Author: Hugh McMaster  
  Date:   2022-03-16  

  Use pkg-config to detect libxml2, falling back to xml2-config if the
  former is not available.

  This patch ensures Asterisk continues to build on systems without
  xml2-config installed.

  The patch also updates the associated 'configure' files.

  ASTERISK-29970 #close


- ### time: add support for time64 libcs                                              
  Author: Philip Prindeville  
  Date:   2022-02-13  

  Treat time_t's as entirely unique and use the POSIX API's for
  converting to/from strings.

  Lastly, a 64-bit integer formats as 20 digits at most in base10.
  Don't need to have any 100 byte buffers to hold that.

  ASTERISK-29674 #close

  Signed-off-by: Philip Prindeville <philipp@redfish-solutions.com>

- ### res_pjsip_pubsub: RLS 'uri' list attribute mismatch with SUBSCRIBE request      
  Author: Alexei Gradinari  
  Date:   2022-03-15  

  When asterisk generates the RLMI part of NOTIFY request,
  the asterisk uses the local contact uri instead of the URI to which
  the SUBSCRIBE request is sent.
  Because of this mismatch some IP phones (for example Cisco 5XX) ignore
  this list.

  According
  https://datatracker.ietf.org/doc/html/rfc4662#section-5.2
    The first mandatory <list> attribute is "uri", which contains the uri
    that corresponds to the list. Typically, this is the URI to which
    the SUBSCRIBE request was sent.
  https://datatracker.ietf.org/doc/html/rfc4662#section-5.3
    The "uri" attribute identifies the resource to which the <resource>
    element corresponds. Typically, this will be a SIP URI that, if
    subscribed to, would return the state of the resource.

  This patch makes asterisk to generate URI using SUBSCRIBE request URI.

  ASTERISK-29961 #close


- ### app_dial: Document DIALSTATUS return values.                                    
  Author: Naveen Albert  
  Date:   2022-03-05  

  Adds documentation for all of the possible return values
  for the DIALSTATUS variable in the Dial application.

  ASTERISK-25716


- ### stasis_recording: Perform a complete match on requested filename.               
  Author: Sean Bright  
  Date:   2022-03-10  

  Using the length of a file found on the filesystem rather than the
  file being requested could result in filenames whose names are
  substrings of another to be erroneously matched.

  We now ensure a complete comparison before returning a positive
  result.

  ASTERISK-29960 #close


- ### download_externals: Use HTTPS for downloads                                     
  Author: Sean Bright  
  Date:   2022-03-22  

  ASTERISK-29980 #close


- ### conversions.c: Specify that we only want to parse decimal numbers.              
  Author: Sean Bright  
  Date:   2022-03-04  

  Passing 0 as the last argument to strtoimax() or strtoumax() causes
  octal and hexadecimal to be accepted which was not originally
  intended. So we now force to only accept decimal.

  ASTERISK-29950 #close


- ### logger: workaround woefully small BUFSIZ in MUSL                                
  Author: Philip Prindeville  
  Date:   2022-02-21  

  MUSL defines BUFSIZ as 1024 which is not reasonable for log messages.

  More broadly, BUFSIZ is the amount of buffering stdio.h does, which
  is arbitrary and largely orthogonal to what logging should accept
  as the maximum message size.

  ASTERISK-29928

  Signed-off-by: Philip Prindeville <philipp@redfish-solutions.com>

- ### pbx_builtins: Add missing options documentation                                 
  Author: Naveen Albert  
  Date:   2022-03-14  

  BackGround and WaitExten both accept options that are not
  currently documented. This adds documentation for these
  options to the xml documentation for each application.

  ASTERISK-29967 #close


- ### res_pjsip_pubsub: update RLS to reflect the changes to the lists                
  Author: Alexei Gradinari  
  Date:   2022-02-08  

  This patch makes the Resource List Subscriptions (RLS) dynamic.
  The asterisk updates the current subscriptions to reflect the changes
  to the list on the subscriptions refresh. If list items are added,
  removed, updated or do not exist anymore, the asterisk regenerates
  the resource list.

  ASTERISK-29906 #close


- ### res_agi: Fix xmldocs bug with set music.                                        
  Author: Naveen Albert  
  Date:   2022-02-25  

  The XML documentation for the SET MUSIC AGI
  command is invalid, as the parameter does not
  have a name and the on/off enum options for
  the on/off argument are listed separately, which
  is incorrect. The cumulative effect of these currently
  is that the Asterisk Wiki documentation for SET MUSIC
  is broken and external documentation generators crash
  on SET MUSIC due to the malformed documentation.

  These issues are corrected so that the documentation
  can be successfully parsed as with other similar AGI
  commands.

  ASTERISK-29939 #close
  ASTERISK-28891 #close


- ### res_config_pgsql: Add text-type column check in require_pgsql()                 
  Author: Boris P. Korzun  
  Date:   2022-02-18  

  Omit "unsupported column type 'text'" warning in logs while
  using text-type column in the PgSQL backend.

  ASTERISK-29924 #close


- ### app_queue: Add QueueWithdrawCaller AMI action                                   
  Author: Kfir Itzhak  
  Date:   2022-02-09  

  This adds a new AMI action called QueueWithdrawCaller.
  This AMI action makes it possible to withdraw a caller from a queue,
  in a safe and a generic manner.
  This can be useful for retrieving a specific call and
  dispatching it to a specific extension.
  It works by signaling the caller to exit the queue application
  whenever it can. Therefore, it is not guaranteed
  that the call will leave the queue.

  ASTERISK-29909 #close


- ### ami: Improve substring parsing for disabled events.                             
  Author: Naveen Albert  
  Date:   2022-02-24  

  ASTERISK_29853 added the ability to selectively disable
  AMI events on a global basis, but the logic for this uses
  strstr which means that events with names which are the prefix
  of another event, if disabled, could disable those events as
  well.

  Instead, we account for this possibility to prevent this
  undesired behavior from occuring.

  ASTERISK_29853


- ### xml.c, config,c:  Add stylesheets and variable list string parsing              
  Author: George Joseph  
  Date:   2022-03-02  

  Added functions to open, close, and apply XML Stylesheets
  to XML documents.  Although the presence of libxslt was already
  being checked by configure, it was only happening if xmldoc was
  enabled.  Now it's checked regardless.

  Added ability to parse a string consisting of comma separated
  name/value pairs into an ast_variable list.  The reverse of
  ast_variable_list_join().


- ### xmldoc: Fix issue with xmlstarlet validation                                    
  Author: George Joseph  
  Date:   2022-03-01  

  Added the missing xml-stylesheet and Xinclude namespace
  declarations in pjsip_config.xml and pjsip_manager.xml.

  Updated make_xml_documentation to show detailed errors when
  xmlstarlet is the validator.  It's now run once with the '-q'
  option to suppress harmless/expected messages and if it actually
  fails, it's run again without '-q' but with '-e' to show
  the actual errors.


- ### core: Config and XML tweaks needed for geolocation                              
  Author: George Joseph  
  Date:   2022-02-20  

  Added:

  Replace a variable in a list:
  int ast_variable_list_replace_variable(struct ast_variable **head,
      struct ast_variable *old, struct ast_variable *new);
  Added test as well.

  Create a "name=value" string from a variable list:
  'name1="val1",name2="val2"', etc.
  struct ast_str *ast_variable_list_join(
      const struct ast_variable *head, const char *item_separator,
      const char *name_value_separator, const char *quote_char,
      struct ast_str **str);
  Added test as well.

  Allow the name of an XML element to be changed.
  void ast_xml_set_name(struct ast_xml_node *node, const char *name);


- ### Makefile: Allow XML documentation to exist outside source files                 
  Author: George Joseph  
  Date:   2022-02-14  

  Moved the xmldoc build logic from the top-level Makefile into
  its own script "make_xml_documentation" in the build_tools
  directory.

  Created a new utility script "get_sourceable_makeopts", also in
  the build_tools directory, that dumps the top-level "makeopts"
  file in a format that can be "sourced" from shell sscripts.
  This allows scripts to easily get the values of common make
  build variables such as the location of the GREP, SED, AWK, etc.
  utilities as well as the AST* and library *_LIB and *_INCLUDE
  variables.

  Besides moving logic out of the Makefile, some optimizations
  were done like removing "third-party" from the list of
  subdirectories to be searched for documentation and changing some
  assignments from "=" to ":=" so they're only evaluated once.
  The speed increase is noticeable.

  The makeopts.in file was updated to include the paths to
  REALPATH and DIRNAME.  The ./conifgure script was setting them
  but makeopts.in wasn't including them.

  So...

  With this change, you can now place documentation in any"c"
  source file AND you can now place it in a separate XML file
  altogether.  The following are examples of valid locations:

  res/res_pjsip.c
      Using the existing /*** DOCUMENTATION ***/ fragment.

  res/res_pjsip/pjsip_configuration.c
      Using the existing /*** DOCUMENTATION ***/ fragment.

  res/res_pjsip/pjsip_doc.xml
      A fully-formed XML file.  The "configInfo", "manager",
      "managerEvent", etc. elements that would be in the "c"
      file DOCUMENTATION fragment should be wrapped in proper
      XML.  Example for "somemodule.xml":

      <?xml version="1.0" encoding="UTF-8"?>
      <!DOCTYPE docs SYSTEM "appdocsxml.dtd">
      <docs>
          <configInfo>
          ...
          </configInfo>
      </docs>

  It's the "appdocsxml.dtd" that tells make_xml_documentation
  that this is a documentation XML file and not some other XML file.
  It also allows many XML-capable editors to do formatting and
  validation.

  Other than the ".xml" suffix, the name of the file is not
  significant.

  As a start... This change also moves the documentation that was
  in res_pjsip.c to 2 new XML files in res/res_pjsip:
  pjsip_config.xml and pjsip_manager.xml.  This cut the number of
  lines in res_pjsip.c in half. :)


- ### build: Refactor the earlier "basebranch" commit                                 
  Author: George Joseph  
  Date:   2022-02-17  

  Recap from earlier commit:  If you have a development branch for a
  major project that will receive gerrit reviews it'll probably be
  named something like "development/16/newproject" or a work branch
  based on that "development" branch.  That will necessitate
  setting "defaultbranch=development/16/newproject" in .gitreview.
  The make_version script uses that variable to construct the
  asterisk version however, which results in versions
  like "GIT-development/16/newproject-ee582a8c7b" which is probably
  not what you want.  It also constructs the URLs for downloading
  external modules with that version, which will fail.

  Fast-forward:

  The earlier attempt at adding a "basebranch" variable to
  .gitreview didn't work out too well in practice because changes
  were made to .gitreview, which is a checked-in file.  So, if
  you wanted to rebase your work branch on the base branch, rebase
  would attempt to overwrite your .gitreview with the one from
  the base branch and complain about a conflict.

  This is a slighltly different approach that adds three methods to
  determine the mainline branch:

  1.  --- MAINLINE_BRANCH from the environment

  If MAINLINE_BRANCH is already set in the environment, that will
  be used.  This is primarily for the Jenkins jobs.

  2.  --- .develvars

  Instead of storing the basebranch in .gitreview, it can now be
  stored in a non-checked-in ".develvars" file and keyed by the
  current branch.  So, if you were working on a branch named
  "new-feature-work" based on "development/16/new-feature" and wanted
   to push to that branch in Gerrit but wanted to pull the external
   modules for 16, you'd create the following .develvars file:

  [branch "new-feature-work"]
      mainline-branch = 16

  The .gitreview file would still look like:

  [gerrit]
  defaultbranch=development/16/new-feature

  ...which would cause any reviews pushed from "new-feature-work" to
  go to the "development/16/new-feature" branch in Gerrit.

  The key is that the .develvars file is NEVER checked in (it's been
  added to .gitignore).

  3.  --- Well Known Development Branch

  If you're actually working in a branch named like
  "development/<mainline_branch>/some-feature", the mainline branch
  will be parsed from it.

  4.  --- .gitreview

  If none of the earlier conditions exist, the .gitreview
  "defaultbranch" variable will be used just as before.


- ### jansson: Update bundled to 2.14 version.                                        
  Author: Joshua C. Colp  
  Date:   2022-02-23  

  ASTERISK-29353


- ### func_channel: Add lastcontext and lastexten.                                    
  Author: Naveen Albert  
  Date:   2022-01-06  

  Adds the lastcontext and lastexten channel fields to allow users
  to access previous dialplan execution locations.

  ASTERISK-29840 #close


- ### channel.c: Clean up debug level 1.                                              
  Author: Naveen Albert  
  Date:   2022-02-05  

  Although there are 10 debugs levels, over time,
  many current debug calls have come to use
  inappropriately low debug levels. In particular,
  a select few debug calls (currently all debug 1)
  can result in thousands of debug messages per minute
  for a single call.

  This can adds a lot of noise to core debug
  which dilutes the value in having different
  debug levels in the first place, as these
  log messages are from the core internals are
  are better suited for higher debug levels.

  Some debugs levels are thus adjusted so that
  debug level 1 is not inappropriately overloaded
  with these extremely high-volume and general
  debug messages.

  ASTERISK-29897 #close


- ### configs, LICENSE: remove pbx.digium.com.                                        
  Author: Naveen Albert  
  Date:   2022-02-17  

  pbx.digium.com no longer accepts IAX2 calls and
  there are no plans for it to come back.

  Accordingly, nonworking IAX2 URIs are removed from
  both the LICENSE file and the sample config.

  ASTERISK-29923 #close


- ### documentation: Add since tag to xmldocs DTD                                     
  Author: Naveen Albert  
  Date:   2022-02-05  

  Adds the since tag to the documentation DTD so
  that individual applications, functions, etc.
  can now specify when they were added to Asterisk.

  This tag is added at the individual application,
  function, etc. level as opposed to at the module
  level because modules can expand over time as new
  functionality is added, and granularity only
  to the module level would generally not be useful.

  This enables the ability to more easily determine
  when new functionality was added to Asterisk, down
  to minor version as opposed to just by major version.
  This makes it easier for users to write more portable
  dialplan if desired to not use functionality that may
  not be widely available yet.

  ASTERISK-29896 #close


- ### asterisk: Add macro for curl user agent.                                        
  Author: Naveen Albert  
  Date:   2022-01-13  

  Currently, each module that uses libcurl duplicates the standard
  Asterisk curl user agent.

  This adds a global macro for the Asterisk user agent used for
  curl requests to eliminate this duplication.

  ASTERISK-29861 #close


- ### res_stir_shaken: refactor utility function                                      
  Author: Naveen Albert  
  Date:   2021-12-16  

  Refactors temp file utility function into file.c.

  ASTERISK-29809 #close


- ### app_voicemail: Emit warning if asking for nonexistent mailbox.                  
  Author: Naveen Albert  
  Date:   2022-02-16  

  Currently, if VoiceMailMain is called with a mailbox, if that
  mailbox doesn't exist, then the application silently falls back
  to prompting the user for the mailbox, as if no arguments were
  provided.

  However, if a specific mailbox is requested and it doesn't exist,
  then no warning at all is emitted.

  This fixes this behavior to now warn if a specifically
  requested mailbox could not be accessed, before falling back to
  prompting the user for the correct mailbox.

  ASTERISK-29920 #close


- ### res_pjsip_pubsub: fix Batched Notifications stop working                        
  Author: Alexei Gradinari  
  Date:   2022-02-07  

  If Subscription refresh occurred between when the batched notification
  was scheduled and the serialized notification was to be sent,
  then new schedule notification task would never be added.

  There are 2 threads:

  thread #1. ast_sip_subscription_notify is called,
  if notification_batch_interval then call schedule_notification.
  1.1. The schedule_notification checks notify_sched_id > -1
  not true, then
  send_scheduled_notify = 1
  notify_sched_id =
    ast_sched_add(sched, sub_tree->notification_batch_interval, sched_cb....
  1.2. The sched_cb pushes task serialized_send_notify to serializer
  and returns 0 which means no reschedule.
  1.3. The serialized_send_notify checks send_scheduled_notify if it's false
  the just returns. BUT notify_sched_id is still set, so no more ast_sched_add.

  thread #2. pubsub_on_rx_refresh is called
  2.1 it pushes serialized_pubsub_on_refresh_timeout to serializer
  2.2. The serialized_pubsub_on_refresh_timeout calls pubsub_on_refresh_timeout
  which calls send_notify
  2.3. The send_notify set send_scheduled_notify = 0;

  The serialized_send_notify should always unset notify_sched_id.

  ASTERISK-29904 #close


- ### res_pjsip_pubsub: provide a display name for RLS subscriptions                  
  Author: Alexei Gradinari  
  Date:   2022-02-01  

  Whereas BLFs allow to show a display name for each RLS entry,
  the asterisk provides only the extension now.
  This is not end user friendly.

  This commit adds a new resource_list option, resource_display_name,
  to indicate whether display name of resource or the resource name being
  provided for RLS entries.
  If this option is enabled, the Display Name will be provided.
  This option is disabled by default to remain the previous behavior.
  If the 'event' set to 'presence' or 'dialog' the non-empty HINT name
  will be set as the Display Name.
  The 'message-summary' is not supported yet.

  ASTERISK-29891 #close


- ### func_db: Add validity check for key names when writing.                         
  Author: Naveen Albert  
  Date:   2022-02-18  

  Adds a simple sanity check for key names when users are
  writing data to AstDB. This captures four cases indicating
  malformed keynames that generally result in bad data going
  into the DB that the user didn't intend: an empty key name,
  a key name beginning or ending with a slash, and a key name
  containing two slashes in a row. Generally, this is the
  result of a variable being used in the key name being empty.

  If a malformed key name is detected, a warning is emitted
  to indicate the bug in the dialplan.

  ASTERISK-29925 #close


- ### cli: Add core dump info to core show settings.                                  
  Author: Naveen Albert  
  Date:   2022-01-14  

  Adds two pieces of information to the core show settings command
  which are useful in the context of getting backtraces.

  The first is to display whether or not Asterisk would generate
  a core dump if it were to crash.

  The second is to show the current running directory of Asterisk.

  ASTERISK-29866 #close


- ### documentation: Adds missing default attributes.                                 
  Author: Naveen Albert  
  Date:   2022-02-05  

  The configObject tag contains a default attribute which
  allows the default value to be specified, if applicable.
  This allows for the default value to show up specially on
  the wiki in a way that is clear to users.

  There are a couple places in the tree where default values
  are included in the description as opposed to as attributes,
  which means these can't be parsed specially for the wiki.
  These are changed to use the attribute instead of being
  included in the text description.

  ASTERISK-29898 #close


- ### app_mp3: Document and warn about HTTPS incompatibility.                         
  Author: Naveen Albert  
  Date:   2022-02-05  

  mpg123 doesn't support HTTPS, but the MP3Player application
  doesn't document this or warn the user about this. HTTPS
  streams have become more common nowadays and users could
  reasonably try to play them without being aware they should
  use the HTTP stream instead.

  This adds documentation to note this limitation. It also
  throws a warning if users try to use the HTTPS stream to
  tell them to use the HTTP stream instead.

  ASTERISK-29900 #close


- ### app_mf: Add max digits option to ReceiveMF.                                     
  Author: Naveen Albert  
  Date:   2022-01-22  

  Adds an option to the ReceiveMF application to allow specifying a
  maximum number of digits.

  Originally, this capability was not added to ReceiveMF as it was
  with ReceiveSF because typically a ST digit is used to denote that
  sending of digits is complete. However, there are certain signaling
  protocols which simply transmit a digit (such as Expanded In-Band
  Signaling) and for these, it's necessary to be able to read a
  certain number of digits, as opposed to until receiving a ST digit.

  This capability is added as an option, as opposed to as a parameter,
  to remain compatible with existing usage (and not shift the
  parameters).

  ASTERISK-29877 #close


- ### ami: Allow events to be globally disabled.                                      
  Author: Naveen Albert  
  Date:   2022-01-09  

  The disabledevents setting has been added to the general section
  in manager.conf, which allows users to specify events that
  should be globally disabled and not sent to any AMI listeners.

  This allows for processing of these AMI events to end sooner and,
  for frequent AMI events such as Newexten which users may not have
  any need for, allows them to not be processed. Additionally, it also
  cleans up core debug as previously when debug was 3 or higher,
  the debug was constantly spammed by "Analyzing AMI event" messages
  along with a complete dump of the event contents (often for Newexten).

  ASTERISK-29853 #close


- ### taskprocessor.c: Prevent crash on graceful shutdown                             
  Author: Mike Bradeen  
  Date:   2022-02-02  

  When tps_shutdown is called as part of the cleanup process there is a
  chance that one of the taskprocessors that references the
  tps_singletons object is still running.  The change is to allow for
  tps_shutdown to check tps_singleton's container count and give the
  running taskprocessors a chance to finish.  If after
  AST_TASKPROCESSOR_SHUTDOWN_MAX_WAIT (10) seconds there are still
  container references we shutdown anyway as this is most likely a bug
  due to a taskprocessor not being unreferenced.

  ASTERISK-29365


- ### app_queue: load queues and members from Realtime when needed                    
  Author: Alexei Gradinari  
  Date:   2022-01-21  

  There are a lot of Queue AMI actions and Queue applications
  which do not load queue and queue members from Realtime.

  AMI actions
  QueuePause - if queue not in memory - response "Interface not found".
  QueueStatus/QueueSummary - if queue not in memory - empty response.

  Applications:
  PauseQueueMember - if queue not in memory
  	Attempt to pause interface %s, not found
  UnpauseQueueMember - if queue not in memory
  	Attempt to unpause interface xxxxx, not found

  This patch adds a new function load_realtime_queues
  which loads queue and queue members for desired queue
  or all queues and all members if param 'queuename' is NULL or empty.
  Calls the function load_realtime_queues when needed.

  Also this patch fixes leak of ast_config in function set_member_value.

  Also this patch fixes incorrect LOG_WARNING when pausing/unpausing
  already paused/unpaused member.
  The function ast_update_realtime returns 0 when no record modified.
  So 0 is not an error to warn about.

  ASTERISK-29873 #close
  ASTERISK-18416 #close
  ASTERISK-27597 #close


- ### manager.c: Simplify AMI ModuleCheck handling                                    
  Author: Sean Bright  
  Date:   2022-02-07  

  This code was needlessly complex and would fail to properly delimit
  the response message if LOW_MEMORY was defined.


- ### res_prometheus.c: missing module dependency                                     
  Author: Mark Petersen  
  Date:   2022-01-21  

  added res_pjsip_outbound_registration to .requires in AST_MODULE_INFO
  which fixes issue with module crashes on load "FRACK!, Failed assertion"

  ASTERISK-29871


- ### res_pjsip.c: Correct minor typos in 'realm' documentation.                      
  Author: Sean Bright  
  Date:   2022-02-03  


- ### manager.c: Generate valid XML if attribute names have leading digits.           
  Author: Sean Bright  
  Date:   2022-01-31  

  The XML Manager Event Interface (amxml) now generates attribute names
  that are compliant with the XML 1.1 specification. Previously, an
  attribute name that started with a digit would be rendered as-is, even
  though attribute names must not begin with a digit. We now prefix
  attribute names that start with a digit with an underscore ('_') to
  prevent XML validation failures.

  This is not backwards compatible but my assumption is that compliant
  XML parsers would already have been complaining about this.

  ASTERISK-29886 #close


- ### build_tools/make_version: Fix bashism in comparison.                            
  Author: Sean Bright  
  Date:   2022-02-01  

  In POSIX sh (which we indicate in the shebang), there is no ==
  operator.


- ### bundled_pjproject:  Add additional multipart search utils                       
  Author: George Joseph  
  Date:   2022-01-21  

  Added the following APIs:
  pjsip_multipart_find_part_by_header()
  pjsip_multipart_find_part_by_header_str()
  pjsip_multipart_find_part_by_cid_str()
  pjsip_multipart_find_part_by_cid_uri()


- ### chan_sip.c Fix pickup on channel that are in AST_STATE_DOWN                     
  Author: Mark Petersen  
  Date:   2022-01-07  

  resolve issue with pickup on device that uses "183" and not "180"

  ASTERISK-29832


- ### build: Add "basebranch" to .gitreview                                           
  Author: George Joseph  
  Date:   2022-01-26  

  If you have a development branch for a major project that
  will receive gerrit reviews it'll probably be named something
  like "development/16/newproject".  That will necessitate setting
  "defaultbranch=development/16/newproject" in .gitreview.  The
  make_version script uses that variable to construct the asterisk
  version however, which results in versions like
  "GIT-development/16/newproject-ee582a8c7b" which is probably not
  what you want.  Worse, since the download_externals script uses
  make_version to construct the URL to download the binary codecs
  or DPMA.  Since it's expecting a simple numeric version, the
  downloads will fail.

  To get this to work, a new variable "basebranch" has been added
  to .gitreview and make_version has been updated to use that instead
  of defaultversion:

  .gitreview:
  defaultbranch=development/16/myproject
  basebranch=16

  Now git-review will send the reviews to the proper branch
  (development/16/myproject) but the version will still be
  constructed using the simple branch number (16).

  If "basebranch" is missing from .gitreview, make_version will
  fall back to using "defaultbranch".


- ### res_pjsip_outbound_authenticator_digest: Prevent ABRT on cleanup                
  Author: George Joseph  
  Date:   2022-01-31  

  In dev mode, if you call pjsip_auth_clt_deinit() with an auth_sess
  that hasn't been initialized, it'll assert and abort.  If
  digest_create_request_with_auth() fails to find the proper
  auth object however, it jumps to its cleanup which does exactly
  that.  So now we no longer attempt to call pjsip_auth_clt_deinit()
  if we never actually initialized it.

  ASTERISK-29888


- ### cdr: allow disabling CDR by default on new channels                             
  Author: Naveen Albert  
  Date:   2021-12-15  

  Adds a new option, defaultenabled, to the CDR core to
  control whether or not CDR is enabled on a newly created
  channel. This allows CDR to be disabled by default on
  new channels and require the user to explicitly enable
  CDR if desired. Existing behavior remains unchanged.

  ASTERISK-29808 #close


- ### res_tonedetect: Fixes some logic issues and typos                               
  Author: Naveen Albert  
  Date:   2022-01-11  

  Fixes some minor logic issues with the module:

  Previously, the OPT_END_FILTER flag was getting
  tested before options were parsed, so it could
  never evaluate to true (wrong ordering).

  Additionally, the initially parsed timeout (float)
  needs to be compared with 0, not the result int
  which is set afterwards (wrong variable).

  ASTERISK-29857 #close


- ### func_frame_drop: Fix typo referencing wrong buffer                              
  Author: Naveen Albert  
  Date:   2022-01-11  

  In order to get around the issue of certain frames
  having names that could overlap, func_frame_drop
  surrounds names with commas for the purposes of
  comparison.

  The buffer is allocated and printed to properly,
  but the original buffer is used for comparison.
  In most cases, this wouldn't have had any effect,
  but that was not the intention behind the buffer.
  This updates the code to reference the modified
  buffer instead.

  ASTERISK-29854 #close


- ### res/res_rtp_asterisk: fix skip in rtp sequence numbers after dtmf               
  Author: Torrey Searle  
  Date:   2022-01-20  

  When generating dtmfs, asterisk can incorrectly think packet loss
  occured during the dtmf generation, resulting in a jump in sequence
  numbers when forwarding voice frames resumes.  This patch forces
  asterisk to re-learn the expected sequence number after each DTMF
  to avoid this

  ASTERISK-29869 #close


- ### res_http_websocket: Add a client connection timeout                             
  Author: Kevin Harwell  
  Date:   2022-01-13  

  Previously there was no way to specify a connection timeout when
  attempting to connect a websocket client to a server. This patch
  makes it possible to now do such.


- ### build: Rebuild configure and autoconfig.h.in                                    
  Author: Sean Bright  
  Date:   2022-01-21  

  autoconfigh.h.in was missed in the original review for this
  issue. Additionally it looks like I have newer pkg-config autoconf
  macros on my development machine.

  ASTERISK-29817


- ### sched: fix and test a double deref on delete of an executing call back          
  Author: Mike Bradeen  
  Date:   2021-12-08  

  sched: Avoid a double deref when AST_SCHED_DEL_UNREF is called on an
  executing call-back. This is done by adding a new variable 'rescheduled'
  to the struct sched which is set in ast_sched_runq and checked in
  ast_sched_del_nonrunning. ast_sched_del_nonrunning is a replacement for
  now deprecated ast_sched_del which returns a new possible value -2
  if called on an executing call-back with rescheduled set. ast_sched_del
  is modified to call ast_sched_del_nonrunning to maintain existing code.
  AST_SCHED_DEL_UNREF is also updated to look for the -2 in which case it
  will not throw a warning or invoke refcall.
  test_sched: Add a new unit test sched_test_freebird that will check the
  reference count in the resolved scenario.

  ASTERISK-29698


- ### app_queue.c: Queue don't play "thank-you" when here is no hold time announceme..
  Author: Mark Petersen  
  Date:   2022-01-04  

  if holdtime is (0 min, 0 sec) there is no hold time announcements
  we should then also not playing queue-thankyou

  ASTERISK-29831


- ### res_pjsip_sdp_rtp.c: Support keepalive for video streams.                       
  Author: Luke Escude  
  Date:   2022-01-19  

  ASTERISK-28890 #close


- ### build_tools/make_version: Fix sed(1) syntax compatibility with NetBSD           
  Author: Michał Górny  
  Date:   2021-11-11  

  Fix the sed(1) invocation used to process git-svn-id not to use "\s"
  that is a GNU-ism and is not supported by NetBSD sed.  As a result,
  this call did not work properly and make_version did output the full
  git-svn-id line rather than the revision.

  ASTERISK-29852


- ### main/utils: Implement ast_get_tid() for NetBSD                                  
  Author: Michał Górny  
  Date:   2021-11-11  

  Implement the ast_get_tid() function for NetBSD system.  NetBSD supports
  getting the TID via _lwp_self().

  ASTERISK-29850


- ### main: Enable rdtsc support on NetBSD                                            
  Author: Michał Górny  
  Date:   2021-11-11  

  Enable the Linux rdtsc implementation on NetBSD as well.  The assembly
  works correctly there.

  ASTERISK-29851


- ### BuildSystem: Fix misdetection of gethostbyname_r() on NetBSD                    
  Author: Michał Górny  
  Date:   2021-11-11  

  Fix the configure script not to detect the presence of gethostbyname_r()
  on NetBSD incorrectly.  NetBSD includes it as an internal libc symbol
  that is not exposed in system headers and that is incompatible with
  other implementations.  In order to avoid misdetecting it, perform
  the symbol check only if the declaration is found in the public header
  first.

  ASTERISK-29817


- ### include: Remove unimplemented HMAC declarations                                 
  Author: Michał Górny  
  Date:   2021-11-11  

  Remove the HMAC declarations from the includes.  They are
  not implemented nor used anywhere, and their presence breaks the build
  on NetBSD that delivers an incompatible hmac() function in <stdlib.h>.

  ASTERISK-29818


- ### frame.h: Fix spelling typo                                                      
  Author: Naveen Albert  
  Date:   2022-01-11  

  Fixes CNG description from "noice" to "noise".

  ASTERISK-29855 #close


- ### res_rtp_asterisk: Fix typo in flag test/set                                     
  Author: Naveen Albert  
  Date:   2022-01-11  

  The code currently checks to see if an RFC3389
  warning flag is set, except if it is, it merely
  sets the flag again, the logic of which doesn't
  make any sense.

  This adjusts the if comparison to check if the
  flag has NOT been set, and if so, emit a notice
  log event and set the flag so that future frames
  do not cause an event to be logged.

  ASTERISK-29856 #close


- ### bundled_pjproject: Fix srtp detection                                           
  Author: George Joseph  
  Date:   2022-01-18  

  Reverted recent change that set '--with-external-srtp' instead
  of '--without-external-srtp'.  Since Asterisk handles all SRTP,
  we don't need it enabled in pjproject at all.

  ASTERISK-29867


- ### res_pjsip: Make message_filter and session multipart aware                      
  Author: George Joseph  
  Date:   2022-01-10  

  Neither pjsip_message_filter's filter_on_tx_message() nor
  res_pjsip_session's session_outgoing_nat_hook() were multipart
  aware and just assumed that an SDP would be the only thing in
  a message body.  Both were changed to use the new
  pjsip_get_sdp_info() function which searches for an sdp in
  both single- and multi- part message bodies.

  ASTERISK-29813


- ### build: Fix issues building pjproject                                            
  Author: George Joseph  
  Date:   2022-01-12  

  The change to allow easier hacking on bundled pjproject created
  a few issues:

  * The new Makefile was trying to run the bundled make even if
    PJPROJECT_BUNDLED=no.  third-party/Makefile now checks for
    PJPROJECT_BUNDLED and JANSSON_BUNDLED and skips them if they
    are "no".

  * When building with bundled, config_site.h was being copied
    only if a full make or a "make main" was done.  A "make res"
    would fail all the pjsip modules because they couldn't find
    config_site.h.  The Makefile now copies config_site.h and
    asterisk_malloc_debug.h into the pjproject source tree
    when it's "configure" is performed.  This is how it used
    to be before the big change.

  ASTERISK-29858


- ### res_pjsip: Add utils for checking media types                                   
  Author: George Joseph  
  Date:   2022-01-06  

  Added two new functions to assist checking media types...

  * ast_sip_are_media_types_equal compares two pjsip_media_types.
  * ast_sip_is_media_type_in tests if one media type is in a list
    of others.

  Added static definitions for commonly used media types to
  res_pjsip.h.

  Changed several modules to use the new functions and static
  definitions.

  ASTERISK_29813
  (not ready to close)


- ### bundled_pjproject: Create generic pjsip_hdr_find functions                      
  Author: George Joseph  
  Date:   2022-01-12  

  pjsip_msg_find_hdr(), pjsip_msg_find_hdr_by_name(), and
  pjsip_msg_find_hdr_by_names() require a pjsip_msg to be passed in
  so if you need to search a header list that's not in a pjsip_msg,
  you have to do it yourself.  This commit adds generic versions of
  those 3 functions that take in the actual header list head instead
  of a pjsip_msg so if you need to search a list of headers in
  something like a pjsip_multipart_part, you can do so easily.


- ### say.c: Prevent erroneous failures with 'say' family of functions.               
  Author: Sean Bright  
  Date:   2022-01-12  

  A regression was introduced in ASTERISK~29531 that caused 'say'
  functions to fail with file lists that would previously have
  succeeded. This caused affected channels to hang up where previously
  they would have continued.

  We now explicitly check for the empty string to restore the previous
  behavior.

  ASTERISK-29859 #close


- ### documentation: Document built-in system and channel vars                        
  Author: Naveen Albert  
  Date:   2022-01-08  

  Documentation for built-in special system and channel
  vars is currently outdated, and updating is a manual
  process since there is no XML documentation for these
  anywhere.

  This adds documentation for system vars to func_env
  and for channel vars to func_channel so that they
  appear along with the corresponding fields that would
  be accessed using a function.

  ASTERISK-29848 #close


- ### pbx_variables: add missing ASTSBINDIR variable                                  
  Author: Naveen Albert  
  Date:   2022-01-08  

  Every config variable in the directories
  section of asterisk.conf currently has a
  counterpart built-in variable containing
  the value of the config option, except
  for the last one, astsbindir, which should
  have an ASTSBINDIR variable.

  However, the actual corresponding ASTSBINDIR
  variable is missing in pbx_variables.c.

  This adds the missing variable so that all
  the config options have their corresponding
  variable.

  ASTERISK-29847 #close


- ### bundled_pjproject:  Make it easier to hack                                      
  Author: George Joseph  
  Date:   2021-11-30  

  There are times when you need to troubleshoot issues with bundled
  pjproject or add new features that need to be pushed upstream
  but...

  * The source directory created by extracting the pjproject tarball
    is not scanned for code changes so you have to keep forcing
    rebuilds.
  * The source directory isn't a git repo so you can't easily create
    patches, do git bisects, etc.
  * Accidentally doing a make distclean will ruin your day by wiping
    out the source directory, and your changes.
  * etc.

  This commit makes that easier.
  See third-party/pjproject/README-hacking.md for the details.

  ASTERISK-29824


- ### utils.c: Remove all usages of ast_gethostbyname()                               
  Author: Sean Bright  
  Date:   2021-12-24  

  gethostbyname() and gethostbyname_r() are deprecated in favor of
  getaddrinfo() which we use in the ast_sockaddr family of functions.

  ASTERISK-29819 #close


- ### say.conf: fix 12pm noon logic                                                   
  Author: Naveen Albert  
  Date:   2021-12-13  

  Fixes 12pm noon incorrectly returning 0/a.m.
  Also fixes a misspelling typo in the config.

  ASTERISK-29695 #close


- ### pjproject: Fix incorrect unescaping of tokens during parsing                    
  Author: Sean Bright  
  Date:   2022-01-04  

  ASTERISK-29664 #close


- ### app_queue.c: Support for Nordic syntax in announcements                         
  Author: Mark Petersen  
  Date:   2021-12-30  

  adding support for playing the correct en/et for nordic languages
  by adding 'n' for neuter gender in the relevant ast_say_number

  ASTERISK-29827


- ### dsp: Add define macro for DTMF_MATRIX_SIZE                                      
  Author: Naveen Albert  
  Date:   2021-12-23  

  Adds the macro DTMF_MATRIX_SIZE to replace
  the magic number 4 sprinkled throughout
  dsp.c.

  ASTERISK-29815 #close


- ### ami: Add AMI event for Wink                                                     
  Author: Naveen Albert  
  Date:   2022-01-03  

  Adds an AMI event for a wink frame.

  ASTERISK-29830 #close


- ### cli: Add module refresh command                                                 
  Author: Naveen Albert  
  Date:   2021-12-15  

  Adds a command to the CLI to unload and then
  load a module. This makes it easier to perform
  these operations which are often done
  subsequently to load a new version of a module.

  "module reload" already refers to reloading of
  configuration, so the name "refresh" is chosen
  instead.

  ASTERISK-29807 #close


- ### app_mp3: Throw warning on nonexistent stream                                    
  Author: Naveen Albert  
  Date:   2022-01-03  

  Currently, the MP3Player application doesn't
  emit a warning if attempting to play a stream
  which no longer exists. This can be a common
  scenario as many mp3 streams are valid at some
  point but can disappear at any time.

  Now a warning is thrown if attempting to play
  a nonexistent MP3 stream, instead of silently
  exiting.

  ASTERISK-29829 #close


- ### documentation: Add missing AMI documentation                                    
  Author: Naveen Albert  
  Date:   2021-12-13  

  Adds missing documentation for some channel,
  bridge, and queue events.

  ASTERISK-24427
  ASTERISK-29515


- ### tcptls.c: refactor client connection to be more robust                          
  Author: Kevin Harwell  
  Date:   2021-11-15  

  The current TCP client connect code, blocks and does not handle EINTR
  error case.

  This patch makes the client socket non-blocking while connecting,
  ensures a connect does not immediately fail due to EINTR "errors",
  and adds a connect timeout option.

  The original client start call sets the new timeout option to
  "infinite", thus making sure old, orginal behavior is retained.

  ASTERISK-29746 #close


- ### app_sf: Add full tech-agnostic SF support                                       
  Author: Naveen Albert  
  Date:   2021-12-13  

  Adds tech-agnostic support for SF signaling
  by adding SF sender and receiver applications
  as well as Dial integration.

  ASTERISK-29802 #close


- ### app_queue: Fix hint updates, allow dup. hints                                   
  Author: Steve Davies  
  Date:   2021-12-15  

  A previous patch for ASTERISK_29578 caused a 'leak' of
  extension state information across queues, causing the
  state of the first member of unrelated queues to be
  updated in addition to the correct member. Which queues
  and members depended on the order of queues in the
  iterator.

  Additionally, it is possible to use the same 'hint:' on
  multiple queue members, so the update cannot break out
  of the update loop early when a match is found.

  ASTERISK-29806 #close


- ### say.c: Honor requests for DTMF interruption.                                    
  Author: Sean Bright  
  Date:   2021-12-23  

  SayAlpha, SayAlphaCase, SayDigits, SayMoney, SayNumber, SayOrdinal,
  and SayPhonetic all claim to allow DTMF interruption if the
  SAY_DTMF_INTERRUPT channel variable is set to a truthy value, but we
  are failing to break out of a given 'say' application if DTMF actually
  occurs.

  ASTERISK-29816 #close


- ### res_pjsip_sdp_rtp: Preserve order of RTP codecs                                 
  Author: Florentin Mayer  
  Date:   2021-11-16  

  The ast_rtp_codecs_payloads functions do not preserve the order in which
  the payloads were specified on an incoming SDP media line. This leads to
  a problem with the codec negotiation functionality, as the format
  capabilities of the stream are extracted from the ast_rtp_codecs. This
  commit moves the ast_rtp_codec to ast_format conversion to the place
  where the order is still known.

  ASTERISK-28863
  ASTERISK-29320


- ### bridge: Unlock channel during Local peer check.                                 
  Author: Joshua C. Colp  
  Date:   2021-12-27  

  It's not safe to keep the channel locked while locking
  the peer Local channel, as it can result in a deadlock.

  This change unlocks it during this time but keeps the
  bridge locked to ensure nothing changes about the bridge.

  ASTERISK-29821


- ### test_time.c: Tolerate DST transitions                                           
  Author: Josh Soref  
  Date:   2021-11-07  

  When test_timezone_watch runs very near a DST transition,
  two time zones that would otherwise be expected to report the same
  time can differ because of the DST transition.

  Instead of having the test fail when this happens, report the
  times, time zones, and dst flags.

  ASTERISK-29722


- ### bundled_pjproject:  Add more support for multipart bodies                       
  Author: George Joseph  
  Date:   2021-12-14  

  Adding upstream patch for pull request...
  https://github.com/pjsip/pjproject/pull/2920
  ---------------------------------------------------------------

  sip_inv:  Additional multipart support (#2919)

  sip_inv.c:inv_check_sdp_in_incoming_msg() deals with multipart
  message bodies in rdata correctly. In the case where early media is
  involved though, the existing sdp has to be retrieved from the last
  tdata sent in this transaction. This, however, always assumes that
  the sdp sent is in a non-multipart body. While there's a function
  to retrieve the sdp from multipart and non-multpart rdata bodies,
  no similar function for tdata exists.  So...

  * The existing pjsip_rdata_get_sdp_info2 was refactored to
    find the sdp in any body, multipart or non-multipart, and
    from either an rdata or tdata.  The new function is
    pjsip_get_sdp_info.  This new function detects whether the
    pjsip_msg->body->data is the text representation of the sdp
    from an rdata or an existing pjmedia_sdp_session object
    from a tdata, or whether pjsip_msg->body is a multipart
    body containing either of the two sdp formats.

  * The exsting pjsip_rdata_get_sdp_info and pjsip_rdata_get_sdp_info2
    functions are now wrappers that get the body and Content-Type
    header from the rdata and call pjsip_get_sdp_info.

  * Two new wrappers named pjsip_tdata_get_sdp_info and
    pjsip_tdata_get_sdp_info2 have been created that get the body
    from the tdata and call pjsip_get_sdp_info.

  * inv_offer_answer_test.c was updated to test multipart scenarios.

  ASTERISK-29804


- ### ast_coredumper: Fix deleting results when output dir is set                     
  Author: Frederic Van Espen  
  Date:   2021-12-09  

  When OUTPUTDIR is set to another directory and the
  --delete-results-after is set, the resulting txt files are
  not deleted.

  ASTERISK-29794 #close


- ### pbx_variables: initialize uninitialized variable                                
  Author: Naveen Albert  
  Date:   2021-12-13  

  The variable cp4 in a variable substitution function
  can potentially be used without being initialized
  currently. This causes Asterisk to no longer compile.

  This initializes cp4 to NULL to make the compiler
  happy.

  ASTERISK-29803 #close


- ### app_queue.c: added DIALEDPEERNUMBER on outgoing channel                         
  Author: Mark Petersen  
  Date:   2021-12-08  

  added that we set DIALEDPEERNUMBER on the outgoing channels
  so it is avalible in b(content^extension^line)
  this add the same behaviour as Dial

  ASTERISK-29795


- ### http.c: Add ability to create multiple HTTP servers                             
  Author: Kevin Harwell  
  Date:   2021-11-15  

  Previously, it was only possible to have one HTTP server in Asterisk.
  With this patch it is now possible to have multiple HTTP servers
  listening on different addresses.

  Note, this behavior has only been made available through an API call
  from within the TEST_FRAMEWORK. Specifically, this feature has been
  added in order to allow unit test to create/start and stop servers,
  if one has not been enabled through configuration.


- ### app.c: Throw warnings for nonexistent options                                   
  Author: Naveen Albert  
  Date:   2021-12-13  

  Currently, Asterisk doesn't throw warnings if options
  are passed into applications that don't accept them.
  This can confuse users if they're unaware that they
  are doing something wrong.

  This adds an additional check to parse_options so that
  a warning is thrown anytime an option is parsed that
  doesn't exist in the parsing application, so that users
  are notified of the invalid usage.

  ASTERISK-29801 #close


- ### app_voicemail.c: Support for Danish syntax in VM                                
  Author: Mark Petersen  
  Date:   2021-12-08  

  added support for playing the correct plural sound file
  dependen on where you have 1 or multipe messages
  based on the existing SE/NO code

  ASTERISK-29797


- ### app_sendtext: Add ReceiveText application                                       
  Author: Naveen Albert  
  Date:   2021-11-17  

  Adds a ReceiveText application that can be used in
  conjunction with SendText. Currently, there is no
  way in Asterisk to receive text in the dialplan
  (or anywhere else, really). This allows for Asterisk
  to be the recipient of text instead of just the sender.

  ASTERISK-29759 #close


- ### strings: Fix enum names in comment examples                                     
  Author: Naveen Albert  
  Date:   2021-12-12  

  The enum values for ast_strsep_flags includes
  AST_STRSEP_STRIP. However, some comments reference
  AST_SEP_STRIP, which doesn't exist. This fixes
  these comments to use the correct value.

  ASTERISK-29800 #close


- ### pbx_variables: Increase parsing capabilities of MSet                            
  Author: Naveen Albert  
  Date:   2021-11-20  

  Currently MSet can only parse a maximum of 24 variables.
  If more variables are provided to MSet, the 24th variable
  will simply contain the remainder of the string and the
  remaining variables thereafter will never get set.

  This increases the number of variables that can be parsed
  in one go from 24 to 99. Additionally, documentation is added
  since this limitation is currently undocumented and is
  confusing to users who encounter this limitation.

  ASTERISK-29766 #close


- ### chan_sip: Fix crash when accessing RURI before initiating outgoing call         
  Author: Naveen Albert  
  Date:   2021-11-24  

  Attempting to access ${CHANNEL(ruri)} in a pre-dial handler before
  initiating an outgoing call will cause Asterisk to crash. This is
  because a null field is accessed, resulting in an offset from null and
  subsequent memory access violation.

  Since RURI is not guaranteed to exist, we now check if the base
  pointer is non-null before calculating an offset.

  ASTERISK-29772


- ### func_json: Adds JSON_DECODE function                                            
  Author: Naveen Albert  
  Date:   2021-10-25  

  Adds the JSON_DECODE function for parsing JSON in the
  dialplan. JSON parsing already exists in the Asterisk
  core and is used for many different things. This
  function exposes the basic parsing capability to
  the user in the dialplan, for instance, in conjunction
  with CURL for using API responses.

  ASTERISK-29706 #close


- ### configs: Updates to sample configs                                              
  Author: Naveen Albert  
  Date:   2021-11-17  

  Includes some minor updates to extensions.conf
  and iax.conf. In particular, the demonstration
  of macros in extensions.conf is removed, as
  Macro is deprecated and will be removed soon.
  These examples have been replaced with examples
  demonstrating the usage of Gosub instead.

  The older exten => ...,n syntax is also mostly
  replaced with the same keyword to demonstrate the
  newer, more concise way of defining extensions.

  IAXTEL no longer exists, so this example is replaced
  with something more generic.

  Some documentation is also added to extensions.conf
  and iax.conf to clarify some of the new expanded
  encryption capabilities with IAX2.

  ASTERISK-29758 #close


- ### pbx: Add variable substitution API for extensions                               
  Author: Naveen Albert  
  Date:   2021-11-15  

  Currently, variable substitution involving dialplan
  extensions is quite clunky since it entails obtaining
  the current dialplan location, backing it up, storing
  the desired variables for substitution on the channel,
  performing substitution, then restoring the original
  location.

  In addition to being clunky, things could also go wrong
  if an async goto were to occur and change the dialplan
  location during a substitution.

  Fundamentally, there's no reason it needs to be done this
  way, so new API is added to allow for directly passing in
  the dialplan location for the purposes of variable
  substitution so we don't need to mess with the channel
  information anymore. Existing API is not changed.

  ASTERISK-29745 #close


- ### CHANGES: Correct reference to configuration file.                               
  Author: Sean Bright  
  Date:   2021-12-11  


- ### app_mf: Add full tech-agnostic MF support                                       
  Author: Naveen Albert  
  Date:   2021-09-22  

  Adds tech-agnostic support for MF signaling by adding
  MF sender and receiver applications as well as Dial
  integration.

  ASTERISK-29496-mf #do-not-close


- ### xmldoc: Avoid whitespace around value for parameter/required.                   
  Author: Alexander Traud  
  Date:   2021-12-06  

  Otherwise, the value 'false' was not found in the enumerated set of
  the XML DTD for the XML attribute 'required' in the XML element
  'parameter'. Therefore, DTD validation of the runtime XML failed.

  ASTERISK-29790


- ### progdocs: Fix Doxygen left-overs.                                               
  Author: Alexander Traud  
  Date:   2021-12-04  


- ### xmldoc: Correct definition for XML element 'matchInfo'.                         
  Author: Alexander Traud  
  Date:   2021-12-06  

  ASTERISK-29791


- ### progdocs: Update Makefile.                                                      
  Author: Alexander Traud  
  Date:   2021-11-23  

  In developer mode, use internal documentation as well.
  This should produce no warnings. Fix yours!

  In noisy mode, output all possible warnings of Doxygen.
  This creates zillion of warnings. Double-check your current module!

  Any warnings are in the file './doxygen.log'. Beside that, this change
  avoids deprecated parameters because the configuration file for Doxygen
  contains only those parameters which differ from the default. This
  avoids the need to update the file on each run. Furthermore, it adds
  AST_VECTOR to be expanded. Finally, the default name for that file is
  Doxyfile. Therefore, let us use that!

  ASTERISK-26991
  ASTERISK-20259


- ### res_pjsip_sdp_rtp: Do not warn on unknown sRTP crypto suites.                   
  Author: Alexander Traud  
  Date:   2021-12-03  

  res_sdp_crypto_parse_offer(.) emits many log messages already.

  ASTERISK-29785


- ### channel: Short-circuit ast_channel_get_by_name() on empty arg.                  
  Author: Sean Bright  
  Date:   2021-11-30  

  We know that passing a NULL or empty argument to
  ast_channel_get_by_name() will never result in a matching channel and
  will always result in an error being emitted, so just short-circuit
  out in that case.

  ASTERISK-28219 #close


- ### res_rtp_asterisk: Addressing possible rtp range issues                          
  Author: Mike Bradeen  
  Date:   2021-10-26  

  res/res_rtp_asterisk.c: Adding 1 to rtpstart if it is deteremined
  that rtpstart was configured to be an odd value. Also adding a loop
  counter to prevent a possible infinite loop when looking for a free
  port.

  ASTERISK-27406


- ### apps/app_dial.c: HANGUPCAUSE reason code for CANCEL is set to AST_CAUSE_NORMAL..
  Author: Mark Petersen  
  Date:   2021-08-24  

  changed that when we recive a CANCEL that we set HANGUPCAUSE to AST_CAUSE_NORMAL_CLEARING

  ASTERISK-28053
  Reported by: roadkill


- ### res: Fix for Doxygen.                                                           
  Author: Alexander Traud  
  Date:   2021-11-19  

  These are the remaining issues found in /res.

  ASTERISK-29761


- ### res_fax_spandsp: Add spandsp 3.0.0+ compatibility                               
  Author: Dustin Marquess  
  Date:   2021-11-08  

  Newer versions of spandsp did refactoring of code to add new features
  like color FAXing. This refactoring broke backwards compatibility.
  Add support for the new version while retaining support for 0.0.6.

  ASTERISK-29729 #close


- ### main: Fix for Doxygen.                                                          
  Author: Alexander Traud  
  Date:   2021-11-19  

  ASTERISK-29763


- ### progdocs: Fix for Doxygen, the hidden parts.                                    
  Author: Alexander Traud  
  Date:   2021-11-27  

  ASTERISK-29779


- ### progdocs: Fix grouping for latest Doxygen.                                      
  Author: Alexander Traud  
  Date:   2021-11-12  

  Since Doxygen 1.8.16, a special comment block is required. Otherwise
  (pure C comment), the group command is ignored. Additionally, several
  unbalanced group commands were fixed.

  ASTERISK-29732


- ### documentation: Standardize examples                                             
  Author: Naveen Albert  
  Date:   2021-11-25  

  Most examples in the XML documentation use the
  example tag to demonstrate examples, which gets
  parsed specially in the Wiki to make it easier
  to follow for users.

  This fixes a few modules to use the example
  tag instead of vanilla para tags to bring them
  in line with the standard syntax.

  ASTERISK-29777 #close


- ### config.c: Prevent UB in ast_realtime_require_field.                             
  Author: Sean Bright  
  Date:   2021-11-28  

  A backend's implementation of the realtime 'require' function may call
  va_arg() and then fail, leaving the va_list in an undefined
  state. Pass a copy of the va_list instead.

  ASTERISK-29771 #close


- ### app_voicemail: Refactor email generation functions                              
  Author: Naveen Albert  
  Date:   2021-11-01  

  Refactors generic functions used for email generation
  into utils.c so that they can be used by multiple
  modules, including app_voicemail and app_minivm,
  to avoid code duplication.

  ASTERISK-29715 #close


- ### stir/shaken: Avoid a compiler extension of GCC.                                 
  Author: Alexander Traud  
  Date:   2021-11-25  

  ASTERISK-29776


- ### progdocs: Remove outdated references in doxyref.h.                              
  Author: Alexander Traud  
  Date:   2021-11-23  

  ASTERISK-29773


- ### logger: use __FUNCTION__ instead of __PRETTY_FUNCTION__                         
  Author: Jaco Kroon  
  Date:   2021-10-28  

  This avoids a few long-name overflows, at the cost of less instructive
  names in the case of C++ (specifically overloaded functions and class
  methods).  This in turn is offset against the fact that we're logging
  the filename and line numbers in any case.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### xmldoc: Fix for Doxygen.                                                        
  Author: Alexander Traud  
  Date:   2021-11-20  

  ASTERISK-29765


- ### astobj2.c: Fix core when ref_log enabled                                        
  Author: Mike Bradeen  
  Date:   2021-11-16  

  In the AO2_ALLOC_OPT_LOCK_NOLOCK case the referenced obj
  structure is freed, but is then referenced later if ref_log is
  enabled. The change is to store the obj->priv_data.options value
  locally and reference it instead of the value from the freed obj

  ASTERISK-29730


- ### channels: Fix for Doxygen.                                                      
  Author: Alexander Traud  
  Date:   2021-11-19  

  ASTERISK-29762


- ### bridge: Deny full Local channel pair in bridge.                                 
  Author: Joshua C. Colp  
  Date:   2021-11-16  

  Local channels are made up of two pairs - the 1 and 2
  sides. When a frame goes in one side, it comes out the
  other. Back and forth. When both halves are in a
  bridge this creates an infinite loop of frames.

  This change makes it so that bridging no longer
  allows both of these sides to exist in the same
  bridge.

  ASTERISK-29748


- ### res_tonedetect: Add call progress tone detection                                
  Author: Naveen Albert  
  Date:   2021-11-06  

  Makes basic call progress tone detection available
  in a tech-agnostic manner with the addition of the
  ToneScan application. This can determine if the channel
  has encountered a busy signal, SIT tones, dial tone,
  modem, fax machine, etc. A few basic async progress
  tone detect options are also added to the TONE_DETECT
  function.

  ASTERISK-29720 #close


- ### rtp_engine: Add type field for JSON RTCP Report stasis messages                 
  Author: Boris P. Korzun  
  Date:   2021-11-08  

  ASTERISK-29727 #close


- ### odbc: Fix for Doxygen.                                                          
  Author: Alexander Traud  
  Date:   2021-11-17  

  ASTERISK-29754


- ### parking: Fix for Doxygen.                                                       
  Author: Alexander Traud  
  Date:   2021-11-17  

  ASTERISK-29753


- ### res_ari: Fix for Doxygen.                                                       
  Author: Alexander Traud  
  Date:   2021-11-17  

  ASTERISK-29756


- ### frame: Fix for Doxygen.                                                         
  Author: Alexander Traud  
  Date:   2021-11-17  

  ASTERISK-29755


- ### ari-stubs: Avoid 'is' as comparism with an literal.                             
  Author: Alexander Traud  
  Date:   2021-11-17  

  Python 3.9.7 gave a syntax warning.


- ### BuildSystem: Consistently allow 'ye' even for Jansson.                          
  Author: Alexander Traud  
  Date:   2021-11-08  

  Furthermore, consistently use not 'No' but ':' for non-existent file
  paths. Finally, use the same pattern for checking file paths:
    a)  = ":"
    b) != "x:"


- ### stasis: Fix for Doxygen.                                                        
  Author: Alexander Traud  
  Date:   2021-11-16  

  ASTERISK-29750


- ### app: Fix for Doxygen.                                                           
  Author: Alexander Traud  
  Date:   2021-11-17  

  ASTERISK-29752


- ### res_xmpp: Fix for Doxygen.                                                      
  Author: Alexander Traud  
  Date:   2021-11-16  

  ASTERISK-29749


- ### channel: Fix for Doxygen.                                                       
  Author: Alexander Traud  
  Date:   2021-11-16  

  ASTERISK-29751


- ### chan_iax2: Fix for Doxygen.                                                     
  Author: Alexander Traud  
  Date:   2021-11-13  

  ASTERISK-29737


- ### res_pjsip: Fix for Doxygen.                                                     
  Author: Alexander Traud  
  Date:   2021-11-16  

  ASTERISK-29747


- ### bridges: Fix for Doxygen.                                                       
  Author: Alexander Traud  
  Date:   2021-11-15  

  ASTERISK-29743


- ### addons: Fix for Doxygen.                                                        
  Author: Alexander Traud  
  Date:   2021-11-15  

  ASTERISK-29742


- ### apps: Fix for Doxygen.                                                          
  Author: Alexander Traud  
  Date:   2021-11-15  

  ASTERISK-29740


- ### tests: Fix for Doxygen.                                                         
  Author: Alexander Traud  
  Date:   2021-11-15  

  ASTERISK-29741


- ### progdocs: Avoid multiple use of section labels.                                 
  Author: Alexander Traud  
  Date:   2021-11-12  

  ASTERISK-29735


- ### progdocs: Use Doxygen \example correctly.                                       
  Author: Alexander Traud  
  Date:   2021-11-12  

  ASTERISK-29734


- ### bridge_channel: Fix for Doxygen.                                                
  Author: Alexander Traud  
  Date:   2021-11-13  

  ASTERISK-29736


- ### progdocs: Avoid 'name' with Doxygen \file.                                      
  Author: Alexander Traud  
  Date:   2021-11-12  

  Fixes four misuses of the parameter 'name'. Additionally, for
  consistency and to avoid such an issue in future, those few other
  places, which used '\file name', were changed just to '\file'. Then,
  Doxygen uses the name of the current file.

  ASTERISK-29733


- ### app_morsecode: Fix deadlock                                                     
  Author: Naveen Albert  
  Date:   2021-11-15  

  Fixes a deadlock in app_morsecode caused by locking
  the channel twice when reading variables from the
  channel. The duplicate lock is simply removed.

  ASTERISK-29744 #close


- ### app_read: Fix custom terminator functionality regression                        
  Author: Naveen Albert  
  Date:   2021-10-25  

  Currently, when the t option is specified with no arguments,
  the # character is still treated as a terminator, even though
  no character should be treated as a terminator.

  This is because a previous regression fix was modified to
  remove the use of NULL as a default altogether. However,
  NULL and an empty string actually refer to different
  arrangements and should be treated differently. NULL is the
  default terminator (#), while an empty string removes the
  terminator altogether. This is the behavior being used by
  the rest of the core.

  Additionally, since S_OR catches empty strings as well as
  NULL (not intended), this is changed to a ternary operator
  instead, which fixes the behavior.

  ASTERISK-29705 #close


- ### res_pjsip_callerid: Fix OLI parsing                                             
  Author: Naveen Albert  
  Date:   2021-10-24  

  Fix parsing of ANI2/OLI information, since it was previously
  parsing the user, when it should have been parsing other_param.

  Also improves the parsing by using pjproject native functions
  rather than trying to parse the parameters ourselves like
  chan_sip did. A previous attempt at this caused a crash, but
  this works correctly now.

  ASTERISK-29703 #close


- ### build_tools: Spelling fixes                                                     
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  binutils

  ASTERISK-29714


- ### contrib: Spelling fixes                                                         
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  standard
  increase
  comments
  valgrind
  promiscuous
  editing
  libtonezone
  storage
  aggressive
  whitespace
  russellbryant
  consecutive
  peternixon

  ASTERISK-29714


- ### codecs: Spelling fixes                                                          
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  voiced
  denumerator
  codeword
  upsampling
  constructed
  residual
  subroutine
  conditional
  quantizing
  courtesy
  number

  ASTERISK-29714


- ### formats: Spelling fixes                                                         
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  truncate

  ASTERISK-29714


- ### CREDITS: Spelling fixes                                                         
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  contributors

  ASTERISK-29714


- ### addons: Spelling fixes                                                          
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  definition
  listener
  fastcopy
  logical
  registration
  classify
  documentation
  explicitly
  dialed
  endpoint
  elements
  arithmetic
  might
  prepend
  byte
  terminal
  inquiry
  skipping
  aliases
  calling
  absent
  authentication
  transmit
  their
  ericsson
  disconnecting
  redir
  items
  client
  adapter
  transmitter
  existing
  satisfies
  pointer
  interval
  supplied

  ASTERISK-29714


- ### configs: Spelling fixes                                                         
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  password
  excludes
  undesirable
  checksums
  through
  screening
  interpreting
  database
  causes
  initiation
  member
  busydetect
  defined
  severely
  throughput
  recognized
  counter
  require
  indefinitely
  accounts

  ASTERISK-29714


- ### doc: Spelling fixes                                                             
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  transparent
  roughly

  ASTERISK-29714


- ### menuselect: Spelling fixes                                                      
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  dependency
  unless
  random
  dependencies
  delimited
  randomly
  modules

  ASTERISK-29714


- ### include: Spelling fixes                                                         
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  activities
  forward
  occurs
  unprepared
  association
  compress
  extracted
  doubly
  callback
  prometheus
  underlying
  keyframe
  continue
  convenience
  calculates
  ignorepattern
  determine
  subscribers
  subsystem
  synthetic
  applies
  example
  manager
  established
  result
  microseconds
  occurrences
  unsuccessful
  accommodates
  related
  signifying
  unsubscribe
  greater
  fastforward
  itself
  unregistering
  using
  translator
  sorcery
  implementation
  serializers
  asynchronous
  unknowingly
  initialization
  determining
  category
  these
  persistent
  propagate
  outputted
  string
  allocated
  decremented
  second
  cacheability
  destructor
  impaired
  decrypted
  relies
  signaling
  based
  suspended
  retrieved
  functions
  search
  auth
  considered

  ASTERISK-29714


- ### UPGRADE.txt: Spelling fixes                                                     
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  themselves
  support
  received

  ASTERISK-29714


- ### bridges: Spelling fixes                                                         
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  multiplication
  potentially
  iteration
  interaction
  virtual
  synthesis
  convolve
  initializes
  overlap

  ASTERISK-29714


- ### apps: Spelling fixes                                                            
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  simultaneously
  administrator
  directforward
  attachfmt
  dailplan
  automatically
  applicable
  nouns
  explicit
  outside
  sponsored
  attachment
  audio
  spied
  doesn't
  counting
  encoded
  implements
  recursively
  emailaddress
  arguments
  queuerules
  members
  priority
  output
  advanced
  silencethreshold
  brazilian
  debugging
  argument
  meadmin
  formatting
  integrated
  sneakiness

  ASTERISK-29714


- ### channels: Spelling fixes                                                        
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  appease
  permanently
  overriding
  residue
  silliness
  extension
  channels
  globally
  reference
  japanese
  group
  coordinate
  registry
  information
  inconvenience
  attempts
  cadence
  payloads
  presence
  provisioning
  mimics
  behavior
  width
  natively
  syslabel
  not owning
  unquelch
  mostly
  constants
  interesting
  active
  unequipped
  brodmann
  commanding
  backlogged
  without
  bitstream
  firmware
  maintain
  exclusive
  practically
  structs
  appearance
  range
  retransmission
  indication
  provisional
  associating
  always
  whether
  cyrillic
  distinctive
  components
  reinitialized
  initialized
  capability
  switches
  occurring
  happened
  outbound

  ASTERISK-29714


- ### tests: Spelling fixes                                                           
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  mounting
  jitterbuffer
  thrashing
  original
  manipulating
  entries
  actual
  possibility
  tasks
  options
  positives
  taskprocessor
  other
  dynamic
  declarative

  ASTERISK-29714


- ### CHANGES: Spelling fixes                                                         
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  issuing
  execution
  bridging
  alert
  respective
  unlikely
  confbridge
  offered
  negotiation
  announced
  engineer
  systems
  inherited
  passthrough
  functionality
  supporting
  conflicts
  semantically
  monitor
  specify
  specifiable

  ASTERISK-29714


- ### funcs: Spelling fixes                                                           
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  effectively
  emitted
  expect
  anthony

  ASTERISK-29714


- ### pbx: Spelling fixes                                                             
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  process
  populate
  with
  africa
  accessing
  contexts
  exercise
  university
  organizations
  withhold
  maintaining
  independent
  rotation
  ignore
  eventname

  ASTERISK-29714


- ### main: Spelling fixes                                                            
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  analysis
  nuisance
  converting
  although
  transaction
  desctitle
  acquire
  update
  evaluate
  thousand
  this
  dissolved
  management
  integrity
  reconstructed
  decrement
  further on
  irrelevant
  currently
  constancy
  anyway
  unconstrained
  featuregroups
  right
  larger
  evaluated
  encumbered
  languages
  digits
  authoritative
  framing
  blindxfer
  tolerate
  traverser
  exclamation
  perform
  permissions
  rearrangement
  performing
  processing
  declension
  happily
  duplicate
  compound
  hundred
  returns
  elicit
  allocate
  actually
  paths
  inheritance
  atxferdropcall
  earlier
  synchronization
  multiplier
  acknowledge
  across
  against
  thousands
  joyous
  manipulators
  guaranteed
  emulating
  soundfile

  ASTERISK-29714


- ### utils: Spelling fixes                                                           
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  command-line
  immediately
  extensions
  momentarily
  mustn't
  numbered
  bytes
  caching

  ASTERISK-29714


- ### Makefile: Spelling fixes                                                        
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  libraries
  install
  overwrite

  ASTERISK-29714


- ### res: Spelling fixes                                                             
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  identifying
  structures
  actcount
  initializer
  attributes
  statement
  enough
  locking
  declaration
  userevent
  provides
  unregister
  session
  execute
  searches
  verification
  suppressed
  prepared
  passwords
  recipients
  event
  because
  brief
  unidentified
  redundancy
  character
  the
  module
  reload
  operation
  backslashes
  accurate
  incorrect
  collision
  initializing
  instance
  interpreted
  buddies
  omitted
  manually
  requires
  queries
  generator
  scheduler
  configuration has
  owner
  resource
  performed
  masquerade
  apparently
  routable

  ASTERISK-29714


- ### rest-api-templates: Spelling fixes                                              
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  overwritten
  descendants

  ASTERISK-29714


- ### agi: Spelling fixes                                                             
  Author: Josh Soref  
  Date:   2021-10-30  

  Correct typos of the following word families:

  pretend
  speech

  ASTERISK-29714


- ### CI: Rename 'master' node to 'built-in'                                          
  Author: George Joseph  
  Date:   2021-11-08  

  Jenkins renamed the 'master' node to 'built-in' in version
  2.319 so we have to adjust as well.


- ### BuildSystem: In POSIX sh, == in place of = is undefined.                        
  Author: Alexander Traud  
  Date:   2021-11-08  

  ASTERISK-29724


- ### pbx.c: Don't remove dashes from hints on reload.                                
  Author: Sean Bright  
  Date:   2021-11-08  

  When reloading dialplan, hints created dynamically would lose any dash
  characters. Now we ignore those dashes if we are dealing with a hint
  during a reload.

  ASTERISK-28040 #close


- ### sig_analog: Fix truncated buffer copy                                           
  Author: Naveen Albert  
  Date:   2021-10-24  

  Fixes compiler warning caused by a truncated copy of the ANI2 into a
  buffer of size 10. This could prevent the null terminator from being
  copied if the copy value exceeds the size of the buffer. This increases
  the buffer size to 101 to ensure there is no way for truncation to occur.

  ASTERISK-29702 #close


- ### app_voicemail: Fix phantom voicemail bug on rerecord                            
  Author: Naveen Albert  
  Date:   2021-10-24  

  If users are able to press # for options while leaving
  a message and then press 3 to rerecord the message, if
  the caller hangs up during the rerecord prompt but before
  Asterisk starts recording a message, then an "empty"
  voicemail gets processed whereby an email gets sent out
  notifying the user of a 0:00 duration message. The file
  doesn't actually exist, so playback will fail since there
  was no message to begin with.

  This adds a check after the streaming of the rerecord
  announcement to see if the caller has hung up. If so,
  we bail out early so that we can clean up properly.

  ASTERISK-29391 #close


- ### chan_iax2: Allow both secret and outkey at dial time                            
  Author: Naveen Albert  
  Date:   2021-10-26  

  Historically, the dial syntax for IAX2 has held that
  an outkey (used only for RSA authenticated calls)
  and a secret (used only for plain text and MD5 authenticated
  calls, historically) were mutually exclusive, and thus
  the same position in the dial string was used for both
  values.

  Now that encryption is possible with RSA authentication,
  this poses a limitation, since encryption requires a
  secret and RSA authentication requires an outkey. Thus,
  the dial syntax is extended so that both a secret and
  an outkey can be specified.

  The new extended syntax is backwards compatible with the
  old syntax. However, a secret can now be specified after
  the outkey, or the outkey can be specified after the secret.
  This makes it possible to spawn an encrypted RSA authenticated
  call without a corresponding peer being predefined in iax.conf.

  ASTERISK-29707 #close


- ### res_snmp: As build tool, prefer pkg-config over net-snmp-config.                
  Author: Alexander Traud  
  Date:   2021-10-28  

  ASTERISK-29709


- ### res_config_sqlite: Remove deprecated module.                                    
  Author: Alexander Traud  
  Date:   2021-11-04  

  ASTERISK-29717


- ### stasis: Avoid 'dispatched' as unused variable in normal mode.                   
  Author: Alexander Traud  
  Date:   2021-10-28  

  ASTERISK-29710


- ### various: Fix GCC 11.2 compilation issues.                                       
  Author: Sean Bright  
  Date:   2021-10-29  

  * Initialize some variables that are never used anyway.

  * Use valid pointers instead of integers cast to void pointers when
    calling pthread_setspecific().

  ASTERISK-29711 #close
  ASTERISK-29713 #close


- ### ast_coredumper:  Refactor to better find things                                 
  Author: George Joseph  
  Date:   2021-09-09  

  The search for a running asterisk when --running is used
  has been greatly simplified and in the event it doesn't
  work, you can now specify a pid to use on the command
  line with --pid.

  The search for asterisk modules when --tarball-coredumps
  is used has been enhanced to have a better chance of finding
  them and in the event it doesn't work, you can now specify
  --libdir on the command line to indicate the library directory
  where they were installed.

  The DATEFORMAT variable was renamed to DATEOPTS and is now
  passed to the 'date' utility rather than running DATEFORMAT
  as a command.

  The coredump and output files are now renamed with DATEOPTS.
  This can be disabled by specifying --no-rename.

  Several confusing and conflicting options were removed:
  --append-coredumps
  --conffile
  --no-default-search
  --tarball-uniqueid

  The script was re-structured to make it easier for follow.


- ### strings/json: Add string delimter match, and object create with vars methods    
  Author: Kevin Harwell  
  Date:   2021-10-21  

  Add a function to check if there is an exact match a one string between
  delimiters in another string.

  Add a function that will create an ast_json object out of a list of
  Asterisk variables. An excludes string can also optionally be passed
  in.

  Also, add a macro to make it easier to get object integers.


- ### STIR/SHAKEN: Option split and response codes.                                   
  Author: Ben Ford  
  Date:   2021-09-21  

  The stir_shaken configuration option now has 4 different choices to pick
  from: off, attest, verify, and on. Off and on behave the same way they
  do now. Attest will only perform attestation on the endpoint, and verify
  will only perform verification on the endpoint.

  Certain responses are required to be sent based on certain conditions
  for STIR/SHAKEN. For example, if we get a Date header that is outside of
  the time range that is considered valid, a 403 Stale Date response
  should be sent. This and several other responses have been added.


- ### app_queue: Add LoginTime field for member in a queue.                           
  Author: Rodrigo Ramírez Norambuena  
  Date:   2021-08-25  

  Add a time_t logintime to storage a time when a member is added into a
  queue.

  Also, includes show this time (in seconds) using a 'queue show' command
  and the field LoginTime for response for AMI events.

  ASTERISK-18069 #close


- ### res_speech: Add a type conversion, and new engine unregister methods            
  Author: Kevin Harwell  
  Date:   2021-10-21  

  Add a new function that converts a speech results type to a string.
  Also add another function to unregister an engine, but returns a
  pointer to the unregistered engine object instead of a success/fail
  integer.


- ### various: Fix GCC 11 compilation issues.                                         
  Author: Mike Bradeen  
  Date:   2021-10-07  

  test_voicemail_api: Use empty char* for empty_msg_ids.
  chan_skinny: Fix size of calledParty to be maximum extension.
  menuselect: Change Makefile to stop deprecated warnings. Added comments
  test_linkedlist: 'bogus' variable was manually allocated from a macro
  and the test fails if this happens but the compiler couldn't 'see' this
  and returns a warning. memset to all 0's after allocation.
  chan_ooh323: Fixed various indentation issues that triggered misleading
   indentation warnings.

  ASTERISK-29682
  Reported by: George Joseph


- ### apps/app_playback.c: Add 'mix' option to app_playback                           
  Author: Shloime Rosenblum  
  Date:   2021-09-20  

  I am adding a mix option that will play by filename and say.conf unlike
  say option that will only play with say.conf. It
  will look on the format of the name, if it is like say it play with
  say.conf if not it will play the file name.

  ASTERISK-29662


- ### BuildSystem: Check for alternate openssl packages                               
  Author: George Joseph  
  Date:   2021-10-19  

  OpenSSL is one of those packages that often have alternatives
  with later versions.  For instance, CentOS/EL 7 has an
  openssl package at version 1.0.2 but there's an openssl11
  package from the epel repository that has 1.1.1.  This gets
  installed to /usr/include/openssl11 and /usr/lib64/openssl11.
  Unfortunately, the existing --with-ssl and --with-crypto
  ./configure options expect to point to a source tree and
  don't work in this situation.  Also unfortunately, the
  checks in ./configure don't use pkg-config.

  In order to make this work with the existing situation, you'd
  have to run...
  ./configure --with-ssl=/usr/lib64/openssl11 \
      --with-crypto=/usr/lib64/openssl11 \
      CFLAGS=-I/usr/include/openssl11

  BUT...  those options don't get passed down to bundled pjproject
  so when you run make, you have to include the CFLAGS again
  which is a big pain.

  Oh...  To make matters worse, although you can specify
  PJPROJECT_CONFIGURE_OPTS on the ./configure command line,
  they don't get saved so if you do a make clean, which will
  force a re-configure of bundled pjproject, those options
  don't get used.

  So...

  * In configure.ac... Since pkg-config is installed by install_prereq
    anyway, we now use it to check for the system openssl >= 1.1.0.
    If that works, great.  If not, we check for the openssl11
    package. If that works, great.  If not, we fall back to just
    checking for any openssl.  If pkg-config isn't installed for some
    reason, or --with-ssl=<dir> or --with-crypto=<dir> were specified
    on the ./configure command line, we fall back to the existing
    logic that uses AST_EXT_LIB_CHECK().

  * The whole OpenSSL check process has been moved up before
    THIRD_PARTY_CONFIGURE(), which does the initial pjproject
    bundled configure, is run.  This way the results of the above
    checks, which may result in new include or library directories,
    is included.

  * Although not strictly needed for openssl, We now save the value of
    PJPROJECT_CONFIGURE_OPTS in the makeopts file so it can be used
    again if a re-configure is triggered.

  ASTERISK-29693


- ### func_talkdetect.c: Fix logical errors in silence detection.                     
  Author: Sean Bright  
  Date:   2021-10-14  

  There are 3 separate changes here:

  1. The documentation erroneously stated that the dsp_talking_threshold
     argument was a number of milliseconds when it is actually an energy
     level used by the DSP code to classify talking vs. silence.

  2. Fixes a copy paste error in the argument handling code.

  3. Don't erroneously switch to the talking state if we aren't actively
     handling a frame we've classified as talking.

  Patch inspired by one provided by Moritz Fain (License #6961).

  ASTERISK-27816 #close


- ### configure: Remove unused OpenSSL SRTP check.                                    
  Author: Sean Bright  
  Date:   2021-10-11  

  Discovered while looking at ASTERISK~29684. Usage was removed in change
  I3c77c7b00b2ffa2e935632097fa057b9fdf480c0.


- ### build: prevent binary downloads for non x86 architectures                       
  Author: Mike Bradeen  
  Date:   2021-10-12  

  download_externals: Add check for i686 and i386 (in addition
  to the current x86_64) and exit if not one of the three.

  ASTERISK-26497


- ### main/stun.c: fix crash upon STUN request timeout                                
  Author: Sebastien Duthil  
  Date:   2021-10-14  

  Some ast_stun_request users do not provide a destination address when
  sending to a connection-mode socket.

  ASTERISK-29691


- ### Makefile: Use basename in a POSIX-compliant way.                                
  Author: Sean Bright  
  Date:   2021-10-07  

  If you aren't using GNU coreutils, chances are that your basename
  doesn't know about the -s argument. Luckily for us, basename does what
  we need it do even without the -s argument.


- ### pbx_ael:  Fix crash and lockup issue regarding 'ael reload'                     
  Author: Mark Murawski  
  Date:   2021-10-05  

  Avoid infinite recursion and crash


- ### chan_iax2: Add encryption for RSA authentication                                
  Author: Naveen Albert  
  Date:   2021-05-24  

  Adds support for encryption to RSA-authenticated
  calls. Also prevents crashes if an RSA IAX2 call
  is initiated to a switch requiring encryption
  but no secret is provided.

  ASTERISK-20219


- ### res_pjsip_t38: bind UDPTL sessions like RTP                                     
  Author: Matthew Kern  
  Date:   2021-07-19  

  In res_pjsip_sdp_rtp, the bind_rtp_to_media_address option and the
  fallback use of the transport's bind address solve problems sending
  media on systems that cannot send ipv4 packets on ipv6 sockets, and
  certain other situations. This change extends both of these behaviors
  to UDPTL sessions as well in res_pjsip_t38, to fix fax-specific
  problems on these systems, introducing a new option
  endpoint/t38_bind_udptl_to_media_address.

  ASTERISK-29402


- ### app_read: Fix null pointer crash                                                
  Author: Naveen Albert  
  Date:   2021-09-29  

  If the terminator character is not explicitly specified
  and an indications tone is used for reading a digit,
  there is no null pointer check so Asterisk crashes.
  This prevents null usage from occuring.

  ASTERISK-29673 #close


- ### res_rtp_asterisk: fix memory leak                                               
  Author: Jean Aunis  
  Date:   2021-09-29  

  Add missing reference decrement in rtp_deallocate_transport()

  ASTERISK-29671


- ### main/say.c: Support future dates with Q and q format params                     
  Author: Shloime Rosenblum  
  Date:   2021-09-19  

  The current versions do not support future dates in all say application when using the 'Q' or 'q' format parameter and says "today" for everything that is greater than today

  ASTERISK-29637


- ### res_pjsip_registrar: Remove unavailable contacts if exceeds max_contacts        
  Author: Joseph Nadiv  
  Date:   2021-07-21  

  The behavior of max_contacts and remove_existing are connected.  If
  remove_existing is enabled, the soonest expiring contacts are removed.
  This may occur when there is an unavailable contact.  Similarly,
  when remove_existing is not enabled, registrations from good
  endpoints are rejected in favor of retaining unavailable contacts.

  This commit adds a new AOR option remove_unavailable, and the effect
  of this setting will depend on remove_existing.  If remove_existing
  is set to no, we will still remove unavailable contacts when they
  exceed max_contacts, if there are any. If remove_existing is set to
  yes, we will prioritize the removal of unavailable contacts before
  those that are expiring soonest.

  ASTERISK-29525


- ### ari: Ignore invisible bridges when listing bridges.                             
  Author: Joshua C. Colp  
  Date:   2021-09-23  

  When listing bridges we go through the ones present in
  ARI, get their snapshot, turn it into JSON, and add it
  to the payload we ultimately return.

  An invisible "dial bridge" exists within ARI that would
  also try to be added to this payload if the channel
  "create" and "dial" routes were used. This would ultimately
  fail due to invisible bridges having no snapshot
  resulting in the listing of bridges failing.

  This change makes it so that the listing of bridges
  ignores invisible ones.

  ASTERISK-29668


- ### func_vmcount: Add support for multiple mailboxes                                
  Author: Naveen Albert  
  Date:   2021-09-19  

  Allows multiple mailboxes to be specified for VMCOUNT
  instead of just one.

  ASTERISK-29661 #close


- ### message.c: Support 'To' header override with AMI's MessageSend.                 
  Author: Sean Bright  
  Date:   2021-09-21  

  The MessageSend AMI action has been updated to allow the Destination
  and the To addresses to be provided separately. This brings the
  MessageSend manager command in line with the capabilities of the
  MessageSend dialplan application.

  ASTERISK-29663 #close


- ### func_channel: Add CHANNEL_EXISTS function.                                      
  Author: Naveen Albert  
  Date:   2021-09-15  

  Adds a function to check for the existence of a channel by
  name or by UNIQUEID.

  ASTERISK-29656 #close


- ### app_queue: Fix hint updates for included contexts                               
  Author: Naveen Albert  
  Date:   2021-09-05  

  Previously, if custom hints were used with the hint:
  format in app_queue, when device state changes occured,
  app_queue would only do a literal string comparison of
  the context used for the hint in app_queue and the context
  of the hint which just changed state. This caused hints
  to not update and become stale if the context associated
  with the agent included the context which actually changes
  state, essentially completely breaking device state for
  any such agents defined in this manner.

  This fix adds an additional check to ensure that included
  contexts are also compared against the context which changed
  state, so that the behavior is correct no matter whether the
  context is specified to app_queue directly or indirectly.

  ASTERISK-29578 #close


- ### res_http_media_cache.c: Compare unaltered MIME types.                           
  Author: Sean Bright  
  Date:   2021-09-10  

  Rather than stripping parameters from Content-Type headers before
  comparison, first try to compare the whole string. If no match is
  found, strip the parameters and try that way.

  ASTERISK-29275 #close


- ### logger: Add custom logging capabilities                                         
  Author: Naveen Albert  
  Date:   2021-07-25  

  Adds the ability for users to log to custom log levels
  by providing custom log level names in logger.conf. Also
  adds a logger show levels CLI command.

  ASTERISK-29529


- ### app_externalivr.c: Fix mixed leading whitespace in source code.                 
  Author: Sean Bright  
  Date:   2021-09-17  

  No functional changes.


- ### res_rtp_asterisk.c: Fix build failure when not building with pjproject.         
  Author: Guido Falsi  
  Date:   2021-09-17  

  Some code has been added referencing symbols defined in a block
  protected by #ifdef HAVE_PJPROJECT. Protect those code parts in
  ifdef blocks too.

  ASTERISK-29660


- ### pjproject: Add patch to fix trailing whitespace issue in rtpmap                 
  Author: George Joseph  
  Date:   2021-09-14  

  An issue was found where a particular manufacturer's phones add a
  trailing space to the end of the rtpmap attribute when specifying
  a payload type that has a "param" after the format name and clock
  rate. For example:

  a=rtpmap:120 opus/48000/2 \r\n

  Because pjmedia_sdp_attr_get_rtpmap currently takes everything after
  the second '/' up to the line end as the param, the space is
  included in future comparisons, which then fail if the param being
  compared to doesn't also have the space.

  We now use pj_scan_get() to parse the param part of rtpmap so
  trailing whitespace is automatically stripped.

  ASTERISK-29654


- ### app_mp3: Force output to 16 bits in mpg123                                      
  Author: Carlos Oliva  
  Date:   2021-09-13  

  In new mpg123 versions (since 1.26) the default output is 32 bits
  Asterisk expects the output in 16 bits, so we force the output to be on 16 bits.
  It will work wit new and old versions of mpg123.
  Thanks Thomas Orgis <thomas-forum@orgis.org> for giving the key!

  ASTERISK-29635 #close


- ### res_pjsip_caller_id: Add ANI2/OLI parsing                                       
  Author: Naveen Albert  
  Date:   2021-06-08  

  Adds parsing of ANI II digits (Originating
  Line Information) to PJSIP, on par with
  what currently exists in chan_sip.

  ASTERISK-29472


- ### app_mf: Add channel agnostic MF sender                                          
  Author: Naveen Albert  
  Date:   2021-06-28  

  Adds a SendMF application and PlayMF manager
  event to send arbitrary R1 MF tones on the
  current or specified channel.

  ASTERISK-29496


- ### app_stack: Include current location if branch fails                             
  Author: Naveen Albert  
  Date:   2021-09-02  

  Previously, the error emitted when app_stack tries
  to branch to a dialplan location that doesn't exist
  has included only the information about the attempted
  branch in the error log. This adds the current location
  as well so users can see where the branch failed in
  the logs.

  ASTERISK-29626


- ### test_http_media_cache.c: Fix copy/paste error during test deregistration.       
  Author: Sean Bright  
  Date:   2021-09-10  


- ### resource_channels.c: Fix external media data option                             
  Author: Sungtae Kim  
  Date:   2021-09-04  

  Fixed the external media creation handle to handle the 'data' option correctly.

  ASTERISK-29629


- ### func_strings: Add STRBETWEEN function                                           
  Author: Naveen Albert  
  Date:   2021-09-02  

  Adds the STRBETWEEN function, which can be used to insert a
  substring between each character in a string. For instance,
  this can be used to insert pauses between DTMF tones in a
  string of digits.

  ASTERISK-29627


- ### test_abstract_jb.c: Fix put and put_out_of_order memory leaks.                  
  Author: Sean Bright  
  Date:   2021-09-08  

  We can't rely on RAII_VAR(...) to properly clean up data that is
  allocated within a loop.

  ASTERISK-27176 #close


- ### func_env: Add DIRNAME and BASENAME functions                                    
  Author: Naveen Albert  
  Date:   2021-09-03  

  Adds the DIRNAME and BASENAME functions, which are
  wrappers around the corresponding C library functions.
  These can be used to safely and conveniently work with
  file paths and names in the dialplan.

  ASTERISK-29628 #close


- ### func_sayfiles: Retrieve say file names                                          
  Author: Naveen Albert  
  Date:   2021-07-26  

  Up until now, all of the logic used to translate
  arguments to the Say applications has been
  directly coupled to playback, preventing other
  modules from using this logic.

  This refactors code in say.c and adds a SAYFILES
  function that can be used to retrieve the file
  names that would be played. These can then be
  used in other applications or for other purposes.

  Additionally, a SayMoney application and a SayOrdinal
  application are added. Both SayOrdinal and SayNumber
  are also expanded to support integers greater than
  one billion.

  ASTERISK-29531


- ### res_tonedetect: Tone detection module                                           
  Author: Naveen Albert  
  Date:   2021-08-09  

  dsp.c contains arbitrary tone detection functionality
  which is currently only used for fax tone recognition.
  This change makes this functionality publicly
  accessible so that other modules can take advantage
  of this.

  Additionally, a WaitForTone and TONE_DETECT app and
  function are included to allow users to do their
  own tone detection operations in the dialplan.

  ASTERISK-29546


- ### res_snmp: Add -fPIC to _ASTCFLAGS                                               
  Author: George Joseph  
  Date:   2021-09-08  

  With gcc 11, res/res_snmp.c and res/snmp/agent.c need the
  -fPIC option added to its _ASTCFLAGS.

  ASTERISK-29634


- ### app_voicemail.c: Ability to silence instructions if greeting is present.        
  Author: Sean Bright  
  Date:   2021-09-07  

  There is an option to silence voicemail instructions but it does not
  take into consideration if a recorded greeting exists or not. Add a
  new 'S' option that does that.

  ASTERISK-29632 #close


- ### term.c: Add support for extended number format terminfo files.                  
  Author: Sean Bright  
  Date:   2021-09-04  

  ncurses 6.1 introduced an extended number format for terminfo files
  which the terminfo parsing in Asterisk is not able to parse. This
  results in some TERM values that do support color (screen-256color on
  Ubuntu 20.04 for example) to not get a color console.

  ASTERISK-29630 #close


- ### res_srtp: Disable parsing of not enabled cryptos                                
  Author: Jasper Hafkenscheid  
  Date:   2021-09-03  

  When compiled without extended srtp crypto suites also disable parsing
  these from received SDP. This prevents using these, as some client
  implementations are not stable.

  ASTERISK-29625


- ### dns.c: Load IPv6 DNS resolvers if configured.                                   
  Author: Sean Bright  
  Date:   2021-09-06  

  IPv6 nameserver addresses are stored in different part of the
  __res_state structure, so look there if we appear to have support for
  it.

  ASTERISK-28004 #close


- ### bridge_softmix: Suppress error on topology change failure                       
  Author: George Joseph  
  Date:   2021-09-08  

  There are conditions under which a failure to change topology
  is expected so there's no need to print an ERROR message.

  ASTERISK-29618
  Reported by: Alexander


- ### resource_channels.c: Fix wrong external media parameter parse                   
  Author: sungtae kim  
  Date:   2021-08-31  

  Fixed ARI external media handler to accept body parameters.

  ASTERISK-29622


- ### config_options: Handle ACO arrays correctly in generated XML docs.              
  Author: Sean Bright  
  Date:   2021-08-25  

  There are 3 separate changes here but they are all closely related:

  * Only try to set matchfield attributes on 'field' nodes

  * We need to adjust how we treat the category pointer based on the
    value of the category_match, to avoid memory corruption. We now
    generate a regex-like string when match types other than
    ACO_WHITELIST and ACO_BLACKLIST are used.

  * Switch app_agent_pool from ACO_BLACKLIST_ARRAY to
    ACO_BLACKLIST_EXACT since we only have one category we need to
    ignore, not two.

  ASTERISK-29614 #close


- ### chan_iax2: Add ANI2/OLI information element                                     
  Author: Naveen Albert  
  Date:   2021-08-18  

  Adds an information element for ANI2 so that
  Originating Line Information can be transmitted
  over IAX2 channels.

  ASTERISK-29605 #close


- ### pbx_ael:  Fix crash and lockup issue regarding 'ael reload'                     
  Author: Mark Murawski  
  Date:   2021-08-31  

  Currently pbx_ael does not check if a reload is currently pending
  before proceeding with a reload. This can cause multiple threads to
  operate at the same time on what should be mutex protected data. This
  change adds protection to reloading to ensure only one ael reload is
  executing at a time.

  ASTERISK-29609 #close


- ### app_read: Allow reading # as a digit                                            
  Author: Naveen Albert  
  Date:   2021-08-25  

  Allows for the digit # to be read as a digit,
  just like any other DTMF digit, as opposed to
  forcing it to be used as an end of input
  indicator. The default behavior remains
  unchanged.

  ASTERISK-18454 #close


- ### res_rtp_asterisk: Automatically refresh stunaddr from DNS                       
  Author: Sebastien Duthil  
  Date:   2021-04-05  

  This allows the STUN server to change its IP address without having to
  reload the res_rtp_asterisk module.

  The refresh of the name resolution occurs first when the module is
  loaded, then recurringly, slightly after the previous DNS answer TTL
  expires.

  ASTERISK-29508 #close


- ### bridge_basic: Change warning to verbose if transfer cancelled                   
  Author: Naveen Albert  
  Date:   2021-08-25  

  The attended transfer feature will emit a warning if the user
  cancels the transfer or the attended transfer doesn't complete
  for any reason. Changes the warning to a verbose message,
  since nothing is actually wrong here.

  ASTERISK-29612 #close


- ### app_queue: Don't reset queue stats on reload                                    
  Author: Naveen Albert  
  Date:   2021-08-20  

  Prevents reloads of app_queue from also resetting
  queue statistics.

  Also preserves individual queue agent statistics
  if we're just reloading members.

  ASTERISK-28701


- ### res_rtp_asterisk: sqrt(.) requires the header math.h.                           
  Author: Alexander Traud  
  Date:   2021-08-25  

  ASTERISK-29616


- ### dialplan: Add one static and fix two whitespace errors.                         
  Author: Alexander Traud  
  Date:   2021-08-25  


- ### sig_analog: Changes to improve electromechanical signalling compatibility       
  Author: Sarah Autumn  
  Date:   2021-06-19  

  This changeset is intended to address compatibility issues encountered
  when interfacing Asterisk to electromechanical telephone switches that
  implement ANI-B, ANI-C, or ANI-D.

  In particular the behaviours that this impacts include:

   - FGC-CAMA did not work at all when using MF signaling. Modified the
     switch case block to send calls to the correct part of the
     signaling-handling state machine.

   - For FGC-CAMA operation, the delay between called number ST and
     second wink for ANI spill has been made configurable; previously
     all calls were made to wait for one full second.

   - After the ANI spill, previous behavior was to require a 'ST' tone
     to advance the call.  This has been changed to allow 'STP' 'ST2P'
     or 'ST3P' as well, for compatibility with ANI-D.

   - Store ANI2 (ANI INFO) digits in the CALLERID(ANI2) channel variable.

   - For calls with an ANI failure, No. 1 Crossbar switches will send
     forward a single-digit failure code, with no calling number digits
     and no ST pulse to terminate the spill.  I've made the ANI timeout
     configurable so to reduce dead air time on calls with ANI fail.

   - ANI info digits configurable.  Modern digital switches will send 2
     digits, but ANI-B sends only a single info digit.  This caused the
     ANI reported by Asterisk to be misaligned.

   - Changed a confusing log message to be more informative.

  ASTERISK-29518


- ### media_cache: Don't lock when curl the remote file                               
  Author: Andre Barbosa  
  Date:   2021-08-05  

  When playing a remote sound file, which is not in cache, first we need
  to download it with ast_bucket_file_retrieve.

  This can take a while if the remote host is slow. The current CURL
  timeout is 180secs, so in extreme situations, it can take 3 minutes to
  return.

  Because ast_media_cache_retrieve has a lock on all function, while we
  are waiting for the delayed download, Asterisk is not able to play any
  more files, even the files already cached locally.

  ASTERISK-29544 #close


- ### res_pjproject: Allow mapping to Asterisk TRACE level                            
  Author: George Joseph  
  Date:   2021-08-16  

  Allow mapping pjproject log messages to the Asterisk TRACE
  log level.  The defaults were also changes to log pjproject
  levels 3,4 to DEBUG and 5,6 to TRACE.  Previously 3,4,5,6
  all went to DEBUG.

  ASTERISK-29582


- ### app_milliwatt: Timing fix                                                       
  Author: Naveen Albert  
  Date:   2021-08-12  

  The Milliwatt application uses incorrect tone timings
  that cause it to play the 1004 Hz tone constantly.

  This adds an option to enable the correct timing
  behavior, so that the Milliwatt application can
  be used for milliwatt test lines. The default behavior
  remains unchanged for compatability reasons, even
  though it is incorrect.

  ASTERISK-29575 #close


- ### func_math: Return integer instead of float if possible                          
  Author: Naveen Albert  
  Date:   2021-06-28  

  The MIN, MAX, and ABS functions all support float
  arguments, but currently return floats even if the
  arguments are all integers and the response is
  a whole number, in which case the user is likely
  expecting an integer. This casts the float to an integer
  before printing into the response buffer if possible.

  ASTERISK-29495


- ### app_morsecode: Add American Morse code                                          
  Author: Naveen Albert  
  Date:   2021-08-04  

  Previously, the Morsecode application only supported international
  Morse code. This adds support for American Morse code and adds an
  option to configure the frequency used in off intervals.

  Additionally, the application checks for hangup between tones
  to prevent application execution from continuing after hangup.

  ASTERISK-29541


- ### func_scramble: Audio scrambler function                                         
  Author: Naveen Albert  
  Date:   2021-08-04  

  Adds a function to scramble audio on a channel using
  whole spectrum frequency inversion. This can be used
  as a privacy enhancement with applications like
  ChanSpy or other potentially sensitive audio.

  ASTERISK-29542


- ### app_originate: Add ability to set codecs                                        
  Author: Naveen Albert  
  Date:   2021-08-05  

  A list of codecs to use for dialplan-originated calls can
  now be specified in Originate, similar to the ability
  in call files and the manager action.

  Additionally, we now default to just using the slin codec
  for originated calls, rather than all the slin* codecs up
  through slin192, which has been known to cause issues
  and inconsistencies from AMI and call file behavior.

  ASTERISK-29543


- ### BuildSystem: Remove two dead exceptions for compiler Clang.                     
  Author: Alexander Traud  
  Date:   2021-08-16  

  Commit 305ce3d added -Wno-parentheses-equality to Makefile.rules,
  turning the previous two warning suppressions from commit e9520db
  redundant. Let us remove the latter.


- ### chan_alsa, chan_sip: Add replacement to moduleinfo                              
  Author: Naveen Albert  
  Date:   2021-08-16  

  Adds replacement modules to the moduleinfo for
  chan_alsa and chan_sip.

  ASTERISK-29601 #close


- ### res_monitor: Disable building by default.                                       
  Author: Joshua C. Colp  
  Date:   2021-08-17  

  ASTERISK-29602


- ### muted: Remove deprecated application.                                           
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29600


- ### conf2ael: Remove deprecated application.                                        
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29599


- ### res_config_sqlite: Remove deprecated module.                                    
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29598


- ### chan_vpb: Remove deprecated module.                                             
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29597


- ### chan_misdn: Remove deprecated module.                                           
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29596


- ### chan_nbs: Remove deprecated module.                                             
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29595


- ### chan_phone: Remove deprecated module.                                           
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29594


- ### chan_oss: Remove deprecated module.                                             
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29593


- ### cdr_syslog: Remove deprecated module.                                           
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29592


- ### app_dahdiras: Remove deprecated module.                                         
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29591


- ### app_nbscat: Remove deprecated module.                                           
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29590


- ### app_image: Remove deprecated module.                                            
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29589


- ### app_url: Remove deprecated module.                                              
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29588


- ### app_fax: Remove deprecated module.                                              
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29587


- ### app_ices: Remove deprecated module.                                             
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29586


- ### app_mysql: Remove deprecated module.                                            
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29585


- ### cdr_mysql: Remove deprecated module.                                            
  Author: Joshua C. Colp  
  Date:   2021-08-16  

  ASTERISK-29584


- ### mgcp: Remove dead debug code                                                    
  Author: Sean Bright  
  Date:   2021-08-10  

  ASTERISK-20339 #close


- ### policy: Deprecate modules and add versions to others.                           
  Author: Joshua C. Colp  
  Date:   2021-08-11  

  app_meetme is deprecated in 19, to be removed in 21.
  app_osplookup is deprecated in 19, to be removed in 21.
  chan_alsa is deprecated in 19, to be removed in 21.
  chan_mgcp is deprecated in 19, to be removed in 21.
  chan_skinny is deprecated in 19, to be removed in 21.
  res_pktccops is deprecated in 19, to be removed in 21.
  app_macro was deprecated in 16, to be removed in 21.
  chan_sip was deprecated in 17, to be removed in 21.
  res_monitor was deprecated in 16, to be removed in 21.

  ASTERISK-29548
  ASTERISK-29549
  ASTERISK-29550
  ASTERISK-29551
  ASTERISK-29552
  ASTERISK-29553
  ASTERISK-29558
  ASTERISK-29567
  ASTERISK-29572


- ### func_frame_drop: New function                                                   
  Author: Naveen Albert  
  Date:   2021-06-16  

  Adds function to selectively drop specified frames
  in the TX or RX direction on a channel, including
  control frames.

  ASTERISK-29478


- ### aelparse: Accept an included context with timings.                              
  Author: Alexander Traud  
  Date:   2021-08-02  

  With Asterisk 1.6.0, in the main parser for the configuration file
  extensions.conf, the separator was changed from vertical bar to comma.
  However, the first separator was not changed in aelparse; it still had
  to be a vertical bar, and no comma was allowed.

  Additionally, this change allows the vertical bar for the first and
  last parameter again, even in the main parser, because the vertical bar
  was still accepted for the other parameters.

  ASTERISK-29540


- ### format_ogg_speex: Implement a "not supported" write handler                     
  Author: Kevin Harwell  
  Date:   2021-08-03  

  This format did not specify a "write" handler, so when attempting to write
  to it (ast_writestream) a crash would occur.

  This patch adds a default handler that simply issues a "not supported"
  warning, thus no longer crashing.

  ASTERISK-29539


- ### cdr_adaptive_odbc: Prevent filter warnings                                      
  Author: Naveen Albert  
  Date:   2021-06-28  

  Previously, if CDR filters were used so that
  not all CDR records used all sections defined
  in cdr_adaptive_odbc.conf, then warnings will
  always be emitted (if each CDR record is unique
  to a particular section, n-1 warnings to be
  specific).

  This turns the offending warning log into
  a verbose message like the other one, since
  this behavior is intentional and not
  indicative of anything wrong.

  ASTERISK-29494


- ### app_queue: Allow streaming multiple announcement files                          
  Author: Naveen Albert  
  Date:   2021-07-25  

  Allows multiple files comprising an agent announcement
  to be played by separating on the ampersand, similar
  to the multi-file support in other Asterisk applications.

  ASTERISK-29528


- ### res_pjsip_header_funcs: Add PJSIP_HEADERS() ability to read header by pattern   
  Author: Igor Goncharovsky  
  Date:   2021-04-13  

  PJSIP currently does not provide a function to replace SIP_HEADERS() function to get a list of headers from INVITE request.
  It may be used to get all X- headers in case the actual set and names of headers unknown.

  ASTERISK-29389


- ### res_statsd: handle non-standard meter type safely                               
  Author: Rijnhard Hessel  
  Date:   2021-07-08  

  Meter types are not well supported,
  lacking support in telegraf, datadog and the official statsd servers.
  We deprecate meters and provide a compliant fallback for any existing usages.

  A flag has been introduced to allow meters to fallback to counters.


  ASTERISK-29513


- ### app_dtmfstore: New application to store digits                                  
  Author: Naveen Albert  
  Date:   2021-06-16  

  Adds application to asynchronously collect digits
  dialed on a channel in the TX or RX direction
  using a framehook and stores them in a specified
  variable, up to a configurable number of digits.

  ASTERISK-29477


- ### codec_builtin.c: G729 audio gets corrupted by Asterisk due to smoother          
  Author: under  
  Date:   2021-07-22  

  If Asterisk gets G.729 6-byte VAD frames inbound, then at outbound Asterisk sends this G.729 stream with non-continuous timestamps.
  This makes the audio stream not-playable at the receiver side.
  Linphone isn't able to play such an audio - lots of disruptions are heard.
  Also I had complains of bad audio from users which use other types of phones.

  After debugging, I found this is a regression connected with RTP Smoother (main/smoother.c).

  Smoother has a special code to handle G.729 VAD frames (search for AST_SMOOTHER_FLAG_G729 in smoother.c).

  However, this flag is never set in Asterisk-12 and newer.
  Previously it has been set (see Asterisk-11).

  ASTERISK-29526 #close


- ### res_http_media_cache: Cleanup audio format lookup in HTTP requests              
  Author: Sean Bright  
  Date:   2021-07-23  

  Asterisk first looks at the end of the URL to determine the file
  extension of the returned audio, which in many cases will not work
  because the URL may end with a query string or a URL fragment. If that
  fails, Asterisk then looks at the Content-Type header and then finally
  parses the URL to get the extension.

  The order has been changed such that we look at the Content-Type
  header first, followed by looking for the extension of the parsed
  URL. We no longer look at the end of the URL, which was error prone.

  ASTERISK-29527 #close


- ### docs: Remove embedded macro in WaitForCond XML documentation.                   
  Author: Joshua C. Colp  
  Date:   2021-07-27  


- ### Update AMI and ARI versions for Asterisk 20.                                    
  Author: Ben Ford  
  Date:   2021-07-21  

  Bumped AMI and ARI versions for the next major Asterisk version (20).


- ### AST-2021-009 - pjproject-bundled: Avoid crash during handshake for TLS          
  Author: Kevin Harwell  
  Date:   2021-06-14  

  If an SSL socket parent/listener was destroyed during the handshake,
  depending on timing, it was possible for the handling callback to
  attempt access of it after the fact thus causing a crash.

  ASTERISK-29415 #close


- ### AST-2021-008 - chan_iax2: remote crash on unsupported media format              
  Author: Kevin Harwell  
  Date:   2021-05-10  

  If chan_iax2 received a packet with an unsupported media format, for
  example vp9, then it would set the frame's format to NULL. This could
  then result in a crash later when an attempt was made to access the
  format.

  This patch makes it so chan_iax2 now ignores/drops frames received
  with unsupported media format types.

  ASTERISK-29392 #close


- ### AST-2021-007 - res_pjsip_session: Don't offer if no channel exists.             
  Author: Joshua C. Colp  
  Date:   2021-04-28  

  If a re-INVITE is received after we have sent a BYE request then it
  is possible for no channel to be present on the session. If this
  occurs we allow PJSIP to produce the offer instead. Since the call
  is being hung up if it produces an incorrect offer it doesn't
  actually matter. This also ensures that code which produces SDP
  does not need to handle if a channel is not present.

  ASTERISK-29381


- ### res_stasis_playback: Check for chan hangup on play_on_channels                  
  Author: Andre Barbosa  
  Date:   2021-06-29  

  Verify `ast_check_hangup` before looping to the next sound file.
  If the call is already hangup we just break the cycle.
  It also ensures that the PlaybackFinished event is sent if the call was hangup.

  This is also use-full when we are playing a big list of file for a channel that is hangup.
  Before this patch Asterisk will give a warning for every sound not played and fire a PlaybackStart for every sound file on the list tried to be played.

  With the patch we just break the playback cycle when the chan is hangup.

  ASTERISK-29501 #close


- ### res_http_media_cache.c: Fix merge errors from 18 -> master                      
  Author: Sean Bright  
  Date:   2021-07-02  

  ASTERISK-27871 #close


- ### res_pjsip_stir_shaken: RFC 8225 compliance and error message cleanup.           
  Author: Sean Bright  
  Date:   2021-07-15  

  From RFC 8225 Section 5.2.1:

      The "dest" claim is a JSON object with the claim name of "dest"
      and MUST have at least one identity claim object.  The "dest"
      claim value is an array containing one or more identity claim JSON
      objects representing the destination identities of any type
      (currently "tn" or "uri").  If the "dest" claim value array
      contains both "tn" and "uri" claim names, the JSON object should
      list the "tn" array first and the "uri" array second.  Within the
      "tn" and "uri" arrays, the identity strings should be put in
      lexicographical order, including the scheme-specific portion of
      the URI characters.

  Additionally, make it clear that there was a failure to sign the JWT
  payload and not necessarily a memory allocation failure.


- ### res_http_media_cache.c: Parse media URLs to find extensions.                    
  Author: Sean Bright  
  Date:   2021-07-02  

  Use cURL's URL parsing API, falling back to the urlparser library, to
  parse playback URLs in order to find their file extensions.

  For backwards compatibility, we first look at the full URL, then at
  any Content-Type header, and finally at just the path portion of the
  URL.

  ASTERISK-27871 #close


- ### main/cdr.c: Correct Party A selection.                                          
  Author: Sean Bright  
  Date:   2021-07-13  

  This appears to just have been a copy/paste error from 6258bbe7. Fix
  suggested by Ross Beer in ASTERISK~29166.


- ### stun: Emit warning message when STUN request times out                          
  Author: Sebastien Duthil  
  Date:   2021-06-30  

  Without this message, it is not obvious that the reason is STUN timeout.

  ASTERISK-29507 #close


- ### app_reload: New Reload application                                              
  Author: Naveen Albert  
  Date:   2021-05-26  

  Adds an application to reload modules
  from within the dialplan.

  ASTERISK-29454


- ### res_ari: Fix audiosocket segfault                                               
  Author: Igor Goncharovsky  
  Date:   2021-07-08  

  Add check that data parameter specified when audiosocket used for externalMedia.

  ASTERISK-29514 #close


- ### res_pjsip_config_wizard.c: Add port matching support.                           
  Author: Sean Bright  
  Date:   2021-06-30  

  In f8b0c2c9 we added support for port numbers in 'match' statements
  but neglected to include that support in the PJSIP config wizard.

  The removed code would have also prevented IPv6 addresses from being
  successfully used in the config wizard as well.

  ASTERISK-29503 #close


- ### app_waitforcond: New application                                                
  Author: Naveen Albert  
  Date:   2021-05-22  

  While several applications exist to wait for
  a certain event to occur, none allow waiting
  for any generic expression to become true.
  This application allows for waiting for a condition
  to become true, with configurable timeout and
  checking interval.

  ASTERISK-29444


- ### res_stasis_playback: Send PlaybackFinish event only once for errors             
  Author: Andre Barbosa  
  Date:   2021-06-04  

  When we try to play a list of sound files in the same Play command,
  we get only one PlaybackFinish event, after all sounds are played.

  But in the case where the Play fails (because channel is destroyed
  for example), Asterisk will send one PlaybackFinish event for each
  sound file still to be played. If the list is big, Asterisk is
  sending many events.

  This patch adds a failed state so we can understand that the play
  failed. On that case we don't send the event, if we still have a
  list of sounds to be played.

  When we reach the last sound, we send the PlaybackFinish with
  the failed state.

  ASTERISK-29464 #close


- ### jitterbuffer:  Correct signed/unsigned mismatch causing assert                  
  Author: George Joseph  
  Date:   2021-06-17  

  If the system time has stepped backwards because of a time
  adjustment between the time a frame is timestamped and the
  time we check the timestamps in abstract_jb:hook_event_cb(),
  we get a negative interval, but we don't check for that there.
  abstract_jb:hook_event_cb() then calls
  fixedjitterbuffer:fixed_jb_get() (via abstract_jb:jb_get_fixed)
  and the first thing that does is assert(interval >= 0).

  There are several issues with this...

   * abstract_jb:hook_event_cb() saves the interval in a variable
     named "now" which is confusing in itself.

   * "now" is defined as an unsigned int which converts the negative
     value returned from ast_tvdiff_ms() to a large positive value.

   * fixed_jb_get()'s parameter is defined as a signed int so the
     interval gets converted back to a negative value.

   * fixed_jb_get()'s assert is NOT an ast_assert but a direct define
     that points to the system assert() so it triggers even in
     production mode.

  So...

   * hook_event_cb()'s "now" was renamed to "relative_frame_start" and
     changed to an int64_t.
   * hook_event_cb() now checks for a negative value right after
     retrieving both the current and framedata timestamps and just
     returns the frame if the difference is negative.
   * fixed_jb_get()'s local define of ASSERT() was changed to call
     ast_assert() instead of the system assert().

  ASTERISK-29480
  Reported by: Dan Cropp


- ### app_dial: Expanded A option to add caller announcement                          
  Author: Naveen Albert  
  Date:   2021-05-21  

  Hitherto, the A option has made it possible to play
  audio upon answer to the called party only. This option
  is expanded to allow for playback of an audio file to
  the caller instead of or in addition to the audio
  played to the answerer.

  ASTERISK-29442


- ### core: Don't play silence for Busy() and Congestion() applications.              
  Author: Joshua C. Colp  
  Date:   2021-06-21  

  When using the Busy() and Congestion() applications the
  function ast_safe_sleep is used by wait_for_hangup to safely
  wait on the channel. This function may send silence if Asterisk
  is configured to do so using the transmit_silence option.

  In a scenario where an answered channel dials a Local channel
  either directly or through call forwarding and the Busy()
  or Congestion() dialplan applications were executed with the
  transmit_silence option enabled the busy or congestion
  tone would not be heard.

  This is because inband generation of tones (such as busy
  and congestion) is stopped when other audio is sent to
  the channel they are being played to. In the given
  scenario the transmit_silence option would result in
  silence being sent to the channel, thus stopping the
  inband generation.

  This change adds a variant of ast_safe_sleep which can be
  used when silence should not be played to the channel. The
  wait_for_hangup function has been updated to use this
  resulting in the tones being generated as expected.

  ASTERISK-29485


- ### res_pjsip_sdp_rtp: Evaluate remotely held for Session Progress                  
  Author: Bernd Zobl  
  Date:   2021-05-07  

  With the fix for ASTERISK_28754 channels are no longer put on hold if an
  outbound INVITE is answered with a "Session Progress" containing
  "inactive" audio.

  The previous change moved the evaluation of the media attributes to
  `negotiate_incoming_sdp_stream()` to have the `remotely_held` status
  available when building the SDP in `create_outgoing_sdp_stream()`.
  This however means that an answer to an outbound INVITE, which does not
  traverse `negotiate_incoming_sdp_stream()`, cannot set the
  `remotely_held` status anymore.

  This change moves the check so that both, `negotiate_incoming_sdp_stream()` and
  `apply_negotiated_sdp_stream()` can do the checks.

  ASTERISK-29479


- ### res_pjsip_messaging: Overwrite user in existing contact URI                     
  Author: George Joseph  
  Date:   2021-06-16  

  When the MessageSend destination is in the form
  PJSIP/<number>@<endpoint> and the endpoint's contact
  URI already has a user component, that user component
  will now be replaced with <number> when creating the
  request URI.

  ASTERISK_29404


- ### res_pjsip/pjsip_message_filter: set preferred transport in pjsip_message_filter 
  Author: Bernd Zobl  
  Date:   2021-03-16  

  Set preferred transport when querying the local address to use in
  filter_on_tx_messages(). This prevents the module to erroneously select
  the wrong transport if more than one transports of the same type (TCP or
  TLS) are configured.

  ASTERISK-29241


- ### pbx_builtins: Corrects SayNumber warning                                        
  Author: Naveen Albert  
  Date:   2021-06-10  

  Previously, SayNumber always emitted a warning if the caller hung up
  during execution. Usually this isn't correct, so check if the channel
  hung up and, if so, don't emit a warning.

  ASTERISK-29475


- ### func_lock: Add "dialplan locks show" cli command.                               
  Author: Jaco Kroon  
  Date:   2021-05-22  

  For example:

  arthur*CLI> dialplan locks show
  func_lock locks:
  Name                                     Requesters Owner
  uls-autoref                              0          (unlocked)
  1 total locks listed.

  Obviously other potentially useful stats could be added (eg, how many
  times there was contention, how many times it failed etc ... but that
  would require keeping the stats and I'm not convinced that's worth the
  effort.  This was useful to troubleshoot some other issues so submitting
  it.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### func_lock: Prevent module unloading in-use module.                              
  Author: Jaco Kroon  
  Date:   2021-05-22  

  The scenario where a channel still has an associated datastore we
  cannot unload since there is a function pointer to the destroy and fixup
  functions in play.  Thus increase the module ref count whenever we
  allocate a datastore, and decrease it during destroy.

  In order to tighten the race that still exists in spite of this (below)
  add some extra failure cases to prevent allocations in these cases.

  Race:

  If module ref is zero, an LOCK or TRYLOCK is invoked (near)
  simultaneously on a channel that has NOT PREVIOUSLY taken a lock, and if
  in such a case the datastore is created *prior* to unloading being set
  to true (first step in module unload) then it's possible that the module
  will unload with the destructor being called (and segfault) post the
  module being unloaded.  The module will however wait for such locks to
  release prior to unloading.

  If post that we can recheck the module ref before returning the we can
  (in theory, I think) eliminate the last of the race.  This race is
  mostly theoretical in nature.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### func_lock: Fix memory corruption during unload.                                 
  Author: Jaco Kroon  
  Date:   2021-05-22  

  AST_TRAVERSE accessess current as current = current->(field).next ...
  and since we free current (and ast_free poisons the memory) we either
  end up on a ast_mutex_lock to a non-existing lock that can never be
  obtained, or a segfault.

  Incidentally add logging in the "we have to wait for a lock to release"
  case, and remove an ineffective statement that sets memory that was just
  cleared by ast_calloc to zero.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### func_lock: Fix requesters counter in error paths.                               
  Author: Jaco Kroon  
  Date:   2021-05-22  

  In two places we bail out with failure after we've already incremented
  the requesters counter, if this occured then it would effectively result
  in unload to wait indefinitely, thus preventing clean shutdown.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### app_originate: Allow setting Caller ID and variables                            
  Author: Naveen Albert  
  Date:   2021-05-25  

  Caller ID can now be set on the called channel and
  Variables can now be set on the destination
  using the Originate application, just as
  they can be currently using call files
  or the Manager Action.

  ASTERISK-29450


- ### menuselect: Fix description of several modules.                                 
  Author: Sean Bright  
  Date:   2021-06-10  

  The text description needs to be the last thing on the AST_MODULE_INFO
  line to be pulled in properly by menuselect.


- ### app_confbridge: New ConfKick() application                                      
  Author: Naveen Albert  
  Date:   2021-05-23  

  Adds a new ConfKick() application, which may
  be used to kick a specific channel, all channels,
  or all non-admin channels from a specified
  conference bridge, similar to existing CLI and
  AMI commands.

  ASTERISK-29446


- ### res_pjsip_dtmf_info: Hook flash                                                 
  Author: Naveen Albert  
  Date:   2021-06-02  

  Adds hook flash recognition support
  for application/hook-flash.

  ASTERISK-29460


- ### app_confbridge: New option to prevent answer supervision                        
  Author: Naveen Albert  
  Date:   2021-05-20  

  A new user option, answer_channel, adds the capability to
  prevent answering the channel if it hasn't already been
  answered yet.

  ASTERISK-29440


- ### sip_to_pjsip: Fix missing cases                                                 
  Author: Naveen Albert  
  Date:   2021-06-02  

  Adds the "auto" case which is valid with
  both chan_sip dtmfmode and chan_pjsip's
  dtmf_mode, adds subscribecontext to
  subscribe_context conversion, and accounts
  for cipher = ALL being invalid.

  ASTERISK-29459


- ### res_pjsip_messaging: Refactor outgoing URI processing                           
  Author: George Joseph  
  Date:   2021-04-22  

   * Implemented the new "to" parameter of the MessageSend()
     dialplan application.  This allows a user to specify
     a complete SIP "To" header separate from the Request URI.

   * Completely refactored the get_outbound_endpoint() function
     to actually handle all the destination combinations that
     we advertized as supporting.

   * We now also accept a destination in the same format
     as Dial()...  PJSIP/number@endpoint

   * Added lots of debugging.

  ASTERISK-29404
  Reported by Brian J. Murrell


- ### func_math: Three new dialplan functions                                         
  Author: Naveen Albert  
  Date:   2021-05-16  

  Introduces three new dialplan functions, MIN and MAX,
  which can be used to calculate the minimum or
  maximum of up to two numbers, and ABS, an absolute
  value function.

  ASTERISK-29431


- ### STIR/SHAKEN: Add Date header, dest->tn, and URL checking.                       
  Author: Ben Ford  
  Date:   2021-05-19  

  STIR/SHAKEN requires a Date header alongside the Identity header, so
  that has been added. Still on the outgoing side, we were missing the
  dest->tn section of the JSON payload, so that has been added as well.
  Moving to the incoming side, URL checking has been added to the public
  cert URL to ensure that it starts with http.

  https://wiki.asterisk.org/wiki/display/AST/OpenSIPit+2021


- ### res_pjsip: On partial transport reload also move factories.                     
  Author: Joshua C. Colp  
  Date:   2021-05-24  

  For connection oriented transports PJSIP uses factories to
  produce transports. When doing a partial transport reload
  we need to also move the factory of the transport over so
  that anything referencing the transport (such as an endpoint)
  has the factory available.

  ASTERISK-29441


- ### func_volume: Add read capability to function.                                   
  Author: Naveen Albert  
  Date:   2021-05-20  

  Up until now, the VOLUME function has been write
  only, so that TX/RX values can be set but not
  read afterwards. Now, previously set TX/RX values
  can be read later.

  ASTERISK-29439


- ### stasis: Fix "FRACK!, Failed assertion bad magic number" when unsubscribing      
  Author: Evgenios_Greek  
  Date:   2021-04-13  

  When unsubscribing from an endpoint technology a FRACK
  would occur due to incorrect reference counting. This fixes
  that issue, along with some other issues.

  Fixed a typo in get_subscription when calling ao2_find as it
  needed to pass the endpoint ID and not the entire object.

  Fixed scenario where a subscription would get returned when
  it shouldn't have been when searching based on endpoint
  technology.

  A doulbe unreference has also been resolved by only explicitly
  releasing the reference held by tech_subscriptions.

  ASTERISK-28237 #close
  Reported by: Lucas Tardioli Silveira


- ### res_pjsip.c: Support endpoints with domain info in username                     
  Author: Joseph Nadiv  
  Date:   2021-05-20  

  In multidomain environments, it is desirable to create
  PJSIP endpoints with the domain info in the endpoint name
  in pjsip_endpoint.conf.  This resulted in an error with
  registrations, NOTIFY, and OPTIONS packet generation.

  This commit will detect if there is an @ in the endpoint
  identifier and generate the URI accordingly so NOTIFY and
  OPTIONS From headers will generate correctly.

  ASTERISK-28393


- ### res_rtp_asterisk: Set correct raddr port on RTCP srflx candidates.              
  Author: Joshua C. Colp  
  Date:   2021-05-20  

  RTCP ICE candidates use a base address derived from the RTP
  candidate. The port on the base address was not being updated to
  the RTCP port.

  This change sets the base port to the RTCP port and all is well.

  ASTERISK-29433


- ### asterisk: We've moved to Libera Chat!                                           
  Author: Joshua C. Colp  
  Date:   2021-05-25  


- ### res_rtp_asterisk: make it possible to remove SOFTWARE attribute                 
  Author: Jeremy Lainé  
  Date:   2021-05-19  

  By default Asterisk reports the PJSIP version in a SOFTWARE attribute
  of every STUN packet it sends. This may not be desired in a production
  environment, and RFC5389 recommends making the use of the SOFTWARE
  attribute a configurable option:

  https://datatracker.ietf.org/doc/html/rfc5389#section-16.1.2

  This patch adds a `stun_software_attribute` yes/no option to make it
  possible to omit the SOFTWARE attribute from STUN packets.

  ASTERISK-29434


- ### res_pjsip_outbound_authenticator_digest: Be tolerant of RFC8760 UASs            
  Author: George Joseph  
  Date:   2021-04-15  

  RFC7616 and RFC8760 allow more than one WWW-Authenticate or
  Proxy-Authenticate header per realm, each with different digest
  algorithms (including new ones like SHA-256 and SHA-512-256).
  Thankfully however a UAS can NOT send back multiple Authenticate
  headers for the same realm with the same digest algorithm.  The
  UAS is also supposed to send the headers in order of preference
  with the first one being the most preferred.  We're supposed to
  send an Authorization header for the first one we encounter for a
  realm that we can support.

  The UAS can also send multiple realms, especially when it's a
  proxy that has forked the request in which case the proxy will
  aggregate all of the Authenticate headers and then send them all
  back to the UAC.

  It doesn't stop there though... Each realm can require a
  different username from the others.  There's also nothing
  preventing each digest algorithm from having a unique password
  although I'm not sure if that adds any benefit.

  So now... For each Authenticate header we encounter, we have to
  determine if we support the digest algorithm and, if not, just
  skip the header.  We then have to find an auth object that
  matches the realm AND the digest algorithm or find a wildcard
  object that matches the digest algorithm. If we find one, we add
  it to the results vector and read the next Authenticate header.
  If the next header is for the same realm AND we already added an
  auth object for that realm, we skip the header. Otherwise we
  repeat the process for the next header.

  In the end, we'll have accumulated a list of credentials we can
  pass to pjproject that it can use to add Authentication headers
  to a request.

  NOTE: Neither we nor pjproject can currently handle digest
  algorithms other than MD5.  We don't even have a place for it in
  the ast_sip_auth object. For this reason, we just skip processing
  any Authenticate header that's not MD5.  When we support the
  others, we'll move the check into the loop that searches the
  objects.

  Changes:

   * Added a new API ast_sip_retrieve_auths_vector() that takes in
     a vector of auth ids (usually supplied on a call to
     ast_sip_create_request_with_auth()) and populates another
     vector with the actual objects.

   * Refactored res_pjsip_outbound_authenticator_digest to handle
     multiple Authenticate headers and set the stage for handling
     additional digest algorithms.

   * Added a pjproject patch that allows them to ignore digest
     algorithms they don't support.  This patch has already been
     merged upstream.

   * Updated documentation for auth objects in the XML and
     in pjsip.conf.sample.

   * Although res_pjsip_authenticator_digest isn't affected
     by this change, some debugging and a testsuite AMI event
     was added to facilitate testing.

  Discovered during OpenSIPit 2021.

  ASTERISK-29397


- ### res_pjsip_dialog_info_body_generator: Add LOCAL/REMOTE tags in dialog-info+xml  
  Author: Joseph Nadiv  
  Date:   2021-04-14  

  RFC 4235 Section 4.1.6 describes XML elements that should be
  sent to subscribed endpoints to identify the local and remote
  participants in the dialog.

  This patch adds this functionality to PJSIP by iterating through the
  ringing channels causing the NOTIFY, and inserts the channel info
  into the dialog so that information is properly passed to the endpoint
  in dialog-info+xml.

  ASTERISK-24601
  Patch submitted: Joshua Elson
  Modified by: Joseph Nadiv and Sean Bright
  Tested by: Joseph Nadiv


- ### AMI: Add AMI event to expose hook flash events                                  
  Author: Naveen Albert  
  Date:   2021-05-13  

  Although Asterisk can receive and propogate flash events, it currently
  provides no mechanism for doing anything with them itself.

  This AMI event allows flash events to be processed by Asterisk.
  Additionally, AST_CONTROL_FLASH is included in a switch statement
  in channel.c to avoid throwing a warning when we shouldn't.

  ASTERISK-29380


- ### app_voicemail: Configurable voicemail beep                                      
  Author: Naveen Albert  
  Date:   2021-05-13  

  Hitherto, VoiceMail() played a non-customizable beep tone to indicate
  the caller could leave a message. In some cases, the beep may not
  be desired, or a different tone may be desired.

  To increase flexibility, a new option allows customization of the tone.
  If the t option is specified, the default beep will be overridden.
  Supplying an argument will cause it to use the specified file for the tone,
  and omitting it will cause it to skip the beep altogether. If the option
  is not used, the default behavior persists.

  ASTERISK-29349


- ### main/file.c: Don't throw error on flash event.                                  
  Author: Naveen Albert  
  Date:   2021-05-13  

  AST_CONTROL_FLASH isn't accounted for in a switch statement in file.c
  where it should be ignored. Adding this to the switch ensures a
  warning isn't thrown on RFC2833 flash events, since nothing's amiss.

  ASTERISK-29372


- ### chan_sip: Expand hook flash recognition.                                        
  Author: Naveen Albert  
  Date:   2021-05-13  

  Some ATAs send hook flash events as application/hook-flash, rather than a DTMF
  event. Now, we also recognize hook-flash as a flash event.

  ASTERISK-29370


- ### pjsip: Add patch for resolving STUN packet lifetime issues.                     
  Author: Joshua C. Colp  
  Date:   2021-05-11  

  In some cases it was possible for a STUN packet to be destroyed
  prematurely or even destroyed partially multiple times.

  This patch provided by Teluu fixes the lifetime of these
  packets and ensures they aren't partially destroyed multiple
  times.

  https://github.com/pjsip/pjproject/pull/2709

  ASTERISK-29377


- ### chan_pjsip: Correct misleading trace message                                    
  Author: Sean Bright  
  Date:   2021-05-12  

  ASTERISK-29358 #close


- ### STIR/SHAKEN: Switch to base64 URL encoding.                                     
  Author: Ben Ford  
  Date:   2021-04-26  

  STIR/SHAKEN encodes using base64 URL format. Currently, we just use
  base64. New functions have been added that convert to and from base64
  encoding.

  The origid field should also be an UUID. This means there's no reason to
  have it as an option in stir_shaken.conf, as we can simply generate one
  when creating the Identity header.

  https://wiki.asterisk.org/wiki/display/AST/OpenSIPit+2021


- ### STIR/SHAKEN: OPENSSL_free serial hex from openssl.                              
  Author: Ben Ford  
  Date:   2021-05-11  

  We're getting the serial number of the certificate from openssl and
  freeing it with ast_free(), but it needs to be freed with OPENSSL_free()
  instead. Now we duplicate the string and free the one from openssl with
  OPENSSL_free(), which means we can still use ast_free() on the returned
  string.

  https://wiki.asterisk.org/wiki/display/AST/OpenSIPit+2021


- ### STIR/SHAKEN: Fix certificate type and storage.                                  
  Author: Ben Ford  
  Date:   2021-04-21  

  During OpenSIPit, we found out that the public certificates must be of
  type X.509. When reading in public keys, we use the corresponding X.509
  functions now.

  We also discovered that we needed a better naming scheme for the
  certificates since certificates with the same name would cause issues
  (overwriting certs, etc.). Now when we download a public certificate, we
  get the serial number from it and use that as the name of the cached
  certificate.

  The configuration option public_key_url in stir_shaken.conf has also
  been renamed to public_cert_url, which better describes what the option
  is for.

  https://wiki.asterisk.org/wiki/display/AST/OpenSIPit+2021


- ### translate.c: Avoid refleak when checking for a translation path                 
  Author: Sean Bright  
  Date:   2021-04-30  


- ### res_rtp_asterisk: More robust timestamp checking                                
  Author: Sean Bright  
  Date:   2021-04-27  

  We assume that a timestamp value of 0 represents an 'uninitialized'
  timestamp, but 0 is a valid value. Add a simple wrapper to be able to
  differentiate between whether the value is set or not.

  This also removes the fix for ASTERISK~28812 which should not be
  needed if we are checking the last timestamp appropriately.

  ASTERISK-29030 #close


- ### chan_local: Skip filtering audio formats on removed streams.                    
  Author: Joshua C. Colp  
  Date:   2021-04-28  

  When a stream topology is provided to chan_local when dialing
  it filters the audio formats down. This operation did not skip
  streams which were removed (that have no formats) resulting in
  calling being aborted.

  This change causes such streams to be skipped.

  ASTERISK-29407


- ### res_pjsip.c: OPTIONS processing can now optionally skip authentication          
  Author: Sean Bright  
  Date:   2021-04-23  

  ASTERISK-27477 #close


- ### translate.c: Take sampling rate into account when checking codec's buffer size  
  Author: Jean Aunis  
  Date:   2021-04-21  

  Up/down sampling changes the number of samples produced by a translation.
  This must be taken into account when checking the codec's buffer size.

  ASTERISK-29328


- ### svn: Switch to https scheme.                                                    
  Author: Joshua C. Colp  
  Date:   2021-04-25  

  Some versions of SVN seemingly don't follow the redirect
  to https.


- ### res_pjsip:  Update documentation for the auth object                            
  Author: George Joseph  
  Date:   2021-04-20  


- ### res_aeap: Add basic config skeleton and CLI commands.                           
  Author: Ben Ford  
  Date:   2021-03-29  

  Added support for a basic AEAP configuration read from aeap.conf.
  Also added 2 CLI commands for showing individual configurations as
  well as all of them: aeap show server <id> and aeap show servers.

  Only one configuration option is required at the moment, and that one is
  server_url. It must be a websocket URL. The other option, codecs, is
  optional and will be used over the codecs specified on the endpoint if
  provided.

  https://wiki.asterisk.org/wiki/pages/viewpage.action?pageId=45482453


- ### bridge_channel_write_frame: Check for NULL channel                              
  Author: George Joseph  
  Date:   2021-04-02  

  There is a possibility, when bridge_channel_write_frame() is
  called, that the bridge_channel->chan will be NULL.  The first
  thing bridge_channel_write_frame() does though is call
  ast_channel_is_multistream() which had no check for a NULL
  channel and therefore caused a segfault. Since it's still
  possible for bridge_channel_write_frame() to write the frame to
  the other channels in the bridge, we don't want to bail before we
  call ast_channel_is_multistream() but we can just skip the
  multi-channel stuff.  So...

  bridge_channel_write_frame() only calls ast_channel_is_multistream()
  if bridge_channel->chan is not NULL.

  As a safety measure, ast_channel_is_multistream() now returns
  false if the supplied channel is NULL.

  ASTERISK-29379
  Reported-by: Vyrva Igor
  Reported-by: Ross Beer


- ### loader.c: Speed up deprecation metadata lookup                                  
  Author: Sean Bright  
  Date:   2021-04-01  

  Only use an XPath query once per module, then just navigate the DOM for
  everything else.


- ### res_prometheus: Clone containers before iterating                               
  Author: George Joseph  
  Date:   2021-04-01  

  The channels, bridges and endpoints scrape functions were
  grabbing their respective global containers, getting the
  count of entries, allocating metric arrays based on
  that count, then iterating over the container.  If the
  global container had new objects added after the count
  was taken and the metric arrays were allocated, we'd run
  out of metric entries and attempt to write past the end
  of the arrays.

  Now each of the scape functions clone their respective
  global containers and all operations are done on the
  clone.  Since the clone is stable between getting the
  count and iterating over it, we can't run past the end
  of the metrics array.

  ASTERISK-29130
  Reported-By: Francisco Correia
  Reported-By: BJ Weschke
  Reported-By: Sébastien Duthil


- ### loader: Output warnings for deprecated modules.                                 
  Author: Joshua C. Colp  
  Date:   2021-03-10  

  Using the information from the MODULEINFO XML we can
  now output useful information at the end of module
  loading for deprecated modules. This includes the
  version it was deprecated in, the version it will be
  removed in, and the replacement if available.

  ASTERISK-29339


- ### res_rtp_asterisk: Fix standard deviation calculation                            
  Author: Kevin Harwell  
  Date:   2021-03-22  

  For some input to the standard deviation algorithm extremely large,
  and wrong numbers were being calculated.

  This patch uses a new formula for correctly calculating both the
  running mean and standard deviation for the given inputs.

  ASTERISK-29364 #close


- ### res_rtp_asterisk: Don't count 0 as a minimum lost packets                       
  Author: Kevin Harwell  
  Date:   2021-03-29  

  The calculated minimum lost packets represents the lowest number of
  lost packets missed during an RTCP report interval. Zero of course
  is the lowest, but the idea is that this value contain the lowest
  number of lost packets once some have been missed.

  This patch checks to make sure the number of lost packets over an
  interval is not zero before checking and setting the minimum value.

  Also, this patch updates the rtp lost packet test to check for
  packet loss over several reports vs one.


- ### res_rtp_asterisk: Statically declare rtp_drop_packets_data object               
  Author: Kevin Harwell  
  Date:   2021-03-31  

  This patch makes the drop_packets_data object static.


- ### res_rtp_asterisk: Only raise flash control frame on end.                        
  Author: Joshua C. Colp  
  Date:   2021-03-29  

  Flash in RTP is conveyed the same as DTMF, just with a
  specific digit. In Asterisk however we do flash as a
  single control frame.

  This change makes it so that only on end do we provide
  the flash control frame to the core. Previously we would
  provide a flash control frame on both begin and end,
  causing flash to work improperly.

  ASTERISK-29373


- ### res_rtp_asterisk: Add a DEVMODE RTP drop packets CLI command                    
  Author: Kevin Harwell  
  Date:   2021-03-05  

  This patch makes it so when Asterisk is compiled in DEVMODE a CLI
  command is available that allows someone to drop incoming RTP
  packets. The command allows for dropping of packets once, or on a
  timed interval (e.g. drop 10 packets every 5 seconds). A user can
  also specify to drop packets by IP address.


- ### res_pjsip: Give error when TLS transport configured but not supported.          
  Author: Joshua C. Colp  
  Date:   2021-03-30  


- ### time: Add timeval create and unit conversion functions                          
  Author: Kevin Harwell  
  Date:   2021-03-05  

  Added a TIME_UNIT enumeration, and a function that converts a
  string to one of the enumerated values. Also, added functions
  that create and initialize a timeval object using a specified
  value, and unit type.


- ### app_queue: Add alembic migration to add ringinuse to queue_members.             
  Author: Sean Bright  
  Date:   2021-03-24  

  ASTERISK-28356 #close


- ### modules.conf: Fix more differing usages of assignment operators.                
  Author: Sean Bright  
  Date:   2021-03-28  

  I missed the changes in 18 and master in the previous review.

  ASTERISK-24434 #close


- ### logger.conf.sample: Add more debug documentation.                               
  Author: Ben Ford  
  Date:   2021-03-24  


- ### logging: Add .log to samples and update asterisk.logrotate.                     
  Author: Ben Ford  
  Date:   2021-03-24  

  Added .log extension to the sample logs in logger.conf.sample so that
  they will be able to be opened in the browser when attached to JIRA
  tickets. Because of this, asterisk.logrotate has also been updated to
  look for .log extensions instead of no extension for log files such as
  full and messages.


- ### app_queue.c: Remove dead 'updatecdr' code.                                      
  Author: Sean Bright  
  Date:   2021-03-23  

  Also removed the sample documentation, and some oddly-placed
  documentation about the timeout argument to the Queue() application
  itself. There is a large section on the timeout behavior below.

  ASTERISK-26614 #close


- ### queues.conf.sample: Correct 'context' documentation.                            
  Author: Sean Bright  
  Date:   2021-03-23  

  ASTERISK-24631 #close


- ### logger: Console sessions will now respect logger.conf dateformat= option        
  Author: Mark Murawski  
  Date:   2021-03-19  

  The 'core' console (ie: asterisk -c) does read logger.conf and does
  use the dateformat= option.

  Whereas 'remote' consoles (ie: asterisk -r -T) does not read logger.conf
  and uses a hard coded dateformat option for printing received verbose messages:
    main/logger.c: static char dateformat[256] = "%b %e %T"

  This change will load logger.conf for each remote console session and
  use the dateformat= option to set the per-line timestamp for verbose messages

  ASTERISK-25358: #close
  Reported-by: Igor Liferenko

- ### app_queue.c: Don't crash when realtime queue name is empty.                     
  Author: Sean Bright  
  Date:   2021-03-19  

  ASTERISK-27542 #close


- ### res_pjsip_session: Make reschedule_reinvite check for NULL topologies           
  Author: George Joseph  
  Date:   2021-03-18  

  When the check for equal topologies was added to reschedule_reinvite()
  it was assumed that both the pending and active media states would
  actually have non-NULL topologies.  We since discovered this isn't
  the case.

  We now only test for equal topologies if both media states have
  non-NULL topologies.  The logic had to be rearranged a bit to make
  sure that we cloned the media states if their topologies were
  non-NULL but weren't equal.

  ASTERISK-29215


- ### app_queue: Only send QueueMemberStatus if status changes.                       
  Author: Joshua C. Colp  
  Date:   2021-03-19  

  If a queue member was updated with the same status multiple
  times each time a QueueMemberStatus event would be sent
  which would be a duplicate of the previous.

  This change makes it so that the QueueMemberStatus event is
  only sent if the status actually changes.

  ASTERISK-29355


- ### core_unreal: Fix deadlock with T.38 control frames.                             
  Author: Joshua C. Colp  
  Date:   2021-03-19  

  When using the ast_unreal_lock_all function no channel
  locks can be held before calling it.

  This change unlocks the channel that indicate was
  called on before doing so and then relocks it afterwards.

  ASTERISK-29035


- ### res_pjsip: Add support for partial transport reload.                            
  Author: Joshua C. Colp  
  Date:   2021-03-01  

  Some configuration items for a transport do not result in
  the underlying transport changing, but instead are just
  state we keep ourselves and use. It is perfectly reasonable
  to change these items.

  These include local_net and external_* information.

  ASTERISK-29354


- ### menuselect: exit non-zero in case of failure on --enable|disable options.       
  Author: Jaco Kroon  
  Date:   2021-03-13  

  ASTERISK-29348

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### res_rtp_asterisk: Force resync on SSRC change.                                  
  Author: Joshua C. Colp  
  Date:   2021-03-17  

  When an SSRC change occurs the timestamps are likely
  to change as well. As a result we need to reset the
  timestamp mapping done in the calc_rxstamp function
  so that they map properly from timestamp to real
  time.

  This previously occurred but due to packet
  retransmission support the explicit setting
  of the marker bit was not effective.

  ASTERISK-29352


- ### menuselect: Add ability to set deprecated and removed versions.                 
  Author: Joshua C. Colp  
  Date:   2021-03-10  

  The "deprecated_in" and "removed_in" information can now be
  set in MODULEINFO for a module and is then displayed in
  menuselect so users can be aware of when a module is slated
  to be deprecated and then removed.

  ASTERISK-29337


- ### xml: Allow deprecated_in and removed_in for MODULEINFO.                         
  Author: Joshua C. Colp  
  Date:   2021-03-10  

  ASTERISK-29337


- ### xml: Embed module information into core XML documentation.                      
  Author: Joshua C. Colp  
  Date:   2021-03-09  

  This change embeds the MODULEINFO block of modules
  into the core XML documentation. This provides a shared
  mechanism for use by both menuselect and Asterisk for
  information and a definitive source of truth.

  ASTERISK-29335


- ### documentation: Fix non-matching module support levels.                          
  Author: Joshua C. Colp  
  Date:   2021-03-10  

  Some modules have a different support level documented in their
  MODULEINFO XML and Asterisk module definition. This change
  brings the two in sync for the modules which were not matching.

  ASTERISK-29336


- ### channel: Fix crash in suppress API.                                             
  Author: Joshua C. Colp  
  Date:   2021-03-09  

  There exists an inconsistency with framehook usage
  such that it is only on reads that the frame should
  be freed, not on writes as well.

  ASTERISK-29071


- ### func_callerid+res_agi: Fix compile errors related to -Werror=zero-length-bounds 
  Author: Jaco Kroon  
  Date:   2021-02-24  

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### app.h: Fix -Werror=zero-length-bounds compile errors in dev mode.               
  Author: Jaco Kroon  
  Date:   2021-02-24  

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### app_dial.c: Only send DTMF on first progress event.                             
  Author: Sean Bright  
  Date:   2021-03-06  

  ASTERISK-29329 #close


- ### res_format_attr_*: Parameter Names are Case-Insensitive.                        
  Author: Alexander Traud  
  Date:   2021-03-05  

  see RFC 4855:
  parameter names are case-insensitive both in media type strings and
  in the default mapping to the SDP a=fmtp attribute.

  This change is required for H.263+ because some implementations are
  known to use even mixed-case. This does not fix ASTERISK~29268 because
  H.264 was not fixed. This approach here lowers/uppers both parameter
  names and parameter values. H.264 needs a different approach because
  one of its parameter values is not case-insensitive:
  sprop-parameter-sets is Base64.


- ### chan_iax2: System Header strings is included via asterisk.h/compat.h.           
  Author: Alexander Traud  
  Date:   2021-03-05  

  The system header strings was included mistakenly with commit 3de0204.
  That header is included via asterisk.h and there via the compat.h.


- ### modules.conf: Fix differing usage of assignment operators.                      
  Author: Sean Bright  
  Date:   2021-03-08  

  ASTERISK-24434 #close


- ### strings.h: ast_str_to_upper() and _to_lower() are not pure.                     
  Author: Sean Bright  
  Date:   2021-03-08  

  Because they modify their argument they are not pure functions and
  should not be marked as such, otherwise the compiler may optimize
  them away.

  ASTERISK-29306 #close


- ### res_musiconhold.c: Plug ref leak caused by ao2_replace() misuse.                
  Author: Sean Bright  
  Date:   2021-03-08  

  ao2_replace() bumps the reference count of the object that is doing the
  replacing, which is not what we want. We just want to drop the old ref
  on the old object and update the pointer to point to the new object.

  Pointed out by George Joseph in #asterisk-dev


- ### res/res_rtp_asterisk: generate new SSRC on native bridge end                    
  Author: Torrey Searle  
  Date:   2021-02-19  

  For RTCP to work, we update the ssrc to be the one corresponding to
  the native bridge while active.  However when the bridge ends we
  should generate a new SSRC as the sequence numbers will not continue
  from the native bridge left off.

  ASTERISK-29300 #close


- ### sorcery: Add support for more intelligent reloading.                            
  Author: Joshua C. Colp  
  Date:   2021-03-01  

  Some sorcery objects actually contain dynamic content
  that can change despite the underlying configuration
  itself not changing. A good example of this is the
  res_pjsip_endpoint_identifier_ip module which allows
  specifying hostnames. While the configuration may not
  change between reloads the DNS information of the
  hostnames can.

  This change adds the ability for a sorcery object to be
  marked as having dynamic contents which is then taken
  into account when reloading by the sorcery file based
  config module. If there is an object with dynamic content
  then a reload will be forced while if there are none
  then the existing behavior of not reloading occurs.

  ASTERISK-29321


- ### res_pjsip_refer: Move the progress dlg release to a serializer                  
  Author: George Joseph  
  Date:   2021-03-02  

  Although the dlg session count was incremented in a pjsip servant
  thread, there's no guarantee that the last thread to unref this
  progress object was one.  Before we decrement, we need to make
  sure that this is either a servant thread or that we push the
  decrement to a serializer that is one.

  Because pjsip_dlg_dec_session requires the dialog lock, we don't
  want to wait on the task to complete if we had to push it to a
  serializer.


- ### res_pjsip_registrar: Include source IP and port in log messages.                
  Author: Joshua C. Colp  
  Date:   2021-03-03  

  When registering it can be useful to see the source IP address and
  port in cases where multiple devices are using the same endpoint
  or when anonymous is in use.

  ASTERISK-29325


- ### asterisk: Update copyright.                                                     
  Author: Joshua C. Colp  
  Date:   2021-03-03  

  ASTERISK-29326


- ### AST-2021-006 - res_pjsip_t38.c: Check for session_media on reinvite.            
  Author: Ben Ford  
  Date:   2021-02-25  

  When Asterisk sends a reinvite negotiating T38 faxing, it's possible a
  crash can occur if the response contains a m=image and zero port. The
  reinvite callback code now checks session_media to see if it is null or
  not before trying to access the udptl variable on it.

  ASTERISK-29305


- ### res_format_attr_h263: Generate valid SDP fmtp for H.263+.                       
  Author: Alexander Traud  
  Date:   2021-01-28  

  Fixed:
  * RFC 4629 does not allow the value "0" for MPI, K, and N.
  * Allow value "0" for PAR.
  * BPP is printed only when specified because "0" has a meaning.

  New:
  * Added CPCF and MaxBR.
  * Some implementations provide CIF without MPI: a=fmtp:xx CIF;F=1
    Although a violation of RFC 3555 section 3, we can support that.

  Changed:
  * Resorts the CIFs from large to small which partly fixes ASTERISK~29267.


- ### res_pjsip_nat: Don't rewrite Contact on REGISTER responses.                     
  Author: Joshua C. Colp  
  Date:   2021-02-24  

  When sending a SIP response to an incoming REGISTER request
  we don't want to change the Contact header as it will
  contain the Contacts registered to the AOR and not our own
  Contact URI.

  ASTERISK-29235


- ### channel: Fix memory leak in suppress API.                                       
  Author: Joshua C. Colp  
  Date:   2021-03-03  

  A frame suppression API exists as part of channels
  which allows audio frames to or from a channel to
  be dropped. The MuteAudio AMI action uses this
  API to perform its job.

  This API uses a framehook to intercept flowing
  audio and drop it when appropriate. It is the
  responsibility of the framehook to free the
  frame it is given if it changes the frame. The
  suppression API failed to do this resulting in
  a leak of audio frames.

  This change adds the freeing of these frames.

  ASTERISK-29071


- ### res_rtp_asterisk:  Check remote ICE reset and reset local ice attrb             
  Author: Salah Ahmed  
  Date:   2021-01-27  

  This change will check is the remote ICE session got reset or not by
  checking the offered ufrag and password with session. If the remote ICE
  reset session then Asterisk reset its local ufrag and password to reject
  binding request with Old ufrag and Password.

  ASTERISK-29266


- ### pjsip: Generate progress (once) when receiving a 180 with a SDP                 
  Author: Holger Hans Peter Freyther  
  Date:   2021-01-07  

  ASTERISK-29105


- ### main: With Dutch language year after 2020 is not spoken in say.c                
  Author: Nico Kooijman  
  Date:   2021-02-28  

  Implemented the english way of saying the year in ast_say_date_with_format_nl.
  Currently the numbers are spoken correctly until 2020 and stopped working
  this year.

  ASTERISK-29297 #close
  Reported-by: Jacek Konieczny


- ### res_pjsip: dont return early from registration if init auth fails               
  Author: Nick French  
  Date:   2021-02-24  

  If set_outbound_initial_authentication_credentials() fails,
  handle_client_registration() bails early without creating or
  sending a register message.

  [set_outbound_initial_authentication_credentials() failures
  can occur during the process of retrieving an oauth access
  token.]

  The return from handle_client_registration is ignored, so
  returning an error doesn't do any good.

  This is a real problem when the registration request is a
  re-register, because then the registration will still be
  marked 'active' despite the re-register never being sent at all.

  So instead, log a warning but let the registration be created
  and sent (and probably fail) and follow the normal registration
  failed retry/abort logic.

  ASTERISK-29315 #close


- ### res_fax: validate the remote/local Station ID for UTF-8 format                  
  Author: Alexei Gradinari  
  Date:   2021-02-23  

  If the remote Station ID contains invalid UTF-8 characters
  the asterisk fails to publish the Stasis and ReceiveFax status messages.

  json.c: Error building JSON from '{s: s, s: s}': Invalid UTF-8 string.
  0: /usr/sbin/asterisk(ast_json_vpack+0x98) [0x4f3f28]
  1: /usr/sbin/asterisk(ast_json_pack+0x8c) [0x4f3fcc]
  2: /usr/sbin/asterisk(ast_channel_publish_varset+0x2b) [0x57aa0b]
  3: /usr/sbin/asterisk(pbx_builtin_setvar_helper+0x121) [0x530641]
  4: /usr/lib64/asterisk/modules/res_fax.so(+0x44fe) [0x7f27f4bff4fe]
  ...
  stasis_channels.c: Error creating message

  json.c: Error building JSON from '{s: s, s: s, s: s, s: s, s: s, s: s, s: o}': Invalid UTF-8 string.
  0: /usr/sbin/asterisk(ast_json_vpack+0x98) [0x4f3f28]
  1: /usr/sbin/asterisk(ast_json_pack+0x8c) [0x4f3fcc]
  2: /usr/lib64/asterisk/modules/res_fax.so(+0x5acd) [0x7f27f4c00acd]
  ...
  res_fax.c: Error publishing ReceiveFax status message

  This patch replaces the invalid UTF-8 Station IDs with an empty string.

  ASTERISK-29312 #close


- ### app_page.c: Don't fail to Page if beep sound file is missing                    
  Author: Sean Bright  
  Date:   2021-02-25  

  ASTERISK-16799 #close


- ### res_pjsip_refer: Refactor progress locking and serialization                    
  Author: George Joseph  
  Date:   2021-02-19  

  Although refer_progress_notify() always runs in the progress
  serializer, the pjproject evsub module itself can cause the
  subscription to be destroyed which then triggers
  refer_progress_on_evsub_state() to clean it up.  In this case,
  it's possible that refer_progress_notify() could get the
  subscription pulled out from under it while it's trying to use
  it.

  At one point we tried to have refer_progress_on_evsub_state()
  push the cleanup to the serializer and wait for its return before
  returning to pjproject but since pjproject calls its state
  callbacks with the dialog locked, this required us to unlock the
  dialog while waiting for the serialized cleanup, then lock it
  again before returning to pjproject. There were also still some
  cases where other callers of refer_progress_notify() weren't
  using the serializer and crashes were resulting.

  Although all callers of refer_progress_notify() now use the
  progress serializer, we decided to simplify the locking so we
  didn't have to unlock and relock the dialog in
  refer_progress_on_evsub_state().

  Now, refer_progress_notify() holds the dialog lock for its
  duration and since pjproject also holds the dialog lock while
  calling refer_progress_on_evsub_state() (which does the cleanup),
  there should be no more chances for the subscription to be
  cleaned up while still being used to send NOTIFYs.

  To be extra safe, we also now increment the session count on
  the dialog when we create a progress object and decrement
  the count when the progress is destroyed.

  ASTERISK-29313


- ### res_rtp_asterisk: Add packet subtype during RTCP debug when relevant            
  Author: Kevin Harwell  
  Date:   2021-02-24  

  For some RTCP packet types the report count is actually the packet's subtype.
  This was not being reflected in the packet debug output.

  This patch makes it so for some RTCP packet types a "Packet Subtype" is
  now output in the debug replacing the "Reception reports" (i.e count).


- ### res_pjsip_session: Always produce offer on re-INVITE without SDP.               
  Author: Joshua C. Colp  
  Date:   2021-02-16  

  When PJSIP receives a re-INVITE without an SDP offer the INVITE
  session library will first call the on_create_offer callback and
  if unavailable then use the active negotiated SDP as the offer.

  In some cases this would result in a different SDP then was
  previously used without an incremented SDP version number. The two
  known cases are:

  1. Sending an initial INVITE with a set of codecs and having the
  remote side answer with a subset. The active negotiated SDP would
  have the pruned list but would not have an incremented SDP version
  number.

  2. Using re-INVITE for unhold. We would modify the active negotiated
  SDP but would not increment the SDP version.

  To solve these, and potential other unknown cases, the on_create_offer
  callback has now been implemented which produces a fresh offer with
  incremented SDP version number. This better fits within the model
  provided by the INVITE session library.

  ASTERISK-28452


- ### res_odbc_transaction: correctly initialise forcecommit value from DSN.          
  Author: Jaco Kroon  
  Date:   2021-02-23  

  Also improve the in-process documentation to clarify that the value is
  initialised from the DSN and not default false, but that the DSN's value
  is default false if unset.

  ASTERISK-29311 #close

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### res_pjsip_session.c: Check topology on re-invite.                               
  Author: Ben Ford  
  Date:   2021-02-15  

  Removes an unnecessary check for the conditional that compares the
  stream topologies to see if they are equal to suppress re-invites. This
  was a problem when a Digium phone received an INVITE that offered codecs
  different than what it supported, causing Asterisk to send the
  re-invite.

  ASTERISK-29303


- ### res_config_pgsql: Limit realtime_pgsql() to return one (no more) record.        
  Author: Boris P. Korzun  
  Date:   2021-02-15  

  Added a SELECT 'LIMIT' clause to realtime_pgsql() and refactored the function.

  ASTERISK-29293 #close


- ### app_queue: Fix conversion of complex extension states into device states        
  Author: Ivan Poddubnyi  
  Date:   2019-09-13  

  Queue members using dialplan hints as a state interface must handle
  INUSE+RINGING hint as RINGINUSE devstate, and INUSE + ONHOLD as INUSE.

  ASTERISK-28369


- ### app.h: Restore C++ compatibility for macro AST_DECLARE_APP_ARGS                 
  Author: Jaco Kroon  
  Date:   2021-02-10  

  This partially reverts commit 3d1bf3c537bba0416f691f48165fdd0a32554e8a,
  specifically for app.h.

  This works with both gcc 9.3.0 and 10.2.0 now, both for C and C++ (as
  tested with external modules).

  ASTERISK-29287

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### chan_sip: Filter pass-through audio/video formats away, again.                  
  Author: Alexander Traud  
  Date:   2021-02-05  

  Instead of looking for pass-through formats in the list of transcodable
  formats (which is going to find nothing), go through the result which
  is going to be the jointcaps of the tech_pvt of the channel. Finally,
  only with that list, ast_format_cap_remove(.) is going to succeed.

  This restores the behaviour of Asterisk 1.8. However, it does not fix
  ASTERISK_29282 because that issue report is about chan_sip and PJSIP.
  Here, only chan_sip is fixed because PJSIP does not even call
  ast_rtp_instance_available_formats -> ast_translate_available_format.


- ### func_odbc:  Introduce minargs config and expose ARGC in addition to ARGn.       
  Author: Jaco Kroon  
  Date:   2021-02-17  

  minargs enables enforcing of minimum count of arguments to pass to
  func_odbc, so if you're unconditionally using ARG1 through ARG4 then
  this should be set to 4.  func_odbc will generate an error in this case,
  so for example

  [FOO]
  minargs = 4

  and ODBC_FOO(a,b,c) in dialplan will now error out instead of using a
  potentially leaked ARG4 from Gosub().

  ARGC is needed if you're using optional argument, to verify whether or
  not an argument has been passed, else it's possible to use a leaked ARGn
  from Gosub (app_stack).  So now you can safely do
  ${IF($[${ARGC}>3]?${ARGV}:default value)} kind of thing.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### app_mixmonitor: Add AMI events MixMonitorStart, -Stop and -Mute.                
  Author: Sebastien Duthil  
  Date:   2021-01-13  

  ASTERISK-29244


- ### AST-2021-002: Remote crash possible when negotiating T.38                       
  Author: Kevin Harwell  
  Date:   2021-02-01  

  When an endpoint requests to re-negotiate for fax and the incoming
  re-invite is received prior to Asterisk sending out the 200 OK for
  the initial invite the re-invite gets delayed. When Asterisk does
  finally send the re-inivite the SDP includes streams for both audio
  and T.38.

  This happens because when the pending topology and active topologies
  differ (pending stream is not in the active) in the delayed scenario
  the pending stream is appended to the active topology. However, in
  the fax case the pending stream should replace the active.

  This patch makes it so when a delay occurs during fax negotiation,
  to or from, the audio stream is replaced by the T.38 stream, or vice
  versa instead of being appended.

  Further when Asterisk sent the re-invite with both audio and T.38,
  and the endpoint responded with a declined T.38 stream then Asterisk
  would crash when attempting to change the T.38 state.

  This patch also puts in a check that ensures the media state has a
  valid fax session (associated udptl object) before changing the
  T.38 state internally.

  ASTERISK-29203 #close


- ### rtp:  Enable srtp replay protection                                             
  Author: Alexander Traud  
  Date:   2021-01-26  

  Add option "srtpreplayprotection" rtp.conf to enable srtp
  replay protection.

  ASTERISK-29260
  Reported by: Alexander Traud


- ### res_pjsip_diversion: Fix adding more than one histinfo to Supported             
  Author: Ivan Poddubnyi  
  Date:   2020-12-28  

  New responses sent within a PJSIP sessions are based on those that were
  sent before. Therefore, adding/modifying a header once causes it to be
  sent on all responses that follow.

  Sending 181 Call Is Being Forwarded many times first adds "histinfo"
  duplicated more and more, and eventually overflows past the array
  boundary.

  This commit adds a check preventing adding "histinfo" more than once,
  and skipping it if there is no more space in the header.

  Similar overflow situations can also occur in res_pjsip_path and
  res_pjsip_outbound_registration so those were also modified to
  check the bounds and suppress duplicate Supported values.

  ASTERISK-29227
  Reported by: Ivan Poddubny


- ### res_rtp_asterisk.c: Fix signed mismatch that leads to overflow                  
  Author: Sean Bright  
  Date:   2020-12-11  

  ASTERISK-29205 #close


- ### pjsip: Make modify_local_offer2 tolerate previous failed SDP.                   
  Author: Joshua C. Colp  
  Date:   2021-02-05  

  If a remote side is broken and sends an SDP that can not be
  negotiated the call will be torn down but there is a window
  where a second 183 Session Progress or 200 OK that is forked
  can be received that also attempts to negotiate SDP. Since
  the code marked the SDP negotiation as being done and complete
  prior to this it assumes that there is an active local and remote
  SDP which it can modify, while in fact there is not as the SDP
  did not successfully negotiate. Since there is no local or remote
  SDP a crash occurs.

  This patch changes the pjmedia_sdp_neg_modify_local_offer2
  function to no longer assume that a previous SDP negotiation
  was successful.

  ASTERISK-29196


- ### res_pjsip_refer: Always serialize calls to refer_progress_notify                
  Author: George Joseph  
  Date:   2021-02-09  

  refer_progress_notify wasn't always being called from the progress
  serializer.  This could allow clearing notification->progress->sub
  in one thread while another was trying to use it.

  * Instances where refer_progress_notify was being called in-line,
    have been changed to use ast_sip_push_task().


- ### core_unreal: Fix T.38 faxing when using local channels.                         
  Author: Ben Ford  
  Date:   2021-01-11  

  After some changes to streams and topologies, receiving fax through
  local channels stopped working. This change adds a stream topology with
  a stream of type IMAGE to the local channel pair and allows fax to be
  received.

  ASTERISK-29035 #close


- ### format_wav: Support of MIME-type for wav16                                      
  Author: Boris P. Korzun  
  Date:   2021-02-02  

  Provided a support of a MIME-type for wav16. Added new MIME-type
  for classic wav.

  ASTERISK-29275 #close


- ### chan_sip: Allow [peer] without audio (text+video).                              
  Author: Alexander Traud  
  Date:   2021-02-05  

  Two previous commits, 620d9f4 and 6d980de, allow to set up a call
  without audio, again. That was introduced originally with commit f04d5fb
  but changed and broke over time. The original commit missed one
  scenario: A [peer] section in sip.conf, which does not allow audio at
  all. In that case, chan_sip rejected the call, although even when the
  requester offered no audio. Now, chan_sip does not check whether there
  is no audio format but checks whether there is no format in general. In
  other words, if there is at least one format to offer, the call succeeds.

  However, to prevent calls with no-audio, chan_sip still rejects calls
  when both call parties (caller = requester of the call *and* callee =
  [peer] section in sip.conf) included audio. In such a case, it is
  expected that the call should have audio.

  ASTERISK-29280


- ### chan_iax2.c: Require secret and auth method if encryption is enabled            
  Author: George Joseph  
  Date:   2021-01-28  

  If there's no secret specified for an iax2 peer and there's no secret
  specified in the dial string, Asterisk will crash if the auth method
  requested by the peer is MD5 or plaintext.  You also couldn't specify
  a default auth method in the [general] section of iax.conf so if you
  don't have static peers defined and just use the dial string, Asterisk
  will still crash even if you have a secret specified in the dial string.

  * Added logic to iax2_call() and authenticate_reply() to print
    a warning and hanhup the call if encryption is requested and
    there's no secret or auth method.  This prevents the crash.

  * Added the ability to specify a default "auth" in the [general]
    section of iax.conf.

  ASTERISK-29624
  Reported by: N A


- ### app_read: Release tone zone reference on early return.                          
  Author: Sean Bright  
  Date:   2021-02-03  


- ### chan_sip: Set up calls without audio (text+video), again.                       
  Author: Alexander Traud  
  Date:   2021-01-27  

  The previous commit 6d980de fixed this issue in the core of Asterisk.
  With that, each channel technology can be used without audio
  theoretically. Practically, the channel-technology driver chan_sip
  turned out to have an invalid check preventing that. chan_sip tested
  whether there is at least one audio format. However, chan_sip has to
  test whether there is at least one format. More cannot be tested while
  requesting chan_sip because only the [general] capabilities but not the
  [peer] caps are known yet. And the [peer] caps might not be a subset or
  show any intersection with the [general] caps. This change here fixes
  this.

  The original commit f04d5fb, thirteen years ago, contained a software
  bug as it passed ANY audio capability to the channel-technology driver.
  Instead, it should have passed NO audio format. Therefore, this
  addressed issue here was not noticed in Asterisk 1.6.x and Asterisk 1.8.
  Then, Asterisk 10 changed that from ANY to NO, but nobody reported since
  then.

  ASTERISK-29265


- ### chan_pjsip, app_transfer: Add TRANSFERSTATUSPROTOCOL variable                   
  Author: Dan Cropp  
  Date:   2021-01-22  

  When a Transfer/REFER is executed, TRANSFERSTATUSPROTOCOL variable is
  0 when no protocl specific error
  SIP example of failure, 3xx-6xx for the SIP error code received

  This allows applications to perform actions based on the failure
  reason.

  ASTERISK-29252 #close
  Reported-by: Dan Cropp


- ### channel: Set up calls without audio (text+video), again.                        
  Author: Alexander Traud  
  Date:   2021-01-22  

  ASTERISK-29259


- ### res/res_pjsip.c: allow user=phone when number contain *#                        
  Author: roadkill  
  Date:   2021-01-22  

  if From number contain * or # asterisk will not add user=phone

  Currently only number that uses AST_DIGIT_ANYNUM can have "user=phone" but the validation should use AST_DIGIT_ANY
  this is a problem when you want to send call to ISUP
  as they will disregard the From header and either replace From with anonymous or with p-asserted-identity

  ASTERISK-29261
  Reported by: Mark Petersen
  Tested by: Mark Petersen


- ### chan_sip: SDP: Reject audio streams correctly.                                  
  Author: Alexander Traud  
  Date:   2021-01-21  

  This completes the fix for ASTERISK_24543. Only when the call is an
  outgoing call, consult and append the configured format capabilities
  (p->caps). When all audio formats got rejected the negotiated format
  capabilities (p->jointcaps) contain no audio formats for incoming
  calls. This is required when there are other accepted media streams.

  ASTERISK-29258


- ### main/frame: Add missing control frame names to ast_frame_subclass2str           
  Author: Ivan Poddubnyi  
  Date:   2021-01-22  

  Log proper control frame names instead of "Unknown control '14'", etc.


- ### res_musiconhold: Add support of various URL-schemes by MoH.                     
  Author: Boris P. Korzun  
  Date:   2021-01-23  

  Provided a support of variuos URL-schemes for res_musiconhold,
  registered by ast_bucket_scheme_register().

  ASTERISK-29262 #close


- ### AC_HEADER_STDC causes a compile failure with autoconf 2.70                      
  Author: Jaco Kroon  
  Date:   2021-01-08  

  From https://www.mail-archive.com/bug-autoconf@gnu.org/msg04408.html

  > ... the long-obsolete AC_HEADER_STDC, previously used internally by
  > AC_INCLUDES_DEFAULT, used AC_EGREP_HEADER.  The AC_HEADER_STDC macro
  > is now a no-op (and is not used at all within Autoconf anymore), so
  > that change is likely what made the first use of AC_EGREP_HEADER the
  > one inside the if condition, causing the observed results.

  The implication is that the test does nothing anyway, and due to it
  being a no-op from 2.70 onwards, results in the required not being set
  to yes, resulting in ./configure to fail.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### pjsip_scheduler: Fix pjsip show scheduled_tasks like for compiler Clang.        
  Author: Alexander Traud  
  Date:   2021-01-15  

  Otherwise, Clang 10 warned because of logical-not-parentheses.


- ### res_pjsip_session: Avoid sometimes-uninitialized warning with Clang.            
  Author: Alexander Traud  
  Date:   2021-01-15  

  ASTERISK-29248


- ### res_pjsip_pubsub: Fix truncation of persisted SUBSCRIBE packet                  
  Author: Sean Bright  
  Date:   2021-01-14  

  The last argument to ast_copy_string() is the buffer size, not the
  number of characters, so we add 1 to avoid stamping out the final \n
  in the persisted SUBSCRIBE message.


- ### chan_pjsip.c: Add parameters to frame in indicate.                              
  Author: Ben Ford  
  Date:   2021-01-11  

  There are a couple of parameters (datalen and data) that do not get set
  in chan_pjsip_indicate which could cause an Invalid message to pop up
  for things such as fax. This patch adds them to the frame.


- ### res/res_pjsip_session.c: Check that media type matches in function ast_sip_ses..
  Author: Robert Cripps  
  Date:   2020-12-22  

  Check ast_media_type matches when a ast_sip_session_media is found
  otherwise when transitioning from say image to audio, the wrong
  session is returned in the first if statement.

  ASTERISK-29220 #close


- ### Stasis/messaging: tech subscriptions conflict with endpoint subscriptions.      
  Author: Jean Aunis  
  Date:   2020-12-30  

  When both a tech subscription and an endpoint subscription exist for a given
  endpoint, TextMessageReceived events are dispatched to the tech subscription
  only.

  ASTERISK-29229


- ### chan_sip: SDP: Sidestep stream parsing when its media is disabled.              
  Author: Alexander Traud  
  Date:   2020-12-23  

  Previously, chan_sip parsed all known media streams in an SDP offer
  like video (and text) even when videosupport=no (and textsupport=no).
  This wasted processor power. Furthermore, chan_sip accepted SDP offers,
  including no audio but just video (or text) streams although
  videosupport=no (or textsupport=no). Finally, chan_sip denied the whole
  offer instead of individual streams when they had encryption (SDES-sRTP)
  unexpectedly enabled.

  ASTERISK-29238
  ASTERISK-29237
  ASTERISK-29222


- ### chan_pjsip: Assign SIPDOMAIN after creating a channel                           
  Author: Ivan Poddubnyi  
  Date:   2020-12-29  

  session->channel doesn't exist until chan_pjsip creates it, so intead of
  setting a channel variable every new incoming call sets one and the same
  global variable.

  This patch moves the code to chan_pjsip so that SIPDOMAIN is set on
  a newly created channel, it also removes a misleading reference to
  channel->session used to fetch call pickup configuraion.

  ASTERISK-29240


- ### chan_pjsip: Stop queueing control frames twice on outgoing channels             
  Author: Ivan Poddubnyi  
  Date:   2020-12-31  

  The fix for ASTERISK-27902 made chan_pjsip process SIP responses twice.
  This resulted in extra noise in logs (for example, "is making progress"
  and "is ringing" get logged twice by app_dial), as well as in noise in
  signalling: one incoming 183 Session Progress results in 2 outgoing 183-s.

  This change splits the response handler into 2 functions:
   - one for updating HANGUPCAUSE, which is still called twice,
   - another that does the rest, which is called only once as before.

  ASTERISK-28016
  Reported-by: Alex Hermann

  ASTERISK-28549
  Reported-by: Gant Liu

  ASTERISK-28185
  Reported-by: Julien


- ### contrib/systemd: Added note on common issues with systemd and asterisk          
  Author: Jaco Kroon  
  Date:   2020-12-18  

  With newer version of linux /var/run/ is a symlink to /run/ that has
  been turned into tmpfs.

  Added note that if asterisk has to bind to a specific IP that
  systemd has to wait until the network is up.

  Added note on how to make sure that the environment variable
  HOSTNAME is included.

  ASTERISK-29216
  Reported by: Mark Petersen
  Tested by: Mark Petersen


- ### Revert "res_pjsip_outbound_registration.c:  Use our own scheduler and other st..
  Author: George Joseph  
  Date:   2021-01-07  

  This reverts commit 2fe76dd816706f045ecbc44bf8ad6498977415b3.

  Reason for revert: Too many issues reported.  Need to research and correct.

  ASTERISK-29230
  ASTERISK-29231
  Reported by: Michael Maier


- ### func_lock: fix multiple-channel-grant problems.                                 
  Author: Jaco Kroon  
  Date:   2020-12-18  

  Under contention it becomes possible that multiple channels will be told
  they successfully obtained the lock, which is a bug.  Please refer

  ASTERISK-29217

  This introduces a couple of changes.

  1.  Replaces requesters ao2 container with simple counter (we don't
      really care who is waiting for the lock, only how many).  This is
      updated undex ->mutex to prevent memory access races.
  2.  Correct semantics for ast_cond_timedwait() as described in
      pthread_cond_broadcast(3P) is used (multiple threads can be released
      on a single _signal()).
  3.  Module unload races are taken care of and memory properly cleaned
      up.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### pbx_lua:  Add LUA_VERSIONS environment variable to ./configure.                 
  Author: Jaco Kroon  
  Date:   2020-12-23  

  On Gentoo it's possible to have multiple lua versions installed, all
  with a path of /usr, so it's not possible to use the current --with-lua
  option to determisticly pin to a specific version as is required by the
  Gentoo PMS standards.

  This environment variable allows to lock to specific versions,
  unversioned check will be skipped if this variable is supplied.

  Signed-off-by: Jaco Kroon <jaco@uls.co.za>

- ### app_mixmonitor: cleanup datastore when monitor thread fails to launch           
  Author: Kevin Harwell  
  Date:   2020-12-23  

  launch_monitor_thread is responsible for creating and initializing
  the mixmonitor, and dependent data structures. There was one off
  nominal path after the datastore gets created that triggers when
  the channel being monitored is hung up prior to monitor starting
  itself.

  If this happened the monitor thread would not "launch", and the
  mixmonitor object and associated objects are freed, including the
  underlying datastore data object. However, the datastore itself was
  not removed from the channel, so when the channel eventually gets
  destroyed it tries to access the previously freed datastore data
  and crashes.

  This patch removes and frees datastore object itself from the channel
  before freeing the mixmonitor object thus ensuring the channel does
  not call it when destroyed.

  ASTERISK-28947 #close


- ### app_voicemail: Prevent deadlocks when out of ODBC database connections          
  Author: Sean Bright  
  Date:   2020-12-24  

  ASTERISK-28992 #close


- ### chan_pjsip: Incorporate channel reference count into transfer_refer().          
  Author: Dan Cropp  
  Date:   2020-12-07  

  Add channel reference count for PJSIP REFER. The call could be terminated
  prior to the result of the transfer. In that scenario, when the SUBSCRIBE/NOTIFY
  occurred several minutes later, it would attempt to access a session which was
  no longer valid.  Terminate event subscription if pjsip_xfer_initiate() or
  pjsip_xfer_send_request() fails in transfer_refer().

  ASTERISK-29201 #close
  Reported-by: Dan Cropp


- ### pbx_realtime: wrong type stored on publish of ast_channel_snapshot_type         
  Author: Kevin Harwell  
  Date:   2020-12-22  

  A prior patch segmented channel snapshots, and changed the underlying
  data object type associated with ast_channel_snapshot_type stasis
  messages. Prior to Asterisk 18 it was a type ast_channel_snapshot, but
  now it type ast_channel_snapshot_update.

  When publishing ast_channel_snapshot_type in pbx_realtime the
  ast_channel_snapshot was being passed in as the message data
  object. When a handler, expecting a data object type of
  ast_channel_snapshot_update, dereferenced this value a crash
  would occur.

  This patch makes it so pbx_realtime now uses the expected type, and
  channel snapshot publish method when publishing.

  ASTERISK-29168 #close


- ### asterisk: Export additional manager functions                                   
  Author: Sean Bright  
  Date:   2020-12-18  

  Rename check_manager_enabled() and check_webmanager_enabled() to begin
  with ast_ so that the symbols are automatically exported by the
  linker.

  ASTERISK~29184


- ### res_pjsip: Prevent segfault in UDP registration with flow transports            
  Author: Nick French  
  Date:   2020-12-19  

  Segfault occurs during outbound UDP registration when all
  transport states are being iterated over. The transport object
  in the transport is accessed, but flow transports have a NULL
  transport object.

  Modify to not iterate over any flow transport

  ASTERISK-29210 #close


- ### codecs: Remove test-law.                                                        
  Author: Alexander Traud  
  Date:   2020-12-01  

  This was dead code, test code introduced with Asterisk 13. This was
  found while analyzing ASTERISK_28416 and ASTERISK_29185. This change
  partly fixes, not closes those two issues.


- ### res/res_pjsip_diversion: prevent crash on tel: uri in History-Info              
  Author: Torrey Searle  
  Date:   2020-12-22  

  Add a check to see if the URI is a Tel URI and prevent crashing on
  trying to retrieve the reason parameter.

  ASTERISK-29191
  ASTERISK-29219


- ### chan_vpb.cc: Fix compile errors.                                                
  Author: Richard Mudgett  
  Date:   2020-12-26  

  Fix the usual compile problem when someone adds a new callback to struct
  ast_channel_tech.


- ### res_pjsip_session.c: Fix compiler warnings.                                     
  Author: Richard Mudgett  
  Date:   2020-12-26  

  AST_VECTOR_SIZE() returns a size_t.  This is not always equivalent to an
  unsigned long on all machines.


- ### res_pjsip_session: Fixed NULL active media topology handle                      
  Author: Sungtae Kim  
  Date:   2020-12-13  

  Added NULL pointer check to prevent Asterisk crash.

  ASTERISK-29215


- ### app_chanspy: Spyee information missing in ChanSpyStop AMI Event                 
  Author: Sean Bright  
  Date:   2020-12-11  

  The documentation in the wiki says there should be spyee-channel
  information elements in the ChanSpyStop AMI event.

      https://wiki.asterisk.org/wiki/x/Xc5uAg

  However, this is not the case in Asterisk <= 16.10.0 Version. We're
  using these Spyee* arguments since Asterisk 11.x, so these arguments
  vanished in Asterisk 12 or higher.

  For maximum compatibility, we still send the ChanSpyStop event even if
  we are not able to find any 'Spyee' information.

  ASTERISK-28883 #close


- ### res_ari: Fix wrong media uri handle for channel play                            
  Author: Sungtae Kim  
  Date:   2020-12-01  

  Fixed wrong null object handle in
  /channels/<channel_id>/play request handler.

  ASTERISK-29188


- ### logger.c: Automatically add a newline to formats that don't have one            
  Author: George Joseph  
  Date:   2020-12-10  

  Scope tracing allows you to not specify a format string or variable,
  in which case it just prints the indent, file, function, and line
  number.  The trace output automatically adds a newline to the end
  in this case.  If you also have debugging turned on for the module,
  a debug message is also printed but the standard log functionality
  which prints it doesn't add the newline so you have messages
  that don't break correctly.

   * format_log_message_ap(), which is the common log
     message formatter for all channels, now adds a
     newline to the end of format strings that don't
     already have a newline.

  ASTERISK-29209
  Reported by: Alexander Traud


- ### res_pjsip_nat.c: Create deep copies of strings when appropriate                 
  Author: Pirmin Walthert  
  Date:   2020-12-08  

  In rewrite_uri asterisk was not making deep copies of strings when
  changing the uri. This was in some cases causing garbage in the route
  header and in other cases even crashing asterisk when receiving a
  message with a record-route header set. Thanks to Ralf Kubis for
  pointing out why this happens. A similar problem was found in
  res_pjsip_transport_websocket.c. Pjproject needs as well to be patched
  to avoid garbage in CANCEL messages.

  ASTERISK-29024 #close


- ### res_musiconhold: Don't crash when real-time doesn't return any entries          
  Author: Nathan Bruning  
  Date:   2020-12-11  

  ASTERISK-29211 #close


- ### res_pjsip_pidf_digium_body_supplement: Support Sangoma user agent.              
  Author: Joshua C. Colp  
  Date:   2020-12-16  

  This adds support for both Digium and Sangoma user agent strings
  for the Sangoma specific body supplement.


- ### pjsip: Match lifetime of INVITE session to our session.                         
  Author: Joshua C. Colp  
  Date:   2020-10-29  

  In some circumstances it was possible for an INVITE
  session to be destroyed while we were still using it.
  This occurred due to the reference on the INVITE session
  being released internally as a result of its state
  changing to DISCONNECTED.

  This change adds a reference to the INVITE session
  which is released when our own session is destroyed,
  ensuring that the INVITE session remains valid for
  the lifetime of our session.

  ASTERISK-29022


- ### res_http_media_cache.c: Set reasonable number of redirects                      
  Author: Sean Bright  
  Date:   2020-11-21  

  By default libcurl does not follow redirects, so we explicitly enable
  it by setting CURLOPT_FOLLOWLOCATION. Once that is enabled, libcurl
  will follow up to CURLOPT_MAXREDIRS redirects, which by default is
  configured to be unlimited.

  This patch sets CURLOPT_MAXREDIRS to a more reasonable default (8). If
  we determine at some point that this needs to be increased on
  configurable it is a trivial change.

  ASTERISK-29173 #close


- ### Introduce astcachedir, to be used for temporary bucket files                    
  Author: lvl  
  Date:   2020-10-29  

  As described in the issue, /tmp is not a suitable location for a
  large amount of cached media files, since most distributions make
  /tmp a RAM-based tmpfs mount with limited capacity.

  I opted for a location that can be configured separately, as opposed
  to using a subdirectory of spooldir, given the different storage
  profile (transient files vs files that might stay there indefinitely).

  This commit just makes the cache directory configurable, and changes
  the default location from /tmp to /var/cache/asterisk.

  ASTERISK-29143


- ### media_cache: Fix reference leak with bucket file metadata                       
  Author: Sean Bright  
  Date:   2020-11-23  


- ### res_pjsip_stir_shaken: Fix module description                                   
  Author: Stanislav  
  Date:   2020-11-24  

  the 'J' is missing in module description.
  "PSIP STIR/SHAKEN Module for Asterisk" -> "PJSIP STIR/SHAKEN Module for Asterisk"

  ASTERISK-29175 #close


- ### voicemail: add option 'e' to play greetings as early media                      
  Author: Joshua C. Colp  
  Date:   2020-10-12  

  When using this option, answering the channel is deferred until
  all prompts/greetings have been played and the caller is about
  to leave their message.

  ASTERISK-29118 #close


- ### loader: Sync load- and build-time deps.                                         
  Author: Alexander Traud  
  Date:   2020-11-02  

  In MODULEINFO, each depend has to be listed in .requires of AST_MODULE_INFO.

  ASTERISK-29148


- ### CHANGES: Remove already applied CHANGES update                                  
  Author: Sean Bright  
  Date:   2020-11-18  


- ### res_pjsip: set Accept-Encoding to identity in OPTIONS response                  
  Author: Alexander Greiner-Baer  
  Date:   2020-11-17  

  RFC 3261 says that the Accept-Encoding header should be present
  in an options response. Permitted values according to RFC 2616
  are only compression algorithms like gzip or the default identity
  encoding. Therefore "text/plain" is not a correct value here.
  As long as the header is hard coded, it should be set to "identity".

  Without this fix an Alcatel OmniPCX periodically logs warnings like
  "[sip_acceptIncorrectHeader] Header Accept-Encoding is malformed"
  on a SIP Trunk.

  ASTERISK-29165 #close


- ### chan_sip: Remove unused sip_socket->port.                                       
  Author: Alexander Traud  
  Date:   2020-11-04  

  12 years ago, with ASTERISK_12115 the last four get/uses of socket.port
  vanished. However, the struct member itself and all seven set/uses
  remained as dead code.

  ASTERISK-28798


- ### bridge_basic: Fixed setup of recall channels                                    
  Author: Boris P. Korzun  
  Date:   2020-11-13  

  Fixed a bug (like a typo) in retransfer_enter() at main/bridge_basic.c:2641.
  common_recall_channel_setup() setups common things on the recalled transfer
  target, but used same target as source instead trasfered.

  ASTERISK-29161 #close


- ### modules.conf: Align the comments for more conclusiveness.                       
  Author: Alexander Traud  
  Date:   2020-11-03  


- ### app_queue: Fix deadlock between update and show queues                          
  Author: George Joseph  
  Date:   2020-11-11  

  Operations that update queues when shared_lastcall is set lock the
  queue in question, then have to lock the queues container to find the
  other queues with the same member. On the other hand, __queues_show
  (which is called by both the CLI and AMI) does the reverse. It locks
  the queues container, then iterates over the queues locking each in
  turn to display them.  This creates a deadlock.

  * Moved queue print logic from __queues_show to a separate function
    that can be called for a single queue.

  * Updated __queues_show so it doesn't need to lock or traverse
    the queues container to show a single queue.

  * Updated __queues_show to snap a copy of the queues container and iterate
    over that instead of locking the queues container and iterating over
    it while locked.  This prevents us from having to hold both the
    container lock and the queue locks at the same time.  This also
    allows us to sort the queue entries.

  ASTERISK-29155


- ### res_pjsip_outbound_registration.c:  Use our own scheduler and other stuff       
  Author: George Joseph  
  Date:   2020-11-02  

  * Instead of using the pjproject timer heap, we now use our own
    pjsip_scheduler.  This allows us to more easily debug and allows us to
    see times in "pjsip show/list registrations" as well as being able to
    see the registrations in "pjsip show scheduled_tasks".

  * Added the last registration time, registration interval, and the next
    registration time to the CLI output.

  * Removed calls to pjsip_regc_info() except where absolutely necessary.
    Most of the calls were just to get the server and client URIs for log
    messages so we now just save them on the client_state object when we
    create it.

  * Added log messages where needed and updated most of the existong ones
    to include the registration object name at the start of the message.


- ### pjsip_scheduler.c: Add type ONESHOT and enhance cli show command                
  Author: George Joseph  
  Date:   2020-11-02  

  * Added a ONESHOT type that never reschedules.

  * Added "like" capability to "pjsip show scheduled_tasks" so you can do
    the following:

    CLI> pjsip show scheduled_tasks like outreg
    PJSIP Scheduled Tasks:

    Task Name                                     Interval  Times Run ...
    ============================================= ========= ========= ...
    pjsip/outreg/testtrunk-reg-0-00000074            50.000   oneshot ...
    pjsip/outreg/voipms-reg-0-00000073              110.000   oneshot ...

  * Fixed incorrect display of "Next Start".

  * Compacted the displays of times in the CLI.

  * Added two new functions (ast_sip_sched_task_get_times2,
    ast_sip_sched_task_get_times_by_name2) that retrieve the interval,
    next start time, and next run time in addition to the times already
    returned by ast_sip_sched_task_get_times().


- ### sched: AST_SCHED_REPLACE_UNREF can lead to use after free of data               
  Author: Alexei Gradinari  
  Date:   2020-10-02  

  The data can be freed if the old object '_data' is the same object as
  new 'data'. Because at first the object is unreferenced which can lead
  to destroying it.

  This could happened in res_pjsip_pubsub when the publication is updated
  which could lead to segfault in function publish_expire.


- ### res_pjsip/config_transport: Load and run without OpenSSL.                       
  Author: Alexander Traud  
  Date:   2020-10-30  

  ASTERISK-28933
  Reported-by: Walter Doekes


- ### res_stir_shaken: Include OpenSSL headers where used actually.                   
  Author: Alexander Traud  
  Date:   2020-10-30  

  This avoids the inclusion of the OpenSSL headers in the public header,
  which avoids one external library dependency in res_pjsip_stir_shaken.


- ### func_curl.c: Allow user to set what return codes constitute a failure.          
  Author: Dovid Bender  
  Date:   2020-10-18  

  Currently any response from res_curl where we get an answer from the
  web server, regardless of what the response is (404, 403 etc.) Asterisk
  currently treats it as a success. This patch allows you to set which
  codes should be considered as a failure by Asterisk. If say we set
  failurecodes=404,403 then when using curl in realtime if a server gives
  a 404 error Asterisk will try to failover to the next option set in
  extconfig.conf

  ASTERISK-28825

  Reported by: Dovid Bender
  Code by: Gobinda Paul


- ### AST-2020-001 - res_pjsip: Return dialog locked and referenced                   
  Author: Kevin Harwell  
  Date:   2020-11-04  

  pjproject returns the dialog locked and with a reference. However,
  in Asterisk the method that handles this decrements the reference
  and removes the lock prior to returning. This makes it possible,
  under some circumstances, for another thread to free said dialog
  before the thread that created it attempts to use it again. Of
  course when the thread that created it tries to use a freed dialog
  a crash can occur.

  This patch makes it so Asterisk now returns the newly created
  dialog both locked, and with an added reference. This allows the
  caller to de-reference, and unlock the dialog when it is safe to
  do so.

  In the case of a new SIP Invite the lock, and reference are now
  held for the entirety of the new invite handling process.
  Otherwise it's possible for the dialog, or its dependent objects,
  like the transaction, to disappear. For example if there is a TCP
  transport error.

  ASTERISK-29057 #close


- ### AST-2020-002 - res_pjsip: Stop sending INVITEs after challenge limit.           
  Author: Ben Ford  
  Date:   2020-11-03  

  If Asterisk sends out and INVITE and receives a challenge with a
  different nonce value each time, it will continually send out INVITEs,
  even if the call is hung up. The endpoint must be configured for
  outbound authentication in order for this to occur. A limit has been set
  on outbound INVITEs so that, once reached, Asterisk will stop sending
  INVITEs and the transaction will terminate.

  ASTERISK-29013


- ### sip_to_pjsip.py: Handle #include globs and other fixes                          
  Author: Sean Bright  
  Date:   2020-10-29  

  * Wildcards in #includes are now properly expanded

  * Implement operators for Section class to allow sorting

  ASTERISK-29142 #close


- ### Compiler fixes for GCC with -Og                                                 
  Author: Alexander Traud  
  Date:   2020-10-29  

  ASTERISK-29144


- ### Compiler fixes for GCC when printf %s is NULL                                   
  Author: Alexander Traud  
  Date:   2020-10-30  

  ASTERISK-29146


- ### Compiler fixes for GCC with -Os                                                 
  Author: Alexander Traud  
  Date:   2020-10-29  

  ASTERISK-29145


- ### chan_sip: On authentication, pick MD5 for sure.                                 
  Author: Alexander Traud  
  Date:   2020-10-23  

  RFC 8760 added new digest-access-authentication schemes. Testing
  revealed that chan_sip does not pick MD5 if several schemes are offered
  by the User Agent Server (UAS). This change does not implement any of
  the new schemes like SHA-256. This change makes sure, MD5 is picked so
  UAS with SHA-2 enabled, like the service www.linphone.org/freesip, can
  still be used. This should have worked since day one because SIP/2.0
  already envisioned several schemes (see RFC 3261 and its augmented BNF
  for 'algorithm' which includes 'token' as third alternative; note: if
  'algorithm' was not present, MD5 is still assumed even in RFC 7616).


- ### main/say: Work around gcc 9 format-truncation false positive                    
  Author: Walter Doekes  
  Date:   2020-06-04  

  Version: gcc (Ubuntu 9.3.0-10ubuntu2) 9.3.0
  Warning:
    say.c:2371:24: error: ‘%d’ directive output may be truncated writing
      between 1 and 11 bytes into a region of size 10
      [-Werror=format-truncation=]
    2371 |     snprintf(buf, 10, "%d", num);
    say.c:2371:23: note: directive argument in the range [-2147483648, 9]

  That's not possible though, as the if() starts out checking for (num < 0),
  making this Warning a false positive.

  (Also replaced some else<TAB>if with else<SP>if while in the vicinity.)


- ### res_pjsip, res_pjsip_session: initialize local variables                        
  Author: Kevin Harwell  
  Date:   2020-10-19  

  This patch initializes a couple of local variables to some default values.
  Interestingly, in the 'pj_status_t dlg_status' case the value not being
  initialized caused memory to grow, and not be recovered, in the off nominal
  path (at least on my machine).


- ### install_prereq: Add GMime 3.0.                                                  
  Author: Alexander Traud  
  Date:   2020-10-23  

  Ubuntu 20.10 does not come with GMime 2.6. Ubuntu 16.04 LTS does not
  come with GMime 3.0. aptitude ignores any missing package. Therefore,
  it installs the correct package(s). However, in Ubuntu 18.04 LTS and
  Ubuntu 20.04 LTS, both versions are installed alongside although only
  one is really needed.


- ### BuildSystem: Enable Lua 5.4.                                                    
  Author: Alexander Traud  
  Date:   2020-10-23  

  Note to maintainers: Lua 5.4, Lua 5.3, and Lua 5.2 have not been tested
  at runtime with pbx_lua. Until then, use the lowest available version
  of Lua, if you enabled the module pbx_lua at all.


- ### res_pjsip_session: Restore calls to ast_sip_message_apply_transport()           
  Author: Nick French  
  Date:   2020-10-13  

  Commit 44bb0858cb3ea6a8db8b8d1c7fedcfec341ddf66 ("debugging: Add enough
  to choke a mule") accidentally removed calls to
  ast_sip_message_apply_transport when it was attempting to just add
  debugging code.

  The kiss of death was saying that there were no functional changes in
  the commit comment.

  This makes outbound calls that use the 'flow' transport mechanism fail,
  since this call is used to relay headers into the outbound INVITE
  requests.

  ASTERISK-29124 #close


- ### features.conf.sample: Sample sound files incorrectly quoted                     
  Author: Sean Bright  
  Date:   2020-10-22  

  ASTERISK-29136 #close


- ### logger.conf.sample: add missing comment mark                                    
  Author: Andrew Siplas  
  Date:   2020-10-12  

  Add missing comment mark from stock configuration.

  ASTERISK-29123 #close


- ### res_pjsip: Adjust outgoing offer call pref.                                     
  Author: Joshua C. Colp  
  Date:   2020-10-06  

  This changes the outgoing offer call preference
  default option to match the behavior of previous
  versions of Asterisk.

  The additional advanced codec negotiation options
  have also been removed from the sample configuration
  and marked as reserved for future functionality in
  XML documentation.

  The codec preference options have also been fixed to
  enforce local codec configuration.

  ASTERISK-29109


- ### tcptls.c: Don't close TCP client file descriptors more than once                
  Author: Sean Bright  
  Date:   2020-09-30  

  ASTERISK-28430 #close


- ### resource_endpoints.c: memory leak when providing a 404 response                 
  Author: Jean Aunis  
  Date:   2020-10-05  

  When handling a send_message request to a non-existing endpoint, the response's
  body is overriden and not properly freed.

  ASTERISK-29108


- ### Logging: Add debug logging categories                                           
  Author: Kevin Harwell  
  Date:   2020-08-28  

  Added debug logging categories that allow a user to output debug
  information based on a specified category. This lets the user limit,
  and filter debug output to data relevant to a particular context,
  or topic. For instance the following categories are now available for
  debug logging purposes:

    dtls, dtls_packet, ice, rtcp, rtcp_packet, rtp, rtp_packet,
    stun, stun_packet

  These debug categories can be enable/disable via an Asterisk CLI command.

  While this overrides, and outputs debug data, core system debugging is
  not affected by this patch. Statements still output at their appropriate
  debug level. As well backwards compatibility has been maintained with
  past debug groups that could be enabled using the CLI (e.g. rtpdebug,
  stundebug, etc.).

  ASTERISK-29054 #close


- ### pbx.c: On error, ast_add_extension2_lockopt should always free 'data'           
  Author: Sean Bright  
  Date:   2020-09-29  

  In the event that the desired extension already exists,
  ast_add_extension2_lockopt() will free the 'data' it is passed before
  returning an error, so we should not be freeing it ourselves.

  Additionally, there were two places where ast_add_extension2_lockopt()
  could return an error without also freeing the 'data' pointer, so we
  add that.

  ASTERISK-29097 #close


- ### app_confbridge/bridge_softmix:  Add ability to force estimated bitrate          
  Author: George Joseph  
  Date:   2020-09-24  

  app_confbridge now has the ability to set the estimated bitrate on an
  SFU bridge.  To use it, set a bridge profile's remb_behavior to "force"
  and set remb_estimated_bitrate to a rate in bits per second.  The
  remb_estimated_bitrate parameter is ignored if remb_behavior is something
  other than "force".


- ### app_voicemail.c: Document VMSayName interruption behavior                       
  Author: Sean Bright  
  Date:   2020-09-29  

  ASTERISK-26424 #close


- ### res_pjsip_sdp_rtp: Fix accidentally native bridging calls                       
  Author: Holger Hans Peter Freyther  
  Date:   2020-09-23  

  Stop advertising RFC2833 support on the rtp_engine when DTMF mode is
  auto but no tel_event was found inside SDP file.

  On an incoming call create_rtp will be called and when session->dtmf is
  set to AST_SIP_DTMF_AUTO, the AST_RTP_PROPERTY_DTMF will be set without
  looking at the SDP file.

  Once get_codecs gets called we move the DTMF mode from RFC2833 to INBAND
  but continued to advertise RFC2833 support.

  This meant the native_rtp bridge would falsely consider the two channels
  as compatible. In addition to changing the DTMF mode we now set or
  remove the AST_RTP_PROPERTY_DTMF.

  The property is checked in ast_rtp_dtmf_compatible and called by
  native_rtp_bridge_compatible.

  ASTERISK-29051 #close


- ### res_musiconhold: Load all realtime entries, not just the first                  
  Author: lvl  
  Date:   2020-09-28  

  ASTERISK-29099


- ### channels: Don't dereference NULL pointer                                        
  Author: Jasper van der Neut  
  Date:   2020-09-23  

  Check result of ast_translator_build_path against NULL before dereferencing.

  ASTERISK-29091


- ### res_pjsip_diversion: fix double 181                                             
  Author: Torrey Searle  
  Date:   2020-09-24  

  Arming response to both AST_SIP_SESSION_BEFORE_REDIRECTING and
  AST_SIP_SESSION_BEFORE_MEDIA causes 302 to to be handled twice,
  resulting in to 181 being generated.


- ### res_musiconhold: Clarify that playlist mode only supports HTTP(S) URLs          
  Author: Sean Bright  
  Date:   2020-09-24  


- ### dsp.c: Update calls to ast_format_cmp to check result properly                  
  Author: Sean Bright  
  Date:   2020-09-23  

  ASTERISK-28311 #close


- ### res_pjsip_session: Fix stream name memory leak.                                 
  Author: Joshua C. Colp  
  Date:   2020-09-22  

  When constructing a stream name based on the media type
  and position the allocated name was not being freed
  causing a leak.


- ### func_curl.c: Prevent crash when using CURLOPT(httpheader)                       
  Author: Sean Bright  
  Date:   2020-09-18  

  Because we use shared thread-local cURL instances, we need to ensure
  that the state of the cURL instance is correct before each invocation.

  In the case of custom headers, we were not resetting cURL's internal
  HTTP header pointer which could result in a crash if subsequent
  requests do not configure custom headers.

  ASTERISK-29085 #close


- ### res_musiconhold: Start playlist after initial announcement                      
  Author: Sean Bright  
  Date:   2020-09-18  

  Only track our sample offset if we are playing a non-announcement file,
  otherwise we will skip that number of samples when we start playing the
  first MoH file.

  ASTERISK-24329 #close


- ### res_pjsip_session: Fix session reference leak.                                  
  Author: Joshua C. Colp  
  Date:   2020-09-22  

  The ast_sip_dialog_get_session function returns the session
  with reference count increased. This was not taken into
  account and was causing sessions to remain around when they
  should not be.

  ASTERISK-29089


- ### res_stasis.c: Add compare function for bridges moh container                    
  Author: Michal Hajek  
  Date:   2020-09-16  

  Sometimes not play MOH on bridge.

  ASTERISK-29081
  Reported-by: Michal Hajek <michal.hajek@daktela.com>


- ### logger.h: Fix ast_trace to respect scope_level                                  
  Author: George Joseph  
  Date:   2020-09-17  

  ast_trace() was always emitting messages when it's level was set to -1
  because it was ignoring scope_level.


- ### chan_sip.c: Don't build by default                                              
  Author: Sean Bright  
  Date:   2020-09-15  

  ASTERISK-29083 #close


- ### audiosocket: Fix module menuselect descriptions                                 
  Author: Sean Bright  
  Date:   2020-09-15  

  The module description needs to be on the same line as the
  AST_MODULE_INFO or it is not parsed correctly.


- ### bridge_softmix/sfu_topologies_on_join: Ignore topology change failures          
  Author: George Joseph  
  Date:   2020-09-17  

  When a channel joins a bridge, we do topology change requests on all
  existing channels to add the new participant to them.  However the
  announcer channel will return an error because it doesn't support
  topology in the first place.  Unfortunately, there doesn't seem to be a
  reliable way to tell if the error is expected or not so the error is
  ignored for all channels.  If the request fails on a "real" channel,
  that channel just won't get the new participant's video.


- ### res_pjsip_session.c: Fix build when TEST_FRAMEWORK is not defined               
  Author: Sean Bright  
  Date:   2020-09-15  


- ### res_pjsip_diversion: implement support for History-Info                         
  Author: Torrey Searle  
  Date:   2020-08-13  

  Implemention of History-Info capable of interworking with Diversion
  Header following RFC7544

  ASTERISK-29027 #close


- ### format_cap: Perform codec lookups by pointer instead of name                    
  Author: Sean Bright  
  Date:   2020-09-14  

  ASTERISK-28416 #close


- ### res_pjsip_session: Fix issue with COLP and 491                                  
  Author: George Joseph  
  Date:   2020-09-11  

  The recent 491 changes introduced a check to determine if the active
  and pending topologies were equal and to suppress the re-invite if they
  were. When a re-invite is sent for a COLP-only change, the pending
  topology is NULL so that check doesn't happen and the re-invite is
  correctly sent. Of course, sending the re-invite sets the pending
  topology.  If a 491 is received, when we resend the re-invite, the
  pending topology is set and since we didn't request a change to the
  topology in the first place, pending and active topologies are equal so
  the topologies-equal check causes the re-invite to be erroneously
  suppressed.

  This change checks if the topologies are equal before we run the media
  state resolver (which recreates the pending topology) so that when we
  do the final topologies-equal check we know if this was a topology
  change request.  If it wasn't a change request, we don't suppress
  the re-invite even though the topologies are equal.

  ASTERISK-29014


- ### debugging:  Add enough to choke a mule                                          
  Author: George Joseph  
  Date:   2020-08-20  

  Added to:
   * bridges/bridge_softmix.c
   * channels/chan_pjsip.c
   * include/asterisk/res_pjsip_session.h
   * main/channel.c
   * res/res_pjsip_session.c

  There NO functional changes in this commit.


- ### res_pjsip_session:  Handle multi-stream re-invites better                       
  Author: George Joseph  
  Date:   2020-08-20  

  When both Asterisk and a UA send re-invites at the same time, both
  send 491 "Transaction in progress" responses to each other and back
  off a specified amount of time before retrying. When Asterisk
  prepares to send its re-invite, it sets up the session's pending
  media state with the new topology it wants, then sends the
  re-invite.  Unfortunately, when it received the re-invite from the
  UA, it partially processed the media in the re-invite and reset
  the pending media state before sending the 491 losing the state it
  set in its own re-invite.

  Asterisk also was not tracking re-invites received while an existing
  re-invite was queued resulting in sending stale SDP with missing
  or duplicated streams, or no re-invite at all because we erroneously
  determined that a re-invite wasn't needed.

  There was also an issue in bridge_softmix where we were using a stream
  from the wrong topology to determine if a stream was added.  This also
  caused us to erroneously determine that a re-invite wasn't needed.

  Regardless of how the delayed re-invite was triggered, we need to
  reconcile the topology that was active at the time the delayed
  request was queued, the pending topology of the queued request,
  and the topology currently active on the session.  To do this we
  need a topology resolver AND we need to make stream named unique
  so we can accurately tell what a stream has been added or removed
  and if we can re-use a slot in the topology.

  Summary of changes:

   * bridge_softmix:
     * We no longer reset the stream name to "removed" in
       remove_all_original_streams().  That was causing  multiple streams
       to have the same name and wrecked the checks for duplicate streams.

     * softmix_bridge_stream_sources_update() was checking the old_stream
       to see if it had the softmix prefix and not considering the stream
       as "new" if it did.  If the stream in that slot has something in it
       because another re-invite happened, then that slot in old might
       have a softmix stream but the same stream in new might actually
       be a new one.  Now we check the new_stream's name instead of
       the old_stream's.

   * stream:
     * Instead of using plain media type name ("audio", "video", etc) as
       the default stream name, we now append the stream position to it
       to make it unique.  We need to do this so we can distinguish multiple
       streams of the same type from each other.

     * When we set a stream's state to REMOVED, we no longer reset its
       name to "removed" or destroy its metadata.  Again, we need to
       do this so we can distinguish multiple streams of the same
       type from each other.

   * res_pjsip_session:
     * Added resolve_refresh_media_states() that takes in 3 media states
       and creates an up-to-date pending media state that includes the changes
       that might have happened while a delayed session refresh was in the
       delayed queue.

     * Added is_media_state_valid() that checks the consistency of
       a media state and returns a true/false value. A valid state has:
       * The same number of stream entries as media session entries.
           Some media session entries can be NULL however.
       * No duplicate streams.
       * A valid stream for each non-NULL media session.
       * A stream that matches each media session's stream_num
         and media type.

     * Updated handle_incoming_sdp() to set the stream name to include the
       stream position number in the name to make it unique.

     * Updated the ast_sip_session_delayed_request structure to include both
       the pending and active media states and updated the associated delay
       functions to process them.

     * Updated sip_session_refresh() to accept both the pending and active
       media states that were in effect when the request was originally queued
       and to pass them on should the request need to be delayed again.

     * Updated sip_session_refresh() to call resolve_refresh_media_states()
       and substitute its results for the pending state passed in.

     * Updated sip_session_refresh() with additional debugging.

     * Updated session_reinvite_on_rx_request() to simply return PJ_FALSE
       to pjproject if a transaction is in progress.  This stops us from
       creating a partial pending media state that would be invalid later on.

     * Updated reschedule_reinvite() to clone both the current pending and
       active media states and pass them to delay_request() so the resolver
       can tell what the original intention of the re-invite was.

     * Added a large unit test for the resolver.

  ASTERISK-29014


- ### realtime: Increased reg_server character size                                   
  Author: Sungtae Kim  
  Date:   2020-08-31  

  Currently, the ps_contacts table's reg_server column in realtime database type is varchar(20).
  This is fine for normal cases, but if the hostname is longer than 20, it returns error and then
  failed to register the contact address of the peer.

  Normally, 20 characters limitation for the hostname is fine, but with the cloud env.
  So, increased the size to 255.

  ASTERISK-29056


- ### res_stasis.c: Added video_single option for bridge creation                     
  Author: Sungtae Kim  
  Date:   2020-08-30  

  Currently, it was not possible to create bridge with video_mode single.
  This made hard to put the bridge in a vidoe_single mode.
  So, added video_single option for Bridge creation using the ARI.
  This allows create a bridge with video_mode single.

  ASTERISK-29055


- ### Bridging: Use a ref to bridge_channel's channel to prevent crash.               
  Author: Ben Ford  
  Date:   2020-08-31  

  There's a race condition with bridging where a bridge can be torn down
  causing the bridge_channel's ast_channel to become NULL when it's still
  needed. This particular case happened with attended transfers, but the
  crash occurred when trying to publish a stasis message. Now, the
  bridge_channel is locked, a ref to the ast_channel is obtained, and that
  ref is passed down the chain.


- ### res_pjsip_session: Deferred re-INVITE without SDP send a=sendrecv instead of a..
  Author: Patrick Verzele  
  Date:   2020-09-01  

  Building on ASTERISK-25854. When the device requests hold by sending SDP with attribute recvonly, asterisk places the session in sendonly mode. When the device later requests to resume the call by using a re-INVITE excluding SDP, asterisk needs to change the sendonly mode to sendrecv again.


- ### conversions: Add string to signed integer conversion functions                  
  Author: Kevin Harwell  
  Date:   2020-08-28  


- ### app_queue: Fix leave-empty not recording a call as abandoned                    
  Author: Kfir Itzhak  
  Date:   2020-08-26  

  This fixes a bug introduced mistakenly in ASTERISK-25665:
  If leave-empty is enabled, a call may sometimes be removed from
  a queue without recording it as abandoned.
  This causes Asterisk to not generate an abandon event for that
  call, and for the queue abandoned counter to be incorrect.

  ASTERISK-29043 #close


- ### ast_coredumper: Fix issues with naming                                          
  Author: George Joseph  
  Date:   2020-08-28  

  If you run ast_coredumper --tarball-coredumps in the same directory
  as the actual coredump, tar can fail because the link to the
  actual coredump becomes recursive.  The resulting tarball will
  have everything _except_ the coredump (which is usually what
  you need)

  There's also an issue that the directory name in the tarball
  is the same as the coredump so if you extract the tarball the
  directory it creates will overwrite the coredump.

  So:

   * Made the link to the coredump use the absolute path to the
     file instead of a relative one.  This prevents the recursive
     link and allows tar to add the coredump.

   * The tarballed directory is now named <coredump>.output instead
     of just <coredump> so if you expand the tarball it won't
     overwrite the coredump.


- ### parking: Copy parker UUID as well.                                              
  Author: Joshua C. Colp  
  Date:   2020-08-28  

  When fixing issues uncovered by GCC10 a copy of the parker UUID
  was removed accidentally. This change restores it so that the
  subscription has the data it needs.

  ASTERISK-29042


- ### sip_nat_settings: Update script for latest Linux.                               
  Author: Alexander Traud  
  Date:   2020-08-26  

  With the latest Linux, 'ifconfig' is not installed on default anymore.
  Furthermore, the output of the current net-tools 'ifconfig' changed.
  Therefore, parsing failed. This update uses 'ip addr show' instead.
  Finally, the service for the external IP changed.


- ### samples: Fix keep_alive_interval default in pjsip.conf.                         
  Author: Alexander Traud  
  Date:   2020-08-26  

  Since ASTERISK_27978 the default is not off but 90 seconds. That change
  happened because ASTERISK_27347 disabled the keep-alives in the bundled
  PJProject and Asterisk should behave the same as before.


- ### chan_pjsip: disallow PJSIP_SEND_SESSION_REFRESH pre-answer execution            
  Author: Kevin Harwell  
  Date:   2020-08-24  

  This patch makes it so if the PJSIP_SEND_SESSION_REFRESH dialplan function
  is called on a channel prior to answering a warning is issued and the
  function returns unsuccessful.

  ASTERISK-28878 #close


- ### pbx: Fix hints deadlock between reload and ExtensionState.                      
  Author: Joshua C. Colp  
  Date:   2020-08-27  

  When the ExtensionState AMI action is executed on a pattern matched
  hint it can end up adding a new hint if one does not already exist.
  This results in a locking order of contexts -> hints -> contexts.

  If at the same time a reload is occurring and adding its own hint
  it will have a locking order of hints -> contexts.

  This results in a deadlock as one thread wants a lock on contexts
  that the other has, and the other thread wants a lock on hints
  that the other has.

  This change enforces a hints -> contexts locking order by explicitly
  locking hints in the places where a hint is added when queried for.
  This matches the order seen through normal adding of hints.

  ASTERISK-29046


- ### logger.c: Added a new log formatter called "plain"                              
  Author: George Joseph  
  Date:   2020-08-14  

  Added a new log formatter called "plain" that always prints
  file, function and line number if available (even for verbose
  messages) and never prints color control characters.  It also
  doesn't apply any special formatting for verbose messages.
  Most suitable for file output but can be used for other channels
  as well.

  You use it in logger.conf like so:
  debug => [plain]debug
  console => [plain]error,warning,debug,notice,pjsip_history
  messages => [plain]warning,error,verbose


- ### res_speech: Bump reference on format object                                     
  Author: Nickolay Shmyrev  
  Date:   2020-08-21  

  Properly bump reference on format object to avoid memory corruption on double free

  ASTERISK-29040 #close


- ### res_pjsip_diversion: handle 181                                                 
  Author: Torrey Searle  
  Date:   2020-07-22  

  Adapt the response handler so it also called when 181 is received.
  In the case 181 is received, also generate the 181 response.

  ASTERISK-29001 #close


- ### app_voicemail: Process urgent messages with mailcmd                             
  Author: Sean Bright  
  Date:   2020-08-21  

  Rather than putting messages into INBOX and then moving them to Urgent
  later, put them directly in to the Urgent folder. This prevents
  mailcmd from being skipped.

  ASTERISK-27273 #close


- ### app_queue: Member lastpause time reseting                                       
  Author: Evandro César Arruda  
  Date:   2020-08-21  

  This fixes the reseting members lastpause problem when realtime members is being used,
  the function rt_handle_member_record was forcing the reset members lastpause because it
  does not exist in realtime

  ASTERISK-29034 #close


- ### res_pjsip_session: Don't aggressively terminate on failed re-INVITE.            
  Author: Joshua C. Colp  
  Date:   2020-08-18  

  Per the RFC when an outgoing re-INVITE is done we should
  only terminate the dialog if a 481 or 408 is received.

  ASTERISK-29033


- ### bridge_channel: Ensure text messages are zero terminated                        
  Author: Sean Bright  
  Date:   2020-08-19  

  T.140 data in RTP is not zero terminated, so when we are queuing a text
  frame on a bridge we need to ensure that we are passing a zero
  terminated string.

  ASTERISK-28974 #close


- ### res_musiconhold.c: Use ast_file_read_dir to scan MoH directory                  
  Author: Sean Bright  
  Date:   2020-08-07  

  Two changes of note in this patch:

  * Use ast_file_read_dir instead of opendir/readdir/closedir

  * If the files list should be sorted, do that at the end rather than as
    we go which improves performance for large lists


- ### scope_trace: Added debug messages and added additional macros                   
  Author: George Joseph  
  Date:   2020-08-19  

  The SCOPE_ENTER and SCOPE_EXIT* macros now print debug messages
  at the same level as the scope level.  This allows the same
  messages to be printed to the debug log when AST_DEVMODE
  isn't enabled.

  Also added a few variants of the SCOPE_EXIT macros that will
  also call ast_log instead of ast_debug to make it easier to
  use scope tracing and still print error messages.


- ### stream.c:  Added 2 more debugging utils and added pos to stream string          
  Author: George Joseph  
  Date:   2020-08-20  

   * Added ast_stream_to_stra and ast_stream_topology_to_stra() macros
     which are shortcuts for
        ast_str_tmp(256, ast_stream_to_str(stream, &STR_TMP))

   * Added the stream position to the string representation of the
     stream.

   * Fixed some formatting in ast_stream_to_str().


- ### chan_sip: Clear ToHost property on peer when changing to dynamic host           
  Author: Dennis Buteyn  
  Date:   2020-02-18  

  The ToHost parameter was not cleared when a peer's host value was
  changed to dynamic. This causes invites to be sent to the original host.

  ASTERISK-29011 #close


- ### ACN: Changes specific to the core                                               
  Author: George Joseph  
  Date:   2020-07-20  

  Allow passing a topology from the called channel back to the
  calling channel.

   * Added a new function ast_queue_answer() that accepts a stream
     topology and queues an ANSWER CONTROL frame with it as the
     data.  This allows the called channel to indicate its resolved
     topology.

   * Added a new virtual function to the channel tech structure
     answer_with_stream_topology() that allows the calling channel
     to receive the called channel's topology.  Added
     ast_raw_answer_with_stream_topology() that invokes that virtual
     function.

   * Modified app_dial.c and features.c to grab the topology from the
     ANSWER frame queued by the answering channel and send it to
     the calling channel with ast_raw_answer_with_stream_topology().

   * Modified frame.c to automatically cleanup the reference
     to the topology on ANSWER frames.

  Added a few debugging messages to stream.c.


- ### Makefile: Fix certified version numbers                                         
  Author: cmaj  
  Date:   2020-08-06  

  Adds sed before awk to produce reasonable ASTERISKVERSIONNUM
  on certified versions of Asterisk eg. 16.8-cert3 is 160803
  instead of the previous 00800.

  ASTERISK-29021 #close


- ### res_musiconhold.c: Prevent crash with realtime MoH                              
  Author: Sean Bright  
  Date:   2020-08-06  

  The MoH class internal file vector is potentially being manipulated by
  multiple threads at the same time without sufficient locking. Switch to
  a reference counted list and operate on copies where necessary.

  ASTERISK-28927 #close


- ### res_pjsip: Fix codec preference defaults.                                       
  Author: Joshua C. Colp  
  Date:   2020-08-06  

  When reading in a codec preference configuration option
  the value would be set on the respective option before
  applying any default adjustments, resulting in the
  configuration not being as expected.

  This was exposed by the REST API push configuration as
  it used the configuration returned by Asterisk to then do
  a modification. In the case of codec preferences one of
  the options had a transcode value of "unspecified" when the
  defaults should have ensured it would be "allow" instead.

  This also renames the options in other places that were
  missed.


- ### vector.h: Fix implementation of AST_VECTOR_COMPACT() for empty vectors          
  Author: Sean Bright  
  Date:   2020-08-04  

  The assumed behavior of realloc() - that it was effectively a free() if
  its second argument was 0 - is Linux specific behavior and is not
  guaranteed by either POSIX or the C specification.

  Instead, if we want to resize a vector to 0, do it explicitly.


- ### pjproject: clone sdp to protect against (nat) modifications                     
  Author: Michael Neuhauser  
  Date:   2020-06-30  

  PJSIP, UDP transport with external_media_address and session timers
  enabled. Connected to SIP server that is not in local net. Asterisk
  initiated the connection and is refreshing the session after 150s
  (timeout 300s). The 2nd refresh-INVITE triggered by the pjsip timer has
  a malformed IP address in its SDP (garbage string). This only happens
  when the SDP is modified by the nat-code to replace the local IP address
  with the configured external_media_address.
  Analysis: the code to modify the SDP (in
  res_pjsip_session.c:session_outgoing_nat_hook() and also (redundantly?)
  in res_pjsip_sdp_rtp.c:change_outgoing_sdp_stream_media_address()) uses
  the tdata->pool to allocate the replacement string. But the same
  pjmedia_sdp_stream that was modified for the 1st refresh-INVITE is also
  used for the 2nd refresh-INVITE (because it is stored in pjmedia's
  pjmedia_sdp_neg structure). The problem is, that at that moment, the
  tdata->pool that holds the stringified external_media_address from the
  1. refresh-INVITE has long been reused for something else.
  Fix by Sauw Ming of pjproject (see
  https://github.com/pjsip/pjproject/pull/2476): the local, potentially
  modified pjmedia_sdp_stream is cloned in
  pjproject/source/pjsip/src/pjmedia/sip_neg.c:process_answer() and the
  clone is stored, thereby detaching from the tdata->pool (which is only
  released *after* process_answer())

  ASTERISK-28973
  Reported-by: Michael Neuhauser


- ### utils.c: NULL terminate ast_base64decode_string.                                
  Author: Ben Ford  
  Date:   2020-08-04  

  With the addition of STIR/SHAKEN, the function ast_base64decode_string
  was added for convenience since there is a lot of converting done during
  the STIR/SHAKEN process. This function returned the decoded string for
  you, but did not NULL terminate it, causing some issues (specifically
  with MALLOC_DEBUG). Now, the returned string is NULL terminated, and the
  documentation has been updated to reflect this.


- ### ACN: Configuration renaming for pjsip endpoint                                  
  Author: George Joseph  
  Date:   2020-07-21  

  This change renames the codec preference endpoint options.
  incoming_offer_codec_prefs becomes codec_prefs_incoming_offer
  to keep the options together when showing an endpoint.


- ### res_stir_shaken: Fix memory allocation error in curl.c                          
  Author: Ben Ford  
  Date:   2020-07-20  

  Fixed a memory allocation that was not passing in the correct size for
  the struct in curl.c.


- ### res_pjsip_session: Ensure reused streams have correct bundle group              
  Author: George Joseph  
  Date:   2020-07-23  

  When a bundled stream is removed, its bundle_group is reset to -1.
  If that stream is later reused, the bundle parameters on session
  media need to be reset correctly it could mistakenly be rebundled
  with a stream that was removed and never reused.  Since the removed
  stream has no rtp instance, a crash will result.


- ### res_pjsip_registrar: Don't specify an expiration for static contacts.           
  Author: Joshua C. Colp  
  Date:   2020-07-22  

  Statically configured contacts on an AOR don't have an expiration
  time so when adding them to the resulting 200 OK if an endpoint
  registers ensure they are marked as such.

  ASTERISK-28995


- ### utf8.c: Add UTF-8 validation and utility functions                              
  Author: Sean Bright  
  Date:   2020-07-13  

  There are various places in Asterisk - specifically in regards to
  database integration - where having some kind of UTF-8 validation would
  be beneficial. This patch adds:

  * Functions to validate that a given string contains only valid UTF-8
    sequences.

  * A function to copy a string (similar to ast_copy_string) stopping when
    an invalid UTF-8 sequence is encountered.

  * A UTF-8 validator that allows for progressive validation.

  All of this is based on the excellent UTF-8 decoder by Björn Höhrmann.
  More information is available here:

      https://bjoern.hoehrmann.de/utf-8/decoder/dfa/

  The API was written in such a way that should allow us to replace the
  implementation later should we determine that we need something more
  comprehensive.


- ### stasis_bridge.c: Fixed wrong video_mode shown                                   
  Author: sungtae kim  
  Date:   2020-07-11  

  Currently, if the bridge has created by the ARI, the video_mode
  parameter was
  not shown in the BridgeCreated event correctly.

  Fixed it and added video_mode shown in the 'bridge show <bridge id>'
  cli.

  ASTERISK-28987


- ### vector.h: Add AST_VECTOR_SORT()                                                 
  Author: Sean Bright  
  Date:   2020-07-20  

  Allows a vector to be sorted in-place, rather than only during
  insertion.


- ### CI: Force publishAsteriskDocs to use python2                                    
  Author: George Joseph  
  Date:   2020-07-16  


- ### websocket / pjsip: Increase maximum packet size.                                
  Author: Joshua C. Colp  
  Date:   2020-07-22  

  When dealing with a lot of video streams on WebRTC
  the resulting SDPs can grow to be quite large. This
  effectively doubles the maximum size to allow more
  streams to exist.

  The res_http_websocket module has also been changed
  to use a buffer on the session for reading in packets
  to ensure that the stack space usage is not excessive.


- ### Prepare master for the next Asterisk version                                    
  Author: George Joseph  
  Date:   2020-07-15  

  * Updated AMI version to 8.0.0
  * Updated ARI version to 7.0.0
  * Update make_ari_stubs.py to "Asterisk 19"


- ### acl.c: Coerce a NULL pointer into the empty string                              
  Author: Sean Bright  
  Date:   2020-07-13  

  If an ACL is misconfigured in the realtime database (for instance, the
  "rule" is blank) and Asterisk attempts to read the ACL, Asterisk will
  crash.

  ASTERISK-28978 #close


- ### pjsip: Include timer patch to prevent cancelling timer 0.                       
  Author: Joshua C. Colp  
  Date:   2020-07-13  

  I noticed this while looking at another issue and brought
  it up with Teluu. It was possible for an uninitialized timer
  to be cancelled, resulting in the invalid timer id of 0
  being placed into the timer heap causing issues.

  This change is a backport from the pjproject repository
  preventing this from happening.


