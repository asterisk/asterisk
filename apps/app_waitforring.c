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
 * \brief Wait for Ring Application
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"

static char *synopsis = "Wait for Ring Application";

static char *desc = "  WaitForRing(timeout):\n"
"Returns 0 after waiting at least timeout seconds. and\n"
"only after the next ring has completed.  Returns 0 on\n"
"success or -1 on hangup\n";

static char *app = "WaitForRing";


static int waitforring_exec(struct ast_channel *chan, void *data)
{
	struct ast_frame *f;
	int res = 0;
	double s;
	int ms;

	if (!data || (sscanf(data, "%lg", &s) != 1)) {
                ast_log(LOG_WARNING, "WaitForRing requires an argument (minimum seconds)\n");
		return 0;
	}

	ms = s*1000.0;
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
				ast_verb(3, "Got a ring but still waiting for timeout\n");
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
					ast_verb(3, "Got a ring after the timeout\n");
					ast_frfree(f);
					break;
				}
				ast_frfree(f);
			}
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
	return ast_register_application(app, waitforring_exec, synopsis, desc);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Waits until first ring after time");
