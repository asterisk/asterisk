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
 * \page Reviewboard Reviewboard Usage and Guidelines
 *
 * \AsteriskTrunkWarning
 *
 * <hr/>
 *
 * \section ReviewboardGuidelines Usage Guidelines
 *
 * Mantis (https://issues.asterisk.org) and Reviewboard (https://reviewboard.asterisk.org)
 * are both utilities that the Asterisk development community uses to help
 * track and review code being written for Asterisk.  Since both systems
 * are used for posting patches, it is worth discussing when it is appropriate
 * to use reviewboard and when not.
 *
 * Here are the situations in which it is appropriate to post code to reviewboard:
 *  - A committer has a patch that they would like to get some feedback on before
 *    merging into one of the main branches.
 *  - A committer or bug marshal has requested a contributor to post their patch
 *    from Mantis on reviewboard to aid in the review process.  This typically
 *    happens with complex code contributions where reviewboard can help aid in
 *    providing feedback.
 *
 * We do encourage all interested parties to participate in the review process.
 * However, aside from the cases mentioned above, we prefer that all code
 * submissions first go through Mantis.
 *
 * \note It is acceptable for a committer to post patches to reviewboard before
 * they are complete to get some feedback on the approach being taken.  However,
 * if the code is not yet ready to be merged, it \b must be documented as such.
 * A review request with a patch proposed for merging should have documented
 * testing and should not have blatant coding guidelines violations.  Lack of
 * these things is careless and shows disrespect for those reviewing your code.
 *
 * <hr/>
 *
 * \section ReviewboardPosting Posting Code to Reviewboard
 *
 * \subsection postreview Using post-review
 *
 * The easiest way to post a patch to reviewboard is by using the
 * post-review tool.  We have post-review in our repotools svn repository.
 *
   \verbatim
   $ svn co http://svn.digium.com/svn/repotools
   \endverbatim
 *
 * Essentially, post-review is a script that will take the output of "svn
 * diff" and create a review request out of it for you.  So, once you have
 * a working copy with the changes you expect in the output of "svn diff",
 * you just run the following command:
 *
   \verbatim
   $ post-review
   \endverbatim
 * 
 * If it complains about not knowing which reviewboard server to use, add
 * the server option:
 * 
   \verbatim
   $ post-review --server=https://reviewboard.asterisk.org
   \endverbatim
 *
 * \subsection postreviewnewfiles Dealing with New Files
 * 
 * I have one final note about an oddity with using post-review.  If you
 * maintain your code in a team branch, and the new code includes new
 * files, there are some additional steps you must take to get post-review
 * to behave properly.
 * 
 * You would start by getting your changes applied to a trunk working copy:
 * 
   \verbatim
   $ cd .../trunk
   \endverbatim
 * 
 * Then, apply the changes from your branch:
 * 
   \verbatim
   $ svn merge .../trunk .../team/group/my_new_code
   \endverbatim
 * 
 * Now, the code is merged into your working copy.  However, for a new
 * file, subversion treats it as a copy of existing content and not new
 * content, so new files don't show up in "svn diff" at this point.  To get
 * it to show up in the diff, use the following commands so svn treats it
 * as new content and publishes it in the diff:
 * 
   \verbatim
   $ svn revert my_new_file.c
   $ svn add my_new_file.c
   \endverbatim
 * 
 * Now, it should work, and you can run "post-review" as usual.
 *
 * \subsection postreviewupdate Updating Patch on Existing Review Request
 *
 * Most of the time, a patch on reviewboard will require multiple iterations
 * before other sign off on it being ready to be merged.  To update the diff
 * for an existing review request, you can use post-review and the -r option.
 * Apply the current version of the diff to a working copy as described above,
 * and then run the following command:
 * 
   \verbatim
   $ post-review -r <review request number>
   \endverbatim
 */
