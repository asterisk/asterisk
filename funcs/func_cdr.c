/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Call Detail Record related dialplan functions
 * 
 * Copyright (C) 2005, Digium, Inc.
 * Portions Copyright (C) 2005, Anthony Minessale II
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk.h"

/* ASTERISK_FILE_VERSION(__FILE__, "$Revision$") */

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/cdr.h"

static char *builtin_function_cdr_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *ret;
	char *mydata;
	int argc;
	char *argv[2];
	int recursive = 0;

	if (!data || ast_strlen_zero(data))
		return NULL;
	
	if (!chan->cdr)
		return NULL;

	mydata = ast_strdupa(data);
	argc = ast_separate_app_args(mydata, '|', argv, sizeof(argv) / sizeof(argv[0]));

	/* check for a trailing flags argument */
	if (argc > 1) {
		argc--;
		if (strchr(argv[argc], 'r'))
			recursive = 1;
	}

	ast_cdr_getvar(chan->cdr, argv[0], &ret, buf, len, recursive);

	return ret;
}

static void builtin_function_cdr_write(struct ast_channel *chan, char *cmd, char *data, const char *value) 
{
	char *mydata;
	int argc;
	char *argv[2];
	int recursive = 0;

	if (!data || ast_strlen_zero(data) || !value)
		return;
	
	if (!chan->cdr)
		return;

	mydata = ast_strdupa(data);
	argc = ast_separate_app_args(mydata, '|', argv, sizeof(argv) / sizeof(argv[0]));

	/* check for a trailing flags argument */
	if (argc > 1) {
		argc--;
		if (strchr(argv[argc], 'r'))
			recursive = 1;
	}

	if (!strcasecmp(argv[0], "accountcode"))
		ast_cdr_setaccount(chan, value);
	else if (!strcasecmp(argv[0], "userfield"))
		ast_cdr_setuserfield(chan, value);
	else
		ast_cdr_setvar(chan->cdr, argv[0], value, recursive);
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function cdr_function = {
	.name = "CDR",
	.synopsis = "Gets or sets a CDR variable",
	.desc= "Option 'r' searches the entire stack of CDRs on the channel\n",
	.syntax = "CDR(<name>[|options])",
	.read = builtin_function_cdr_read,
	.write = builtin_function_cdr_write,
};

