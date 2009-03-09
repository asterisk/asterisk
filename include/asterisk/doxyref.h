/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! 
 * \file 
 * \brief This file generates Doxygen pages from files in the /doc
 *        directory of the Asterisk source code tree 
 */

/* 
 * The following is for Doxygen Developer's documentation generated
 * by running "make progdocs" with doxygen installed on your
 * system.
 */

/*! 
 * \page DevDoc Asterisk Developer's Documentation - Appendices
 *
 * \section devpolicy Development and Release Policies
 * \arg \ref CodeGuide : The must-read document for all developers
 * \arg \ref CommitMessages : Information on formatting and special tags for commit messages
 * \arg \ref ReleaseStatus : The current support level for various Asterisk releases
 * \arg \ref ReleasePolicies : Asterisk Release and Commit Policies
 * \arg \ref AstCREDITS : A Thank You to contributors (unfortunately out of date)
 *
 * \section apisandinterfaces Asterisk APIs and Interfaces
 * \arg \ref AstAPI
 * \arg \ref AstAPIChanges
 * \arg \ref Def_Channel : What's a channel, anyway?
 * \arg \ref channel_drivers : Existing channel drivers
 * \arg \ref AstAMI : The Call management socket API
 * \arg \ref AstARA : A generic data storage and retrieval API for Asterisk
 * \arg \ref AstDUNDi : A way to find phone services dynamically by using the DUNDi protocol
 * \arg \ref AJI_intro : The Asterisk Jabber Interface
 * \arg \ref AstCDR
 * \arg \ref AstVar
 * \arg \ref AstVideo
 * \arg \ref AstENUM : The IETF way to redirect from phone numbers to VoIP calls
 * \arg \ref AstHTTP
 * \arg \ref AstSpeech
 *
 * \section debugconfig Debugging and Configuration References
 * \arg \ref AstREADME : General Administrator README file
 * \arg \ref AstDebug : Hints on debugging
 * \arg \ref extref 
 * \arg \ref ConfigFiles
 * \arg \ref SoundFiles included in the Asterisk distribution
 *
 * \section weblinks Web sites
 * \arg \b Main:  Asterisk Developer's website http://www.asterisk.org/developers/
 * \arg \b Bugs: The Issue Tracker http://bugs.digium.com
 * \arg \b Lists: List Server http://lists.digium.com
 * \arg \b Wiki: The Asterisk Wiki 	http://www.voip-info.org
 * \arg \b Docs: The Asterisk Documentation Project http://www.asteriskdocs.org
 * \arg \b Digium: The Asterisk Company http://www.digium.com
 */

/*!
 * \page ReleaseStatus Asterisk Release Status
 *
 * @AsteriskTrunkWarning
 *
 * \section warranty Warranty
 * The following warranty applies to all open source releases of Asterisk:
 *
 * NO WARRANTY
 *
 * BECAUSE THE PROGRAM IS LICENSED FREE OF CHARGE, THERE IS NO WARRANTY
 * FOR THE PROGRAM, TO THE EXTENT PERMITTED BY APPLICABLE LAW.  EXCEPT WHEN
 * OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES
 * PROVIDE THE PROGRAM \"AS IS\" WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED
 * OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE ENTIRE RISK AS
 * TO THE QUALITY AND PERFORMANCE OF THE PROGRAM IS WITH YOU.  SHOULD THE
 * PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF ALL NECESSARY SERVICING,
 * REPAIR OR CORRECTION.

 * IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING
 * WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MAY MODIFY AND/OR
 * REDISTRIBUTE THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES,
 * INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING
 * OUT OF THE USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED
 * TO LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY
 * YOU OR THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER
 * PROGRAMS), EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * \section releasestatustypes Release Status Types
 *
 * Release management is a essentially an agreement between the development
 * community and the %user community on what kind of updates can be expected
 * for Asterisk releases, and what types of changes these updates will contain.
 * Once these policies are established, the development community works very
 * hard to adhere to them.  However, the development community does reserve
 * the right to make exceptions to these rules for special cases as the need
 * arises.
 *
 * Asterisk releases are in various states of maintenance.  The states are
 * defined here:
 *
 * \arg <b>None</b> - This release series is receiving no updates whatsoever.
 * \arg <b>Security-Only</b> - This release series is receiving updates, but
 *      only to address security issues.  Security issues found and fixed in
 *      this release series will be accompanied by a published security advisory
 *      from the Asterisk project.
 * \arg <b>Full-Support</b> - This release series is receiving updates for all
 *      types of bugs.
 * \arg <b>Full-Development</b> - Changes in this part of Asterisk include bug
 *      fixes, as well as new %features and architectural improvements.
 *
 * \section AsteriskReleases Asterisk Maintenance Levels
 *
 * \htmlonly
 * <table border="1">
 *  <tr>
 *   <td><b>Name</b></td>
 *   <td><b>SVN Branch</b></td>
 *   <td><b>Status</b></td>
 *   <td><b>Notes</b></td>
 *  </tr>
 *  <tr>
 *   <td>Asterisk 1.0</td>
 *   <td>/branches/1.0</td>
 *   <td>None</td>
 *  </tr>
 *  <tr>
 *   <td>Asterisk 1.2</td>
 *   <td>/branches/1.2</td>
 *   <td>Security-Only</td>
 *  </tr>
 *  <tr>
 *   <td>Asterisk 1.4</td>
 *   <td>/branches/1.4</td>
 *   <td>Full-Support</td>
 *  </tr>
 *  <tr>
 *   <td>Asterisk 1.6.0</td>
 *   <td>/branches/1.6.0</td>
 *   <td>Full-Support</td>
 *  </tr>
 *  <tr>
 *   <td>Asterisk 1.6.1</td>
 *   <td>/branches/1.6.1</td>
 *   <td>Full-Support</td>
 *   <td>Still in beta</td>
 *  </tr>
 *  <tr>
 *   <td>Asterisk trunk</td>
 *   <td>/trunk</td>
 *   <td>Full-Development</td>
 *   <td>No releases are made directly from trunk.</td>
 *  </tr>
 * </table>
 * \endhtmlonly
 *
 * For more information on how and when Asterisk releases are made, see the
 * release policies page:
 * \arg \ref ReleasePolicies
 */

