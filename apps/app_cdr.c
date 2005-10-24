/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Martin Pycko <martinp@digium.com>
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
 * \brief Applications connected with CDR engine
 *
 */

#include <sys/types.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"


static char *tdesc = "Make sure asterisk doesn't save CDR for a certain call";

static char *nocdr_descrip = "NoCDR(): makes sure there won't be any CDR written for a certain call";
static char *nocdr_app = "NoCDR";
static char *nocdr_synopsis = "Make sure asterisk doesn't save CDR for a certain call";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int nocdr_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	
	LOCAL_USER_ADD(u);

	if (chan->cdr) {
		ast_cdr_free(chan->cdr);
		chan->cdr = NULL;
	}

	LOCAL_USER_REMOVE(u);

	return 0;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(nocdr_app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	return ast_register_application(nocdr_app, nocdr_exec, nocdr_synopsis, nocdr_descrip);
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
