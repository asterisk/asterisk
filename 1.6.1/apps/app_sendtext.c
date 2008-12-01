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
 * \author Mark Spencer <markster@digium.com>
 * 
 * \note Requires support of sending text messages from channel driver
 *
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

static const char *app = "SendText";

static const char *synopsis = "Send a Text Message";

static const char *descrip = 
"  SendText(text): Sends text to current channel (callee).\n"
"Result of transmission will be stored in the SENDTEXTSTATUS\n"
"channel variable:\n"
"      SUCCESS      Transmission succeeded\n"
"      FAILURE      Transmission failed\n"
"      UNSUPPORTED  Text transmission not supported by channel\n"
"\n"
"At this moment, text is supposed to be 7 bit ASCII in most channels.\n";

static int sendtext_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *status = "UNSUPPORTED";
	char *parse = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(text);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SendText requires an argument (text)\n");
		return -1;
	} else
		parse = ast_strdupa(data);
	
	AST_STANDARD_APP_ARGS(args, parse);

	ast_channel_lock(chan);
	if (!chan->tech->send_text) {
		ast_channel_unlock(chan);
		/* Does not support transport */
		pbx_builtin_setvar_helper(chan, "SENDTEXTSTATUS", status);
		return 0;
	}
	status = "FAILURE";
	ast_channel_unlock(chan);
	res = ast_sendtext(chan, args.text);
	if (!res)
		status = "SUCCESS";
	pbx_builtin_setvar_helper(chan, "SENDTEXTSTATUS", status);
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, sendtext_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Send Text Applications");
