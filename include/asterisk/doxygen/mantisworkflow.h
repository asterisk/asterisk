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
 * \page MantisWorkflow Workflow Guidelines for Asterisk Open Source Issue Tracker
 *
 * \AsteriskTrunkWarning
 *
 * <hr/>
 * \section WorkflowDescription Description of the Issue Tracker Workflow
 * 
 * (This document is most beneficial for Asterisk bug marshals, however it is good
 * reading for anyone who may be filing issues or wondering how the Asterisk Open
 * Source project moves issues through from filing to completion.)
 * 
 * The workflow in the issue tracker should be handled in the following way:
 * 
 * -# A bug is reported and is automatically placed in the 'New' status.
 * -# The Bug Marshall team should go through bugs in the 'New' status to determine 
 *    whether the report is valid (not a duplicate, hasn't already been fixed, not 
 *    a Digium tech support issue, etc.).  Invalid reports should be set to 
 *    'Closed' with the appropriate resolution set. Categories and descriptions 
 *    should be corrected at this point.[Note1]\n
 *    Issues should also have enough information for a developer to either 
 *    reproduce the issue or determine where an issue exists (or both). If this is 
 *    not the case then the issue should be moved to 'Feedback' prior to moving 
 *    forward in the workflow.
 * -# The next step is to determine whether the report is about a bug or a 
 *    submission of a new feature:
 *       -# BUG: A bug should be moved into the status 'Acknowledged' if enough
 *          information has been provided by the reporter to either reproduce the
 *          issue or clearly see where an issue may lie. The bug may also be
 *          assigned to a developer for the creation of the initial patch, or
 *          review of the issue.\n
 *          Once a patch has been created for the issue and attached, the issue can
 *          then be moved to the 'Confirmed' status. At this point, initial code 
 *          review and discussion about the patch will take place. Once an adequate
 *          amount of support for the implementation of the patch is acquired, then
 *          the bug can be moved to the 'Ready for Testing' status for wider 
 *          testing by the community. After the testing phase is complete and it
 *          appears the issue is resolved, the patch can be committed by a 
 *          developer and closed.
 *       -# FEATURE: As new features should be filed with a patch, it can be 
 *          immediately moved to the 'confirmed' status, making it ready for basic
 *          formatting and code review. From there any changes to style or feel of
 *          the patch based on feedback from the community can be discussed, and
 *          changes to the patch made. It can then be moved forward to the 'Ready 
 *          for Testing' status. Once the feature has been merged, or a decision
 *          has been made that it will not be merged, the issue should be taken to 
 *          'Closed' with the appropriate resolution.[Note2]
 * -# If at any point in the workflow, an issue requires feedback from the original
 *    poster of the issue, the status should be changed to 'Feedback'.  Once the 
 *    required information has been provided, it should be placed back in the
 *    appropriate point of the workflow.
 * -# If at any point in the workflow, a developer or bug marshal would like to 
 *    take responsibility for doing the work that is necessary to progress an 
 *    issue, the status can be changed to 'Assigned'. At that point the developer
 *    assigned to the issue will be responsible for moving the issue to completion.
 * 
 * \section WorkflowSummary Workflow Summary
 * 
 * The following is a list of valid statuses and what they mean to the work flow.
 *
 * \subsection New New
 *    This issue is awaiting review by bug marshals.  Categorization and summaries
 *    should be fixed as appropriate.
 * 
 * \subsection Feedback
 *    This issue requires feedback from the poster of the issue before any
 *    additional progress in the workflow can be made. This may include providing
 *    additional debugging information, or a backtrace with DONT_OPTIMIZE enabled,
 *    for example. (See https://wiki.asterisk.org/wiki/display/AST/Debugging)
 * 
 * \subsection Acknowledged
 *    This is a submitted bug which has no patch associated with it, but appears
 *    to be a valid bug based on the description and provided debugging
 *    information.
 * 
 * \subsection Confirmed
 *    The patch associated with this issue requires initial formatting and code
 *    review, and may have some initial testing done. It is waiting for a 
 *    developer to confirm the patch will no longer need large changes made to it,
 *    and is ready for wider testing from the community. This stage is used for
 *    discussing the feel and style of a patch, in addition to the coding style
 *    utilized.
 * 
 * \subsection Ready For Testing
 *    This is an issue which has a patch that is waiting for testing feedback from
 *    the community after it has been deemed to no longer need larger changes.
 * 
 * \subsection Assigned
 *    A developer or bug marshal has taken responsibility for taking the necessary
 *    steps to move forward in the workflow.  Once the issue is ready to be
 *    reviewed and feedback provided, it should be placed back into the
 *    appropriate place of the workflow.
 * 
 * \subsection Resolved
 *    A resolution for this issue has been reached.  This issue should immediately
 *    be Closed.
 * 
 * \subsection Closed
 *    No further action is necessary for this issue.
 * 
 * \section SeverityLevels Severity Levels
 * 
 * Severity levels generally represent the number of users who are potentially
 * affected by the reported issue. 
 * 
 * \subsection Feature Feature
 *    This issue is a new feature and will only be committed to Asterisk trunk.
 *    Asterisk trunk is where future branches will be created and thus this
 *    feature will only be found in future branches of Asterisk and not merged
 *    into existing branches. (See Release Branch Commit Policy below.)
 * 
 * \subsection Trivial Trivial
 *    A trivial issue is something that either affects an insignificant number of
 *    Asterisk users, or is a minimally invasive change that does not affect
 *    functionality.
 * 
 * \subsection Text Text
 *    A text issue is typically something like a spelling fix, a clarifying of a
 *    debugging or verbose message, or changes to documentation.
 * 
 * \subsection Tweak Tweak
 *    A tweak to the code the has the potential to either make code clearer to
 *    read, or a change that could speed up processing in certain circumstances.
 *    These changes are typically only a couple of lines.
 * 
 * \subsection Minor Minor
 *    An issue that does not affect a large number of Asterisk users, but not an
 *    insignificant number. The number of lines of code and development effort to
 *    resolve this issue could be non-trivial.
 * 
 * \subsection Major Major
 *    As issue that affects the majority of Asterisk users. The number of lines of
 *    code and development effort required to resolve this issue could be
 *    non-trivial.
 * 
 * \subsection Crash Crash
 *    An issue marked as a Crash is something that would cause Asterisk to be
 *    unusable for a majority of Asterisk users and is an issue that causes a
 *    deadlock or crash of the Asterisk process.
 * 
 * \subsection Block Block
 *    A blocking issue is an issue that must be resolved before the next release
 *    of Asterisk as would affect a significant number of Asterisk users, or could
 *    be a highly visible regression. A severity of block should only be set by
 *    Asterisk bug marshals at their discretion.
 * 
 *        *** USERS SHOULD NOT FILE ISSUES WITH A SEVERITY OF BLOCK ***
 * 
 * \section PriorityLevels Priority Levels
 *
 * Currently, the following priority levels are listed on the issue tracker:
 * - None
 * - Low
 * - Normal
 * - High
 * - Urgent
 * - Immediate
 *
 * However, at this time they are not utilized and all new issue should have a priority of 'Normal'.
 *
 * \section Notes Notes
 * 
 * -# Using the "Need Triage" filter is useful for finding these issues quickly.
 * -# The issue tracker now has the ability to monitor the commits list, and if
 *    the commit message contains something like, "(Closes issue #9999)", the bug
 *    will be automatically closed.\n
 *    See http://www.asterisk.org/doxygen/trunk/CommitMessages.html for more
 *    information on commit messages.
 * 
 * \section ReleaseBranchCommitPolicy Release Branch Commit Policy
 * 
 * The code in the release branches should be changed as little as possible.  The 
 * only time the release branches will be changed is to fix a bug.  New features 
 * will never be included in the release branch unless a special exception is made
 * by the release branch maintainers.
 * 
 * Sometimes it is difficult to determine whether a patch is considered to fix a 
 * bug or if it is a new feature.  Patches that are considered code cleanup, or to 
 * improve performance, are NOT to be included in the release branches. Performance
 * issues will only be considered for the release branch if they are considered 
 * significant, and should be approved by the maintainers.
 * 
 * If there is ever a question about what should be included in the release branch,
 * the maintainers should be allowed to make the decision.
 */
