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
 * \brief Get ADSI CPE ID
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/adsi.h"
#include "asterisk/options.h"

static char *app = "GetCPEID";

static char *synopsis = "Get ADSI CPE ID";

static char *descrip =
"  GetCPEID: Obtains and displays ADSI CPE ID and other information in order\n"
"to properly setup chan_dahdi.conf for on-hook operations.\n";


static int cpeid_setstatus(struct ast_channel *chan, char *stuff[], int voice)
{
	int justify[5] = { ADSI_JUST_CENT, ADSI_JUST_LEFT, ADSI_JUST_LEFT, ADSI_JUST_LEFT };
	char *tmp[5];
	int x;
	for (x=0;x<4;x++)
		tmp[x] = stuff[x];
	tmp[4] = NULL;
	return ast_adsi_print(chan, tmp, justify, voice);
}

static int cpeid_exec(struct ast_channel *chan, void *idata)
{
	int res=0;
	struct ast_module_user *u;
	unsigned char cpeid[4];
	int gotgeometry = 0;
	int gotcpeid = 0;
	int width, height, buttons;
	char *data[4];
	unsigned int x;

	u = ast_module_user_add(chan);

	for (x = 0; x < 4; x++)
		data[x] = alloca(80);

	strcpy(data[0], "** CPE Info **");
	strcpy(data[1], "Identifying CPE...");
	strcpy(data[2], "Please wait...");
	res = ast_adsi_load_session(chan, NULL, 0, 1);
	if (res > 0) {
		cpeid_setstatus(chan, data, 0);
		res = ast_adsi_get_cpeid(chan, cpeid, 0);
		if (res > 0) {
			gotcpeid = 1;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Got CPEID of '%02x:%02x:%02x:%02x' on '%s'\n", cpeid[0], cpeid[1], cpeid[2], cpeid[3], chan->name);
		}
		if (res > -1) {
			strcpy(data[1], "Measuring CPE...");
			strcpy(data[2], "Please wait...");
			cpeid_setstatus(chan, data, 0);
			res = ast_adsi_get_cpeinfo(chan, &width, &height, &buttons, 0);
			if (res > -1) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "CPE has %d lines, %d columns, and %d buttons on '%s'\n", height, width, buttons, chan->name);
				gotgeometry = 1;
			}
		}
		if (res > -1) {
			if (gotcpeid)
				snprintf(data[1], 80, "CPEID: %02x:%02x:%02x:%02x", cpeid[0], cpeid[1], cpeid[2], cpeid[3]);
			else
				strcpy(data[1], "CPEID Unknown");
			if (gotgeometry) 
				snprintf(data[2], 80, "Geom: %dx%d, %d buttons", width, height, buttons);
			else
				strcpy(data[2], "Geometry unknown");
			strcpy(data[3], "Press # to exit");
			cpeid_setstatus(chan, data, 1);
			for(;;) {
				res = ast_waitfordigit(chan, 1000);
				if (res < 0)
					break;
				if (res == '#') {
					res = 0;
					break;
				}
			}
			ast_adsi_unload_session(chan);
		}
	}
	ast_module_user_remove(u);
	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	ast_module_user_hangup_all();

	return res;	
}

static int load_module(void)
{
	return ast_register_application(app, cpeid_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Get ADSI CPE ID");
