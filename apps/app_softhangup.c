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
 * \brief SoftHangup application
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"

static char *synopsis = "Soft Hangup Application";

static char *desc = "  SoftHangup(Technology/resource[,options]):\n"
"Hangs up the requested channel.  If there are no channels to hangup,\n"
"the application will report it.\n"
"  Options:\n"
"     'a'  - hang up all channels on a specified device instead of a single resource\n";

static char *app = "SoftHangup";

enum {
	OPTION_ALL = (1 << 0),
};

AST_APP_OPTIONS(app_opts,{
	AST_APP_OPTION('a', OPTION_ALL),
});

static int softhangup_exec(struct ast_channel *chan, void *data)
{
	struct ast_channel *c = NULL;
	char *cut, *opts[0];
	char name[AST_CHANNEL_NAME] = "", *parse;
	struct ast_flags flags;
	int lenmatch;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SoftHangup requires an argument (Technology/resource)\n");
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc == 2)
		ast_app_parse_options(app_opts, &flags, opts, args.options);
	lenmatch = strlen(args.channel);

	for (c = ast_walk_channel_by_name_prefix_locked(NULL, args.channel, lenmatch);
		 c;
		 c = ast_walk_channel_by_name_prefix_locked(c, args.channel, lenmatch)) {
		ast_copy_string(name, c->name, sizeof(name));
		if (ast_test_flag(&flags, OPTION_ALL)) {
			/* CAPI is set up like CAPI[foo/bar]/clcnt */ 
			if (!strcmp(c->tech->type, "CAPI")) 
				cut = strrchr(name, '/');
			/* Basically everything else is Foo/Bar-Z */
			else
				cut = strchr(name, '-');
			/* Get rid of what we've cut */
			if (cut)
				*cut = 0;
		}
		if (!strcasecmp(name, args.channel)) {
			ast_log(LOG_WARNING, "Soft hanging %s up.\n", c->name);
			ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
			if (!ast_test_flag(&flags, OPTION_ALL)) {
				ast_channel_unlock(c);
				break;
			}
		}
		ast_channel_unlock(c);
	}

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, softhangup_exec, synopsis, desc);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Hangs up the requested channel");
