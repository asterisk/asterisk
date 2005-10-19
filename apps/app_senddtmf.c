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

/*
 *
 * App to send DTMF digits
 * 
 */
 
#include <string.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static char *tdesc = "Send DTMF digits Application";

static char *app = "SendDTMF";

static char *synopsis = "Sends arbitrary DTMF digits";

static char *descrip = 
"  SendDTMF(digits[|timeout_ms]): Sends DTMF digits on a channel. \n"
"  Accepted digits: 0-9, *#abcd\n"
" Returns 0 on success or -1 on a hangup.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int senddtmf_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char *digits = NULL, *to = NULL;
	int timeout = 250;

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SendDTMF requires an argument (digits or *#aAbBcCdD)\n");
		return 0;
	}

	LOCAL_USER_ADD(u);

	digits = ast_strdupa(data);
	if (!digits) {
		ast_log(LOG_ERROR, "Out of Memory!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if ((to = strchr(digits,'|'))) {
		*to = '\0';
		to++;
		timeout = atoi(to);
	}
		
	if(timeout <= 0)
		timeout = 250;

	res = ast_dtmf_stream(chan,NULL,digits,timeout);
		
	LOCAL_USER_REMOVE(u);

	return res;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;	
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
