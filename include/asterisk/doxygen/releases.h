/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2009, Digium, Inc.
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
 * <a href="http://downloads.asterisk.org/pub/security/">Asterisk Security Advisory</a>
 * and an immediate release.  When a commit goes in to fix a regression, the previous
 * security advisory that is related to the change that introduced the bug should get
 * updated to indicate that there is an updated version of the fix.  A release should
 * be made immediately for these regression fixes, as well.
 *
 * \subsection ast12releasenumbers Release Numbering
 *
 *  - 1.2.X - a release that contains new security fixes
 *  - 1.2.X.Y - a release that contains fixes to the security patches released in
 *    version 1.2.X
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
 * \subsection ast14releasenumbers Release Numbering
 *
 *  - 1.4.X - a release that contains new bug fixes to the 1.4 release series
 *  - 1.4.X.Y - a release that contains very few changes on top of 1.4.X.  This
 *    may be for a security patch, or for a regression introduced in 1.4.X.
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
 * \subsection ast16releasenumbers Release Numbering
 *
 *  - 1.6.X - a release that includes new functionality
 *  - 1.6.X.Y - a release that contains fixes for bugs or security issues identified
 *    in the 1.6.X release series.
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
