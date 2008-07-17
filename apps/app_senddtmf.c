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
 * \brief App to send DTMF digits
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"
#include "asterisk/channel.h"

static char *app = "SendDTMF";

static char *synopsis = "Sends arbitrary DTMF digits";

static char *descrip = 
" SendDTMF(digits[,[timeout_ms][,duration_ms]]): Sends DTMF digits on a channel. \n"
" Accepted digits: 0-9, *#abcd, (default .25s pause between digits)\n"
" The application will either pass the assigned digits or terminate if it\n"
" encounters an error.\n"
" Optional Params: \n"
"   timeout_ms: pause between digits.\n"
"   duration_ms: duration of each digit.\n";

static int senddtmf_exec(struct ast_channel *chan, void *vdata)
{
	int res = 0;
	char *data;
	int timeout = 0, duration = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(digits);
		AST_APP_ARG(timeout);
		AST_APP_ARG(duration);
	);

	if (ast_strlen_zero(vdata)) {
		ast_log(LOG_WARNING, "SendDTMF requires an argument (digits or *#aAbBcCdD)\n");
		return 0;
	}

	data = ast_strdupa(vdata);
	AST_STANDARD_APP_ARGS(args, data);

	if (!ast_strlen_zero(args.timeout))
		timeout = atoi(args.timeout);
	if (!ast_strlen_zero(args.duration))
		duration = atoi(args.duration);
	res = ast_dtmf_stream(chan, NULL, args.digits, timeout <= 0 ? 250 : timeout, duration);

	return res;
}

static char mandescr_playdtmf[] =
"Description: Plays a dtmf digit on the specified channel.\n"
"Variables: (all are required)\n"
"	Channel: Channel name to send digit to\n"
"	Digit: The dtmf digit to play\n";

static int manager_play_dtmf(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *digit = astman_get_header(m, "Digit");
	struct ast_channel *chan = ast_get_channel_by_name_locked(channel);
	
	if (!chan) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}
	if (!digit) {
		astman_send_error(s, m, "No digit specified");
		ast_channel_unlock(chan);
		return 0;
	}

	ast_senddigit(chan, *digit, 0);

	ast_channel_unlock(chan);
	astman_send_ack(s, m, "DTMF successfully queued");
	
	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	res |= ast_manager_unregister("PlayDTMF");

	return res;	
}

static int load_module(void)
{
	int res;

	res = ast_manager_register2( "PlayDTMF", EVENT_FLAG_CALL, manager_play_dtmf, "Play DTMF signal on a specific channel.", mandescr_playdtmf );
	res |= ast_register_application(app, senddtmf_exec, synopsis, descrip);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Send DTMF digits Application");
