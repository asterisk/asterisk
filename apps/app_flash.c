/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to flash a zap trunk
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/options.h"
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/zaptel.h>
#else
#include <zaptel.h>
#endif /* __linux__ */
#include <string.h>
#include <errno.h>
#include <stdlib.h>

static char *tdesc = "Flash zap trunk application";

static char *app = "Flash";

static char *synopsis = "Flashes a Zap Trunk";

static char *descrip = 
"  Flash(): Sends a flash on a zap trunk.  This is only a hack for\n"
"people who want to perform transfers and such via AGI and is generally\n"
"quite useless otherwise.  Returns 0 on success or -1 if this is not\n"
"a zap trunk\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

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
	struct localuser *u;
	struct zt_params ztp;
	LOCAL_USER_ADD(u);
	if (!strcasecmp(chan->type, "Zap")) {
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
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, flash_exec, synopsis, descrip);
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
