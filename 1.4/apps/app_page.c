/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2006 Digium, Inc.  All rights reserved.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This code is released under the GNU General Public License
 * version 2.0.  See LICENSE for more information.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief page() - Paging application
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>zaptel</depend>
	<depend>app_meetme</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/chanvars.h"
#include "asterisk/utils.h"
#include "asterisk/dial.h"
#include "asterisk/devicestate.h"

static const char *app_page= "Page";

static const char *page_synopsis = "Pages phones";

static const char *page_descrip =
"Page(Technology/Resource&Technology2/Resource2[|options])\n"
"  Places outbound calls to the given technology / resource and dumps\n"
"them into a conference bridge as muted participants.  The original\n"
"caller is dumped into the conference as a speaker and the room is\n"
"destroyed when the original caller leaves.  Valid options are:\n"
"        d - full duplex audio\n"
"        q - quiet, do not play beep to caller\n"
"        r - record the page into a file (see 'r' for app_meetme)\n";

enum {
	PAGE_DUPLEX = (1 << 0),
	PAGE_QUIET = (1 << 1),
	PAGE_RECORD = (1 << 2),
} page_opt_flags;

AST_APP_OPTIONS(page_opts, {
	AST_APP_OPTION('d', PAGE_DUPLEX),
	AST_APP_OPTION('q', PAGE_QUIET),
	AST_APP_OPTION('r', PAGE_RECORD),
});

#define MAX_DIALS 128

static int page_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	char *options, *tech, *resource, *tmp;
	char meetmeopts[88], originator[AST_CHANNEL_NAME];
	struct ast_flags flags = { 0 };
	unsigned int confid = ast_random();
	struct ast_app *app;
	int res = 0, pos = 0, i = 0;
	struct ast_dial *dials[MAX_DIALS];

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "This application requires at least one argument (destination(s) to page)\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	if (!(app = pbx_findapp("MeetMe"))) {
		ast_log(LOG_WARNING, "There is no MeetMe application available!\n");
		ast_module_user_remove(u);
		return -1;
	};

	options = ast_strdupa(data);

	ast_copy_string(originator, chan->name, sizeof(originator));
	if ((tmp = strchr(originator, '-')))
		*tmp = '\0';

	tmp = strsep(&options, "|");
	if (options)
		ast_app_parse_options(page_opts, &flags, NULL, options);

	snprintf(meetmeopts, sizeof(meetmeopts), "MeetMe|%ud|%s%sqxdw(5)", confid, (ast_test_flag(&flags, PAGE_DUPLEX) ? "" : "m"),
		(ast_test_flag(&flags, PAGE_RECORD) ? "r" : "") );

	/* Go through parsing/calling each device */
	while ((tech = strsep(&tmp, "&"))) {
		struct ast_dial *dial = NULL;

		/* don't call the originating device */
		if (!strcasecmp(tech, originator))
			continue;

		/* If no resource is available, continue on */
		if (!(resource = strchr(tech, '/'))) {
			ast_log(LOG_WARNING, "Incomplete destination '%s' supplied.\n", tech);
			continue;
		}

		*resource++ = '\0';

		/* Create a dialing structure */
		if (!(dial = ast_dial_create())) {
			ast_log(LOG_WARNING, "Failed to create dialing structure.\n");
			continue;
		}

		/* Append technology and resource */
		ast_dial_append(dial, tech, resource);

		/* Set ANSWER_EXEC as global option */
		ast_dial_option_global_enable(dial, AST_DIAL_OPTION_ANSWER_EXEC, meetmeopts);

		/* Run this dial in async mode */
		ast_dial_run(dial, chan, 1);

		/* Put in our dialing array */
		dials[pos++] = dial;
	}

	if (!ast_test_flag(&flags, PAGE_QUIET)) {
		res = ast_streamfile(chan, "beep", chan->language);
		if (!res)
			res = ast_waitstream(chan, "");
	}

	if (!res) {
		snprintf(meetmeopts, sizeof(meetmeopts), "%ud|A%s%sqxd", confid, (ast_test_flag(&flags, PAGE_DUPLEX) ? "" : "t"), 
			(ast_test_flag(&flags, PAGE_RECORD) ? "r" : "") );
		pbx_exec(chan, app, meetmeopts);
	}

	/* Go through each dial attempt cancelling, joining, and destroying */
	for (i = 0; i < pos; i++) {
		struct ast_dial *dial = dials[i];

		/* We have to wait for the async thread to exit as it's possible Meetme won't throw them out immediately */
		ast_dial_join(dial);

		/* Hangup all channels */
		ast_dial_hangup(dial);

		/* Destroy dialing structure */
		ast_dial_destroy(dial);
	}

	ast_module_user_remove(u);

	return -1;
}

static int unload_module(void)
{
	int res;

	res =  ast_unregister_application(app_page);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	return ast_register_application(app_page, page_exec, page_synopsis, page_descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Page Multiple Phones");

