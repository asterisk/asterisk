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
 * \page AsteriskGitHowto How to setup a local GIT mirror of the Asterisk SVN repository
 *
 * \AsteriskTrunkWarning
 *
 * <hr/>
 *
 * \section Introduction Introduction
 * This document will instruct you how to setup a local git mirror of the 
 * Asterisk SVN repository.
 * 
 * Why would you want that? for starters, it's a fast repository browser
 * and works well even when offline. More on why and why not at 'Pros and Cons'
 * in the end of this document. 
 * <hr/>
 *
 * \section Setup Setup
 *
 * Make sure you have the package
 *
 \verbatim
  git-svn
 \endverbatim
 *
 * installed. It is part of the standard git distribution and included in
 * any recent Linux distribution.
 *
 * Next, get the files from this repository: 
 *
 \verbatim
  git clone http://git.tzafrir.org.il/git/asterisk-tools.git
 \endverbatim
 *
 * Which will create the subdirectory 'asterisk-tools' under your working 
 * directory. For the purpose of this HOWTO I assume that you will later 
 * download Asterisk under the same directory.
 * 
 * Now let's get Asterisk:
 * 
 \verbatim
  git svn clone -s http://svn.digium.com/svn/asterisk
 \endverbatim
 * 
 * This will download the whole /trunk , /tags and /branches hirarchies
 * to a new git repository under asterisk/ .
 * This will take a   L  O  N  G   time. In the order of magnitude of a
 * day. If it stops in the middle:
 *
 \verbatim
  # cd asterisk; git svn fetch --fetch-all
 \endverbatim
 *
 * All commands as of this point are run from the newly-created subdirectory
 * 'asterisk'
 *
 \verbatim
  cd asterisk
 \endverbatim
 *
 * Next make your repository more compact:
 * 
 * \note FIXME: I now get a .git subdirectory of the size of 135MB. This seems
 *       overly large considering what I got a few monthes ago.
 *
 \verbatim
  git repack -a
 \endverbatim
 *
 * Now fix the menuselect bits. One possible venue is to use submodules.
 * This would require setting a separate menuselect repository . And
 * fixing the submodule references in every new tag to point to the right
 * place. I gave up at this stage, and instead reimplememented menuselect
 *
 \verbatim
  cp -a ../asterisk-tools/menuselect menuselect
  make -C menuselect dummies
  chmod +x menuselect/menuselect
 \endverbatim
 * 
 * Next thing to do is ignore generated files. .gitignore is somewhat
 * like svn:ignore . Though it is possible to use one at the top
 * directory. Hence I decided to make it ignore itself as well:
 *
 \verbatim
  cp ../asterisk-tools/asterisk_gitignore .gitignore
 \endverbatim
 * 
 * Now let's generate tags that will point to the tags/* branches.
 * e.g. tag 'v1.4.8' will point to the head of branch tags/1.4.8 .
 * If you don't like the extra 'v', just edit the sed command.
 *
 \verbatim
  ../asterisk-tools/update-tags
 \endverbatim
 * 
 * Example configuration (refer to menuselect/menuselelct for more
 * information). For instance: res_snmp breaks building 1.4 from git:
 *
 \verbatim
  echo 'exclude res_snmp' >build_tools/conf
 \endverbatim
 *
 * <hr/>
 *
 * \section Update Update
 * The main Asterisk repository tends to get new commits occasionally. I
 * suppose you want those updates in your local copy. The following command
 * should normally be done from the master branch. If you actually use branches, 
 * it is recommended to switch to it beforehand:
 *
 \verbatim
  git checkout master
 \endverbatim
 *
 * Next, get all updates.
 * <hr/>
 *
 * \section Usage Usage
 *
 * If you use git from the command-line, it is highly recommended to enable
 * programmable bash completion. The git command-line is way more complex
 * than svn, but the completion makes it usable:
 *
 *
 \verbatim
  asterisk$ git show v1.2.28<tab><tab>
  v1.2.28     v1.2.28.1

  asterisk$ git show v1.2.28:c<tab><tab>
  callerid.c     channel.c      cli.c          coef_out.h     contrib/
  cdr/           channels/      codecs/        config.c       cryptostub.c
  cdr.c          chanvars.c     coef_in.h      configs/       cygwin/

  asterisk$ git svn<tab><tab>
  clone            fetch            log              set-tree
  commit-diff      find-rev         propget          show-externals
  create-ignore    info             proplist         show-ignore
  dcommit          init             rebase

  asterisk$ git svn rebase --f
  --fetch-all       --follow-parent
 \endverbatim
 * 
 * Some useful commands:
 *
 \verbatim
  git svn rebase --fetch-all # pull updates from upstream
  man git-FOO                # documentation for 'git FOO'
  # <tree> is any place on graph of branches: HEAD, name of a branch or
  # a tag, commit ID, and some others
  git show <tree>            # The top commit in this tree (log + diff)
  git show <tree>:directory  # directory listing
  git show <tree>:some/file  # get that file
  git log <tree>             # commit log up to that point
  git branch                 # shows local branches and in which one you are
  git branch -r              # List remote branches. Such are SVN ones.
 \endverbatim
 *
 * For more information, see the man page gittutorial as well as
 * \arg http://git-scm.com/documentation
 *
 \verbatim
  git svn rebase --fetch-all
 \endverbatim
 *
 * <hr/>
 *
 * \section ProsAndCons Pros and Cons
 *
 * \subsection TheGood The Good
 *
 * Working off-line:
 *  If you want to be able to use 'svn log' and 'svn diff' to a different
 *  branch, now you can.
 *
 * Efficient repository browser:
 *  With git you can effectively browse commit logs and working copies of
 *  various branches. In fact, using it merely as a logs and versions
 *  browser can be useful on its own.
 *
 * Branches really work:
 *  With SVN merging a branch is complicated. Partially because lack of
 *  separate merge tracking.With git you don't need the extra svnmerge:
 *  changes that don't collide with your branch merge in a quick merge
 *  operation.
 *
 * \subsection Limitations Limitations
 * 
 * svn:externals :
 *  does not really work well with git-svn (and similar systems: svk,
 *  bzr-svn and hg-svn). Git has something called submodules that allows
 *  emulating the basic functionality of svn:externals, but is not as
 *  transparent.
 *
 * Commiting:
 *  Not sure how safe it is to commit from such a copy. In most places I
 *  see that it is not recommended to commit directly from git-svn. OTOH,
 *  git has some tools that make it easy to prepare a patch set out of a
 *  branch (e.g. git format-patch).
 *
 *  IIRC there are also some issues for git-svn with https certificate
 *  authentication in the first place.
 *
 * Tags:
 *  /tags are branches. SVN tags are really branches that we pretend not
 *  to change. And in fact in Asterisk we practically do change. But see
 *  workaround below to generate tags from the tag branches.
 *
 * /team branches::
 *  At least with git 1.5.x you can't easily follow all the team branches.
 *  This is due to a bug in their handling of wildcards in branches
 *  description. I believe this has been resolved in 1.6 but I didn't get
 *  to test that. Even if it will, it will require an extra step of manual
 *  editing.
 *
 * <hr/>
 */
