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
 * \brief App to transmit a URL
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
#include "asterisk/channel.h"

static char *app = "SendURL";

static char *synopsis = "Send a URL";

static char *descrip = 
"  SendURL(URL[,option]): Requests client go to URL (IAX2) or sends the \n"
"URL to the client (other channels).\n"
"Result is returned in the SENDURLSTATUS channel variable:\n"
"    SUCCESS       URL successfully sent to client\n"
"    FAILURE       Failed to send URL\n"
"    NOLOAD        Client failed to load URL (wait enabled)\n"
"    UNSUPPORTED   Channel does not support URL transport\n"
"\n"
"If the option 'w' is specified, execution will wait for an\n"
"acknowledgement that the URL has been loaded before continuing\n"
"\n"
"SendURL continues normally if the URL was sent correctly or if the channel\n"
"does not support HTML transport.  Otherwise, the channel is hung up.\n";

enum {
	OPTION_WAIT = (1 << 0),
} option_flags;

AST_APP_OPTIONS(app_opts,{
	AST_APP_OPTION('w', OPTION_WAIT),
});

static int sendurl_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	char *tmp;
	struct ast_frame *f;
	char *status = "FAILURE";
	char *opts[0];
	struct ast_flags flags;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(url);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SendURL requires an argument (URL)\n");
		pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", status);
		return -1;
	}

	tmp = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, tmp);
	if (args.argc == 2)
		ast_app_parse_options(app_opts, &flags, opts, args.options);
	
	if (!ast_channel_supports_html(chan)) {
		/* Does not support transport */
		pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", "UNSUPPORTED");
		return 0;
	}
	res = ast_channel_sendurl(chan, args.url);
	if (res == -1) {
		pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", "FAILURE");
		return res;
	}
	status = "SUCCESS";
	if (ast_test_flag(&flags, OPTION_WAIT)) {
		for(;;) {
			/* Wait for an event */
			res = ast_waitfor(chan, -1);
			if (res < 0) 
				break;
			f = ast_read(chan);
			if (!f) {
				res = -1;
				status = "FAILURE";
				break;
			}
			if (f->frametype == AST_FRAME_HTML) {
				switch(f->subclass) {
				case AST_HTML_LDCOMPLETE:
					res = 0;
					ast_frfree(f);
					status = "NOLOAD";
					goto out;
					break;
				case AST_HTML_NOSUPPORT:
					/* Does not support transport */
					status = "UNSUPPORTED";
					res = 0;
					ast_frfree(f);
					goto out;
					break;
				default:
					ast_log(LOG_WARNING, "Don't know what to do with HTML subclass %d\n", f->subclass);
				};
			}
			ast_frfree(f);
		}
	} 
out:	
	pbx_builtin_setvar_helper(chan, "SENDURLSTATUS", status);
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, sendurl_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Send URL Applications");