/*!
 * \page ReleasePolicies Asterisk Release and Commit Policies
 *
 * \AsteriskTrunkWarning
 *
 * \section releasestatus Asterisk Release Status
 *
 * For more information on the current status of each Asterisk release series,
 * please see the Asterisk Release Status page:
 *
 * \arg \ref ReleaseStatus
 *
 * <hr/>
 *
 * \section commitmonitoring Commit Monitoring
 *
 * To monitor commits to Asterisk and related projects, visit 
 * <a href="http://lists.digium.com/">http://lists.digium.com</a>.  The Digium
 * mailing list server hosts a %number of mailing lists for commits.
 *
 * <hr/>
 *
 * \section ast10policy Asterisk 1.0
 *
 * \subsection svnbranch SVN Branch
 *
 * \arg /branches/1.0
 *
 * \subsection ast10releases Release and Commit Policy
 * No more releases of Asterisk 1.0 will be made for any reason.
 *
 * No commits should be made to the Asterisk 1.0 branch.
 * 
 * <hr/>
 *
 * \section ast12policy Asterisk 1.2
 *
 * \subsection svnbranch SVN Branch
 *
 * \arg /branches/1.2
 *
 * \subsection ast12releases Release and Commit Policy
 *
 * There will be no more scheduled releases of Asterisk 1.2.
 * 
 * Commits to the Asterisk 1.2 branch should only address security issues or
 * regressions introduced by previous security fixes.  For a security issue, the
 * commit should be accompanied by an 
 * <a href="http://downloads.digium.com/pub/security/">Asterisk Security Advisory</a>
 * and an immediate release.  When a commit goes in to fix a regression, the previous
 * security advisory that is related to the change that introduced the bug should get
 * updated to indicate that there is an updated version of the fix.  A release should
 * be made immediately for these regression fixes, as well.
 *
 * <hr/>
 *
 * \section ast14policy Asterisk 1.4
 *
 * \subsection svnbranch SVN Branch
 *
 * \arg /branches/1.4
 *
 * \subsection ast14releases Release and Commit Policy
 *
 * Asterisk 1.4 is receiving regular bug fix release updates.  An attempt is made to
 * make releases of every four to six weeks.  Since this release series is receiving
 * changes for all types of bugs, the number of changes in a single release can be
 * significant.  1.4.X releases go through a release candidate testing cycle to help
 * catch any regressions that may have been introduced.
 *
 * Commits to Asterisk 1.4 must be to address bugs only.  No new %features should be
 * introduced into Asterisk 1.4 to reduce the %number of changes to this established
 * release series.  The only exceptions to this %rule are for cases where something
 * that may be considered a feature is needed to address a bug or security issue.
 *
 * <hr/>
 *
 * \section ast16policy Asterisk 1.6
 *
 * \subsection svnbranch SVN Branch
 *
 * \arg /branches/1.6.*
 *
 * \subsection ast16releases Release and Commit Policy
 *
 * Asterisk 1.6 is managed in a different way than previous Asterisk release series.
 * From a high level, it was inspired by the release model used for Linux 2.6.
 * The intended time frame for 1.6.X releases is every 2 or 3 months.  Each 1.6.X
 * release gets its own branch.  The 1.6.X branches are branches off of trunk.
 * Once the branch is created, it only receives bug fixes.  Each 1.6.X release goes
 * through a beta and release candidate testing cycle.
 *
 * After a 1.6.X release is published, it will be maintained until 1.6.[X + 3] is
 * released.  While a 1.6.X release branch is still maintained, it will receive only
 * bug fixes.  Periodic maintenance releases will be made and labeled as 1.6.X.Y.
 * 1.6.X.Y releases should go through a release candidate test cycle before being
 * published.
 *
 * For now, all previous 1.6 release will be maintained for security issues.  Once
 * we have more 1.6 releases to deal with, this part of the policy will likely change.
 * 
 * For some history on the motivations for Asterisk 1.6 release management, see the
 * first two sections of this
 * <a href="http://lists.digium.com/pipermail/asterisk-dev/2007-October/030083.html">mailing list post</a>.
 *
 * <hr/>
 *
 * \section asttrunk Asterisk Trunk
 *
 * \subsection svnbranch SVN Branch
 *
 * \arg /trunk
 *
 * \subsection asttrunkpolicy Release and Commit Policy
 *
 * No releases are ever made directly from Asterisk trunk.
 *
 * Asterisk trunk is used as the main development area for upcoming Asterisk 1.6 
 * releases.  Commits to Asterisk trunk are not limited.  They can be bug fixes,
 * new %features, and architectural improvements.  However, for larger sets
 * of changes, developers should work with the Asterisk project leaders to
 * schedule them for inclusion.  Care is taken not to include too many invasive
 * sets of changes for each new Asterisk 1.6 release.
 *
 * No changes should go into Asterisk trunk that are not ready to go into a
 * release.  While the upcoming release will go through a beta and release
 * candidate test cycle, code should not be in trunk until the code has been
 * tested and reviewed such that there is reasonable belief that the code
 * is ready to go.
 *
 * <hr/>
 *
 * \section astteam Asterisk Team Branches
 *
 * \subsection svnbranch SVN Branch
 *
 * \arg /team/&lt;developername&gt;
 *
 * \subsection astteampolicy Release and Commit Policy
 *
 * The Asterisk subversion repository has a special directory called "team"
 * where developers can make their own personal development branches.  This is
 * where new %features, bug fixes, and architectural improvements are developed
 * while they are in %progress.
 *
 * Just about anything goes as far as commits to this area goes.  However,
 * developers should keep in mind that anything committed here, as well as
 * anywhere else on Digium's SVN server, falls under the contributor license
 * agreement.
 *
 * In addition to each developer having their own space for working on projects,
 * there is also a team/group folder where %group development efforts take place.
 *
 * Finally, in each developer folder, there is a folder called "private".  This
 * is where developers can create branches for working on things that they are
 * not ready for the whole world to see.
 */

