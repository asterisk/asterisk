/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Wait for Ring Application
 * 
 * Copyright (C) 2003, Digium
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static char *synopsis = "Wait for Ring Application";

static char *tdesc = "Waits until first ring after time";

static char *desc = "  WaitForRing(timeout)\n"
"Returns 0 after waiting at least timeout seconds. and\n"
"only after the next ring has completed.  Returns 0 on\n"
"success or -1 on hangup\n";

static char *app = "WaitForRing";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int waitforring_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	struct ast_frame *f;
	int res = 0;
	int ms;
	if (!data || (sscanf(data, "%d", &ms) != 1)) {
                ast_log(LOG_WARNING, "WaitForRing requires an argument (minimum seconds)\n");
		return 0;
	}
	ms *= 1000;
	LOCAL_USER_ADD(u);
	while(ms > 0) {
		ms = ast_waitfor(chan, ms);
		if (ms < 0) {
			res = ms;
			break;
		}
		if (ms > 0) {
			f = ast_read(chan);
			if (!f) {
				res = -1;
				break;
			}
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_RING)) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Got a ring but still waiting for timeout\n");
			}
			ast_frfree(f);
		}
	}
	/* Now we're really ready for the ring */
	if (!res) {
		ms = 99999999;
		while(ms > 0) {
			ms = ast_waitfor(chan, ms);
			if (ms < 0) {
				res = ms;
				break;
			}
			if (ms > 0) {
				f = ast_read(chan);
				if (!f) {
					res = -1;
					break;
				}
				if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_RING)) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Got a ring after the timeout\n");
					ast_frfree(f);
					break;
				}
				ast_frfree(f);
			}
		}
	}
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
	return ast_register_application(app, waitforring_exec, synopsis, desc);
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
