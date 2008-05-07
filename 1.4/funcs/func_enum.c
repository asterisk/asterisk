/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006
 *
 * Mark Spencer <markster@digium.com>
 * Oleksiy Krivoshey <oleksiyk@gmail.com>
 * Russell Bryant <russelb@clemson.edu>
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
 * \brief ENUM Functions
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Oleksiy Krivoshey <oleksiyk@gmail.com>
 * \author Russell Bryant <russelb@clemson.edu>
 *
 * \arg See also AstENUM
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/enum.h"
#include "asterisk/app.h"

 static char *synopsis = "Syntax: ENUMLOOKUP(number[|Method-type[|options[|record#[|zone-suffix]]]])\n";

static int function_enum(struct ast_channel *chan, char *cmd, char *data,
			 char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(number);
		AST_APP_ARG(tech);
		AST_APP_ARG(options);
		AST_APP_ARG(record);
		AST_APP_ARG(zone);
	);
	int res = 0;
	char tech[80];
	char dest[256] = "", tmp[2] = "", num[AST_MAX_EXTENSION] = "";
	struct ast_module_user *u;
	char *s, *p;
	unsigned int record = 1;

	buf[0] = '\0';

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, synopsis);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc < 1) {
		ast_log(LOG_WARNING, synopsis);
		return -1;
	}

	u = ast_module_user_add(chan);

	ast_copy_string(tech, args.tech ? args.tech : "sip", sizeof(tech));

	if (!args.zone)
		args.zone = "e164.arpa";

	if (!args.options)
		args.options = "";

	if (args.record)
		record = atoi(args.record);

	/* strip any '-' signs from number */
	for (s = p = args.number; *s; s++) {
		if (*s != '-') {
			snprintf(tmp, sizeof(tmp), "%c", *s);
			strncat(num, tmp, sizeof(num) - strlen(num) - 1);
		}

	}

	res = ast_get_enum(chan, num, dest, sizeof(dest), tech, sizeof(tech), args.zone,
			   args.options, record);

	p = strchr(dest, ':');
	if (p && strcasecmp(tech, "ALL"))
		ast_copy_string(buf, p + 1, len);
	else
		ast_copy_string(buf, dest, len);

	ast_module_user_remove(u);

	return 0;
}

static struct ast_custom_function enum_function = {
	.name = "ENUMLOOKUP",
	.synopsis =
		"ENUMLOOKUP allows for general or specific querying of NAPTR records"
		" or counts of NAPTR types for ENUM or ENUM-like DNS pointers",
	.syntax =
		"ENUMLOOKUP(number[|Method-type[|options[|record#[|zone-suffix]]]])",
	.desc =
		"Option 'c' returns an integer count of the number of NAPTRs of a certain RR type.\n"
		"Combination of 'c' and Method-type of 'ALL' will return a count of all NAPTRs for the record.\n"
		"Defaults are: Method-type=sip, no options, record=1, zone-suffix=e164.arpa\n\n"
		"For more information, see doc/enum.txt",
	.read = function_enum,
};

static int function_txtcidname(struct ast_channel *chan, char *cmd,
			       char *data, char *buf, size_t len)
{
	int res;
	char tech[80];
	char txt[256] = "";
	char dest[80];
	struct ast_module_user *u;

	buf[0] = '\0';


	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "TXTCIDNAME requires an argument (number)\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	res = ast_get_txt(chan, data, dest, sizeof(dest), tech, sizeof(tech), txt,
			  sizeof(txt));

	if (!ast_strlen_zero(txt))
		ast_copy_string(buf, txt, len);

	ast_module_user_remove(u);

	return 0;
}

static struct ast_custom_function txtcidname_function = {
	.name = "TXTCIDNAME",
	.synopsis = "TXTCIDNAME looks up a caller name via DNS",
	.syntax = "TXTCIDNAME(<number>)",
	.desc =
		"This function looks up the given phone number in DNS to retrieve\n"
		"the caller id name.  The result will either be blank or be the value\n"
		"found in the TXT record in DNS.\n",
	.read = function_txtcidname,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&enum_function);
	res |= ast_custom_function_unregister(&txtcidname_function);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&enum_function);
	res |= ast_custom_function_register(&txtcidname_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ENUM related dialplan functions");
