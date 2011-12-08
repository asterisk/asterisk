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
 * \brief App to set callerid presentation
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>deprecated</support_level>
	<replacement>func_callerid</replacement>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/callerid.h"

/*** DOCUMENTATION
	<application name="SetCallerPres" language="en_US">
		<synopsis>
			Set CallerID Presentation.
		</synopsis>
		<syntax>
			<parameter name="presentation" required="true">
				<enumlist>
					<enum name="allowed_not_screened">
						<para>Presentation Allowed, Not Screened.</para>
					</enum>
					<enum name="allowed_passed_screen">
						<para>Presentation Allowed, Passed Screen.</para>
					</enum>
					<enum name="allowed_failed_screen">
						<para>Presentation Allowed, Failed Screen.</para>
					</enum>
					<enum name="allowed">
						<para>Presentation Allowed, Network Number.</para>
					</enum>
					<enum name="prohib_not_screened">
						<para>Presentation Prohibited, Not Screened.</para>
					</enum>
					<enum name="prohib_passed_screen">
						<para>Presentation Prohibited, Passed Screen.</para>
					</enum>
					<enum name="prohib_failed_screen">
						<para>Presentation Prohibited, Failed Screen.</para>
					</enum>
					<enum name="prohib">
						<para>Presentation Prohibited, Network Number.</para>
					</enum>
					<enum name="unavailable">
						<para>Number Unavailable.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Set Caller*ID presentation on a call.</para>
		</description>
	</application>
 ***/

static char *app2 = "SetCallerPres";

static int setcallerid_pres_exec(struct ast_channel *chan, const char *data)
{
	int pres = -1;
	static int deprecated = 0;

	if (!deprecated) {
		deprecated = 1;
		ast_log(LOG_WARNING, "SetCallerPres is deprecated.  Please use Set(CALLERPRES()=%s) instead.\n", (char *)data);
	}

	/* For interface consistency, permit the argument to be specified as a number */
	if (sscanf(data, "%30d", &pres) != 1 || pres < 0 || pres > 255 || (pres & 0x9c)) {
		pres = ast_parse_caller_presentation(data);
	}

	if (pres < 0) {
		ast_log(LOG_WARNING, "'%s' is not a valid presentation (see 'show application SetCallerPres')\n",
			(char *) data);
		return 0;
	}
	
	/* Set the combined caller id presentation. */
	chan->caller.id.name.presentation = pres;
	chan->caller.id.number.presentation = pres;
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app2);
}

static int load_module(void)
{
	return ast_register_application_xml(app2, setcallerid_pres_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Set CallerID Presentation Application");
