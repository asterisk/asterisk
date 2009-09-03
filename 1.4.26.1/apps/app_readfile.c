/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Matt O'Gorman <mogorman@digium.com>
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
 * \brief ReadFile application -- Reads in a File for you.
 *
 * \author Matt O'Gorman <mogorman@digium.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"

static char *app_readfile = "ReadFile";

static char *readfile_synopsis = "ReadFile(varname=file,length)";

static char *readfile_descrip =
"ReadFile(varname=file,length)\n"
"  Varname - Result stored here.\n"
"  File - The name of the file to read.\n"
"  Length - Maximum number of characters to capture.\n";


static int readfile_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct ast_module_user *u;
	char *s, *varname=NULL, *file=NULL, *length=NULL, *returnvar=NULL;
	int len=0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ReadFile require an argument!\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	s = ast_strdupa(data);

	varname = strsep(&s, "=");
	file = strsep(&s, "|");
	length = s;

	if (!varname || !file) {
		ast_log(LOG_ERROR, "No file or variable specified!\n");
		ast_module_user_remove(u);
		return -1;
	}

	if (length) {
		if ((sscanf(length, "%30d", &len) != 1) || (len < 0)) {
			ast_log(LOG_WARNING, "%s is not a positive number, defaulting length to max\n", length);
			len = 0;
		}
	}

	if ((returnvar = ast_read_textfile(file))) {
		if (len > 0) {
			if (len < strlen(returnvar))
				returnvar[len]='\0';
			else
				ast_log(LOG_WARNING, "%s is longer than %d, and %d \n", file, len, (int)strlen(returnvar));
		}
		pbx_builtin_setvar_helper(chan, varname, returnvar);
		free(returnvar);
	}
	ast_module_user_remove(u);
	return res;
}


static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app_readfile);
	
	ast_module_user_hangup_all();

	return res;	
}

static int load_module(void)
{
	return ast_register_application(app_readfile, readfile_exec, readfile_synopsis, readfile_descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Stores output of file into a variable");