/*! 
 * \page CodeGuide Coding Guidelines
 * \AsteriskTrunkWarning
 * \section Coding Guidelines
 * This file is in the /doc directory in your Asterisk source tree.
 * Make sure to stay up to date with the latest guidelines.
 * \verbinclude CODING-GUIDELINES
 */

/*!
 * \page CommitMessages Guidelines for Commit Messages
 *
 * \AsteriskTrunkWarning
 *
 * <hr/>
 *
 * \section CommitMsgFormatting Commit Message Formatting
 *
 * The following illustrates the basic outline for commit messages:
 *
  \verbatim
  <One-liner summary of changes>
 
  <Verbose description of the changes>

  <Special Tags>
  \endverbatim
 *
 * Some commit history viewers treat the first line of commit messages as the
 * summary for the commit.  So, an effort should be made to format our commit
 * messages in that fashion.  The verbose description may contain multiple 
 * paragraphs, itemized lists, etc.
 *
 * Commit messages should be wrapped at 80 %columns.
 *
 * \note For trivial commits, such as "fix the build", or "fix spelling mistake",
 *       the verbose description may not be necessary.
 *
 * <hr/>
 *
 * \section CommitMsgTags Special Tags for Commit Messages
 *
 * \subsection MantisTags Mantis (http://bugs.digium.com/)
 *
 * To have a commit noted in an issue, use a tag of the form: 
 * \arg (issue #1234)
 *
 * To have a commit automatically close an issue, use a tag of the form:
 * \arg (closes issue #1234)
 *
 * When making a commit for a mantis issue, it is easiest to use the
 * provided commit %message template functionality.  It will format the
 * special tags appropriately, and will also include information about who
 * reported the issue, which patches are being applied, and who did testing.
 * 
 * Assuming that you have bug marshal access (and if you have commit access,
 * it is pretty safe to assume that you do), you will find the commit %message
 * template section directly below the issue details section and above the
 * issue relationships section.  You will have to click the '+' next to
 * "Commit message template" to make the contents of the section visible.
 *
 * Here is an example of what the template will generate for you:
 *
  \verbatim
  (closes issue #1234)
  Reported by: SomeGuy
  Patches:
       fix_bug_1234.diff uploaded by SomeDeveloper (license 5678)
  \endverbatim
 *
 * If the patch being committed was written by the person doing the commit,
 * and is not available to reference as an upload to the issue, there is no
 * need to include something like "fixed by me", as that will be the default
 * assumption when a specific patch is not referenced.
 *
 * \subsection ReviewBoardTags Review Board (http://reviewboard.digium.com/)
 *
 * To have a commit set a review request as submitted, include the full URL
 * to the review request.  For example:
 * \arg Review: %http://reviewboard.digium.com/r/95/
 *
 * \note The trailing slash in the review URL is required.
 *
 * <hr/>
 *
 * \section CommitMsgSvnmerge Commit Messages with svnmerge
 *
 * When using the svnmerge tool for merging changes between branches, use the
 * commit %message generated by svnmerge.  The '-f' option to svnmerge allows
 * you to specify a file for svnmerge to write out a commit %message to.  The
 * '-F' option to svn commit allows you to specify a file that contains the
 * commit %message.
 *
 * If you are using the expect script wrappers for svnmerge from repotools,
 * a commit %message is automatically placed in the file '../merge.msg'.
 *
 * For more detailed information about working with branches and merging,
 * see the following page on %asterisk.org:
 * \arg http://www.asterisk.org/developers/svn-branching-merging
 */

