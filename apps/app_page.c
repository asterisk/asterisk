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
 ***/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/chanvars.h"
#include "asterisk/utils.h"

static const char *app_page= "Page";

static const char *page_synopsis = "Pages phones";

static const char *page_descrip =
"Page(Technology/Resource&Technology2/Resource2[|options])\n"
"  Places outbound calls to the given technology / resource and dumps\n"
"them into a conference bridge as muted participants.  The original\n"
"caller is dumped into the conference as a speaker and the room is\n"
"destroyed when the original caller leaves.  Valid options are:\n"
"        d - full duplex audio\n"
"	 q - quiet, do not play beep to caller\n"
"        r - record the page into a file (see 'r' for app_meetme)\n";

LOCAL_USER_DECL;

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

struct calloutdata {
	char cidnum[64];
	char cidname[64];
	char tech[64];
	char resource[256];
	char meetmeopts[64];
	struct ast_variable *variables;
};

static void *page_thread(void *data)
{
	struct calloutdata *cd = data;
	ast_pbx_outgoing_app(cd->tech, AST_FORMAT_SLINEAR, cd->resource, 30000,
		"MeetMe", cd->meetmeopts, NULL, 0, cd->cidnum, cd->cidname, cd->variables, NULL, NULL);
	free(cd);
	return NULL;
}

static void launch_page(struct ast_channel *chan, const char *meetmeopts, const char *tech, const char *resource)
{
	struct calloutdata *cd;
	const char *varname;
	struct ast_variable *lastvar = NULL;
	struct ast_var_t *varptr;
	pthread_t t;
	pthread_attr_t attr;
	if ((cd = ast_calloc(1, sizeof(*cd)))) {
		ast_copy_string(cd->cidnum, chan->cid.cid_num ? chan->cid.cid_num : "", sizeof(cd->cidnum));
		ast_copy_string(cd->cidname, chan->cid.cid_name ? chan->cid.cid_name : "", sizeof(cd->cidname));
		ast_copy_string(cd->tech, tech, sizeof(cd->tech));
		ast_copy_string(cd->resource, resource, sizeof(cd->resource));
		ast_copy_string(cd->meetmeopts, meetmeopts, sizeof(cd->meetmeopts));

		AST_LIST_TRAVERSE(&chan->varshead, varptr, entries) {
			if (!(varname = ast_var_full_name(varptr)))
				continue;
			if (varname[0] == '_') {
				struct ast_variable *newvar = NULL;

				if (varname[1] == '_') {
					newvar = ast_variable_new(varname, ast_var_value(varptr));
				} else {
					newvar = ast_variable_new(&varname[1], ast_var_value(varptr));
				}

				if (newvar) {
					if (lastvar)
						lastvar->next = newvar;
					else
						cd->variables = newvar;
					lastvar = newvar;
				}
			}
		}

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ast_pthread_create(&t, &attr, page_thread, cd)) {
			ast_log(LOG_WARNING, "Unable to create paging thread: %s\n", strerror(errno));
			free(cd);
		}
	}
}

static int page_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	char *options;
	char *tech, *resource;
	char meetmeopts[80];
	struct ast_flags flags = { 0 };
	unsigned int confid = ast_random();
	struct ast_app *app;
	char *tmp;
	int res=0;
	char originator[AST_CHANNEL_NAME];

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "This application requires at least one argument (destination(s) to page)\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	if (!(app = pbx_findapp("MeetMe"))) {
		ast_log(LOG_WARNING, "There is no MeetMe application available!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	};

	if (!(options = ast_strdupa(data))) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	ast_copy_string(originator, chan->name, sizeof(originator));
	if ((tmp = strchr(originator, '-')))
		*tmp = '\0';

	tmp = strsep(&options, "|");
	if (options)
		ast_app_parse_options(page_opts, &flags, NULL, options);

	snprintf(meetmeopts, sizeof(meetmeopts), "%ud|%s%sqxdw", confid, (ast_test_flag(&flags, PAGE_DUPLEX) ? "" : "m"),
		(ast_test_flag(&flags, PAGE_RECORD) ? "r" : "") );

	while ((tech = strsep(&tmp, "&"))) {
		/* don't call the originating device */
		if (!strcasecmp(tech, originator))
			continue;

		if ((resource = strchr(tech, '/'))) {
			*resource++ = '\0';
			launch_page(chan, meetmeopts, tech, resource);
		} else {
			ast_log(LOG_WARNING, "Incomplete destination '%s' supplied.\n", tech);
		}
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

	LOCAL_USER_REMOVE(u);

	return -1;
}

static int unload_module(void *mod)
{
	int res;

	res =  ast_unregister_application(app_page);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

static int load_module(void *mod)
{
	return ast_register_application(app_page, page_exec, page_synopsis, page_descrip);
}

static const char *description(void)
{
	return "Page Multiple Phones";
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD1;

