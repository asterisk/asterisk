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
 * Page application
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"


static char *tdesc = "Page Multiple Phones";

static char *app_page= "Page";

static char *page_synopsis = "Pages phones";

static char *page_descrip =
"Page(Technology/Resource&Technology2/Resource2)\n"
"  Places outbound calls to the given technology / resource and dumps\n"
"them into a conference bridge as muted participants.  The original\n"
"caller is dumped into the conference as a speaker and the room is\n"
"destroyed when the original caller leaves.  Always returns -1.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int page_exec(struct ast_channel *chan, void *data)
{
	char *options;
	char *tech, *resource;
	char meetmeopts[80];
	unsigned int confid = rand();
	struct ast_app *app;

	if (data) {
		options = ast_strdupa((char *)data);
		if (options) {
			char *tmp = strsep(&options, "|,");
			if (options) {
				/* XXX Parse options if we had any XXX */
			}
			snprintf(meetmeopts, sizeof(meetmeopts), "%ud|mqxdw", confid);
			while(tmp && !ast_strlen_zero(tmp)) {
				tech = strsep(&tmp, "&");
				if (tech) {
					resource = strchr(tech, '/');
					if (resource) {
						*resource = '\0';
						resource++;
						ast_pbx_outgoing_app(tech, AST_FORMAT_SLINEAR, resource, 30000, "MeetMe", meetmeopts, NULL, 0, chan->cid.cid_num, chan->cid.cid_name, NULL, NULL);
					}
				}
			}
			snprintf(meetmeopts, sizeof(meetmeopts), "%ud|Atqxd", confid);
			app = pbx_findapp("Meetme");
			if (app) {
				pbx_exec(chan, app, meetmeopts, 1);
			} else
				ast_log(LOG_WARNING, "Whoa, meetme doesn't exist!\n");
		} else {
			ast_log(LOG_ERROR, "Out of memory\n");
		}
	}

	return -1;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app_page);
}

int load_module(void)
{
	return ast_register_application(app_page, page_exec, page_synopsis, page_descrip);
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
