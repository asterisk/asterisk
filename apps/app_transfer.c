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
 * \brief Transfer a caller
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/options.h"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static const char *tdesc = "Transfer";

static const char *app = "Transfer";

static const char *synopsis = "Transfer caller to remote extension";

static const char *descrip = 
"  Transfer([Tech/]dest):  Requests the remote caller be transfered\n"
"to a given extension. If TECH (SIP, IAX2, LOCAL etc) is used, only\n"
"an incoming call with the same channel technology will be transfered.\n"
"Note that for SIP, if you transfer before call is setup, a 302 redirect\n"
"SIP message will be returned to the caller.\n"
"\nThe result of the application will be reported in the TRANSFERSTATUS\n"
"channel variable:\n"
"       SUCCESS       Transfer succeeded\n"
"       FAILURE      Transfer failed\n"
"       UNSUPPORTED  Transfer unsupported by channel driver\n"
"Returns -1 on hangup, or 0 on completion regardless of whether the\n"
"transfer was successful.\n\n"
"Old depraciated behaviour: If the transfer was *not* supported or\n"
"successful and there exists a priority n + 101,\n"
"then that priority will be taken next.\n" ;

static int transfer_exec(struct ast_channel *chan, void *data)
{
	int res;
	int len;
	struct localuser *u;
	char *slash;
	char *tech = NULL;
	char *dest = data;
	char *status;

	if (!dest || ast_strlen_zero(dest)) {
		ast_log(LOG_WARNING, "Transfer requires an argument ([Tech/]destination)\n");
		pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "FAILURE");
		return 0;
	}

	LOCAL_USER_ADD(u);

	if ((slash = strchr(dest, '/')) && (len = (slash - dest))) {
		tech = dest;
		dest = slash + 1;
		/* Allow execution only if the Tech/destination agrees with the type of the channel */
		if (strncasecmp(chan->type, tech, len)) {
			pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "FAILURE");
			LOCAL_USER_REMOVE(u);
			return 0;
		}
	}

	/* Check if the channel supports transfer before we try it */
	if (!chan->tech->transfer) {
		pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", "UNSUPPORTED");
		LOCAL_USER_REMOVE(u);
		return 0;
	}

	res = ast_transfer(chan, dest);

	if (res < 0) {
		status = "FAILURE";
		if (option_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		res = 0;
	} else {
		status = "SUCCESS";
		res = 0;
	}

	pbx_builtin_setvar_helper(chan, "TRANSFERSTATUS", status);

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
	return ast_register_application(app, transfer_exec, synopsis, descrip);
}

char *description(void)
{
	return (char *) tdesc;
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