/*! 
 * \page AstAPI Asterisk API
 * \section Asteriskapi Asterisk API
 * Some generic documents on the Asterisk architecture
 *
 * \arg \ref AstThreadStorage
 * \arg \ref DataStores
 * \arg \ref AstExtState
 *
 * \subsection model_txt Generic Model
 * \verbinclude model.txt
 * \subsection channel_txt Channels
 * \arg See \ref Def_Channel
 */

/*! \page AstAPIChanges Asterisk API Changes
 *  \section Changes161 Version 1.6.1
 *  \li ast_install_vm_functions()
 *  \li vmwi_generate()
 *  \li ast_channel_datastore_alloc()
 *  \li ast_channel_datastore_free()
 *  \li ast_channel_cmpwhentohangup()
 *  \li ast_channel_setwhentohangup()
 *  \li ast_settimeout()
 *  \li ast_datastore_alloc()
 *  \li ast_datastore_free()
 *  \li ast_device_state_changed()
 *  \li ast_device_state_changed_literal()
 *  \li ast_dnsmgr_get()
 *  \li ast_dnsmgr_lookup()
 *  \li ast_dsp_set_digitmode()
 *  \li ast_get_txt()
 *  \li ast_event_unsubscribe()
 *  \li localized_context_find_or_create()
 *  \li localized_merge_contexts_and_delete()
 *  \li ast_console_puts_mutable()
 *  \li ast_rtp_get_quality()
 *  \li ast_tcptls_client_start()
 *  \li ast_tcptls_server_start()
 *  \li ast_tcptls_server_stop()
 */

/*! 
 * \page AstDebug Debugging
 * \section debug Debugging
 * \verbinclude backtrace.txt
 */

/*!
 * \page AstSpeech The Generic Speech Recognition API
 * \section debug The Generic Speech Recognition API
 * \verbinclude speechrec.txt
 */

/*! 
 * \page DataStores Channel Data Stores
 * \section debug Channel Data Stores
 * \verbinclude datastores.txt
 */

/*! 
 * \page AstAMI AMI - The Manager Interface
 * \section ami AMI - The manager Interface
 * \arg \link Config_ami Configuration file \endlink
 * \arg \ref manager.c
 * \verbinclude manager.txt
 */

/*!
 * \page AstARA ARA - The Asterisk Realtime Interface
 * \section realtime ARA - a generic API to storage and retrieval
 * Implemented in \ref config.c 
 * Implemented in \ref pbx_realtime.c 
 * \verbinclude realtime.txt
 * \verbinclude extconfig.txt
 */

/*! 
 * \page AstDUNDi DUNDi
 *
 * DUNDi is a peer-to-peer system for locating Internet gateways to telephony 
 * services. Unlike traditional centralized services (such as the remarkably 
 * simple and concise ENUM standard), DUNDi is fully-distributed with no 
 * centralized authority whatsoever.
 *
 * DUNDi is not itself a Voice-over IP signaling or media protocol. Instead, 
 * it publishes routes which are in turn accessed via industry standard 
 * protocols such as IAX, SIP and H.323. 
 *
 * \par References
 * \arg DUNDi is documented at http://www.dundi.com
 * \arg Implemented in \ref pbx_dundi.c and \ref dundi-parser.c
 * \arg Configuration in \link Config_dun dundi.conf \endlink
 */

/*! 
 * \page AstCDR CDR - Call Data Records and billing
 * \section cdr Call Data Records
 * \par See also
 * \arg \ref cdr.c
 * \arg \ref cdr_drivers
 * \arg \ref Config_cdr CDR configuration files
 *
 * \verbinclude cdrdriver.txt
 */

/*! 
 * \page AstREADME README
 * \verbinclude README
 */
 
/*! 
 * \page AstCREDITS CREDITS
 * \verbinclude CREDITS
 */

/*! 
 * \page AstVideo Video support in Asterisk
 * \section sectAstVideo Video support in Asterisk
 * \verbinclude video.txt
 */

/*! 
 * \page AstVar Globally predefined channel variables
 * \section globchan Globally predefined channel variables
 *
 * More and more of these variables are being replaced by dialplan functions.
 * Some still exist though and some that does still exist needs to move to
 * dialplan functions.
 *
 * See also
 * - \ref pbx_retrieve_variable()
 * - \ref AstChanVar
 *
 *  \verbinclude channelvariables.tex
 */

