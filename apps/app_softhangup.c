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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="SoftHangup" language="en_US">
		<synopsis>
			Hangs up the requested channel.
		</synopsis>
		<syntax>
			<parameter name="Technology/Resource" required="true" />
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>Hang up all channels on a specified device instead of a single resource</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>	
		<description>
			<para>Hangs up the requested channel.  If there are no channels to 
			hangup, the application will report it.</para>
		</description>
	</application>

 ***/

static char *app = "SoftHangup";

enum {
	OPTION_ALL = (1 << 0),
};

AST_APP_OPTIONS(app_opts,{
	AST_APP_OPTION('a', OPTION_ALL),
});

static int softhangup_exec(struct ast_channel *chan, const char *data)
{
	struct ast_channel *c = NULL;
	char *cut, *opts[0];
	char name[AST_CHANNEL_NAME] = "", *parse;
	struct ast_flags flags = {0};
	int lenmatch;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);
		AST_APP_ARG(options);
	);
	struct ast_channel_iterator *iter;
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SoftHangup requires an argument (Technology/resource)\n");
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc == 2)
		ast_app_parse_options(app_opts, &flags, opts, args.options);
	lenmatch = strlen(args.channel);

	if (!(iter = ast_channel_iterator_by_name_new(args.channel, lenmatch))) {
		return -1;
	}

	while ((c = ast_channel_iterator_next(iter))) {
		ast_channel_lock(c);
		ast_copy_string(name, ast_channel_name(c), sizeof(name));
		if (ast_test_flag(&flags, OPTION_ALL)) {
			/* CAPI is set up like CAPI[foo/bar]/clcnt */ 
			if (!strcmp(ast_channel_tech(c)->type, "CAPI")) {
				cut = strrchr(name, '/');
			/* Basically everything else is Foo/Bar-Z */
			} else {
				/* use strrchr() because Foo/Bar-Z could actually be Foo/B-a-r-Z */
				cut = strrchr(name,'-');
			}
			/* Get rid of what we've cut */
			if (cut)
				*cut = 0;
		}
		if (!strcasecmp(name, args.channel)) {
			ast_log(LOG_WARNING, "Soft hanging %s up.\n", ast_channel_name(c));
			ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
			if (!ast_test_flag(&flags, OPTION_ALL)) {
				ast_channel_unlock(c);
				c = ast_channel_unref(c);
				break;
			}
		}
		ast_channel_unlock(c);
		c = ast_channel_unref(c);
	}

	ast_channel_iterator_destroy(iter);

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, softhangup_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Hangs up the requested channel");
