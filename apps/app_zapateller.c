/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Playback the special information tone to get rid of telemarketers
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static char *tdesc = "Block Telemarketers with Special Information Tone";

static char *app = "Zapateller";

static char *synopsis = "Block telemarketers with SIT";

static char *descrip = 
"  Zapateller(options):  Generates special information tone to block telemarketers\n"
"from calling you.  Returns 0 normally or -1 on hangup.  Options is a pipe-delimited\n"
"list of options.  The only supported option is 'answer' which will cause the line to\n"
"be answered before playing the tone";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int zapateller_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	
	LOCAL_USER_ADD(u);
	ast_stopstream(chan);
	if (chan->_state != AST_STATE_UP) {
		if (data && !strcasecmp(data, "answer")) 
			res = ast_answer(chan);
		if (!res) {
			res = ast_safe_sleep(chan, 500);
		}
	}
	if (!res) 
		res = ast_tonepair(chan, 950, 0, 330, 0);
	if (!res) 
		res = ast_tonepair(chan, 1400, 0, 330, 0);
	if (!res) 
		res = ast_tonepair(chan, 1800, 0, 330, 0);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, zapateller_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