/*! 
 * \page AstChanVar Asterisk Dialplan Variables
 *	Asterisk Dialplan variables are divided into three groups:
 *	- Predefined global variables, handled by the PBX core
 *	- Global variables, that exist for the duration of the pbx execution
 *	- Channel variables, that exist during a channel
 *
 * Global variables are reachable in all channels, all of the time.
 * Channel variables are only reachable within the channel.
 *
 * For more information on the predefined variables, see \ref AstVar
 * 
 * Global and Channel variables:
 * - Names are Case insensitive
 * - Names that start with a character, but are alphanumeric
 * - Global variables are defined and reached with the GLOBAL() dialplan function
 *   and the set application, like
 *
 * 	exten => 1234,1,set(GLOBAL(myvariable)=tomteluva)
 *
 * 	- \ref func_global.c
 *
 * - Channel variables are defined with the set() dialplan application
 *
 *	exten => 1234,1,set(xmasattribute=tomtegröt)
 *
 * - Some channels also supports setting channel variables with the \b setvar=
 *   configuraiton option for a device or line.
 *
 * \section AstChanVar_globalvars Global Variables
 * Global variables can also be set in the [globals] section of extensions.conf. The
 * setting \b clearglobalvars in extensions.conf [general] section affects whether
 * or not the global variables defined in \b globals are reset at dialplan reload.
 * 
 * There are CLI commands to change and read global variables. This can be handy
 * to reset counters at midnight from an external script.
 *
 * \section AstChanVar_devnotes Developer notes
 * Variable handling is managed within \ref pbx.c
 * You need to include pbx.h to reach these functions.
 *	- \ref pbx_builtin_setvar_helper()
 * 	- \ref pbx_builtin_getvar_helper()
 *
 * The variables is a linked list stored in the channel data structure
 * with the list starting at varshead in struct ast_channel
 */

/*! 
 * \page AstENUM ENUM
 * \section enumreadme ENUM
 * \arg Configuration: \ref Config_enum
 * \arg \ref enum.c
 * \arg \ref func_enum.c
 *
 * \verbinclude enum.txt
 */

/*! 
 * \page ConfigFiles Configuration files
 * \section config Main configuration files
 * \arg \link Config_ast asterisk.conf - the main configuration file \endlink
 * \arg \link Config_ext extensions.conf - The Dial Plan \endlink
 * \arg \link Config_mod modules.conf - which modules to load and not to load \endlink
 * \arg \link Config_fea features.conf - call features (transfer, parking, etc) \endlink
 * \section chanconf Channel configuration files
 * \arg \link Config_iax IAX2 configuration  \endlink
 * \arg \link Config_sip SIP configuration  \endlink
 * \arg \link Config_mgcp MGCP configuration  \endlink
 * \arg \link Config_rtp RTP configuration  \endlink
 * \arg \link Config_dahdi DAHDI configuration  \endlink
 * \arg \link Config_oss OSS (sound card) configuration  \endlink
 * \arg \link Config_alsa ALSA (sound card) configuration  \endlink
 * \arg \link Config_agent Agent (proxy channel) configuration  \endlink
 * \arg \link Config_misdn MISDN Experimental ISDN BRI channel configuration  \endlink
 * \arg \link Config_h323 H.323 configuration  \endlink
 * \section appconf Application configuration files
 * \arg \link Config_mm Meetme (conference bridge) configuration  \endlink
 * \arg \link Config_qu Queue system configuration  \endlink
 * \arg \link Config_vm Voicemail configuration  \endlink
 * \arg \link Config_followme Followme configuration  \endlink
 * \section cdrconf CDR configuration files
 * \arg \link Config_cdr CDR configuration  \endlink
 * \arg \link cdr_custom Custom CDR driver configuration \endlink
 * \arg \link cdr_ami Manager CDR driver configuration \endlink
 * \arg \link cdr_odbc ODBC CDR driver configuration \endlink
 * \arg \link cdr_pgsql PostgreSQL CDR driver configuration \endlink
 * \arg \link cdr_sqlite SQLite CDR driver configuration \endlink
 * \arg \link cdr_tds FreeTDS CDR driver configuration (Microsoft SQL Server) \endlink
 * \section miscconf Miscellenaous configuration files
 * \arg \link Config_adsi ADSI configuration  \endlink
 * \arg \link Config_ami AMI - Manager configuration  \endlink
 * \arg \link Config_ara Realtime configuration  \endlink
 * \arg \link Config_codec Codec configuration  \endlink
 * \arg \link Config_dun DUNDi configuration  \endlink
 * \arg \link Config_enum ENUM configuration  \endlink
 * \arg \link Config_moh Music on Hold configuration  \endlink
 * \arg \link Config_vm Voicemail configuration  \endlink
 * \arg \link res_config_sqlite SQLite Resource driver configuration \endlink
 */

/*! 
 * \page Config_ast Asterisk.conf
 * \verbinclude asterisk-conf.txt
 */

/*! 
 * \page Config_mod Modules configuration
 * All res_ resource modules are loaded with globals on, which means
 * that non-static functions are callable from other modules.
 *
 * If you want your non res_* module to export functions to other modules
 * you have to include it in the [global] section.
 * \verbinclude modules.conf.sample
 */

/*! 
 * \page Config_fea Call features configuration
 * \par See also
 * \arg \ref res_features.c : Call feature implementation
 * \section featconf features.conf
 * \verbinclude features.conf.sample
 */

/*! 
 * \page Config_followme Followme: An application for simple follow-me calls
 * \section followmeconf Followme.conf
 * - See app_followme.c
 * \verbinclude followme.conf.sample
 */

/*! 
 * \page Config_ext Extensions.conf - the Dial Plan
 * \section dialplan Extensions.conf 
 * \verbinclude extensions.conf.sample
 */

