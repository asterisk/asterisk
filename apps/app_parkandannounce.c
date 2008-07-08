/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Author: Ben Miller <bgmiller@dccinc.com>
 *    With TONS of help from Mark!
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
 * \brief ParkAndAnnounce application for Asterisk
 *
 * \author Ben Miller <bgmiller@dccinc.com>
 * \arg With TONS of help from Mark!
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/features.h"
#include "asterisk/say.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static char *app = "ParkAndAnnounce";

static char *synopsis = "Park and Announce";

static char *descrip =
"  ParkAndAnnounce(announce:template,timeout,dial[,return_context]):\n"
"Park a call into the parkinglot and announce the call to another channel.\n"
"\n"
"announce template: Colon-separated list of files to announce.  The word PARKED\n"
"                   will be replaced by a say_digits of the extension in which\n"
"                   the call is parked.\n"
"timeout:           Time in seconds before the call returns into the return\n"
"                   context.\n"
"dial:              The app_dial style resource to call to make the\n"
"                   announcement.  Console/dsp calls the console.\n"
"return_context:    The goto-style label to jump the call back into after\n"
"                   timeout.  Default <priority+1>.\n"
"\n"
"The variable ${PARKEDAT} will contain the parking extension into which the\n"
"call was placed.  Use with the Local channel to allow the dialplan to make\n"
"use of this information.\n";


static int parkandannounce_exec(struct ast_channel *chan, void *data)
{
	int res = -1;
	int lot, timeout = 0, dres;
	char *dialtech, *tmp[100], buf[13];
	int looptemp, i;
	char *s;

	struct ast_channel *dchan;
	struct outgoing_helper oh = { 0, };
	int outstate;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(template);
		AST_APP_ARG(timeout);
		AST_APP_ARG(dial);
		AST_APP_ARG(return_context);
	);
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ParkAndAnnounce requires arguments: (announce:template|timeout|dial|[return_context])\n");
		return -1;
	}
  
	s = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, s);

	if (args.timeout)
		timeout = atoi(args.timeout) * 1000;

	if (ast_strlen_zero(args.dial)) {
		ast_log(LOG_WARNING, "PARK: A dial resource must be specified i.e: Console/dsp or Zap/g1/5551212\n");
		return -1;
	}

	dialtech = strsep(&args.dial, "/");
	ast_verb(3, "Dial Tech,String: (%s,%s)\n", dialtech, args.dial);

	if (!ast_strlen_zero(args.return_context))
		ast_parseable_goto(chan, args.return_context);

	ast_verb(3, "Return Context: (%s,%s,%d) ID: %s\n", chan->context, chan->exten, chan->priority, chan->cid.cid_num);
		if (!ast_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
		ast_verb(3, "Warning: Return Context Invalid, call will return to default|s\n");
		}

	/* we are using masq_park here to protect * from touching the channel once we park it.  If the channel comes out of timeout
	before we are done announcing and the channel is messed with, Kablooeee.  So we use Masq to prevent this.  */

	res = ast_masq_park_call(chan, NULL, timeout, &lot);
	if (res == -1)
		return res;

	ast_verb(3, "Call Parking Called, lot: %d, timeout: %d, context: %s\n", lot, timeout, args.return_context);

	/* Now place the call to the extension */

	snprintf(buf, sizeof(buf), "%d", lot);
	oh.parent_channel = chan;
	oh.vars = ast_variable_new("_PARKEDAT", buf, "");
	dchan = __ast_request_and_dial(dialtech, AST_FORMAT_SLINEAR, args.dial, 30000, &outstate, chan->cid.cid_num, chan->cid.cid_name, &oh);

	if (dchan) {
		if (dchan->_state == AST_STATE_UP) {
			ast_verb(4, "Channel %s was answered.\n", dchan->name);
		} else {
			ast_verb(4, "Channel %s was never answered.\n", dchan->name);
			ast_log(LOG_WARNING, "PARK: Channel %s was never answered for the announce.\n", dchan->name);
			ast_hangup(dchan);
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "PARK: Unable to allocate announce channel.\n");
		return -1; 
	}

	ast_stopstream(dchan);

	/* now we have the call placed and are ready to play stuff to it */

	ast_verb(4, "Announce Template:%s\n", args.template);

	for (looptemp = 0; looptemp < ARRAY_LEN(tmp); looptemp++) {
		if ((tmp[looptemp] = strsep(&args.template, ":")) != NULL)
			continue;
		else
			break;
	}

	for (i = 0; i < looptemp; i++) {
		ast_verb(4, "Announce:%s\n", tmp[i]);
		if (!strcmp(tmp[i], "PARKED")) {
			ast_say_digits(dchan, lot, "", dchan->language);
		} else {
			dres = ast_streamfile(dchan, tmp[i], dchan->language);
			if (!dres) {
				dres = ast_waitstream(dchan, "");
			} else {
				ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", tmp[i], dchan->name);
				dres = 0;
			}
		}
	}

	ast_stopstream(dchan);  
	ast_hangup(dchan);
	
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	/* return ast_register_application(app, park_exec); */
	return ast_register_application(app, parkandannounce_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Call Parking and Announce Application");
