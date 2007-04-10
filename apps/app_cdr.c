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
 * 
 * \brief Applications connected with CDR engine
 *
 * Martin Pycko <martinp@digium.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <stdlib.h>

#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"

static char *nocdr_descrip = 
"  NoCDR(): This application will tell Asterisk not to maintain a CDR for the\n"
"current call.\n";

static char *nocdr_app = "NoCDR";
static char *nocdr_synopsis = "Tell Asterisk to not maintain a CDR for the current call";


static int nocdr_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	
	u = ast_module_user_add(chan);

	if (chan->cdr) {
		ast_set_flag(chan->cdr, AST_CDR_FLAG_POST_DISABLED);
	}

	ast_module_user_remove(u);

	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(nocdr_app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	return ast_register_application(nocdr_app, nocdr_exec, nocdr_synopsis, nocdr_descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Tell Asterisk to not maintain a CDR for the current call");
