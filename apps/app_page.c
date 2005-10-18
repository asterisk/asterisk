/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2005 Digium, Inc.  All rights reserved.
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

/*
 *
 * Paging application
 *
 */

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


static const char *tdesc = "Page Multiple Phones";

static const char *app_page= "Page";

static const char *page_synopsis = "Pages phones";

static const char *page_descrip =
"Page(Technology/Resource&Technology2/Resource2[|options])\n"
"  Places outbound calls to the given technology / resource and dumps\n"
"them into a conference bridge as muted participants.  The original\n"
"caller is dumped into the conference as a speaker and the room is\n"
"destroyed when the original caller leaves.  Valid options are:\n"
"	d - full duplex audio\n"
"	q - quiet, do not play beep to caller\n"
"Always returns -1.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#define PAGE_DUPLEX (1 << 0)
#define PAGE_QUIET  (1 << 1)

AST_DECLARE_OPTIONS(page_opts,{
	['d'] = { PAGE_DUPLEX },
	['q'] = { PAGE_QUIET },
});

struct calloutdata {
	char cidnum[64];
	char cidname[64];
	char tech[64];
	char resource[256];
	char meetmeopts[64];
};

static void *page_thread(void *data)
{
	struct calloutdata *cd = data;
	ast_pbx_outgoing_app(cd->tech, AST_FORMAT_SLINEAR, cd->resource, 30000,
		"MeetMe", cd->meetmeopts, NULL, 0, cd->cidnum, cd->cidname, NULL, NULL);
	free(cd);
	return NULL;
}

static void launch_page(struct ast_channel *chan, const char *meetmeopts, const char *tech, const char *resource)
{
	struct calloutdata *cd;
	pthread_t t;
	pthread_attr_t attr;
	cd = malloc(sizeof(struct calloutdata));
	if (cd) {
		memset(cd, 0, sizeof(struct calloutdata));
		ast_copy_string(cd->cidnum, chan->cid.cid_num ? chan->cid.cid_num : "", sizeof(cd->cidnum));
		ast_copy_string(cd->cidname, chan->cid.cid_name ? chan->cid.cid_name : "", sizeof(cd->cidname));
		ast_copy_string(cd->tech, tech, sizeof(cd->tech));
		ast_copy_string(cd->resource, resource, sizeof(cd->resource));
		ast_copy_string(cd->meetmeopts, meetmeopts, sizeof(cd->meetmeopts));
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
	unsigned int confid = rand();
	struct ast_app *app;
	char *tmp;
	int res=0;

	if (!data)
		return -1;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "This application requires at least one argument (destination(s) to page)\n");
		return -1;
	}

	if (!(app = pbx_findapp("MeetMe"))) {
		ast_log(LOG_WARNING, "There is no MeetMe application available!\n");
		return -1;
	};

	if (!(options = ast_strdupa((char *) data))) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
		
	LOCAL_USER_ADD(u);

	tmp = strsep(&options, "|");
	if (options)
		ast_parseoptions(page_opts, &flags, NULL, options);

	snprintf(meetmeopts, sizeof(meetmeopts), "%ud|%sqxdw", confid, ast_test_flag(&flags, PAGE_DUPLEX) ? "" : "m");
	while ((tech = strsep(&tmp, "&"))) {
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
		snprintf(meetmeopts, sizeof(meetmeopts), "%ud|A%sqxd", confid, ast_test_flag(&flags, PAGE_DUPLEX) ? "" : "t");
		pbx_exec(chan, app, meetmeopts, 1);
	}

	LOCAL_USER_REMOVE(u);

	return -1;
}

int unload_module(void)
{
	int res;

	res =  ast_unregister_application(app_page);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	return ast_register_application(app_page, page_exec, page_synopsis, page_descrip);
}

char *description(void)
{
	return (char *) tdesc;
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
