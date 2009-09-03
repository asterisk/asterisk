/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

/*! \file
 *
 * \brief Playback the special information tone to get rid of telemarketers
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"

static char *app = "Zapateller";

static char *synopsis = "Block telemarketers with SIT";

static char *descrip = 
"  Zapateller(options):  Generates special information tone to block\n"
"telemarketers from calling you.  Options is a pipe-delimited list of\n" 
"options.  The following options are available:\n"
"'answer' causes the line to be answered before playing the tone,\n" 
"'nocallerid' causes Zapateller to only play the tone if there\n"
"is no callerid information available.  Options should be separated by |\n"
"characters\n";


static int zapateller_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u;
	int answer = 0, nocallerid = 0;
	char *c;
	char *stringp=NULL;
	
	u = ast_module_user_add(chan);

	stringp=data;
        c = strsep(&stringp, "|");
        while(!ast_strlen_zero(c)) {
		if (!strcasecmp(c, "answer"))
			answer = 1;
		else if (!strcasecmp(c, "nocallerid"))
			nocallerid = 1;

                c = strsep(&stringp, "|");
        }

	ast_stopstream(chan);
	if (chan->_state != AST_STATE_UP) {

		if (answer) 
			res = ast_answer(chan);
		if (!res) {
			res = ast_safe_sleep(chan, 500);
		}
	}
	if (!ast_strlen_zero(chan->cid.cid_num) && nocallerid) {
		ast_module_user_remove(u);
		return res;
	} 
	if (!res) 
		res = ast_tonepair(chan, 950, 0, 330, 0);
	if (!res) 
		res = ast_tonepair(chan, 1400, 0, 330, 0);
	if (!res) 
		res = ast_tonepair(chan, 1800, 0, 330, 0);
	if (!res) 
		res = ast_tonepair(chan, 0, 0, 1000, 0);
	ast_module_user_remove(u);
	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	
	ast_module_user_hangup_all();

	return res;	
}

static int load_module(void)
{
	return ast_register_application(app, zapateller_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Block Telemarketers with Special Information Tone");
