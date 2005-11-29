/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to send DTMF digits
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
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/app.h>
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Send DTMF digits Application";

static char *app = "SendDTMF";

static char *synopsis = "Sends arbitrary DTMF digits";

static char *descrip = 
"  SendDTMF(digits): Sends DTMF digits on a channel. \n"
"  Accepted digits: 0-9, *#abcd\n"
" Returns 0 on success or -1 on a hangup.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int senddtmf_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char *digits = data;

	if (!digits || ast_strlen_zero(digits)) {
		ast_log(LOG_WARNING, "SendDTMF requires an argument (digits or *#aAbBcCdD)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	res = ast_dtmf_stream(chan,NULL,digits,250);
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
	return ast_register_application(app, senddtmf_exec, synopsis, descrip);
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
