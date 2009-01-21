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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/manager.h"

static char *app = "SendDTMF";

static char *synopsis = "Sends arbitrary DTMF digits";

static char *descrip = 
" SendDTMF(digits[|timeout_ms]): Sends DTMF digits on a channel. \n"
" Accepted digits: 0-9, *#abcd, w (.5s pause)\n"
" The application will either pass the assigned digits or terminate if it\n"
" encounters an error.\n";


static int senddtmf_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u;
	char *digits = NULL, *to = NULL;
	int timeout = 250;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SendDTMF requires an argument (digits or *#aAbBcCdD)\n");
		return 0;
	}

	u = ast_module_user_add(chan);

	digits = ast_strdupa(data);

	if ((to = strchr(digits,'|'))) {
		*to = '\0';
		to++;
		timeout = atoi(to);
	}
		
	if (timeout <= 0)
		timeout = 250;

	res = ast_dtmf_stream(chan,NULL,digits,timeout);
		
	ast_module_user_remove(u);

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
		ast_mutex_unlock(&chan->lock);
		return 0;
	}

	ast_senddigit(chan, *digit);

	ast_mutex_unlock(&chan->lock);
	astman_send_ack(s, m, "DTMF successfully queued");
	
	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	res |= ast_manager_unregister("PlayDTMF");

	ast_module_user_hangup_all();

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