/*! 
 * \page Config_iax IAX2 configuration
 * IAX2 is implemented in \ref chan_iax2.c
 * \arg \link Config_iax iax.conf Configuration file example \endlink
 * \section iaxreadme IAX readme file
 * \verbinclude iax.txt
 * \section Config_iax IAX Configuration example
 * \verbinclude iax.conf.sample
 * \section iaxjitter IAX Jitterbuffer information
 * \verbinclude jitterbuffer.txt
 */

/*! 
 * \page Config_iax IAX configuration
 * \arg Implemented in \ref chan_iax2.c
 * \section iaxconf iax.conf
 * \verbinclude iax.conf.sample
 */

/*! 
 * \page Config_sip SIP configuration
 * Also see \ref Config_rtp RTP configuration
 * \arg Implemented in \ref chan_sip.c
 * \section sipconf sip.conf
 * \verbinclude sip.conf.sample
 *
 * \arg \b Back \ref chanconf
 */

/*! 
 * \page Config_mgcp MGCP configuration
 * Also see \ref Config_rtp RTP configuration
 * \arg Implemented in \ref chan_mgcp.c
 * \section mgcpconf mgcp.conf
 * \verbinclude mgcp.conf.sample
 */

/*! 
 * \page README_misdn MISDN documentation
 * \arg See \ref Config_misdn
 * \section mISDN configuration
 * \verbinclude misdn.txt
 */

/*! 
 * \page Config_misdn MISDN configuration
 * \arg Implemented in \ref chan_misdn.c
 * \arg \ref README_misdn
 * \arg See the mISDN home page: http://www.isdn4linux.de/mISDN/
 * \section misdnconf misdn.conf
 * \verbinclude misdn.conf.sample
 */

/*! 
 * \page Config_vm VoiceMail configuration
 * \section vmconf voicemail.conf
 * \arg Implemented in \ref app_voicemail.c
 * \verbinclude voicemail.conf.sample
 */

/*! 
 * \page Config_dahdi DAHDI configuration
 * \section dahdiconf dahdi.conf
 * \arg Implemented in \ref chan_dahdi.c
 * \verbinclude dahdi.conf.sample
 */

/*! 
 * \page Config_h323 H.323 channel driver information
 * This is the configuration of the H.323 channel driver within the Asterisk
 * distribution. There's another one, called OH323, in asterisk-addons
 * \arg Implemented in \ref chan_h323.c
 * \section h323conf h323.conf
 * \ref chan_h323.c
 */

/*! 
 * \page Config_oss OSS configuration
 * \section ossconf oss.conf
 * \arg Implemented in \ref chan_oss.c
 * \verbinclude oss.conf.sample
 */

/*! 
 * \page Config_alsa ALSA configuration
 * \section alsaconf alsa.conf
 * \arg Implemented in \ref chan_alsa.c
 * \verbinclude alsa.conf.sample
 */

/*! 
 * \page Config_agent Agent configuration
 * \section agentconf agents.conf
 * The agent channel is a proxy channel for queues
 * \arg Implemented in \ref chan_agent.c
 * \verbinclude agents.conf.sample
 */

/*! 
 * \page Config_rtp RTP configuration
 * \arg Implemented in \ref rtp.c
 * Used in \ref chan_sip.c and \ref chan_mgcp.c (and various H.323 channels)
 * \section rtpconf rtp.conf
 * \verbinclude rtp.conf.sample
 */

/*! 
 * \page Config_dun DUNDi Configuration
 * \arg See also \ref AstDUNDi
 * \section dundiconf dundi.conf
 * \verbinclude dundi.conf.sample
 */

/*! 
 * \page Config_enum ENUM Configuration
 * \section enumconf enum.conf
 * \arg See also \ref enumreadme
 * \arg Implemented in \ref func_enum.c and \ref enum.c
 * \verbinclude enum.conf.sample
 */

/*! 
 * \page cdr_custom Custom CDR Configuration
 * \par See also 
 * \arg \ref cdrconf
 * \arg \ref cdr_custom.c
 * \verbinclude cdr_custom.conf.sample
 */

/*! 
 * \page cdr_ami Manager CDR driver configuration
 * \par See also 
 * \arg \ref cdrconf
 * \arg \ref AstAMI
 * \arg \ref cdr_manager.c
 * \verbinclude cdr_manager.conf.sample
 */

/*! 
 * \page cdr_odbc ODBC CDR driver configuration
 * \arg See also \ref cdrconf
 * \arg \ref cdr_odbc.c
 * \verbinclude cdr_odbc.conf.sample
 * See also:
 * \arg http://www.unixodbc.org
 */

/*! 
 * \page cdr_pgsql PostgreSQL CDR driver configuration
 * \arg See also \ref cdrconf
 * \arg \ref cdr_pgsql.c
 * See also:
 * \arg http://www.postgresql.org
 * \verbinclude cdr_pgsql.conf.sample
 */

/*! 
 * \page cdr_sqlite SQLite CDR driver configuration
 * \arg See also \ref cdrconf
 * \arg \ref cdr_sqlite.c
 * See also:
 * \arg http://www.sqlite.org
 */

