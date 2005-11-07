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
 * \brief Caller*id name lookup - Look up the caller's name via DNS
 * 
 * \ingroup applications
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/enum.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char *tdesc = "TXTCIDName";

static char *app = "TXTCIDName";

static char *synopsis = "Lookup caller name from TXT record";

static char *descrip = 
"  TXTCIDName(<CallerIDNumber>[|options]):  Looks up a Caller Name via DNS and sets\n"
"the variable 'TXTCIDNAME'. TXTCIDName will either be blank\n"
"or return the value found in the TXT record in DNS.\n" 
"The option string may contain the following character:\n"
"'j' -- jump to n+101 priority if the lookup fails\n"
"This application sets the following channel variable upon completion:\n"
"  TXTCIDNAMESTATUS The status of the lookup as a text string, one of\n"
"      SUCCESS | FAILED\n";

static int txtcidname_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	char tech[80];
	char txt[256] = "";
	char dest[80];
	struct localuser *u;
	static int dep_warning = 0;
	char *parse = NULL;
	int priority_jump = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(cidnum);
		AST_APP_ARG(options);
	);

	LOCAL_USER_ADD(u);
	
	if (!dep_warning) {
		ast_log(LOG_WARNING, "The TXTCIDName application has been deprecated in favor of the TXTCIDNAME dialplan function.\n");
		dep_warning = 1;
	}
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "TXTCIDName requires an argument (extension[|options])\n");
		LOCAL_USER_REMOVE(u);
		return(0);
	}

	parse = ast_strdupa(data);
	if (!parse) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args,parse);

	if (args.options) {
		if (strchr(args.options, 'j'))
			priority_jump = 1;
	}
	
	if (!res) {
		res = ast_get_txt(chan, data, dest, sizeof(dest), tech, sizeof(tech), txt, sizeof(txt));
	}
	
	/* Parse it out */
	if (res > 0) {
		if (!ast_strlen_zero(txt)) {
			pbx_builtin_setvar_helper(chan, "TXTCIDNAME", txt);
			pbx_builtin_setvar_helper(chan, "TXTCIDNAMESTATUS", "SUCCESS");
			if (option_debug > 1)
				ast_log(LOG_DEBUG, "TXTCIDNAME got '%s'\n", txt);
		}
	}
	if (!res) {
		/* Look for a "busy" place */
		if (priority_jump || option_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		pbx_builtin_setvar_helper(chan, "TXTCIDNAMESTATUS", "FAILED");
	} else if (res > 0)
		res = 0;

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
	return ast_register_application(app, txtcidname_exec, synopsis, descrip);
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
