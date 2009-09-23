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
 *
 * The purpose of this document is to briefly describe the various statuses an
 * issue can be placed in, and what that status means. In addition, the simple
 * workflow and transition between statuses will be discussed.
 *
 * \section StatusDefinitions Issue Status Definitions
 *
 * \subsection New New
 *       The 'New' status is where all bugs start. This is an issue which has not
 *       received a review by a bug marshal to move it to an appropriate next
 *       status. Steps required to move to the next logical step include:
 *
 *       \arg checking Category and Severity are set correctly
 *       \arg verifying the issue does not look like a support issue (if so, note
 *            the reporter should use the appropriate support channels, and change
 *            status to Closed)
 *       \arg determine that enough debugging information has been provided so that
 *            a developer can move the issue forward (e.g. on SIP issues, that the
 *            standard SIP debug and history, console output, and configuration
 *            file information has been provided, or in the case of a crash issue,
 *            that a proper backtrace has been provided)
 *
 *       If the necessary information has not been collected, then the issue
 *       should be moved to Feedback status, and the missing information be
 *       requested by the reporter*.
 *
 *       When all required information has been collected, the issue can be moved
 *       to the Acknowledged status.
 *
 *       \note (*) issues which remain in the Feedback status for more than 2 weeks
 *             without feedback from the reporter should be marked as
 *             Closed/Suspended
 *
 * \subsection Acknowledged Acknowledged
 *       The 'Acknowledged' status is the first step to issue resolution. It is
 *       an issue that has been filed correctly, the categorization and severity
 *       have been set, and that initial debugging information has been
 *       collected.
 *
 *       A developer may then review the issue tracker for Acknowledged issues
 *       and to determine whether additional information is necessary (i.e. that
 *       a crash issue with backtrace requires valgrind output, or other
 *       non-standard debugging feedback).
 *
 *       Issues may be moved to the next step in the workflow when the developer
 *       has either determined the issue is Confirmed, requires additional
 *       Feedback, is an issue that has already been resolved (or does not need
 *       to be resolved), in which case it should be Closed.
 *
 * \subsection Confirmed Confirmed
 *       The 'Confirmed' status represents issues which have been verified as
 *       existing in the current branch(es), and has all necessary information to
 *       be accepted into Acknowledged status. The general qualifier for an issue
 *       being moved to Confirmed is more than one community member stating the
 *       issue exists.
 *
 *       Confirmed issues may also contain patches created by developers which
 *       need to be applied in order to gain further knowledge or debugging by
 *       the original reporter, or any other community member who has verified
 *       this issue as existing. The developer will then move the issue to
 *       Feedback status while waiting for information from the reporter(s).
 *
 *       Issues with patches that are candidates for inclusion into the various
 *       branches that should resolve the issue are to be moved to the
 *       Ready for Testing status.
 *
 * \subsection ReadyForTesting Ready for Testing
 *       'Ready for Testing' indicates issues which have patches available for
 *       testing by community members or the original reporter which should
 *       resolve the reported issue.
 *
 *       If the patch does not resolve the issue, then it should be placed back
 *       into Confirmed status until an additional patch can be created for
 *       testing.
 *
 *       If the patch resolves the issue, then it should be moved to the Ready
 *       for Review status. 
 *
 * \subsection ReadyForReview Ready for Review
 *       When an issue has a patch that has been tested by a community member and
 *       which resolves the originally reported issue, should then be moved to
 *       the Ready for Review status. Issues marked as Ready for Review should
 *       then either be reviewed by another developer prior to merging (if it is
 *       a non-invasive fix), or the patch should be placed on Reviewboard if it
 *       is a complicated or invasive fix.
 *
 *       If an issue has a reviewboard link, it should be placed in the
 *       Additional Information section of an issue, and the marker [review]
 *       prefixed to the issue title.
 *
 * \subsection Resolved Resolved
 *        The Resolved status is rarely used directly by manual intervention, but
 *       rather is utilized by svnbot and other automated methods prior to an
 *       issue being closed.
 *
 * \subsection Feedback Feedback
 *       Feedback is used when an issue is awaiting information from the original
 *       reporter, or other active members of the community in the issue. Issues
 *       which remain in the feedback state longer than 2 weeks without feedback
 *       from any active participants, and which cannot be moved without, are to
 *       be Closed and marked as suspended.
 *
 * \subsection LicenseRequired License Required
 *       License Required is used when a patch has been attached to an issue, but
 *       which is currently in the License Pending state, or has been rejected
 *       and is awaiting the reporter to resign the license. 
 *
 * \subsection Assigned Assigned
 *        Issues marked as Assigned are the responsibility of the assigned
 *       developer, typically because they contain some sort of special knowledge
 *       required to resolve the issue, or because they have decided to take
 *       responsibility for moving the issue to resolution.
 *
 * \subsection Closed Closed
 *       Issues which have a resolution are marked as Closed. There are several
 *       resolutions that a Closed issue can contain, such as Fixed, Won't Fix,
 *       Duplicate, or Suspended. Issues that have been Closed manually should
 *       have an appropriate resolution set such as Suspended or Won't Fix, along
 *       with a note indicating why the issue was Closed.
 *
 *       If the issue is being closed due to a lack of feedback, the resolution
 *       should be Suspended, and a note indicating the issue was closed due to
 *       a lack of feedback, and that it will be reopened upon request if the
 *       reporter can provide the necessary information to move the issue forward
 *       again.
 *
 * <hr/>
 *
 * \section TypicalWorkflow Typical Workflow
 *
 * The typical workflow of an issue is as follows:
 *
 * \subsection Brief Brief
 *
 * New --> Feedback(*) --> Acknowledged --> Confirmed --> Ready for Testing
 *   --> Ready for Review --> Closed (commited, closed by svnbot)
 *
 * \note (*)Optional status used when not enough information provided to move to
 *          Acknowledged.
 *
 * \subsection Verbose Verbose
 *
 * - Issue is filed by a community member. All issues will start in the status New.
 *
 * - An issue marshal then performs triage on New issues and determined if they are
 *   valid issues (non-support), have been correctly categorized, have the
 *   necessary debugging information, etc...
 *   - Issues without the necessary information are moved to Feedback
 *   - Issues that are support, or feature requests without patches, are Closed
 *   - Issues that have all the necessary debugging information to move forward
 *     are Acknowledged
 *   .
 *
 * - After an issue has been moved to the Acknowledged state, then a developer will
 *   review to determine if the issue exists, and if so, to move it to the
 *   Confirmed state. If additional information is required, the issue may be moved
 *   to Feedback state.
 *
 * - An issue reaches the Confirmed state when the issue has been verified to
 *   exist. Issues that are Confirmed may also contain patches that provide
 *   additional debugging information.
 *
 * - Issues that have patches that require testing and feedback from the community
 *   are then placed in the Ready for Testing status.
 *
 * - Once a patch has been tested and confirmed to resolve the issue, we change the
 *   status to Ready for Review.
 *
 * - An issue that is Ready for Review needs to be looked over by a developer, or
 *   placed on Reviewboard (for larger patches) prior to being commited. Issues
 *   that are in Ready for Review are typically ready, or nearly ready to be
 *   commited.
 *
 * - Once an issue has been commited, svnbot will then Close the issue if the
 *   correct keywords are used in the commit message (see Guidelines for Commit
 *   Messages)
 * .
 * <hr/>
 */
