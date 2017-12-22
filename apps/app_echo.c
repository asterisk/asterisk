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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"

/*** DOCUMENTATION
	<application name="Echo" language="en_US">
		<synopsis>
			Echo media, DTMF back to the calling party
		</synopsis>
		<syntax />
		<description>
			<para>Echos back any media or DTMF frames read from the calling
			channel back to itself. This will not echo CONTROL, MODEM, or NULL
			frames. Note: If '#' detected application exits.</para>
			<para>This application does not automatically answer and should be
			preceeded by an application such as Answer() or Progress().</para>
		</description>
	</application>
 ***/

static const char app[] = "Echo";

static int echo_exec(struct ast_channel *chan, const char *data)
{
	int res = -1;
	int fir_sent = 0;

	while (ast_waitfor(chan, -1) > -1) {
		struct ast_frame *f = ast_read(chan);
		if (!f) {
			break;
		}
		f->delivery.tv_sec = 0;
		f->delivery.tv_usec = 0;
		if (f->frametype == AST_FRAME_CONTROL
			&& f->subclass.integer == AST_CONTROL_VIDUPDATE
			&& !fir_sent) {
			if (ast_write(chan, f) < 0) {
				ast_frfree(f);
				goto end;
			}
			fir_sent = 1;
		}
		if (!fir_sent && f->frametype == AST_FRAME_VIDEO) {
			struct ast_frame frame = {
				.frametype = AST_FRAME_CONTROL,
				.subclass.integer = AST_CONTROL_VIDUPDATE,
			};
			ast_write(chan, &frame);
			fir_sent = 1;
		}
		if (f->frametype != AST_FRAME_CONTROL
			&& f->frametype != AST_FRAME_MODEM
			&& f->frametype != AST_FRAME_NULL
			&& ast_write(chan, f)) {
			ast_frfree(f);
			goto end;
		}
		if ((f->frametype == AST_FRAME_DTMF) && (f->subclass.integer == '#')) {
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
