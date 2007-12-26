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
 * \author Martin Pycko <martinp@digium.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/module.h"

static char *nocdr_descrip = 
"  NoCDR(): This application will tell Asterisk not to maintain a CDR for the\n"
"current call.\n";

static char *nocdr_app = "NoCDR";
static char *nocdr_synopsis = "Tell Asterisk to not maintain a CDR for the current call";


static int nocdr_exec(struct ast_channel *chan, void *data)
{
	if (chan->cdr)
		ast_set_flag(chan->cdr, AST_CDR_FLAG_POST_DISABLED);

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(nocdr_app);
}

static int load_module(void)
{
	if (ast_register_application(nocdr_app, nocdr_exec, nocdr_synopsis, nocdr_descrip))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Tell Asterisk to not maintain a CDR for the current call");
