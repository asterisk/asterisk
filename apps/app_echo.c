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
 * \brief Echo application -- play back what you hear to evaluate latency
 * 
 * \ingroup applications
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"

static char *tdesc = "Simple Echo Application";

static char *app = "Echo";

static char *synopsis = "Echo audio read back to the user";

static char *descrip = 
"  Echo():  Echo audio read from channel back to the channel. \n"
"User can exit the application by either pressing the '#' key, \n"
"or hanging up.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int echo_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	struct ast_frame *f;
	LOCAL_USER_ADD(u);
	ast_set_write_format(chan, ast_best_codec(chan->nativeformats));
	ast_set_read_format(chan, ast_best_codec(chan->nativeformats));
	/* Do our thing here */
	while(ast_waitfor(chan, -1) > -1) {
		f = ast_read(chan);
		if (!f)
			break;
		f->delivery.tv_sec = 0;
		f->delivery.tv_usec = 0;
		if (f->frametype == AST_FRAME_VOICE) {
			if (ast_write(chan, f)) 
				break;
		} else if (f->frametype == AST_FRAME_VIDEO) {
			if (ast_write(chan, f)) 
				break;
		} else if (f->frametype == AST_FRAME_DTMF) {
			if (f->subclass == '#') {
				res = 0;
				break;
			} else
				if (ast_write(chan, f))
					break;
		}
		ast_frfree(f);
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	return ast_register_application(app, echo_exec, synopsis, descrip);
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
