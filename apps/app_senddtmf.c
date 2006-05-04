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
 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

static char *tdesc = "Send DTMF digits Application";

static char *app = "SendDTMF";

static char *synopsis = "Sends arbitrary DTMF digits";

static char *descrip = 
" SendDTMF(digits[|timeout_ms]): Sends DTMF digits on a channel. \n"
" Accepted digits: 0-9, *#abcd, w (.5s pause)\n"
" The application will either pass the assigned digits or terminate if it\n"
" encounters an error.\n";

LOCAL_USER_DECL;

static int senddtmf_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char *digits = NULL, *to = NULL;
	int timeout = 250;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SendDTMF requires an argument (digits or *#aAbBcCdD)\n");
		return 0;
	}

	LOCAL_USER_ADD(u);

	if (!(digits = ast_strdupa(data))) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if ((to = strchr(digits,'|'))) {
		*to = '\0';
		to++;
		timeout = atoi(to);
	}
		
	if(timeout <= 0)
		timeout = 250;

	res = ast_dtmf_stream(chan,NULL,digits,timeout);
		
	LOCAL_USER_REMOVE(u);

	return res;
}

static char mandescr_playdtmf[] =
"Description: Plays a dtmf digit on the specified channel.\n"
"Variables: (all are required)\n"
"	Channel: Channel name to send digit to\n"
"	Digit: The dtmf digit to play\n";

static int manager_play_dtmf(struct mansession *s, struct message *m)
{
	char *channel, *digit;

	channel = astman_get_header(m, "Channel");
	digit = astman_get_header(m, "Digit");
	struct ast_channel *chan = ast_get_channel_by_name_locked(channel);
	if (chan == NULL) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	if (digit == NULL) {
		astman_send_error(s, m, "No digit specified");
		return 0;
	}
	ast_senddigit(chan, *digit);
	ast_mutex_unlock(&chan->lock);
	astman_send_ack(s, m, "DTMF successfully queued");
	return 0;
}

static int unload_module(void *mod)
{
	int res;

	res = ast_unregister_application(app);
	res |= ast_manager_unregister("PlayDTMF");

	STANDARD_HANGUP_LOCALUSERS;

	return res;	
}

static int load_module(void *mod)
{
	ast_manager_register2( "PlayDTMF", EVENT_FLAG_CALL, manager_play_dtmf, "Play DTMF signal on a specific channel.", mandescr_playdtmf );
	return ast_register_application(app, senddtmf_exec, synopsis, descrip);
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
