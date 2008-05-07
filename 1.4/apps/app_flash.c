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
 * \brief App to flash a zap trunk
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
/*** MODULEINFO
	<depend>zaptel</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <zaptel/zaptel.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/options.h"

static char *app = "Flash";

static char *synopsis = "Flashes a Zap Trunk";

static char *descrip = 
"Performs a flash on a zap trunk.  This can be used\n"
"to access features provided on an incoming analogue circuit\n"
"such as conference and call waiting. Use with SendDTMF() to\n"
"perform external transfers\n";


static inline int zt_wait_event(int fd)
{
	/* Avoid the silly zt_waitevent which ignores a bunch of events */
	int i,j=0;
	i = ZT_IOMUX_SIGEVENT;
	if (ioctl(fd, ZT_IOMUX, &i) == -1) return -1;
	if (ioctl(fd, ZT_GETEVENT, &j) == -1) return -1;
	return j;
}

static int flash_exec(struct ast_channel *chan, void *data)
{
	int res = -1;
	int x;
	struct ast_module_user *u;
	struct zt_params ztp;
	u = ast_module_user_add(chan);
	if (!strcasecmp(chan->tech->type, "Zap")) {
		memset(&ztp, 0, sizeof(ztp));
		res = ioctl(chan->fds[0], ZT_GET_PARAMS, &ztp);
		if (!res) {
			if (ztp.sigtype & __ZT_SIG_FXS) {
				x = ZT_FLASH;
				res = ioctl(chan->fds[0], ZT_HOOK, &x);
				if (!res || (errno == EINPROGRESS)) {
					if (res) {
						/* Wait for the event to finish */
						zt_wait_event(chan->fds[0]);
					}
					res = ast_safe_sleep(chan, 1000);
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Flashed channel %s\n", chan->name);
				} else
					ast_log(LOG_WARNING, "Unable to flash channel %s: %s\n", chan->name, strerror(errno));
			} else
				ast_log(LOG_WARNING, "%s is not an FXO Channel\n", chan->name);
		} else
			ast_log(LOG_WARNING, "Unable to get parameters of %s: %s\n", chan->name, strerror(errno));
	} else
		ast_log(LOG_WARNING, "%s is not a Zap channel\n", chan->name);
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
	return ast_register_application(app, flash_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Flash channel application");

