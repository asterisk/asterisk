/*
* Asterisk -- An open source telephony toolkit.
*
* Copyright (C) 1999 - 2005, Digium, Inc.
*
* Mark Spencer <markster@digium.com>
* James Golovich <james@gnuinter.net>
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
 * \brief Check if Channel is Available
 *
 * \author Mark Spencer <markster@digium.com>
 * \author James Golovich <james@gnuinter.net>

 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/ioctl.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/devicestate.h"

static char *app = "ChanIsAvail";

static char *synopsis = "Check channel availability";

static char *descrip =
"  ChanIsAvail(Technology/resource[&Technology2/resource2...][,options]): \n"
"This application will check to see if any of the specified channels are\n"
"available.\n"
"  Options:\n"
"    a - Check for all available channels, not only the first one.\n"
"    s - Consider the channel unavailable if the channel is in use at all.\n"
"    t - Simply checks if specified channels exist in the channel list\n"
"        (implies option s).\n"
"This application sets the following channel variable upon completion:\n"
"  AVAILCHAN     - the name of the available channel, if one exists\n"
"  AVAILORIGCHAN - the canonical channel name that was used to create the channel\n"
"  AVAILSTATUS   - the status code for the available channel\n";


static int chanavail_exec(struct ast_channel *chan, void *data)
{
	int inuse=-1, option_state=0, string_compare=0, option_all_avail=0;
	int status;
	char *info, tmp[512], trychan[512], *peers, *tech, *number, *rest, *cur;
	struct ast_str *tmp_availchan = ast_str_alloca(2048);
	struct ast_str *tmp_availorig = ast_str_alloca(2048);
	struct ast_str *tmp_availstat = ast_str_alloca(2048);
	struct ast_channel *tempchan;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(reqchans);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ChanIsAvail requires an argument (Zap/1&Zap/2)\n");
		return -1;
	}

	info = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, info);

	if (args.options) {
		if (strchr(args.options, 'a')) {
			option_all_avail = 1;
		}
		if (strchr(args.options, 's')) {
			option_state = 1;
		}
		if (strchr(args.options, 't')) {
			string_compare = 1;
		}
	}
	peers = args.reqchans;
	if (peers) {
		cur = peers;
		do {
			/* remember where to start next time */
			rest = strchr(cur, '&');
			if (rest) {
				*rest = 0;
				rest++;
			}
			tech = cur;
			number = strchr(tech, '/');
			if (!number) {
				ast_log(LOG_WARNING, "ChanIsAvail argument takes format ([technology]/[device])\n");
				return -1;
			}
			*number = '\0';
			number++;
			
			if (string_compare) {
				/* ast_parse_device_state checks for "SIP/1234" as a channel name.
				   ast_device_state will ask the SIP driver for the channel state. */

				snprintf(trychan, sizeof(trychan), "%s/%s",cur,number);
				status = inuse = ast_parse_device_state(trychan);
			} else if (option_state) {
				/* If the pbx says in use then don't bother trying further.
				   This is to permit testing if someone's on a call, even if the
				   channel can permit more calls (ie callwaiting, sip calls, etc).  */

				snprintf(trychan, sizeof(trychan), "%s/%s",cur,number);
				status = inuse = ast_device_state(trychan);
			}
			if ((inuse <= 1) && (tempchan = ast_request(tech, chan->nativeformats, number, &status))) {
					ast_str_append(&tmp_availchan, 0, "%s%s", tmp_availchan->used ? "&" : "", tempchan->name);
					
					snprintf(tmp, sizeof(tmp), "%s/%s", tech, number);
					ast_str_append(&tmp_availorig, 0, "%s%s", tmp_availorig->used ? "&" : "", tmp);

					snprintf(tmp, sizeof(tmp), "%d", status);
					ast_str_append(&tmp_availstat, 0, "%s%s", tmp_availstat->used ? "&" : "", tmp);

					ast_hangup(tempchan);
					tempchan = NULL;

					if (!option_all_avail) {
						break;
					}
			} else {
				snprintf(tmp, sizeof(tmp), "%d", status);
				ast_str_append(&tmp_availstat, 0, "%s%s", tmp_availstat->used ? "&" : "", tmp);
			}
			cur = rest;
		} while (cur);
	}

	pbx_builtin_setvar_helper(chan, "AVAILCHAN", tmp_availchan->str);
	/* Store the originally used channel too */
	pbx_builtin_setvar_helper(chan, "AVAILORIGCHAN", tmp_availorig->str);
	pbx_builtin_setvar_helper(chan, "AVAILSTATUS", tmp_availstat->str);

	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, chanavail_exec, synopsis, descrip) ?
		AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Check channel availability");
