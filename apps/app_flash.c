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
 * \brief App to flash a DAHDI trunk
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
/*** MODULEINFO
	<depend>dahdi</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <dahdi/user.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"

/*** DOCUMENTATION
	<application name="Flash" language="en_US">
		<synopsis>
			Flashes a DAHDI Trunk.
		</synopsis>
		<syntax />
		<description>
			<para>Performs a flash on a DAHDI trunk. This can be used to access features
			provided on an incoming analogue circuit such as conference and call waiting.
			Use with SendDTMF() to perform external transfers.</para>
		</description>
		<see-also>
			<ref type="application">SendDTMF</ref>
		</see-also>
	</application>
 ***/

static char *app = "Flash";

static inline int dahdi_wait_event(int fd)
{
	/* Avoid the silly dahdi_waitevent which ignores a bunch of events */
	int i,j=0;
	i = DAHDI_IOMUX_SIGEVENT;
	if (ioctl(fd, DAHDI_IOMUX, &i) == -1) return -1;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1) return -1;
	return j;
}

static int flash_exec(struct ast_channel *chan, const char *data)
{
	int res = -1;
	int x;
	struct dahdi_params dahdip;

	if (strcasecmp(chan->tech->type, "DAHDI")) {
		ast_log(LOG_WARNING, "%s is not a DAHDI channel\n", chan->name);
		return -1;
	}
	
	memset(&dahdip, 0, sizeof(dahdip));
	res = ioctl(chan->fds[0], DAHDI_GET_PARAMS, &dahdip);
	if (!res) {
		if (dahdip.sigtype & __DAHDI_SIG_FXS) {
			x = DAHDI_FLASH;
			res = ioctl(chan->fds[0], DAHDI_HOOK, &x);
			if (!res || (errno == EINPROGRESS)) {
				if (res) {
					/* Wait for the event to finish */
					dahdi_wait_event(chan->fds[0]);
				}
				res = ast_safe_sleep(chan, 1000);
				ast_verb(3, "Flashed channel %s\n", chan->name);
			} else
				ast_log(LOG_WARNING, "Unable to flash channel %s: %s\n", chan->name, strerror(errno));
		} else
			ast_log(LOG_WARNING, "%s is not an FXO Channel\n", chan->name);
	} else
		ast_log(LOG_WARNING, "Unable to get parameters of %s: %s\n", chan->name, strerror(errno));

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, flash_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Flash channel application");

