/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2004, Andy Powell & TAAN Softworks Corp. 
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
 * \brief Say Polish counting words
 * \author Andy Powell
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="SayCountPL" language="en_US">
		<synopsis>
			Say Polish counting words.
		</synopsis>
		<syntax>
			<parameter name="word1" required="true" />
			<parameter name="word2" required="true" />
			<parameter name="word5" required="true" />
			<parameter name="number" required="true" />
		</syntax>
		<description>
			<para>Polish grammar has some funny rules for counting words. for example 1 zloty,
			2 zlote, 5 zlotych. This application will take the words for 1, 2-4 and 5 and
			decide based on grammar rules which one to use with the number you pass to it.</para>
			<para>Example: SayCountPL(zloty,zlote,zlotych,122) will give: zlote</para>
		</description>
	</application>

 ***/
static const char app[] = "SayCountPL";

static int saywords(struct ast_channel *chan, char *word1, char *word2, char *word5, int num)
{
	/* Put this in a separate proc because it's bound to change */
	int d = 0;

	if (num > 0) {
		if (num % 1000 == 1) {
			ast_streamfile(chan, word1, chan->language);
			d = ast_waitstream(chan,"");
		} else if (((num % 10) >= 2) && ((num % 10) <= 4 ) && ((num % 100) < 10 || (num % 100) > 20)) {
			ast_streamfile(chan, word2, chan->language);
			d = ast_waitstream(chan, "");
		} else {
			ast_streamfile(chan, word5, chan->language);
			d = ast_waitstream(chan, "");
		}
	}

	return d;
}


static int sayword_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char *s;
	int inum;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(word1);
		AST_APP_ARG(word2);
		AST_APP_ARG(word5);
		AST_APP_ARG(num);
	);

	if (!data) {
		ast_log(LOG_WARNING, "SayCountPL requires 4 arguments: word-1,word-2,word-5,number\n");
		return -1;
	}

	s = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, s);

	/* Check to see if params passed */
	if (!args.word1 || !args.word2 || !args.word5 || !args.num) {
		ast_log(LOG_WARNING, "SayCountPL requires 4 arguments: word-1,word-2,word-3,number\n");
		return -1;
	}

	if (sscanf(args.num, "%d", &inum) != 1) {
		ast_log(LOG_WARNING, "'%s' is not a valid number\n", args.num);
		return -1;
	}

	/* do the saying part (after a bit of maths) */

	res = saywords(chan, args.word1, args.word2, args.word5, inum);

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(app, sayword_exec);

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Say polish counting words");
