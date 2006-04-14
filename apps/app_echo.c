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
 * \author Mark Spencer <markster@digium.com>
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

static char *synopsis = "Echo audio, video, or DTMF back to the calling party";

static char *descrip = 
"  Echo(): This application will echo any audio, video, or DTMF frames read from\n"
"the calling channel back to itself. If the DTMF digit '#' is received, the\n"
"application will exit.\n";

LOCAL_USER_DECL;

static int echo_exec(struct ast_channel *chan, void *data)
{
	int res = -1;
	int format;
	struct localuser *u;

	LOCAL_USER_ADD(u);

	format = ast_best_codec(chan->nativeformats);
	ast_set_write_format(chan, format);
	ast_set_read_format(chan, format);

	while (ast_waitfor(chan, -1) > -1) {
		struct ast_frame *f = ast_read(chan);
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
			} else {
				if (ast_write(chan, f))
					break;
			}
		}
		ast_frfree(f);
	}

	LOCAL_USER_REMOVE(u);

	return res;
}

static int unload_module(void *mod)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

static int load_module(void *mod)
{
	return ast_register_application(app, echo_exec, synopsis, descrip);
}

static const char *description(void)
{
	return tdesc;
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD1;
