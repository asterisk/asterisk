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
 * \brief App to transmit a text message
 * 
 * Requires support of sending text messages from channel driver
 *
 * \ingroup applications
 */
 
#include <string.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/options.h"

static const char *tdesc = "Send Text Applications";

static const char *app = "SendText";

static const char *synopsis = "Send a Text Message";

static const char *descrip = 
"  SendText(text): Sends text to current channel (callee).\n"
"Otherwise, execution will continue at the next priority level.\n"
"Result of transmission will be stored in the SENDTEXTSTATUS\n"
"channel variable:\n"
"      SUCCESS      Transmission succeeded\n"
"      FAILURE      Transmission failed\n"
"      UNSUPPORTED  Text transmission not supported by channel\n"
"\n"
"At this moment, text is supposed to be 7 bit ASCII in most channels.\n"
"Old deprecated behavior: \n"
" SendText should continue with the next priority upon successful execution.\n"
" If the client does not support text transport, and there exists a\n"
" step with priority n + 101, then execution will continue at that step.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int sendtext_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char *status = "UNSUPPORTED";
		
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SendText requires an argument (text)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	ast_mutex_lock(&chan->lock);
	if (!chan->tech->send_text) {
		ast_mutex_unlock(&chan->lock);
		/* Does not support transport */
		if (option_priority_jumping)
			ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 101);
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	status = "FAILURE";
	ast_mutex_unlock(&chan->lock);
	res = ast_sendtext(chan, (char *)data);
	if (!res)
		status = "SUCCESS";
	pbx_builtin_setvar_helper(chan, "SENDTEXTSTATUS", status);
	LOCAL_USER_REMOVE(u);
	return 0;
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
	return ast_register_application(app, sendtext_exec, synopsis, descrip);
}

char *description(void)
{
	return (char *) tdesc;
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