/*! 
 * \page cdr_tds FreeTDS CDR driver configuration
 * \arg See also \ref cdrconf
 * See also:
 * \arg http://www.freetds.org
 * \verbinclude cdr_tds.conf.sample
 */

/*! 
 * \page Config_cdr CDR configuration
 * \par See also
 * \arg \ref cdr_drivers
 * \arg \link Config_cdr CDR configuration  \endlink  
 * \arg \link cdr_custom Custom CDR driver configuration \endlink
 * \arg \link cdr_ami Manager CDR driver configuration \endlink
 * \arg \link cdr_odbc ODBC CDR driver configuration \endlink
 * \arg \link cdr_pgsql PostgreSQL CDR driver configuration \endlink
 * \arg \link cdr_sqlite SQLite CDR driver configuration \endlink
 * \arg \link cdr_tds FreeTDS CDR driver configuration (Microsoft SQL Server) \endlink
 * \verbinclude cdr.conf.sample
 */

/*! 
 * \page Config_moh Music on Hold Configuration
 * \arg Implemented in \ref res_musiconhold.c
 * \section mohconf musiconhold.conf
 * \verbinclude musiconhold.conf.sample
 */

/*! 
 * \page Config_adsi ADSI Configuration
 * \section adsiconf adsi.conf
 * \verbinclude adsi.conf.sample
 */

/*! 
 * \page Config_codec CODEC Configuration
 * \section codecsconf codecs.conf
 * \verbinclude codecs.conf.sample
 */

/*! 
 * \page Config_ara REALTIME Configuration
 * \arg See also: \arg \link AstARA \endlink
 * \section extconf extconfig.conf
 * \verbinclude extconfig.conf.sample
 */

/*! 
 * \page Config_ami AMI configuration
 * \arg See also: \arg \link AstAMI \endlink
 * \section amiconf manager.conf
 * \verbinclude manager.conf.sample
 */

/*! 
 * \page Config_qu ACD - Queue system configuration
 * \arg Implemented in \ref app_queue.c
 * \section quconf queues.conf
 * \verbinclude queues.conf.sample
 */

/*! 
 * \page Config_mm Meetme - The conference bridge configuration
 * \arg Implemented in \ref app_meetme.c
 * \section mmconf meetme.conf
 * \verbinclude meetme.conf.sample
 */

/*! 
 * \page SoundFiles Sound files
 * \section SecSound Asterisk Sound files
 * Asterisk includes a large number of sound files. Many of these
 * are used by applications and demo scripts within asterisk.
 *
 * Additional sound files are available in the asterisk-addons
 * repository on svn.digium.com
 */

/*! 
 * \addtogroup cdr_drivers Module: CDR Drivers
 * \section CDR_generic Asterisk CDR Drivers
 * \brief CDR drivers are loaded dynamically, each loaded CDR driver produce 
 *        a billing record for each call.
 * \arg \ref Config_mod "Modules Configuration"
 * \arg \ref Config_cdr "CDR Configuration"
 */


/*! 
 * \addtogroup channel_drivers Module: Asterisk Channel Drivers
 * \section channel_generic Asterisk Channel Drivers
 * \brief Channel drivers are loaded dynamically. 
 * \arg \ref Config_mod "Modules Configuration"
 */

/*! 
 * \addtogroup applications Module: Dial plan applications
 * \section app_generic Asterisk Dial Plan Applications
 * \brief Applications support the dialplan. They register dynamically with 
 *        \see ast_register_application() and unregister with 
 *        \see ast_unregister_application()
 * \par See also
 * \arg \ref functions
 */

/*! 
 * \addtogroup functions Module: Dial plan functions
 * \section func_generic Asterisk Dial Plan Functions
 * \brief Functions support the dialplan.  They do not change any property of a channel
 *        or touch a channel in any way.
 * \par See also
 * \arg \ref applications
 *  
 */

/*! 
 * \addtogroup codecs Module: Codecs
 * \section codec_generic Asterisk Codec Modules
 * Codecs are referenced in configuration files by name 
 * \par See also 
 * \arg \ref formats 
 */

/*! 
 * \addtogroup formats Module: Media File Formats 
 * \section codec_generic Asterisk Format drivers
 * Formats are modules that read or write media files to disk.
 * \par See also
 * \arg \ref codecs 
 */

/*! 
 * \page AstHTTP AMI over HTTP support
 * The http.c file includes support for manager transactions over
 * http.
 * \section ami AMI - The manager Interface
 * \arg \link Config_ami Configuration file \endlink
 */

/*! 
 * \page res_config_sqlite SQLite Resource driver configuration
 * \arg Implemented in \ref res_config_sqlite.c
 * \arg Configuration file:
 * \verbinclude res_config_sqlite.conf
 * \arg SQL tables:
 * \verbinclude res_config_sqlite.txt
 * \arg See also:
 * http://www.sqlite.org
 */

