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
 * \brief App to transmit an image
 * 
 * \ingroup applications
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
#include "asterisk/image.h"
#include "asterisk/app.h"
#include "asterisk/options.h"

static char *tdesc = "Image Transmission Application";

static char *app = "SendImage";

static char *synopsis = "Send an image file";

static char *descrip = 
"  SendImage(filename): Sends an image on a channel. \n"
"If the channel supports image transport but the image send\n"
"fails, the channel will be hung up. Otherwise, the dialplan\n"
"continues execution.\n"
"The option string may contain the following character:\n"
"	'j' -- jump to priority n+101 if the channel doesn't support image transport\n"
"This application sets the following channel variable upon completion:\n"
"	SENDIMAGESTATUS		The status is the result of the attempt as a text string, one of\n"
"		OK | NOSUPPORT \n";			

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int sendimage_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char *parse;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(options);
	);
	
	LOCAL_USER_ADD(u);

	if (!(parse = ast_strdupa(data))) {
		ast_log(LOG_WARNING, "Memory Error!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.filename)) {
		ast_log(LOG_WARNING, "SendImage requires an argument (filename[|options])\n");
		return -1;
	}

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}

	if (!ast_supports_images(chan)) {
		/* Does not support transport */
		if (priority_jump || option_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "SENDIMAGESTATUS", "NOSUPPORT");
		LOCAL_USER_REMOVE(u);
		return 0;
	}

	res = ast_send_image(chan, args.filename);
	
	if (!res)
		pbx_builtin_setvar_helper(chan, "SENDIMAGESTATUS", "OK");
	
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
	return ast_register_application(app, sendimage_exec, synopsis, descrip);
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
