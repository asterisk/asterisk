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

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/adsi.h"

/*** DOCUMENTATION
	<application name="GetCPEID" language="en_US">
		<synopsis>
			Get ADSI CPE ID.
		</synopsis>
		<syntax />
		<description>
			<para>Obtains and displays ADSI CPE ID and other information in order
			to properly setup <filename>dahdi.conf</filename> for on-hook operations.</para>
		</description>
	</application>
 ***/
static char *app = "GetCPEID";

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

static int cpeid_exec(struct ast_channel *chan, const char *idata)
{
	int res=0;
	unsigned char cpeid[4];
	int gotgeometry = 0;
	int gotcpeid = 0;
	int width, height, buttons;
	char *data[4];
	unsigned int x;

	for (x = 0; x < 4; x++)
		data[x] = ast_alloca(80);

	strcpy(data[0], "** CPE Info **");
	strcpy(data[1], "Identifying CPE...");
	strcpy(data[2], "Please wait...");
	res = ast_adsi_load_session(chan, NULL, 0, 1);
	if (res > 0) {
		cpeid_setstatus(chan, data, 0);
		res = ast_adsi_get_cpeid(chan, cpeid, 0);
		if (res > 0) {
			gotcpeid = 1;
			ast_verb(3, "Got CPEID of '%02hhx:%02hhx:%02hhx:%02hhx' on '%s'\n",
				cpeid[0], cpeid[1], cpeid[2], cpeid[3], ast_channel_name(chan));
		}
		if (res > -1) {
			strcpy(data[1], "Measuring CPE...");
			strcpy(data[2], "Please wait...");
			cpeid_setstatus(chan, data, 0);
			res = ast_adsi_get_cpeinfo(chan, &width, &height, &buttons, 0);
			if (res > -1) {
				ast_verb(3, "CPE has %d lines, %d columns, and %d buttons on '%s'\n", height, width, buttons, ast_channel_name(chan));
				gotgeometry = 1;
			}
		}
		if (res > -1) {
			if (gotcpeid)
				snprintf(data[1], 80, "CPEID: %02hhx:%02hhx:%02hhx:%02hhx",
					cpeid[0], cpeid[1], cpeid[2], cpeid[3]);
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

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, cpeid_exec);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Get ADSI CPE ID",
		.support_level = AST_MODULE_SUPPORT_EXTENDED,
		.load = load_module,
		.unload = unload_module,
		.nonoptreq = "res_adsi",
		);
