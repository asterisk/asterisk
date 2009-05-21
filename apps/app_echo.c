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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"

/*** DOCUMENTATION
	<application name="Echo" language="en_US">
		<synopsis>
			Echo audio, video, DTMF back to the calling party
		</synopsis>
		<syntax />
		<description>
			<para>Echos back any audio, video or DTMF frames read from the calling 
			channel back to itself. Note: If '#' detected application exits</para>
		</description>
	</application>
 ***/

static char *app = "Echo";

static int echo_exec(struct ast_channel *chan, const char *data)
{
	int res = -1;
	int format;

	format = ast_best_codec(chan->nativeformats);
	ast_set_write_format(chan, format);
	ast_set_read_format(chan, format);

	while (ast_waitfor(chan, -1) > -1) {
		struct ast_frame *f = ast_read(chan);
		if (!f) {
			break;
		}
		f->delivery.tv_sec = 0;
		f->delivery.tv_usec = 0;
		if (ast_write(chan, f)) {
			ast_frfree(f);
			goto end;
		}
		if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '#')) {
			res = 0;
			ast_frfree(f);
			goto end;
		}
		ast_frfree(f);
	}
end:
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, echo_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Simple Echo Application");