/*!
 * \page Licensing Asterisk Licensing Information
 *
 * \section license Asterisk License
 * \verbinclude LICENSE
 *
 * \section otherlicenses Licensing of 3rd Party Code
 *
 * This section contains a (not yet complete) list of libraries that are used
 * by various parts of Asterisk, including related licensing information.
 *
 * \subsection alsa_lib ALSA Library
 * \arg <b>Library</b>: libasound
 * \arg <b>Website</b>: http://www.alsa-project.org
 * \arg <b>Used by</b>: chan_alsa
 * \arg <b>License</b>: LGPL
 *
 * \subsection openssl_lib OpenSSL
 * \arg <b>Library</b>: libcrypto, libssl
 * \arg <b>Website</b>: http://www.openssl.org
 * \arg <b>Used by</b>: Asterisk core (TLS for manager and HTTP), res_crypto
 * \arg <b>License</b>: Apache 2.0
 * \arg <b>Note</b>:    An exception has been granted to allow linking of 
 *                      OpenSSL with Asterisk.
 *
 * \subsection curl_lib Curl
 * \arg <b>Library</b>: libcurl
 * \arg <b>Website</b>: http://curl.haxx.se
 * \arg <b>Used by</b>: func_curl, res_config_curl, res_curl
 * \arg <b>License</b>: BSD
 *
 * \subsection portaudio_lib PortAudio
 * \arg <b>Library</b>: libportaudio
 * \arg <b>Website</b>: http://www.portaudio.com
 * \arg <b>Used by</b>: chan_console
 * \arg <b>License</b>: BSD
 * \arg <b>Note</b>:    Even though PortAudio is licensed under a BSD style
 *                      license, PortAudio will make use of some audio interface,
 *                      depending on how it was built.  That audio interface may
 *                      introduce additional licensing restrictions.  On Linux,
 *                      this would most commonly be ALSA: \ref alsa_lib.
 *
 * \subsection rawlist Raw list of libraries that used by any part of Asterisk
 * \li c-client.a (app_voicemail with IMAP support)
 * \li libSDL-1.2.so.0
 * \li libSaClm.so.2
 * \li libSaEvt.so.2
 * \li libX11.so.6
 * \li libXau.so.6
 * \li libXdmcp.so.6
 * \li libasound.so.2
 * \li libc.so.6
 * \li libcom_err.so.2
 * \li libcrypt.so.1
 * \li libcrypto.so.0.9.8 (chan_h323)
 * \li libcurl.so.4
 * \li libdirect-1.0.so.0
 * \li libdirectfb-1.0.so.0
 * \li libdl.so.2
 * \li libexpat.so (chan_h323)
 * \li libfusion-1.0.so.0
 * \li libgcc_s.so (chan_h323)
 * \li libgcrypt.so.11 (chan_h323)
 * \li libglib-2.0.so.0
 * \li libgmime-2.0.so.2
 * \li libgmodule-2.0.so.0
 * \li libgnutls.so.13 (chan_h323)
 * \li libgobject-2.0.so.0
 * \li libgpg-error.so.0 (chan_h323)
 * \li libgssapi_krb5.so.2
 * \li libgthread-2.0.so.0
 * \li libidn.so.11
 * \li libiksemel.so.3
 * \li libisdnnet.so
 * \li libjack.so.0
 * \li libjpeg.so.62
 * \li libk5crypto.so.3
 * \li libkeyutils.so.1
 * \li libkrb5.so.3
 * \li libkrb5support.so.0
 * \li liblber-2.4.so.2 (chan_h323)
 * \li libldap_r-2.4.so.2 (chan_h323)
 * \li libltdl.so.3
 * \li liblua5.1.so.0
 * \li libm.so.6
 * \li libmISDN.so
 * \li libnbs.so.1
 * \li libncurses.so.5
 * \li libnetsnmp.so.15
 * \li libnetsnmpagent.so.15
 * \li libnetsnmphelpers.so.15
 * \li libnetsnmpmibs.so.15
 * \li libnsl.so.1
 * \li libodbc.so.1
 * \li libogg.so.0
 * \li libopenh323.so (chan_h323)
 * \li libpcre.so.3
 * \li libperl.so.5.8
 * \li libportaudio.so.2
 * \li libpq.so.5
 * \li libpri.so.1.4
 * \li libpt.so (chan_h323)
 * \li libpthread.so.0
 * \li libradiusclient-ng.so.2
 * \li libresample.so.1.0
 * \li libresolv.so.2 (chan_h323)
 * \li librt.so.1
 * \li libsasl2.so.2 (chan_h323)
 * \li libselinux.so.1
 * \li libsensors.so.3
 * \li libspandsp.so.1
 * \li libspeex.so.1
 * \li libsqlite.so.0
 * \li libsqlite3.so.0
 * \li libss7.so.1
 * \li libssl.so.0.9.8 (chan_h323)
 * \li libstdc++.so (chan_h323, chan_vpb)
 * \li libsuppserv.so
 * \li libsybdb.so.5
 * \li libsysfs.so.2
 * \li libtasn1.so.3 (chan_h323)
 * \li libtds.so.4
 * \li libtiff.so.4
 * \li libtonezone.so.1.0
 * \li libvorbis.so.0
 * \li libvorbisenc.so.2
 * \li libvpb.a (chan_vpb)
 * \li libwrap.so.0
 * \li libxcb-xlib.so.0
 * \li libxcb.so.1
 * \li libz.so.1 (chan_h323)
 * \li linux-vdso.so.1
*/
